//! DEFINITION: nio/mem owns byte blocks; C does not own their allocations.
//! This cold, single-owner teaching backend uses the Rust heap, not slabs.
//! OVERVIEW: Memory { blocks: Vec<Block>, next_id: u64 }.
//! Block and Value are private module types in block.rs and value.rs.
//! API: new/copy_bytes/get/get_byte; new/get/set_atomic_byte;
//! new/get/set_atomic_string; release/clear/len/is_empty.
//! Registration/release/clear need exclusive ownership; atomic get/set share &self.
//! Rejection preserves content. Kind tags are internal, NOT vexgraph-ecosystem-gitrepo type IDs.
//! Atomic Bytes publish with release/acquire. Strings retain immutable snapshots.
use crate::annotation::overview;
use std::sync::atomic::{AtomicU8, Ordering};
use crate::primitives::atomic_string::AtomicString;
pub use super::memory_error::MemoryError;
use super::{block::Block, value::Value};

#[overview]
pub struct Memory { blocks: Vec<Block>, next_id: u64 }

impl Default for Memory {
    /// Create the same empty store as `Memory::new`.
    fn default() -> Self { Self::new() }
}

impl Memory {
    /// Create an empty store with the first handle reserved for subsequent insertion.
    pub fn new() -> Self { Self { blocks: Vec::new(), next_id: 1 } }

    /// Copy Bytes, including embedded NUL. Empty blocks are valid.
    pub fn copy_bytes(&mut self, source: &[u8]) -> Result<u64, MemoryError> {
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(source.len()).map_err(|_| MemoryError::Allocation)?;
        bytes.extend_from_slice(source);
        self.insert(Value::Bytes(bytes.into_boxed_slice()))
    }

    /// Assign a fresh store-local handle and append a value, preserving state on failure.
    fn insert(&mut self, value: Value) -> Result<u64, MemoryError> {
        let next = self.next_id.checked_add(1).ok_or(MemoryError::Exhausted)?;
        self.blocks.try_reserve(1).map_err(|_| MemoryError::Allocation)?;
        let id = self.next_id;
        self.blocks.push(Block { id, value });
        self.next_id = next;
        Ok(id)
    }

    /// A borrow cannot outlive this owner or coexist with a mutable operation.
    pub fn get(&self, id: u64) -> Result<&[u8], MemoryError> {
        match self.value(id)? {
            Value::Bytes(bytes) => Ok(bytes),
            _ => Err(MemoryError::WrongKind),
        }
    }

    /// Resolve a store-local handle to its internal value or return UnknownHandle.
    fn value(&self, id: u64) -> Result<&Value, MemoryError> {
        self.blocks.iter().find(|block| block.id == id)
            .map(|block| &block.value).ok_or(MemoryError::UnknownHandle)
    }

    /// Read one byte from a byte block, rejecting wrong-kind, unknown, or out-of-range handles.
    pub fn get_byte(&self, id: u64, index: usize) -> Result<u8, MemoryError> {
        self.get(id)?.get(index).copied().ok_or(MemoryError::Bounds)
    }

    /// Register an atomic byte cell and return its store-local handle.
    pub fn new_atomic_byte(&mut self, value: u8) -> Result<u64, MemoryError> {
        self.insert(Value::AtomicByte(AtomicU8::new(value)))
    }

    /// Acquire-load an atomic byte cell by handle; reject unknown handles and other value kinds.
    pub fn get_atomic_byte(&self, id: u64) -> Result<u8, MemoryError> {
        match self.value(id)? {
            Value::AtomicByte(value) => Ok(value.load(Ordering::Acquire)),
            _ => Err(MemoryError::WrongKind),
        }
    }

    /// Release-store an atomic byte cell by handle; reject unknown handles and other value kinds.
    pub fn set_atomic_byte(&self, id: u64, value: u8) -> Result<(), MemoryError> {
        match self.value(id)? {
            Value::AtomicByte(cell) => { cell.store(value, Ordering::Release); Ok(()) }
            _ => Err(MemoryError::WrongKind),
        }
    }

    /// Create an atomic byte-string cell with a fixed retained-snapshot budget.
    pub fn new_atomic_string(&mut self, bytes: &[u8], retention_limit: usize) -> Result<u64, MemoryError> {
        self.insert(Value::AtomicString(AtomicString::new(bytes, retention_limit)?))
    }

    /// Borrow the currently published byte-string snapshot for this handle.
    pub fn get_atomic_string(&self, id: u64) -> Result<&[u8], MemoryError> {
        match self.value(id)? {
            Value::AtomicString(value) => Ok(value.get()),
            _ => Err(MemoryError::WrongKind),
        }
    }

    /// Publish a new immutable snapshot, returning an error if its budget or writer lock rejects it.
    pub fn set_atomic_string(&self, id: u64, bytes: &[u8]) -> Result<(), MemoryError> {
        match self.value(id)? {
            Value::AtomicString(value) => value.set(bytes),
            _ => Err(MemoryError::WrongKind),
        }
    }

    /// Remove a block handle; the handle becomes unknown and is not reused.
    pub fn release(&mut self, id: u64) -> Result<(), MemoryError> {
        let index = self.blocks.iter().position(|block| block.id == id)
            .ok_or(MemoryError::UnknownHandle)?;
        self.blocks.swap_remove(index);
        Ok(())
    }

    /// Invalidate all handles; do not reset the identity counter.
    pub fn clear(&mut self) { self.blocks.clear(); }
    /// Return the number of currently registered blocks.
    pub fn len(&self) -> usize { self.blocks.len() }
    /// Return whether the store has no registered blocks.
    pub fn is_empty(&self) -> bool { self.blocks.is_empty() }
}
