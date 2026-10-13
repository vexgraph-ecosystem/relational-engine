//! DEFINITION: VariableRegistry owns stable append-only VariableSlot rows, not values.
//! Rust validates/folds names; engine C searches the 24-byte keys in each leaf.
//! OVERVIEW: VariableRegistry { slots: ChunkedList<VariableSlot> }.
//! API: new/zero/add/find/get/set_pointer/len/is_empty; bounded string projections.
//! Duplicates reject without changing existing bindings; missing find returns None.
//! Row addresses/indices survive directory growth until drop. No removal/reuse,
//! generation validation, schema migration, shared mutation or type-ID validation.
//! Every value pointer is borrowed and opaque: never dereferenced/freed here.
//! A consumer must retain its target, obey its type and synchronize access. Registry
//! destruction drops labels only. Pointer replacement requires &mut self and caller
//! exclusion of dereferences through any old pointers, not merely this registry.
use crate::annotation::overview;
use crate::{nio::{storage_error::StorageError, projection}, r#struct::chunked_list::ChunkedList};
use super::variable_slot::{VariableSlot, VARIABLE_NAME_BYTES};

unsafe extern "C" {
    fn re_name_search(source: *const u8, span_bytes: usize, stride: usize,
                      key: *const u8, key_bytes: usize, out_index: *mut usize) -> i32;
}

#[overview]
pub struct VariableRegistry {
    slots: ChunkedList<VariableSlot>,
}

impl VariableRegistry {
    /// Create an empty registry with the requested fixed rows-per-chunk geometry.
    pub fn new(rows_per_chunk: usize) -> Result<Self, StorageError> {
        Ok(Self { slots: ChunkedList::new(rows_per_chunk)? })
    }
    /// Create an empty registry using the chunk list's named default geometry.
    pub fn zero() -> Result<Self, StorageError> { Ok(Self { slots: ChunkedList::zero()? }) }

    /// Add a validated unique name and borrowed opaque value pointer; duplicates leave the registry unchanged.
    pub fn add(&mut self, name: &[u8], pointer: *const u8) -> Result<usize, StorageError> {
        let slot = VariableSlot::new(name, pointer)?;
        if self.find(name)?.is_some() { return Err(StorageError::Duplicate); }
        self.slots.add(slot)
    }

    /// Resolve a validated name with the native span-search routine; return `None` when absent.
    pub fn find(&self, name: &[u8]) -> Result<Option<usize>, StorageError> {
        // Cold name rendezvous per the Cold-Only Reflection Law; hot consumers
        // resolve once and borrow the stable row/value instead of scanning again.
        let key = VariableSlot::new(name, std::ptr::null())?;
        let mut base = 0;
        for chunk_index in 0..self.slots.get_chunk_count() {
            let rows = self.slots.get_chunk(chunk_index).ok_or(StorageError::Bounds)?;
            let mut local = 0;
            // Safety: initialized repr(C) rows are stable for this shared borrow;
            // their first 24 bytes are the fully initialized, padded name. C reads
            // only those bytes and neither retains pointers nor reads pointer fields.
            let status = unsafe { re_name_search(rows.as_ptr().cast(), std::mem::size_of_val(rows),
                std::mem::size_of::<VariableSlot>(), key.get_name_bytes().as_ptr(), VARIABLE_NAME_BYTES, &mut local) };
            match status {
                0 => return Ok(Some(base + local)),
                1 => base += rows.len(),
                _ => return Err(StorageError::Layout),
            }
        }
        Ok(None)
    }

    /// Borrow a slot by stable append-only index, or return `None` when out of range.
    pub fn get(&self, index: usize) -> Option<&VariableSlot> { self.slots.get(index) }
    /// Replace a slot's borrowed pointer; reject invalid indices without changing other slots.
    pub fn set_pointer(&mut self, index: usize, pointer: *const u8) -> Result<(), StorageError> {
        self.slots.get_mut(index).ok_or(StorageError::Bounds)?.set_pointer(pointer);
        Ok(())
    }
    /// Return the number of registered slots.
    pub fn len(&self) -> usize { self.slots.len() }
    /// Return whether no slots are registered.
    pub fn is_empty(&self) -> bool { self.slots.is_empty() }
    /// Write a bounded value summary and report whether the destination was truncated.
    pub fn to_string(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("VariableRegistry(len={})", self.len()), dest, out_truncated)
    }
    /// Write a bounded one-level field summary and report destination truncation.
    pub fn to_string_struct(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("VariableRegistry {{ slots: ChunkedList(len={}) }}", self.len()), dest, out_truncated)
    }
}

#[macro_export]
macro_rules! VariableRegistry {
    () => { $crate::variable::variable_registry::VariableRegistry::zero() };
    ($rows:expr) => { $crate::variable::variable_registry::VariableRegistry::new($rows) };
}
