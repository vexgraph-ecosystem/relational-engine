//! DEFINITION: RowHandle is a non-owning, process-local identity issued by RowPool.
//! OVERVIEW: repr(C) fields owner u64, index usize, generation u32, reserved u32.
//! Zero is invalid. owner distinguishes pools, including after destruction/recreation.
//! API: new/zero/owner/index/generation/is_zero/to_string/to_string_struct.
//! reserved must be zero. This record is not a pointer, ecosystem type ID or disk ID.
use crate::annotation::overview;
use super::projection;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[overview]
pub struct RowHandle {
    owner: u64,       // resident engine-issued pool identity, never reused in this process
    index: usize,     // pool-local location
    generation: u32,  // nonzero generation of the occupied slot
    reserved: u32,    // ABI padding explicitly initialized to zero
}

impl RowHandle {
    /// Construct a candidate identity; only its owning pool can validate it.
    pub fn new(owner: u64, index: usize, generation: u32) -> Self {
        Self { owner, index, generation, reserved: 0 }
    }

    /// Return the invalid zero identity.
    pub fn zero() -> Self { Self::new(0, 0, 0) }

    /// Return the pool identity, not an address.
    pub fn owner(&self) -> u64 { self.owner }

    /// Return the pool-local slot location.
    pub fn index(&self) -> usize { self.index }

    /// Return the generation captured at issue.
    pub fn generation(&self) -> u32 { self.generation }

    /// Whether this is the all-zero invalid identity.
    pub fn is_zero(&self) -> bool { *self == Self::zero() }

    /// Validate fixed ABI bits; this does not establish a live slot.
    pub(crate) fn has_valid_bits(&self) -> bool {
        self.owner != 0 && self.generation != 0 && self.reserved == 0
    }

    /// Write a bounded human value projection (cold, may allocate).
    pub fn to_string(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowHandle({}, {}, {})", self.owner, self.index, self.generation), dest, truncated)
    }

    /// Write own fields in declaration order (cold, may allocate).
    pub fn to_string_struct(&self, dest: &mut [u8], truncated: &mut bool) -> bool {
        projection::write(format!("RowHandle {{ owner: {}, index: {}, generation: {}, reserved: {} }}",
            self.owner, self.index, self.generation, self.reserved), dest, truncated)
    }
}

#[macro_export]
macro_rules! RowHandle {
    () => { $crate::nio::row_handle::RowHandle::zero() };
    ($owner:expr, $index:expr, $generation:expr) => {
        $crate::nio::row_handle::RowHandle::new($owner, $index, $generation)
    };
}
