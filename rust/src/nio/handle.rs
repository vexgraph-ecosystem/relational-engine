//! DEFINITION: Handle is a generation-tagged identity for a reusable typed slot.
//! The index is a pool-local LOCATION; the generation distinguishes reuses, so a
//! handle whose slot was removed and reused is rejected as stale. A zero handle
//! is invalid and never issued. Identity is local to its original owner; another
//! chunk/pool can issue the same pair. A Handle owns nothing and borrows nothing: it is
//! valid only while its slot is live and its generation still matches.
//! OVERVIEW: #[repr(C)] Handle { index: usize, generation: u32 }.
//! API: new/zero/index/generation/is_zero/to_string/to_string_struct.
//! No owning class here; the owning TypedChunk/TypedPool validates the handle.
use crate::annotation::overview;
use super::projection;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[overview]
pub struct Handle {
    index: usize,      // pool-local slot location (the same index the raw API uses)
    generation: u32,   // slot generation captured at issue; distinguishes a later reuse
}

impl Handle {
    /// Build a handle from a pool-local index and its slot generation.
    pub fn new(index: usize, generation: u32) -> Self {
        Self { index, generation }
    }
    /// The pristine handle (index 0, generation 0); never handed out for a live slot.
    pub fn zero() -> Self {
        Self { index: 0, generation: 0 }
    }

    /// The pool-local location this handle names.
    pub fn index(&self) -> usize {
        self.index
    }
    /// The slot generation captured when the handle was issued.
    pub fn generation(&self) -> u32 {
        self.generation
    }
    /// Whether this is the pristine (never-issued) handle.
    pub fn is_zero(&self) -> bool {
        self.index == 0 && self.generation == 0
    }

    /// Write a bounded value summary and report whether the destination was truncated.
    pub fn to_string(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(
            format!("Handle(index={}, generation={})", self.index, self.generation),
            dest,
            out_truncated,
        )
    }

    /// Write a bounded one-level field summary and report destination truncation.
    pub fn to_string_struct(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(
            format!("Handle {{ index: {}, generation: {} }}", self.index, self.generation),
            dest,
            out_truncated,
        )
    }
}

#[macro_export]
macro_rules! Handle {
    () => { $crate::nio::handle::Handle::zero() };
    ($index:expr, $generation:expr) => { $crate::nio::handle::Handle::new($index, $generation) };
}
