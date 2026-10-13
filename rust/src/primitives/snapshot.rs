//! Private immutable primitive string version; never mutated after publication.
use crate::annotation::overview;

#[overview]
pub(super) struct Snapshot {
    pub(super) bytes: Box<[u8]>,
}
