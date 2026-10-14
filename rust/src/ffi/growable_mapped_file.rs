//! DEFINITION: procedural C adapter for the Rust GrowableMappedFile; declarations in
//! growable_mapped_file.h. The reservation, the descriptor and the unmapping order
//! live in Rust; C borrows them and there is no second implementation on the C side.
//! OVERVIEW: create/drop/len/reserved/is_open/allocated_bytes/read/write/borrow/
//! grow_to/flush/sync/close and a bounded projection, all re_gmap_* exports. An
//! integer status is the observable rejection channel (same policy as memory/
//! registry/row_pool and the fixed mapping ABI).
//! Checks precede every dereference. The borrowed pointer is READ ONLY and — unlike
//! the fixed view — stays valid across growth, because the mapping is never remade.
//! No unwinding recovery.
use crate::io::preallocation_error::PreallocationError;
use crate::nio::{growable_mapped_file::GrowableMappedFile, mapping_error::MappingError};

const GMAP_OK: u32 = 0;
const GMAP_INVALID: u32 = 1;
const GMAP_IO: u32 = 2;
const GMAP_BOUNDS: u32 = 3;
const GMAP_READ_ONLY: u32 = 4;
const GMAP_CLOSED: u32 = 5;
const GMAP_UNREPRESENTABLE: u32 = 6;
const GMAP_TRUNCATED: u32 = 7;
/* Unique to this surface: the fixed mapping needs no reservation backend. */
const GMAP_UNSUPPORTED: u32 = 8;

/// Map mapping failures onto the fixed public codes.
fn status(error: MappingError) -> u32 {
    match error {
        MappingError::Closed => GMAP_CLOSED,
        MappingError::ReadOnly => GMAP_READ_ONLY,
        MappingError::Bounds => GMAP_BOUNDS,
        MappingError::Length | MappingError::NotRegularFile => GMAP_UNREPRESENTABLE,
        MappingError::Preallocation(PreallocationError::Unsupported) => GMAP_UNSUPPORTED,
        MappingError::Io(_) | MappingError::Preallocation(_) => GMAP_IO,
    }
}

/// # Safety
/// `path` is a live NUL-terminated string and `out_owner` is valid write storage.
/// Creating a writable mapping requires excluding every other writer for the
/// owner's whole lifetime — the caller's precondition, not checked here.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_create(path: *const core::ffi::c_char, reserved: usize,
                                        committed: usize,
                                        out_owner: *mut *mut GrowableMappedFile) -> u32 {
    if path.is_null() || out_owner.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: caller promises a live NUL-terminated string.
    let text = match unsafe { core::ffi::CStr::from_ptr(path) }.to_str() {
        Ok(text) => text,
        Err(_) => return GMAP_INVALID,
    };
    // SAFETY: the caller accepts the writer-exclusion contract documented in the header.
    match unsafe { GrowableMappedFile::create(text, reserved as u64, committed as u64) } {
        Ok(owner) => {
            unsafe { *out_owner = Box::into_raw(Box::new(owner)) };
            GMAP_OK
        }
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner from `re_gmap_create`; all borrowers stopped; exactly one drop.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_drop(owner: *mut GrowableMappedFile) {
    if !owner.is_null() {
        // SAFETY: caller guarantees a live owner and no further use.
        drop(unsafe { Box::from_raw(owner) });
    }
}

/// # Safety
/// Null or a live owner; `out_len` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_len(owner: *const GrowableMappedFile, out_len: *mut usize) -> u32 {
    if owner.is_null() || out_len.is_null() {
        return GMAP_INVALID;
    }
    unsafe { *out_len = (&*owner).len() };
    GMAP_OK
}

/// # Safety
/// Null or a live owner; `out_reserved` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_reserved(owner: *const GrowableMappedFile,
                                          out_reserved: *mut usize) -> u32 {
    if owner.is_null() || out_reserved.is_null() {
        return GMAP_INVALID;
    }
    unsafe { *out_reserved = (&*owner).reserved() };
    GMAP_OK
}

/// # Safety
/// Null or a live owner; `out_open` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_is_open(owner: *const GrowableMappedFile,
                                         out_open: *mut bool) -> u32 {
    if owner.is_null() || out_open.is_null() {
        return GMAP_INVALID;
    }
    unsafe { *out_open = (&*owner).is_open() };
    GMAP_OK
}

/// # Safety
/// Null or a live owner; `out_allocated` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_allocated_bytes(owner: *const GrowableMappedFile,
                                                 out_allocated: *mut u64) -> u32 {
    if owner.is_null() || out_allocated.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: live owner per the caller.
    match unsafe { (&*owner).allocated_bytes() } {
        Ok(bytes) => {
            unsafe { *out_allocated = bytes };
            GMAP_OK
        }
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner shared without mutation; `dest` holds `capacity` bytes and
/// `out_truncated` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_read(owner: *const GrowableMappedFile, offset: usize,
                                      length: usize, dest: *mut u8, capacity: usize,
                                      out_truncated: *mut bool) -> u32 {
    if owner.is_null() || dest.is_null() || out_truncated.is_null() || capacity > isize::MAX as usize {
        return GMAP_INVALID;
    }
    unsafe { *out_truncated = false };
    // The destination is checked BEFORE the view is touched, so a refusal never
    // half-fills the caller's buffer.
    if length > capacity {
        unsafe { *out_truncated = true };
        return GMAP_TRUNCATED;
    }
    // SAFETY: live owner per the caller.
    match unsafe { (&*owner).read(offset, length) } {
        Ok(bytes) => {
            if !bytes.is_empty() {
                // SAFETY: dest holds `capacity >= length` bytes and cannot alias the mapping.
                unsafe { core::ptr::copy_nonoverlapping(bytes.as_ptr(), dest, bytes.len()) };
            }
            GMAP_OK
        }
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner; `source` is a readable span of `length` bytes, disjoint
/// from the owner's mapping.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_write(owner: *mut GrowableMappedFile, offset: usize,
                                       source: *const u8, length: usize) -> u32 {
    if owner.is_null() || source.is_null() || length > isize::MAX as usize {
        return GMAP_INVALID;
    }
    // SAFETY: caller guarantees a readable span of `length` bytes.
    let input = unsafe { core::slice::from_raw_parts(source, length) };
    // SAFETY: live owner, exclusively borrowed for this call.
    match unsafe { (&mut *owner).write(offset, input) } {
        Ok(()) => GMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner shared without mutation; both out parameters are valid.
/// The returned pointer is READ ONLY, survives later growth, and dies with the owner.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_borrow(owner: *const GrowableMappedFile,
                                        out_bytes: *mut *const u8,
                                        out_len: *mut usize) -> u32 {
    if owner.is_null() || out_bytes.is_null() || out_len.is_null() {
        return GMAP_INVALID;
    }
    let view = unsafe { &*owner };
    if !view.is_open() {
        return GMAP_CLOSED;
    }
    let bytes = view.as_slice();
    unsafe {
        *out_bytes = bytes.as_ptr();
        *out_len = bytes.len();
    }
    GMAP_OK
}

/// # Safety
/// Null or a live owner, exclusively borrowed. A borrow taken earlier stays valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_grow_to(owner: *mut GrowableMappedFile, length: usize) -> u32 {
    if owner.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed.
    match unsafe { (&mut *owner).grow_to(length as u64) } {
        Ok(()) => GMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner, exclusively borrowed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_flush(owner: *mut GrowableMappedFile) -> u32 {
    if owner.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed.
    match unsafe { (&*owner).flush() } {
        Ok(()) => GMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner, exclusively borrowed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_sync(owner: *mut GrowableMappedFile) -> u32 {
    if owner.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed.
    match unsafe { (&*owner).sync() } {
        Ok(()) => GMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner; no borrows outstanding afterwards. Does not flush.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_close(owner: *mut GrowableMappedFile) -> u32 {
    if owner.is_null() {
        return GMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed; the caller drops outstanding borrows.
    unsafe { (&mut *owner).close() };
    GMAP_OK
}

/// # Safety
/// Null or a live owner shared without mutation; `dest` holds `capacity` bytes and
/// `out_truncated` is valid. Cold projection; may allocate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_gmap_to_string(owner: *const GrowableMappedFile, dest: *mut u8,
                                           capacity: usize, out_truncated: *mut bool) -> u32 {
    if owner.is_null() || dest.is_null() || out_truncated.is_null() {
        return GMAP_INVALID;
    }
    let mut truncated = false;
    // SAFETY: caller guarantees a writable span of `capacity` bytes.
    let span = unsafe { core::slice::from_raw_parts_mut(dest, capacity) };
    let ok = unsafe { (&*owner).to_string(span, &mut truncated) };
    unsafe { *out_truncated = truncated };
    if ok { GMAP_OK } else { GMAP_TRUNCATED }
}
