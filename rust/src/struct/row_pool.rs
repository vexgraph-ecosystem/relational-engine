//! DEFINITION: RowPool is the runtime-geometry Rust byte-row owner for C consumers.
//! A growable flat directory owns aligned RowChunks; growth never moves live rows.
//! OVERVIEW: chunks Vec<RowChunk>, row_size usize, alignment usize, rows_per_chunk
//! usize, len usize, owner u64. Geometry/owner are immutable; count changes only
//! through add/remove. API: new/zero/add/get/get_mut/remove/release_empty_chunks,
//! len/is_empty/row_size/alignment/rows_per_chunk/owner/to_string/to_string_struct.
//! Owner IDs are process-local, issued cold without wrap; resident engine required.
//! Rejection preserves live bytes/identities; private capacity/backing may grow.
//! External C pointers require exclusion before mutation/removal/destruction.
use crate::annotation::overview;
use crate::nio::{handle::Handle, row_chunk::RowChunk, row_handle::RowHandle,
    storage_error::StorageError, typed_chunk::TYPED_CHUNK_ROWS_DEFAULT, projection};
use std::sync::atomic::{AtomicU64, Ordering};

#[crate::annotation::intention("derived count, backing and immutable identity/geometry have no arbitrary setters; operation-only mutation preserves ownership per the Conflict Triage Law")]
const OWNERSHIP_CONTRACT: () = ();

// Never reuse an owner ID in the resident engine; exhaustion rejects construction.
static NEXT_OWNER: AtomicU64 = AtomicU64::new(1);

#[overview]
pub struct RowPool {
    chunks: Vec<RowChunk>,  // directory may move; byte backing and identity history do not
    row_size: usize,       // exact visible row bytes, immutable
    alignment: usize,      // caller-selected power-of-two alignment, immutable
    rows_per_chunk: usize, // named default or caller geometry, never a total ceiling
    len: usize,           // live rows, changed only by admitted add/remove
    owner: u64,           // engine-issued nonzero owner identity, not a type ID
}

impl RowPool {
    /// Construct an empty lazy pool after validating geometry and issuing a unique owner ID.
    pub fn new(size: usize, alignment: usize, capacity: usize) -> Result<Self, StorageError> {
        let () = OWNERSHIP_CONTRACT;
        RowChunk::geometry(size, alignment, capacity)?;
        let owner = NEXT_OWNER.load(Ordering::Relaxed);
        let next = owner.checked_add(1).ok_or(StorageError::Capacity)?;
        // One attempt, never a retry/wait loop. Racing cold constructions may reject
        // with Capacity; the host retries under its serialization domain.
        NEXT_OWNER.compare_exchange(owner, next, Ordering::Relaxed, Ordering::Relaxed)
            .map_err(|_| StorageError::Capacity)?;
        Ok(Self { chunks: Vec::new(), row_size: size, alignment, rows_per_chunk: capacity, len: 0, owner })
    }

    /// Construct with named default chunk geometry; size/alignment remain explicit.
    pub fn zero(size: usize, alignment: usize) -> Result<Self, StorageError> {
        Self::new(size, alignment, TYPED_CHUNK_ROWS_DEFAULT)
    }

    /// Copy a row, growing cold only when no backed hole remains; preserve live state on rejection.
    pub fn add(&mut self, source: &[u8]) -> Result<RowHandle, StorageError> {
        if source.len() != self.row_size { return Err(StorageError::Layout); }
        let next_len = self.len.checked_add(1).ok_or(StorageError::Capacity)?;
        let chunk_index = self.chunks.iter().position(|chunk| chunk.has_backing() && !chunk.is_full())
            .or_else(|| self.chunks.iter().position(|chunk| !chunk.is_full()))
            .unwrap_or(self.chunks.len());
        let base = chunk_index.checked_mul(self.rows_per_chunk).ok_or(StorageError::Capacity)?;
        base.checked_add(self.rows_per_chunk - 1).ok_or(StorageError::Capacity)?;
        let handle = if chunk_index == self.chunks.len() {
            self.chunks.try_reserve(1).map_err(|_| StorageError::Allocation)?;
            let mut chunk = RowChunk::new(self.row_size, self.alignment, self.rows_per_chunk)?;
            let handle = chunk.add(source)?;
            self.chunks.push(chunk);
            handle
        } else {
            self.chunks[chunk_index].add(source)?
        };
        self.len = next_len;
        Ok(RowHandle::new(self.owner, base + handle.index(), handle.generation()))
    }

    /// Validate owner and fixed bits before selecting a chunk-local identity.
    fn local(&self, handle: RowHandle) -> Option<(usize, Handle)> {
        if !handle.has_valid_bits() || handle.owner() != self.owner { return None; }
        Some((handle.index() / self.rows_per_chunk,
            Handle::new(handle.index() % self.rows_per_chunk, handle.generation())))
    }

    /// Borrow live bytes, rejecting wrong-owner, stale, zero and out-of-range handles.
    pub fn get(&self, handle: RowHandle) -> Option<&[u8]> {
        let (index, local) = self.local(handle)?;
        self.chunks.get(index)?.get(local)
    }

    /// Exclusively borrow live bytes through the same identity checks.
    pub fn get_mut(&mut self, handle: RowHandle) -> Option<&mut [u8]> {
        let (index, local) = self.local(handle)?;
        self.chunks.get_mut(index)?.get_mut(local)
    }

    /// Invalidate one row identity; embedded pointers/destructors remain caller-owned.
    pub fn remove(&mut self, handle: RowHandle) -> Result<(), StorageError> {
        let (index, local) = self.local(handle).ok_or(StorageError::Bounds)?;
        self.chunks.get_mut(index).ok_or(StorageError::Bounds)?.remove(local)?;
        self.len -= 1;
        Ok(())
    }

    /// Release empty byte/marker backing but preserve all generation history.
    pub fn release_empty_chunks(&mut self) -> usize {
        self.chunks.iter_mut().filter_map(|chunk| chunk.release_empty().then_some(())).count()
    }

    /// Number of live rows.
    pub fn len(&self) -> usize { self.len }

    /// Whether the pool owns no live rows.
    pub fn is_empty(&self) -> bool { self.len == 0 }

    /// Exact row byte count.
    pub fn row_size(&self) -> usize { self.row_size }

    /// Required row start alignment.
    pub fn alignment(&self) -> usize { self.alignment }

    /// Immutable rows-per-chunk geometry.
    pub fn rows_per_chunk(&self) -> usize { self.rows_per_chunk }

    /// Process-local owner identity.
    pub fn owner(&self) -> u64 { self.owner }

    /// Write a bounded cold value projection.
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowPool(owner={}, live={})", self.owner, self.len), dest, truncated)
    }

    /// Write own fields in declaration order, without walking rows.
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowPool {{ chunks: [{}], row_size: {}, alignment: {}, rows_per_chunk: {}, len: {}, owner: {} }}",
            self.chunks.len(), self.row_size, self.alignment, self.rows_per_chunk, self.len, self.owner), dest, truncated)
    }
}

#[macro_export]
macro_rules! RowPool {
    ($size:expr, $alignment:expr) => { $crate::r#struct::row_pool::RowPool::zero($size, $alignment) };
    ($size:expr, $alignment:expr, $capacity:expr) => {
        $crate::r#struct::row_pool::RowPool::new($size, $alignment, $capacity)
    };
}
