//! DEFINITION: Procedural platform seam for physical reservation of a new file.
//! OVERVIEW: supported/reserve/reserve_more/set_length/allocated_bytes. macOS uses F_PREALLOCATE
//! with F_ALLOCATEALL; Linux uses kernel fallocate with flags zero. No sparse,
//! zero-writing or unsupported-filesystem fallback. Other hosts reject explicitly.
//! Allocation is observed using the OS st_blocks count, not logical file length.
//! These calls can block; no realtime, crash-atomic or future-COW-space guarantee.
use super::preallocation_error::PreallocationError;
use std::fs::File;

// POSIX st_blocks counts allocated 512-byte units, independently of filesystem
// page/block size. This is an ABI unit, not a guessed allocation granularity.
#[cfg(any(target_os = "macos", target_os = "linux"))]
const ALLOCATED_BLOCK_BYTES: u64 = 512;

/// Whether a real reservation backend exists; reject unsupported hosts before creation.
pub(crate) fn supported() -> bool { cfg!(any(target_os = "macos", target_os = "linux")) }

/// Request complete physical allocation; unsupported filesystems/ENOSPC propagate.
#[cfg(target_os = "macos")]
pub(crate) fn reserve(file: &File, length: u64) -> Result<(), PreallocationError> {
    use std::os::fd::AsRawFd;
    let length = i64::try_from(length).map_err(|_| PreallocationError::Length)?;
    if length == 0 { return Err(PreallocationError::Length); }
    let mut request = libc::fstore_t {
        fst_flags: libc::F_ALLOCATEALL,
        fst_posmode: libc::F_PEOFPOSMODE,
        fst_offset: 0,
        fst_length: length,
        fst_bytesalloc: 0,
    };
    // SAFETY: live borrowed descriptor; initialized platform fstore_t remains live
    // for the synchronous variadic fcntl call. Length fits off_t, offset is zero.
    if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_PREALLOCATE, &mut request) } == -1 {
        return Err(std::io::Error::last_os_error().into());
    }
    let allocated = u64::try_from(request.fst_bytesalloc).unwrap_or(0);
    if allocated < length as u64 {
        return Err(PreallocationError::Incomplete { requested: length as u64, allocated });
    }
    Ok(())
}

/// Linux kernel allocation, without libc's possible posix_fallocate emulation.
#[cfg(target_os = "linux")]
pub(crate) fn reserve(file: &File, length: u64) -> Result<(), PreallocationError> {
    use std::os::fd::AsRawFd;
    let length = libc::off_t::try_from(length).map_err(|_| PreallocationError::Length)?;
    if length == 0 { return Err(PreallocationError::Length); }
    // SAFETY: live descriptor and representable nonnegative extent; flags zero
    // requests reservation and lets the kernel reject unsupported filesystems.
    if unsafe { libc::fallocate(file.as_raw_fd(), 0, 0, length) } == -1 {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(())
}

/// No invented Windows/other-host reservation implementation or sparse fallback.
#[cfg(not(any(target_os = "macos", target_os = "linux")))]
pub(crate) fn reserve(_file: &File, _length: u64) -> Result<(), PreallocationError> {
    Err(PreallocationError::Unsupported)
}

/// Reserve `delta` further bytes at the physical end, for a file that already has
/// a reserved prefix. `reserve` cannot express this: it checks the whole requested
/// extent, so a second call reports only the delta and would look incomplete.
/// On macOS this is F_PEOFPOSMODE from the physical end; on Linux the kernel
/// fallocate is idempotent over an allocated prefix, so the target is derived from
/// the allocation actually observed rather than assumed.
#[cfg(target_os = "macos")]
pub(crate) fn reserve_more(file: &File, delta: u64) -> Result<(), PreallocationError> {
    use std::os::fd::AsRawFd;
    if delta == 0 {
        return Ok(());
    }
    let length = i64::try_from(delta).map_err(|_| PreallocationError::Length)?;
    let mut request = libc::fstore_t {
        fst_flags: libc::F_ALLOCATEALL,
        fst_posmode: libc::F_PEOFPOSMODE,
        fst_offset: 0,
        fst_length: length,
        fst_bytesalloc: 0,
    };
    // SAFETY: live borrowed descriptor; initialized platform fstore_t stays live for
    // the synchronous variadic fcntl call. Delta fits off_t.
    if unsafe { libc::fcntl(file.as_raw_fd(), libc::F_PREALLOCATE, &mut request) } == -1 {
        return Err(std::io::Error::last_os_error().into());
    }
    let allocated = u64::try_from(request.fst_bytesalloc).unwrap_or(0);
    if allocated < delta {
        return Err(PreallocationError::Incomplete { requested: delta, allocated });
    }
    Ok(())
}

/// # Safety note for this seam: the target is observed, never assumed, so a
/// filesystem that already over-allocates still satisfies the request.
#[cfg(target_os = "linux")]
pub(crate) fn reserve_more(file: &File, delta: u64) -> Result<(), PreallocationError> {
    use std::os::fd::AsRawFd;
    if delta == 0 {
        return Ok(());
    }
    let target = allocated_bytes(file)?.checked_add(delta).ok_or(PreallocationError::Length)?;
    let length = libc::off_t::try_from(target).map_err(|_| PreallocationError::Length)?;
    // SAFETY: live descriptor and representable nonnegative extent; flags zero.
    if unsafe { libc::fallocate(file.as_raw_fd(), 0, 0, length) } == -1 {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(())
}

/// Unsupported hosts keep rejecting; there is no sparse or zero-writing fallback,
/// because a write that fails later is exactly what reservation exists to prevent.
#[cfg(not(any(target_os = "macos", target_os = "linux")))]
pub(crate) fn reserve_more(_file: &File, _delta: u64) -> Result<(), PreallocationError> {
    Err(PreallocationError::Unsupported)
}

/// Set logical EOF only after physical reservation. set_len alone is never reservation.
pub(crate) fn set_length(file: &File, length: u64) -> Result<(), PreallocationError> {
    file.set_len(length)?;
    Ok(())
}

/// Query actual allocated units; compression/sparse short allocation cannot pass as full.
#[cfg(any(target_os = "macos", target_os = "linux"))]
pub(crate) fn allocated_bytes(file: &File) -> Result<u64, PreallocationError> {
    use std::os::unix::fs::MetadataExt;
    file.metadata()?.blocks().checked_mul(ALLOCATED_BLOCK_BYTES)
        .ok_or(PreallocationError::Length)
}

/// Unsupported hosts expose no fabricated allocation evidence.
#[cfg(not(any(target_os = "macos", target_os = "linux")))]
pub(crate) fn allocated_bytes(_file: &File) -> Result<u64, PreallocationError> {
    Err(PreallocationError::Unsupported)
}
