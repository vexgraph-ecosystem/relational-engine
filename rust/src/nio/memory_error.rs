//! Error results for managed storage. Each error preserves stored content.
use crate::annotation::overview;

#[overview]
#[derive(Debug, PartialEq, Eq)]
pub enum MemoryError {
    Allocation,
    Exhausted,
    UnknownHandle,
    WrongKind,
    Bounds,
    Busy,
    Capacity,
}
