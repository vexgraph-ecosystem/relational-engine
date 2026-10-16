//! Internal storage kind, separate from vexgraph-ecosystem-gitrepo type IDs.
use crate::annotation::overview;
use std::sync::atomic::AtomicU8;
use crate::primitives::atomic_string::AtomicString;

#[overview]
pub(super) enum Value {
    Bytes(Box<[u8]>),
    AtomicByte(AtomicU8),
    AtomicString(AtomicString),
}
