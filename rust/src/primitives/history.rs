//! Private primitive string retention ledger. Snapshots live until destruction.
use crate::annotation::overview;
use super::snapshot::Snapshot;

#[overview]
pub(super) struct History {
    pub(super) snapshots: Vec<Box<Snapshot>>,
    pub(super) retained: usize,
}
