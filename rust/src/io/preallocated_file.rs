//! DEFINITION: PreallocatedFile creates a new private byte file and reserves its
//! entire caller-selected extent before returning ownership. It never opens an
//! existing destination for writing, never creates parents and never falls back to
//! sparse sizing. The selected parent must be trusted/stable during creation and
//! failure cleanup; this is not a hostile-directory sandbox or TOCTOU guarantee.
//! OVERVIEW: file Option<File> (retained handle), path Option<PathBuf> (selected
//! path), length u64 (immutable logical extent). Public: zero/new; len/is_empty/
//! is_open/path/allocated_bytes/sync/close; bounded projections; PreallocatedFile!.
//! Crate-private into_file transfers the SAME descriptor to FixedMappedFile, never
//! reopening the pathname. Drop closes but does not delete/sync a successful file.
//! Validation precedes creation. Reservation/EOF/allocation-query failures close
//! and remove only the newly created path under the stable-parent contract. Cleanup
//! failure returns BOTH errors; a failed artifact may remain and is not success.
//! macOS creates mode 0600; same-user/privileged actors and parent symlinks remain
//! outside confinement. Physical allocation is observed at creation, not a promise
//! that future APFS COW/snapshots/quotas/device failure cannot require more space.
use super::{preallocation, preallocation_error::PreallocationError};
use crate::{annotation::{definition, intention, overview}, nio::projection};
use std::{fs::{self, File, OpenOptions}, path::{Path, PathBuf}};

#[definition]
#[overview]
#[intention("file/path/length change only through create/close/ownership transfer; arbitrary setters would forge reservation and lifetime per the Conflict Triage Law + Single Class Per File Law (Java Law)")]
pub struct PreallocatedFile {
    file: Option<File>,
    path: Option<PathBuf>,
    length: u64,
}

impl PreallocatedFile {
    /// Closed identity without filesystem work.
    pub fn zero() -> Self { Self { file: None, path: None, length: 0 } }

    /// Create a NEW file and physically reserve a nonzero fixed extent. The caller
    /// supplies an existing trusted/stable parent directory; existing files and
    /// final-component symlinks reject without modification. Cold only; can block.
    pub fn new(path: impl AsRef<Path>, length: u64) -> Result<Self, PreallocationError> {
        if length == 0 || length > isize::MAX as u64 || length > i64::MAX as u64 {
            return Err(PreallocationError::Length);
        }
        if !preallocation::supported() { return Err(PreallocationError::Unsupported); }
        let path = path.as_ref().to_path_buf();
        let mut options = OpenOptions::new();
        options.read(true).write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let file = options.open(&path)?;
        let prepared = (|| {
            preallocation::reserve(&file, length)?;
            preallocation::set_length(&file, length)?;
            let allocated = preallocation::allocated_bytes(&file)?;
            if allocated < length {
                return Err(PreallocationError::Incomplete { requested: length, allocated });
            }
            Ok(())
        })();
        if let Err(operation) = prepared {
            drop(file);
            return match fs::remove_file(&path) {
                Ok(()) => Err(operation),
                Err(cleanup) => Err(PreallocationError::Cleanup { operation: Box::new(operation), cleanup }),
            };
        }
        Ok(Self { file: Some(file), path: Some(path), length })
    }

    /// Selected logical byte length, not a heap allocation size; closed owners return zero.
    pub fn len(&self) -> u64 { self.length }
    /// Whether the owner has no byte extent (successful creation never has zero size).
    pub fn is_empty(&self) -> bool { self.length == 0 }
    /// Whether the original creation descriptor is retained.
    pub fn is_open(&self) -> bool { self.file.is_some() }
    /// Borrow the selected path; no canonicalization or sandbox guarantee is implied.
    pub fn path(&self) -> Option<&Path> { self.path.as_deref() }
    /// Observe current physical allocation through the descriptor, not by reopening path.
    pub fn allocated_bytes(&self) -> Result<u64, PreallocationError> {
        preallocation::allocated_bytes(self.file.as_ref().ok_or(PreallocationError::Closed)?)
    }
    /// Request OS synchronization; no crash-atomic publication or power-loss guarantee.
    pub fn sync(&self) -> Result<(), PreallocationError> {
        self.file.as_ref().ok_or(PreallocationError::Closed)?.sync_all()?;
        Ok(())
    }
    /// Close without deleting or implicitly syncing the successfully created file.
    pub fn close(&mut self) { self.file = None; self.path = None; self.length = 0; }
    /// Transfer the live original descriptor; path replacement cannot redirect this owner.
    pub(crate) fn into_file(mut self) -> Result<File, PreallocationError> {
        self.file.take().ok_or(PreallocationError::Closed)
    }
    /// Bounded cold value projection; path text is escaped through Path's Debug formatter.
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("PreallocatedFile(open={}, len={})", self.is_open(), self.len()), dest, truncated)
    }
    /// Own fields in order; never print descriptor addresses or read file contents.
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("PreallocatedFile {{ file: {}, path: {:?}, length: {} }}",
            self.is_open(), self.path, self.length), dest, truncated)
    }
}

#[macro_export]
macro_rules! PreallocatedFile {
    () => { $crate::PreallocatedFile::zero() };
    ($path:expr, $length:expr) => { $crate::PreallocatedFile::new($path, $length) };
}
