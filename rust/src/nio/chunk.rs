//! DEFINITION: Chunk owns one fixed-capacity, aligned allocation of typed rows.
//! Vec is reserved once; add never exceeds that bound or reallocates its rows.
//! Moving the Chunk header does not move its rows. No row removal/reuse is offered.
//! OVERVIEW: Chunk<T> { rows: Vec<T>, capacity: usize }. T supplies size/alignment.
//! API: new/zero/add/get/get_mut/as_slice/len/is_empty/get_capacity; bounded
//! to_string/to_string_struct. Geometry is immutable after construction.
//! Zero capacity/ZST/size overflow reject before allocation. Full add drops its
//! incoming T but preserves existing rows. Allocation failure returns Allocation;
//! allocator abort policies elsewhere are not changed. Mutation requires &mut self.
//! Raw addresses borrowed from rows survive append/header moves but not owner drop.
use crate::annotation::overview;
use super::{storage_error::StorageError, projection};

/// Named initial row capacity, not a collection-wide ceiling.
pub const CHUNK_ROWS_DEFAULT: usize = 64;

#[overview]
pub struct Chunk<T> {
    rows: Vec<T>,
    capacity: usize,
}

impl<T> Chunk<T> {
    /// Reserve fixed row storage; reject zero-sized, zero-capacity, or unrepresentable layouts.
    pub fn new(capacity: usize) -> Result<Self, StorageError> {
        let bytes = capacity.checked_mul(std::mem::size_of::<T>())
            .ok_or(StorageError::Layout)?;
        if capacity == 0 || std::mem::size_of::<T>() == 0 || bytes > isize::MAX as usize {
            return Err(StorageError::Layout);
        }
        let mut rows = Vec::new();
        rows.try_reserve_exact(capacity).map_err(|_| StorageError::Allocation)?;
        Ok(Self { rows, capacity })
    }

    /// Create a chunk using the named default row capacity.
    pub fn zero() -> Result<Self, StorageError> { Self::new(CHUNK_ROWS_DEFAULT) }

    /// Append a row without moving existing rows; return its index or reject when full.
    pub fn add(&mut self, value: T) -> Result<usize, StorageError> {
        if self.rows.len() == self.capacity { return Err(StorageError::Capacity); }
        let index = self.rows.len();
        self.rows.push(value);
        Ok(index)
    }

    /// Borrow the initialized row at `index`, or return `None` when out of range.
    pub fn get(&self, index: usize) -> Option<&T> { self.rows.get(index) }
    /// Mutably borrow the initialized row at `index`, or return `None` when out of range.
    pub fn get_mut(&mut self, index: usize) -> Option<&mut T> { self.rows.get_mut(index) }
    /// Borrow all initialized rows in insertion order.
    pub fn as_slice(&self) -> &[T] { &self.rows }
    /// Return the number of initialized rows.
    pub fn len(&self) -> usize { self.rows.len() }
    /// Return whether the chunk contains no initialized rows.
    pub fn is_empty(&self) -> bool { self.rows.is_empty() }
    /// Return the fixed row capacity selected at construction.
    pub fn get_capacity(&self) -> usize { self.capacity }

    /// Write a bounded value summary and report whether the destination was truncated.
    pub fn to_string(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("Chunk(len={}, capacity={})", self.len(), self.capacity), dest, out_truncated)
    }

    /// Write a bounded one-level field summary and report destination truncation.
    pub fn to_string_struct(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("Chunk {{ rows: [{} initialized], capacity: {} }}", self.len(), self.capacity), dest, out_truncated)
    }
}

#[macro_export]
macro_rules! Chunk {
    ($type:ty) => { $crate::nio::chunk::Chunk::<$type>::zero() };
    ($type:ty, $capacity:expr) => { $crate::nio::chunk::Chunk::<$type>::new($capacity) };
}
