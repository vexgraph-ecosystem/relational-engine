//! DEFINITION: primitive atomic publication of immutable byte-string snapshots.
//! Readers acquire one pointer and borrow a whole snapshot without a lock.
//! OVERVIEW: AtomicString { current: AtomicPtr<Snapshot>, history: Mutex<History>,
//! retention_limit: usize }. Private helpers live in snapshot.rs and history.rs.
//! API: new/get/set. Writers try_lock, never wait; Busy preserves state.
//! The caller-selected retention budget counts snapshot headers and byte lengths.
//! Old snapshots survive until owner destruction, so old Rust borrows remain valid.
//! Registration/destruction require exclusion of readers. Set is cold/allocation-
//! bearing. Get is one acquire load, no allocation/log/lock. Arbitrary Bytes allowed.

use crate::annotation::overview;
use std::sync::{Mutex, atomic::{AtomicPtr, Ordering}};
use crate::MemoryError;
use super::{history::History, snapshot::Snapshot};

#[overview]
pub struct AtomicString {
    current: AtomicPtr<Snapshot>,
    history: Mutex<History>,
    retention_limit: usize,
}

impl AtomicString {
    /// Publish the initial snapshot and establish a retained-byte budget for future updates.
    pub fn new(bytes: &[u8], retention_limit: usize) -> Result<Self, MemoryError> {
        let value = Self {
            current: AtomicPtr::new(std::ptr::null_mut()),
            history: Mutex::new(History { snapshots: Vec::new(), retained: 0 }),
            retention_limit,
        };
        value.set(bytes)?;
        Ok(value)
    }

    /// Acquire the current immutable snapshot; its borrow remains valid for this owner's lifetime.
    pub fn get(&self) -> &[u8] {
        let pointer = self.current.load(Ordering::Acquire);
        // Construction publishes before returning. Retention owns this allocation
        // until self dies; the returned borrow cannot outlive self.
        unsafe { &(*pointer).bytes }
    }

    /// Copy and publish a new snapshot, rejecting busy writers or exhausted retention without replacing current data.
    pub fn set(&self, source: &[u8]) -> Result<(), MemoryError> {
        let cost = source.len().checked_add(std::mem::size_of::<Snapshot>())
            .ok_or(MemoryError::Exhausted)?;

        let mut history = self.history.try_lock().map_err(|_| MemoryError::Busy)?;
        let retained = history.retained.checked_add(cost).ok_or(MemoryError::Exhausted)?;

        if retained > self.retention_limit {
            return Err(MemoryError::Capacity);
        }

        history.snapshots.try_reserve(1).map_err(|_| MemoryError::Allocation)?;

        let mut bytes = Vec::new();
        bytes.try_reserve_exact(source.len()).map_err(|_| MemoryError::Allocation)?;
        bytes.extend_from_slice(source);
        
        // Like the learning owner's Box constructor, this can abort on allocator
        // OOM; no claim of exhaustive fallible allocation is made.
        let mut snapshot = Box::new(Snapshot { bytes: bytes.into_boxed_slice() });
        let pointer: *mut Snapshot = &mut *snapshot;
        history.snapshots.push(snapshot);
        history.retained = retained;
        self.current.store(pointer, Ordering::Release);

        Ok(())
    }
}
