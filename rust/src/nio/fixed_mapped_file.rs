//! DEFINITION: FixedMappedFile owns a fixed-extent regular-file byte mapping and its file.
//! This is cold R2 storage, not a native Memory block, typed-row allocation, database
//! format or transaction. memmap2 owns platform mapping/unmapping. Page faults, open,
//! flush, sync and cleanup have no guaranteed wall-time bound or realtime suitability.
//! OVERVIEW: read Option<Mmap> (read-only view), write Option<MmapMut> (shared writable
//! view), file Option<File> (retained file), writable bool (admitted access mode).
//! Exactly one view exists for a nonempty open file; empty files are open/unmapped.
//! Fields drop in this order: views before file. No escaped descriptor or resize API.
//! Public: zero/new/open/from_preallocated; len/is_empty/is_open/is_writable/as_slice/as_mut_slice;
//! read/write/flush/sync/close; bounded to_string/to_string_struct; FixedMappedFile!().
//! Invalid ranges preserve bytes; rejected construction publishes no partial owner.
//! close is idempotent and does NOT flush; explicit sync is a caller decision.
//! Rust borrows exclude our own mutation/close while a view is used. Unsafe admission
//! additionally requires excluding external mutation/truncation and competing views
//! for the owner's entire lifetime. This is not enforced by opening read-only.
use super::{mapping_error::MappingError, projection};
use crate::annotation::{definition, intention, overview};
use memmap2::{Mmap, MmapMut, MmapOptions};
use std::{fs::{File, OpenOptions}, path::Path};
use crate::io::preallocated_file::PreallocatedFile;

#[definition]
#[overview]
#[intention("mapping state mutates only through admission/access/close; arbitrary field setters would forge borrow lifetime per the Conflict Triage Law + Single Class Per File Law (Java Law)")]
pub struct FixedMappedFile {
    read: Option<Mmap>,
    write: Option<MmapMut>,
    file: Option<File>,
    writable: bool,
}

impl FixedMappedFile {
    /// Construct an empty closed owner, without opening a file or allocating.
    pub fn zero() -> Self {
        Self { read: None, write: None, file: None, writable: false }
    }

    /// Open a read-only mapping of an existing file.
    ///
    /// # Safety
    /// The caller must meet `open`'s external-file exclusion requirements.
    pub unsafe fn new(path: impl AsRef<Path>) -> Result<Self, MappingError> {
        // SAFETY: caller grants the same lifetime contract as open.
        unsafe { Self::open(path, false) }
    }

    /// Admit a whole existing regular file at its current size, without create/truncate.
    /// Empty files are valid open owners with empty byte views; nonempty views start
    /// at offset zero, so platform page/allocation-granularity alignment is internal.
    /// No arbitrary Rust type reinterpretation or over-alignment promise is offered.
    ///
    /// # Safety
    /// Until close/drop, caller excludes all other access that could mutate, resize
    /// or truncate this file, including other processes and mappings. Any concurrent
    /// readers must also respect exclusive writable borrows. Path permissions and
    /// advisory locks alone do not enforce this against arbitrary actors. Symlinks
    /// follow OS open semantics: this API is not a sandbox/path-confinement boundary.
    pub unsafe fn open(path: impl AsRef<Path>, writable: bool) -> Result<Self, MappingError> {
        let file = OpenOptions::new().read(true).write(writable).open(path)?;
        // SAFETY: caller grants the same external-file exclusion for this handle.
        unsafe { Self::from_file(file, writable) }
    }

    /// Consume a successful reservation and map its SAME original file handle,
    /// never reopening the path. Mapping failure closes this handle but leaves the
    /// successfully reserved file on disk for explicit caller recovery/removal.
    ///
    /// # Safety
    /// Caller excludes external file mutation/truncation and conflicting aliases
    /// until close/drop, exactly as for open. Creation alone cannot enforce this.
    pub unsafe fn from_preallocated(owner: PreallocatedFile) -> Result<Self, MappingError> {
        let file = owner.into_file().map_err(MappingError::Preallocation)?;
        // SAFETY: caller grants stable/exclusive file lifetime for the retained handle.
        unsafe { Self::from_file(file, true) }
    }

    /// Map a retained descriptor after metadata validation; no pathname race/reopen.
    unsafe fn from_file(file: File, writable: bool) -> Result<Self, MappingError> {
        let metadata = file.metadata()?;
        if !metadata.is_file() { return Err(MappingError::NotRegularFile); }
        let length = usize::try_from(metadata.len()).map_err(|_| MappingError::Length)?;
        if length > isize::MAX as usize { return Err(MappingError::Length); }
        let mut owner = Self { read: None, write: None, file: Some(file), writable };
        if length != 0 {
            let file = owner.file.as_ref().expect("admitted file");
            let mut options = MmapOptions::new();
            options.len(length);
            // SAFETY: unsafe admission requires stable file extent/content and excludes
            // aliases. Mapping length is representable; OS validates mapping admission.
            if writable {
                owner.write = Some(unsafe { options.map_mut(file)? });
            } else {
                owner.read = Some(unsafe { options.map(file)? });
            }
        }
        Ok(owner)
    }

    /// Byte count of the fixed view; closed and empty owners both report zero.
    pub fn len(&self) -> usize { self.as_slice().len() }
    /// Whether the byte view is empty, independently of open/closed state.
    pub fn is_empty(&self) -> bool { self.len() == 0 }
    /// Whether a file is retained (an empty file is still open).
    pub fn is_open(&self) -> bool { self.file.is_some() }
    /// Whether writable access was admitted; closed owners return false.
    pub fn is_writable(&self) -> bool { self.writable }

    /// Borrow file bytes for the owner's lifetime; closed/empty owners yield an empty slice.
    pub fn as_slice(&self) -> &[u8] {
        if let Some(view) = &self.read { return view; }
        self.write.as_deref().unwrap_or(&[])
    }

    /// Exclusively borrow writable bytes, including an empty writable file.
    pub fn as_mut_slice(&mut self) -> Result<&mut [u8], MappingError> {
        if !self.is_open() { return Err(MappingError::Closed); }
        if !self.writable { return Err(MappingError::ReadOnly); }
        Ok(self.write.as_deref_mut().unwrap_or(&mut []))
    }

    /// Borrow a checked offset/count range; overflow/out-of-bounds rejects without a read.
    pub fn read(&self, offset: usize, length: usize) -> Result<&[u8], MappingError> {
        if !self.is_open() { return Err(MappingError::Closed); }
        let end = offset.checked_add(length).ok_or(MappingError::Bounds)?;
        self.as_slice().get(offset..end).ok_or(MappingError::Bounds)
    }

    /// Copy bytes into a checked writable range; every rejection preserves the whole view.
    pub fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), MappingError> {
        let end = offset.checked_add(source.len()).ok_or(MappingError::Bounds)?;
        let target = self.as_mut_slice()?.get_mut(offset..end).ok_or(MappingError::Bounds)?;
        target.copy_from_slice(source);
        Ok(())
    }

    /// Request synchronous mapped-page writeback; does not promise metadata durability
    /// or atomic publication. Empty writable files need no mapped-page operation.
    pub fn flush(&self) -> Result<(), MappingError> {
        if !self.is_open() { return Err(MappingError::Closed); }
        if !self.writable { return Err(MappingError::ReadOnly); }
        if let Some(view) = &self.write { view.flush()?; }
        Ok(())
    }

    /// Flush mapped pages, then request file synchronization. OS failures propagate;
    /// writes already performed are not rolled back. No crash/power-loss transaction
    /// or device guarantee is added beyond the platform's flush/sync operations.
    pub fn sync(&self) -> Result<(), MappingError> {
        self.flush()?;
        self.file.as_ref().expect("flush validated open file").sync_all()?;
        Ok(())
    }

    /// Unmap before releasing the file; repeat safely. Does not implicitly flush/sync.
    /// memmap2/std Drop cannot report unmap/descriptor-close errors through this API.
    pub fn close(&mut self) {
        self.read = None;
        self.write = None;
        self.file = None;
        self.writable = false;
    }

    /// Write a bounded cold summary, reporting truncation; formatting may allocate.
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("FixedMappedFile(open={}, len={}, writable={})",
            self.is_open(), self.len(), self.is_writable()), dest, truncated)
    }

    /// Project own fields in declaration order, without addresses or file contents.
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("FixedMappedFile {{ read: {}, write: {}, file: {}, writable: {} }}",
            self.read.is_some(), self.write.is_some(), self.is_open(), self.writable), dest, truncated)
    }
}

#[macro_export]
macro_rules! FixedMappedFile {
    () => { $crate::FixedMappedFile::zero() };
    ($path:expr) => { $crate::FixedMappedFile::new($path) };
    ($path:expr, $writable:expr) => { $crate::FixedMappedFile::open($path, $writable) };
}
