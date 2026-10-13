//! Cold storage admission errors; existing rows remain unchanged on rejection.
use crate::annotation::overview;

#[overview]
#[derive(Debug, PartialEq, Eq)]
pub enum StorageError {
    Allocation,
    Layout,
    Capacity,
    Bounds,
    InvalidName,
    Duplicate,
}
