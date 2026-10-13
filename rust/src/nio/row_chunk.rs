//! DEFINITION: RowChunk owns one aligned flat allocation of initialized byte rows.
//! It stores bytes, not Rust objects or C destructors. Runtime-selected geometry is
//! immutable. TypedChunk<u8> supplies occupancy and non-wrapping generation metadata.
//! OVERVIEW: data Option<NonNull<u8>> (backing); layout Layout (validated total bytes /
//! alignment); row_size usize (visible bytes); stride usize (aligned distance);
//! slots TypedChunk<u8> (identity/live markers). API: new/add/get/get_mut/remove,
//! release_empty/has_backing/is_full/is_empty/to_string/to_string_struct. Drop frees
//! backing. Empty backing release retains slot history. All mutation is exclusive;
//! escaped pointers expire on removal/destruction and require external exclusion.
use crate::annotation::overview;
use super::{handle::Handle, projection, storage_error::StorageError, typed_chunk::TypedChunk};
use std::{alloc::{alloc_zeroed, dealloc, Layout}, ptr::NonNull};

#[overview]
pub struct RowChunk {
    data: Option<NonNull<u8>>, // aligned flat row bytes; None after empty backing release
    layout: Layout,           // validated allocation size/alignment, immutable
    row_size: usize,          // consumer-visible byte count per row
    stride: usize,            // row size rounded up to alignment
    slots: TypedChunk<u8>,    // occupancy and retained generation history
}

impl RowChunk {
    /// Validate runtime byte-row geometry without allocation.
    pub(crate) fn geometry(size: usize, alignment: usize, capacity: usize) -> Result<(usize, Layout), StorageError> {
        if size == 0 || capacity == 0 || !alignment.is_power_of_two() { return Err(StorageError::Layout); }
        let stride = size.checked_add(alignment - 1).ok_or(StorageError::Layout)? & !(alignment - 1);
        let bytes = stride.checked_mul(capacity).ok_or(StorageError::Layout)?;
        let layout = Layout::from_size_align(bytes, alignment).map_err(|_| StorageError::Layout)?;
        Ok((stride, layout))
    }

    /// Allocate aligned bytes and fallible identity metadata, preserving no partial owner on failure.
    pub fn new(size: usize, alignment: usize, capacity: usize) -> Result<Self, StorageError> {
        let (stride, layout) = Self::geometry(size, alignment, capacity)?;
        let slots = TypedChunk::new(capacity)?;
        // SAFETY: nonzero validated Layout; allocation is paired with the same layout in Drop.
        let data = NonNull::new(unsafe { alloc_zeroed(layout) }).ok_or(StorageError::Allocation)?;
        Ok(Self { data: Some(data), layout, row_size: size, stride, slots })
    }

    /// Copy exactly one row and issue a local handle. Errors preserve all live rows.
    pub fn add(&mut self, source: &[u8]) -> Result<Handle, StorageError> {
        if source.len() != self.row_size { return Err(StorageError::Layout); }
        if self.slots.is_full() { return Err(StorageError::Capacity); }
        if self.data.is_none() {
            // SAFETY: stored layout remains validated and nonzero.
            self.data = Some(NonNull::new(unsafe { alloc_zeroed(self.layout) }).ok_or(StorageError::Allocation)?);
        }
        let handle = self.slots.add_handle(1)?;
        // SAFETY: successful admission gives a unique slot within the allocated extent.
        let target = unsafe { self.data.unwrap().as_ptr().add(handle.index() * self.stride) };
        // source is a safe borrow, disjoint from exclusive self; every visible byte is written.
        unsafe { std::ptr::copy_nonoverlapping(source.as_ptr(), target, self.row_size); }
        Ok(handle)
    }

    /// Borrow one initialized row only after validating its local identity.
    pub fn get(&self, handle: Handle) -> Option<&[u8]> {
        self.slots.get_handle(handle)?;
        let data = self.data?;
        // SAFETY: live marker implies backing and initialized bytes within immutable geometry.
        Some(unsafe { std::slice::from_raw_parts(data.as_ptr().add(handle.index() * self.stride), self.row_size) })
    }

    /// Exclusively borrow a validated row for byte mutation.
    pub fn get_mut(&mut self, handle: Handle) -> Option<&mut [u8]> {
        self.slots.get_handle(handle)?;
        let data = self.data?;
        // SAFETY: exclusive self excludes aliases, and live marker proves bounds/initialization.
        Some(unsafe { std::slice::from_raw_parts_mut(data.as_ptr().add(handle.index() * self.stride), self.row_size) })
    }

    /// Remove a local identity without interpreting or freeing pointers inside its bytes.
    pub fn remove(&mut self, handle: Handle) -> Result<(), StorageError> {
        self.slots.remove_handle(handle)?;
        Ok(())
    }

    /// Release empty backing once; retain identity metadata for later reuse.
    pub fn release_empty(&mut self) -> bool {
        if !self.slots.is_empty() { return false; }
        let Some(data) = self.data.take() else { return false; };
        // SAFETY: no live row remains and allocation uses this exact layout.
        unsafe { dealloc(data.as_ptr(), self.layout); }
        self.slots.release_backing();
        true
    }

    /// Whether the byte allocation is present.
    pub fn has_backing(&self) -> bool { self.data.is_some() }

    /// Whether every slot is live or permanently retired.
    pub fn is_full(&self) -> bool { self.slots.is_full() }

    /// Whether no live row remains.
    pub fn is_empty(&self) -> bool { self.slots.is_empty() }

    /// Write a bounded cold summary.
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowChunk(size={}, live={})", self.row_size, self.slots.len()), dest, truncated)
    }

    /// Write own fields in declaration order without walking rows.
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowChunk {{ data: {}, layout: [{}, {}], row_size: {}, stride: {}, slots: {} live }}",
            self.has_backing(), self.layout.size(), self.layout.align(), self.row_size, self.stride, self.slots.len()), dest, truncated)
    }
}

impl Drop for RowChunk {
    /// Free exactly the byte allocation still owned by this chunk.
    fn drop(&mut self) {
        if let Some(data) = self.data.take() {
            // SAFETY: unique allocation; all Rust borrowers end before owner drop.
            unsafe { dealloc(data.as_ptr(), self.layout); }
        }
    }
}

#[macro_export]
macro_rules! RowChunk {
    ($size:expr, $alignment:expr, $capacity:expr) => {
        $crate::nio::row_chunk::RowChunk::new($size, $alignment, $capacity)
    };
}
