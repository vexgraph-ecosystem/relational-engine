//! DEFINITION: TypedPool lazily allocates TypedChunks of one Rust type. A flat
//! directory may move, but live object allocations never do. Removal reuses holes;
//! explicit empty-chunk release retains directory positions and generation history.
//! OVERVIEW: chunks Vec<TypedChunk<T>> (stable directory positions + retained history),
//! rows_per_chunk usize (immutable geometry), len usize (total live count).
//! Public: new/zero/add/remove/get/get_mut/add_handle/get_handle/get_handle_mut/
//! remove_handle/release_empty_chunks/get_chunk_count/get_rows_per_chunk/len/
//! is_empty/to_string/to_string_struct.
//! Failed admission drops the incoming T, preserves live state and may grow only
//! private directory capacity. Exclusive mutation excludes Rust borrows; callers
//! must also exclude raw-pointer users before remove, release or destruction.
//! Raw usize locations are reusable, NOT identities; the generation-tagged Handle
//! surface is the identity API (a reused slot rejects its earlier handle). No
//! registered vexgraph-ecosystem-gitrepo type ID, concurrent allocator or C ABI yet.
use crate::annotation::overview;
use crate::nio::{handle::Handle, typed_chunk::{TypedChunk, TYPED_CHUNK_ROWS_DEFAULT}, storage_error::StorageError, projection};

#[overview]
pub struct TypedPool<T> {
    chunks: Vec<TypedChunk<T>>,         // lazy directory; released row backing retains generation metadata
    rows_per_chunk: usize,               // fixed row capacity of every chunk (immutable geometry)
    len: usize,                          // total live values across all chunks
}

impl<T> TypedPool<T> {
    /// Create an empty typed pool with validated, fixed row geometry for each lazy chunk.
    pub fn new(rows_per_chunk: usize) -> Result<Self, StorageError> {
        TypedChunk::<T>::validate_geometry(rows_per_chunk)?;
        Ok(Self { chunks: Vec::new(), rows_per_chunk, len: 0 })
    }

    /// Create an empty typed pool using the named default rows-per-chunk value.
    pub fn zero() -> Result<Self, StorageError> { Self::new(TYPED_CHUNK_ROWS_DEFAULT) }

    /// Insert into an available slot or a newly allocated chunk and return its reusable pool-local index.
    pub fn add(&mut self, value: T) -> Result<usize, StorageError> {
        let next_len = self.len.checked_add(1).ok_or(StorageError::Capacity)?;
        // Reuse all allocated holes before allocating another chunk.
        for (chunk_index, chunk) in self.chunks.iter_mut().enumerate() {
            if chunk.has_backing() && !chunk.is_full() {
                let index = chunk_index * self.rows_per_chunk + chunk.add(value)?;
                self.len = next_len;
                return Ok(index);
            }
        }
        let chunk_index = self.chunks.iter().position(|chunk| !chunk.has_backing() && !chunk.is_full())
            .unwrap_or(self.chunks.len());
        let base = chunk_index.checked_mul(self.rows_per_chunk).ok_or(StorageError::Capacity)?;
        base.checked_add(self.rows_per_chunk - 1).ok_or(StorageError::Capacity)?;
        if chunk_index == self.chunks.len() {
            self.chunks.try_reserve(1).map_err(|_| StorageError::Allocation)?;
        }
        let index = if chunk_index == self.chunks.len() {
            let mut chunk = TypedChunk::new(self.rows_per_chunk)?;
            let index = base + chunk.add(value)?;
            self.chunks.push(chunk);
            index
        } else {
            base + self.chunks[chunk_index].add(value)?
        };
        self.len = next_len;
        Ok(index)
    }

    /// Remove a live value and return ownership to the caller; the vacant index may later be reused.
    pub fn remove(&mut self, index: usize) -> Result<T, StorageError> {
        let chunk = self.chunks.get_mut(index / self.rows_per_chunk)
            .ok_or(StorageError::Bounds)?;
        let value = chunk.remove(index % self.rows_per_chunk)?;
        self.len -= 1;
        Ok(value)
    }

    /// Borrow a live value by pool-local index, or return `None` when absent.
    pub fn get(&self, index: usize) -> Option<&T> {
        self.chunks.get(index / self.rows_per_chunk)?.get(index % self.rows_per_chunk)
    }

    /// Mutably borrow a live value by pool-local index, or return `None` when absent.
    pub fn get_mut(&mut self, index: usize) -> Option<&mut T> {
        self.chunks.get_mut(index / self.rows_per_chunk)?.get_mut(index % self.rows_per_chunk)
    }

    /// Insert and return a generation-tagged identity for the new value.
    pub fn add_handle(&mut self, value: T) -> Result<Handle, StorageError> {
        let index = self.add(value)?;
        let chunk = &self.chunks[index / self.rows_per_chunk];
        Ok(Handle::new(index, chunk.generation_at(index % self.rows_per_chunk)))
    }

    /// Borrow the value a live handle names; a stale or out-of-range handle returns `None`.
    pub fn get_handle(&self, handle: Handle) -> Option<&T> {
        let index = handle.index();
        let chunk = self.chunks.get(index / self.rows_per_chunk)?;
        chunk.get_handle(Handle::new(index % self.rows_per_chunk, handle.generation()))
    }

    /// Mutably borrow the value a live handle names; a stale handle returns `None`.
    pub fn get_handle_mut(&mut self, handle: Handle) -> Option<&mut T> {
        let index = handle.index();
        let chunk = self.chunks.get_mut(index / self.rows_per_chunk)?;
        chunk.get_handle_mut(Handle::new(index % self.rows_per_chunk, handle.generation()))
    }

    /// Remove the value a live handle names and invalidate the handle; a stale handle rejects.
    pub fn remove_handle(&mut self, handle: Handle) -> Result<T, StorageError> {
        let index = handle.index();
        let chunk = self.chunks.get_mut(index / self.rows_per_chunk)
            .ok_or(StorageError::Bounds)?;
        let value = chunk.remove_handle(Handle::new(index % self.rows_per_chunk, handle.generation()))?;
        self.len -= 1;
        Ok(value)
    }

    /// Release backing allocations only; retain directory positions for survivors.
    pub fn release_empty_chunks(&mut self) -> usize {
        let mut released = 0;
        for chunk in &mut self.chunks {
            if chunk.release_backing() {
                released += 1;
            }
        }
        released
    }

    /// Return the number of allocated chunks, excluding released empty directory entries.
    pub fn get_chunk_count(&self) -> usize { self.chunks.iter().filter(|chunk| chunk.has_backing()).count() }
    /// Return the fixed row capacity of each allocated chunk.
    pub fn get_rows_per_chunk(&self) -> usize { self.rows_per_chunk }
    /// Return the number of live values in the pool.
    pub fn len(&self) -> usize { self.len }
    /// Return whether the pool contains no live values.
    pub fn is_empty(&self) -> bool { self.len == 0 }

    /// Write a bounded value summary and report whether the destination was truncated.
    pub fn to_string(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("TypedPool(len={}, chunks={})", self.len, self.get_chunk_count()), dest, out_truncated)
    }

    /// Write a bounded one-level field summary and report destination truncation.
    pub fn to_string_struct(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("TypedPool {{ chunks: [{} directory entries, {} allocated], rows_per_chunk: {}, len: {} }}",
            self.chunks.len(), self.get_chunk_count(), self.rows_per_chunk, self.len), dest, out_truncated)
    }
}

#[macro_export]
macro_rules! TypedPool {
    ($type:ty) => { $crate::r#struct::typed_pool::TypedPool::<$type>::zero() };
    ($type:ty, $rows:expr) => { $crate::r#struct::typed_pool::TypedPool::<$type>::new($rows) };
}
