//! DEFINITION: procedural C adapter for the Rust FixedMappedFile; declarations in
//! fixed_mapped_file.h. The mapping is owned in Rust and C borrows it — there is no
//! second implementation on the C side.
//! OVERVIEW: open/drop/len/is_open/is_writable/read/write/borrow/flush/sync/close and
//! a bounded projection, all re_mmap_* exports. No owned adapter class. An integer
//! status code is the observable rejection channel (the same Rust FFI policy as
//! memory/registry/row_pool).
//! Geometry and null checks precede every dereference. Live, sized, disjoint and
//! externally-serialized spans remain caller preconditions. The borrowed mapping
//! pointer is read-only and dies with the owner. No unwinding recovery.
use crate::nio::{fixed_mapped_file::FixedMappedFile, mapping_error::MappingError};

// Public ABI status codes; mirrored in relational_engine/fixed_mapped_file.h.
const MMAP_OK: u32 = 0;
const MMAP_INVALID: u32 = 1;
const MMAP_IO: u32 = 2;
const MMAP_BOUNDS: u32 = 3;
const MMAP_READ_ONLY: u32 = 4;
const MMAP_CLOSED: u32 = 5;
const MMAP_UNREPRESENTABLE: u32 = 6;
const MMAP_TRUNCATED: u32 = 7;

/// Map mapping failures onto the fixed public codes.
fn status(error: MappingError) -> u32 {
    match error {
        MappingError::Closed => MMAP_CLOSED,
        MappingError::ReadOnly => MMAP_READ_ONLY,
        MappingError::Bounds => MMAP_BOUNDS,
        MappingError::Length | MappingError::NotRegularFile => MMAP_UNREPRESENTABLE,
        MappingError::Io(_) | MappingError::Preallocation(_) => MMAP_IO,
    }
}

/// # Safety
/// `path` is a live NUL-terminated string and `out_owner` is valid write storage.
/// Opening a mutable mapping requires excluding external mutation/truncation for
/// the owner's whole lifetime — the caller's precondition, not checked here.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_open(path: *const core::ffi::c_char, writable: bool,
                                      out_owner: *mut *mut FixedMappedFile) -> u32 {
    if path.is_null() || out_owner.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: caller promises a live NUL-terminated string.
    let text = match unsafe { core::ffi::CStr::from_ptr(path) }.to_str() {
        Ok(text) => text,
        Err(_) => return MMAP_INVALID,
    };
    // SAFETY: the caller accepts the external-mutation exclusion documented in the
    // header; nothing else can be checked from here.
    match unsafe { FixedMappedFile::open(text, writable) } {
        Ok(owner) => {
            unsafe { *out_owner = Box::into_raw(Box::new(owner)) };
            MMAP_OK
        }
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner from `re_mmap_open`; all borrowers stopped; exactly one drop.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_drop(owner: *mut FixedMappedFile) {
    if !owner.is_null() {
        // SAFETY: caller guarantees a live owner and no further use.
        drop(unsafe { Box::from_raw(owner) });
    }
}

/// # Safety
/// Null or a live owner; `out_len` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_len(owner: *const FixedMappedFile, out_len: *mut usize) -> u32 {
    if owner.is_null() || out_len.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner per the caller.
    unsafe { *out_len = (&*owner).len() };
    MMAP_OK
}

/// # Safety
/// Null or a live owner; `out_open` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_is_open(owner: *const FixedMappedFile, out_open: *mut bool) -> u32 {
    if owner.is_null() || out_open.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner per the caller.
    unsafe { *out_open = (&*owner).is_open() };
    MMAP_OK
}

/// # Safety
/// Null or a live owner; `out_writable` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_is_writable(owner: *const FixedMappedFile,
                                             out_writable: *mut bool) -> u32 {
    if owner.is_null() || out_writable.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner per the caller.
    unsafe { *out_writable = (&*owner).is_writable() };
    MMAP_OK
}

/// # Safety
/// Null or a live owner shared without mutation; `dest` is valid write storage of
/// `capacity` bytes; `out_truncated` is valid write storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_read(owner: *const FixedMappedFile, offset: usize, length: usize,
                                      dest: *mut u8, capacity: usize,
                                      out_truncated: *mut bool) -> u32 {
    if owner.is_null() || dest.is_null() || out_truncated.is_null() || capacity > isize::MAX as usize {
        return MMAP_INVALID;
    }
    unsafe { *out_truncated = false };
    // The destination is checked BEFORE the view is touched, so a refusal never
    // half-fills the caller's buffer.
    if length > capacity {
        unsafe { *out_truncated = true };
        return MMAP_TRUNCATED;
    }
    // SAFETY: live owner per the caller.
    match unsafe { (&*owner).read(offset, length) } {
        Ok(bytes) => {
            if !bytes.is_empty() {
                // SAFETY: dest holds `capacity >= length` bytes and the ranges cannot alias:
                // dest is caller-owned and the mapping is this owner's file view.
                unsafe { core::ptr::copy_nonoverlapping(bytes.as_ptr(), dest, bytes.len()) };
            }
            MMAP_OK
        }
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner; `source` is a valid readable span of `length` bytes,
/// disjoint from the owner's mapping.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_write(owner: *mut FixedMappedFile, offset: usize,
                                       source: *const u8, length: usize) -> u32 {
    if owner.is_null() || source.is_null() || length > isize::MAX as usize {
        return MMAP_INVALID;
    }
    // SAFETY: caller guarantees a readable span of `length` bytes.
    let input = unsafe { core::slice::from_raw_parts(source, length) };
    // SAFETY: live owner, exclusively borrowed for this call.
    match unsafe { (&mut *owner).write(offset, input) } {
        Ok(()) => MMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner shared without mutation; both out parameters are valid.
/// The returned pointer is READ ONLY and stays valid only while the owner is open
/// with no overlapping write, and it dies with the owner.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_borrow(owner: *const FixedMappedFile,
                                        out_bytes: *mut *const u8,
                                        out_len: *mut usize) -> u32 {
    if owner.is_null() || out_bytes.is_null() || out_len.is_null() {
        return MMAP_INVALID;
    }
    let view = unsafe { &*owner };
    if !view.is_open() {
        return MMAP_CLOSED;
    }
    let bytes = view.as_slice();
    unsafe {
        *out_bytes = bytes.as_ptr();
        *out_len = bytes.len();
    }
    MMAP_OK
}

/// # Safety
/// Null or a live owner, exclusively borrowed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_flush(owner: *mut FixedMappedFile) -> u32 {
    if owner.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed.
    match unsafe { (&*owner).flush() } {
        Ok(()) => MMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner, exclusively borrowed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_sync(owner: *mut FixedMappedFile) -> u32 {
    if owner.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed.
    match unsafe { (&*owner).sync() } {
        Ok(()) => MMAP_OK,
        Err(error) => status(error),
    }
}

/// # Safety
/// Null or a live owner; no borrows outstanding afterwards. Does not flush.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_close(owner: *mut FixedMappedFile) -> u32 {
    if owner.is_null() {
        return MMAP_INVALID;
    }
    // SAFETY: live owner, exclusively borrowed; the caller drops outstanding borrows.
    unsafe { (&mut *owner).close() };
    MMAP_OK
}

/// # Safety
/// Null or a live owner shared without mutation; `dest` holds `capacity` bytes and
/// `out_truncated` is valid. This is a cold projection and may allocate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_mmap_to_string(owner: *const FixedMappedFile, dest: *mut u8,
                                           capacity: usize, out_truncated: *mut bool) -> u32 {
    if owner.is_null() || dest.is_null() || out_truncated.is_null() {
        return MMAP_INVALID;
    }
    let mut truncated = false;
    // SAFETY: caller guarantees a writable span of `capacity` bytes.
    let span = unsafe { core::slice::from_raw_parts_mut(dest, capacity) };
    let ok = unsafe { (&*owner).to_string(span, &mut truncated) };
    unsafe { *out_truncated = truncated };
    if ok { MMAP_OK } else { MMAP_TRUNCATED }
}
