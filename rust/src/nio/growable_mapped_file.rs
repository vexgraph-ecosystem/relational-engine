//! DEFINITION: GrowableMappedFile owns a fixed virtual RESERVATION over a file it
//! creates, plus a committed length that grows inside it. The mapping is made once
//! and never remade, so every pointer handed out before a growth step stays valid
//! after it — that stability, not the growth itself, is why this is a separate type
//! from FixedMappedFile, whose extent cannot change and may be read-only.
//! The reservation costs virtual address space, not memory: a mapping may far exceed
//! physical RAM and still work, because its pages are backed by the file. Resident
//! bytes are bounded by RAM plus the file's allocated blocks.
//! Growth is sparse and step-wise: only the step's blocks are reserved, and the
//! committed length is raised only AFTER those blocks exist, so a failed growth
//! leaves the length unchanged and every byte preserved. Access is always bounded by
//! the committed length, never by the reservation — mapping longer than the file is
//! legal, touching past EOF is not.
//! OVERVIEW: write Option<MmapMut> (the reservation), file Option<File>, reserved
//! usize, committed usize. Fields drop in this order: view before file.
//! Public: zero/supported/create/open; len/reserved/is_empty/is_open/allocated_bytes;
//! as_slice/as_mut_slice/read/write; grow_to; flush/sync/close;
//! bounded to_string/to_string_struct.
//! No file format, no transaction, no implicit sync, no typed reinterpretation, no
//! concurrent writers, no truncate or shrink. A page fault is an unbounded wait.
use super::mapping_error::MappingError;
use crate::annotation::{definition, intention, overview};
use crate::io::{preallocation, preallocation_error::PreallocationError};
use crate::nio::projection;
use memmap2::{MmapMut, MmapOptions};
use std::fs::{File, OpenOptions};
use std::path::Path;

#[definition]
#[overview]
#[intention("committed length changes only through create/grow_to/close; arbitrary field setters would forge reservation truth per the Conflict Triage Law + Single Class Per File Law (Java Law)")]
pub struct GrowableMappedFile {
    write: Option<MmapMut>,
    file: Option<File>,
    reserved: usize,
    committed: usize,
}

impl GrowableMappedFile {
    /// Construct an empty closed owner, without creating a file or mapping.
    pub fn zero() -> Self {
        Self { write: None, file: None, reserved: 0, committed: 0 }
    }

    /// Whether this host has a reservation backend. False means every create rejects.
    pub fn supported() -> bool { preallocation::supported() }

    /// Create a new reserved file and map the whole reservation once.
    ///
    /// # Safety
    /// The caller must exclude every other writer (and any aliasing view) of the new
    /// file for the owner's whole lifetime. Rust borrowing cannot constrain another
    /// process, and opening a mutable file-backed mapping is a trust decision.
    pub unsafe fn create(path: impl AsRef<Path>, reserved: u64, committed: u64) -> Result<Self, MappingError> {
        if !preallocation::supported() {
            return Err(MappingError::Preallocation(PreallocationError::Unsupported));
        }
        if reserved == 0 || committed > reserved {
            return Err(MappingError::Bounds);
        }
        let file = OpenOptions::new()
            .create_new(true)
            .read(true)
            .write(true)
            .open(path.as_ref())?;
        // Reserve real blocks for the initial committed region first, then set the
        // logical extent to the whole reservation: the mapping must cover the
        // reservation, and the region past `committed` must stay unallocated so
        // creation stays cheap and growth stays meaningful.
        if committed > 0 {
            preallocation::reserve(&file, committed).map_err(MappingError::Preallocation)?;
        }
        preallocation::set_length(&file, reserved).map_err(MappingError::Preallocation)?;
        let length = usize::try_from(reserved).map_err(|_| MappingError::Length)?;
        // SAFETY: caller supplied the exclusion contract; the file is live and writable.
        let write = unsafe { MmapOptions::new().len(length).map_mut(&file)? };
        Ok(Self {
            write: Some(write),
            file: Some(file),
            reserved: length,
            committed: usize::try_from(committed).map_err(|_| MappingError::Length)?,
        })
    }

    /// Open an EXISTING reserved file and map the whole reservation once — the
    /// counterpart of `create` for a store being *reopened*.
    ///
    /// This never creates, resizes or truncates: the reservation was fixed when the
    /// file was made, so a file shorter than `reserved` is refused rather than mapped
    /// as something it is not. `committed` is the caller's starting extent and must
    /// already be backed by allocated blocks, because the whole point of the type is
    /// that the committed region is physically there; a store opens with one page to
    /// read its header, then calls `grow_to`, which costs nothing when the blocks
    /// already exist.
    ///
    /// # Safety
    /// As `create`: the caller must exclude every other writer (and any aliasing
    /// view) of the file for the owner's whole lifetime.
    pub unsafe fn open(path: impl AsRef<Path>, reserved: u64, committed: u64) -> Result<Self, MappingError> {
        if !preallocation::supported() {
            return Err(MappingError::Preallocation(PreallocationError::Unsupported));
        }
        if reserved == 0 || committed > reserved {
            return Err(MappingError::Bounds);
        }
        let file = OpenOptions::new().read(true).write(true).open(path.as_ref())?;
        if !file.metadata()?.is_file() {
            return Err(MappingError::NotRegularFile);
        }
        if file.metadata()?.len() < reserved {
            return Err(MappingError::Bounds);
        }
        // Never publish a committed region the filesystem cannot actually back.
        let allocated = preallocation::allocated_bytes(&file).map_err(MappingError::Preallocation)?;
        if allocated < committed {
            return Err(MappingError::Preallocation(PreallocationError::Incomplete {
                requested: committed,
                allocated,
            }));
        }
        let length = usize::try_from(reserved).map_err(|_| MappingError::Length)?;
        // SAFETY: caller supplied the exclusion contract; the file is live and writable.
        let write = unsafe { MmapOptions::new().len(length).map_mut(&file)? };
        Ok(Self {
            write: Some(write),
            file: Some(file),
            reserved: length,
            committed: usize::try_from(committed).map_err(|_| MappingError::Length)?,
        })
    }

    /// The committed length: the only extent this API ever exposes.
    pub fn len(&self) -> usize { self.committed }
    /// The fixed virtual reservation chosen at create; never grows.
    pub fn reserved(&self) -> usize { self.reserved }
    /// Whether the committed region is empty, independently of open/closed state.
    pub fn is_empty(&self) -> bool { self.committed == 0 }
    /// Whether a file and reservation are retained.
    pub fn is_open(&self) -> bool { self.file.is_some() }

    /// Bytes the filesystem has physically allocated, as observed rather than assumed.
    pub fn allocated_bytes(&self) -> Result<u64, MappingError> {
        let Some(file) = self.file.as_ref() else { return Err(MappingError::Closed) };
        preallocation::allocated_bytes(file).map_err(MappingError::Preallocation)
    }

    /// Borrow the committed bytes; a closed owner yields an empty slice.
    pub fn as_slice(&self) -> &[u8] {
        match self.write.as_ref() {
            Some(view) if self.is_open() => &view[..self.committed],
            _ => &[],
        }
    }

    /// Exclusively borrow the committed bytes for writing.
    pub fn as_mut_slice(&mut self) -> Result<&mut [u8], MappingError> {
        if !self.is_open() {
            return Err(MappingError::Closed);
        }
        match self.write.as_mut() {
            Some(view) => Ok(&mut view[..self.committed]),
            None => Err(MappingError::Closed),
        }
    }

    /// Borrow a checked offset/count range; overflow and out-of-range reject.
    pub fn read(&self, offset: usize, length: usize) -> Result<&[u8], MappingError> {
        if !self.is_open() {
            return Err(MappingError::Closed);
        }
        let end = offset.checked_add(length).ok_or(MappingError::Bounds)?;
        if end > self.committed {
            return Err(MappingError::Bounds);
        }
        Ok(&self.as_slice()[offset..end])
    }

    /// Copy bytes into a checked writable range; every rejection preserves the view.
    pub fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), MappingError> {
        let end = offset.checked_add(source.len()).ok_or(MappingError::Bounds)?;
        if end > self.committed {
            return Err(MappingError::Bounds);
        }
        let target = self.as_mut_slice()?.get_mut(offset..end).ok_or(MappingError::Bounds)?;
        target.copy_from_slice(source);
        Ok(())
    }

    /// Grow the committed length to `length`, inside the reservation.
    ///
    /// Blocks for the new region are reserved FIRST and the committed length is
    /// published only after the allocation is observed. A failed growth leaves the
    /// length unchanged and every existing byte intact, and the mapping itself is
    /// never remade, so slices borrowed before the call stay valid after it.
    pub fn grow_to(&mut self, length: u64) -> Result<(), MappingError> {
        let Some(file) = self.file.as_ref() else { return Err(MappingError::Closed) };
        let target = usize::try_from(length).map_err(|_| MappingError::Length)?;
        if target < self.committed || target > self.reserved {
            return Err(MappingError::Bounds);
        }
        if target == self.committed {
            return Ok(());
        }
        // Growth is free when the blocks are already there, which is the reopened
        // case: opening a store and raising the extent back to what the file already
        // holds must not reserve a second copy past the physical end. The check is on
        // OBSERVED allocation, never on the request.
        let already = preallocation::allocated_bytes(file).map_err(MappingError::Preallocation)?;
        if already >= target as u64 {
            self.committed = target;
            return Ok(());
        }
        // Allocate before publishing: reserve the step, then verify the filesystem
        // really holds that many bytes rather than taking the request on faith.
        preallocation::reserve_more(file, (target - self.committed) as u64)
            .map_err(MappingError::Preallocation)?;
        let allocated = preallocation::allocated_bytes(file).map_err(MappingError::Preallocation)?;
        if allocated < target as u64 {
            return Err(MappingError::Preallocation(PreallocationError::Incomplete {
                requested: target as u64,
                allocated,
            }));
        }
        self.committed = target;
        Ok(())
    }

    /// Request synchronous mapped-page writeback. Not a durability claim.
    pub fn flush(&self) -> Result<(), MappingError> {
        if !self.is_open() {
            return Err(MappingError::Closed);
        }
        match self.write.as_ref() {
            Some(view) => {
                view.flush()?;
                Ok(())
            }
            None => Err(MappingError::Closed),
        }
    }

    /// Flush mapped pages, then request file synchronization. No atomic publication
    /// and no rollback of writes already performed.
    pub fn sync(&self) -> Result<(), MappingError> {
        self.flush()?;
        let Some(file) = self.file.as_ref() else { return Err(MappingError::Closed) };
        file.sync_all()?;
        Ok(())
    }

    /// Unmap before releasing the file; repeat safely. Does not flush or sync.
    pub fn close(&mut self) {
        self.write = None;
        self.file = None;
        self.reserved = 0;
        self.committed = 0;
    }

    /// Write a bounded cold summary, reporting truncation; formatting may allocate.
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(
            format!("GrowableMappedFile(open={}, committed={}, reserved={})",
                    self.is_open(), self.committed, self.reserved),
            dest, truncated)
    }

    /// Write a bounded by-name field dump; formatting may allocate.
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(
            format!("GrowableMappedFile{{open={}, reserved={}, committed={}, allocated={:?}}}",
                    self.is_open(), self.reserved, self.committed, self.allocated_bytes().ok()),
            dest, truncated)
    }
}
