//! Create-new physical byte-file reservation; buffered readers/writers, gathering,
//! indexing and watching (FFF-style discovery) remain future work.
//! File-backed byte mappings belong to nio, not an implicit read in a getter.
//! Manifest-backed persistence is a future contract: use record IDs/file offsets,
//! not serialized RAM pointers. Durability and concurrent commits remain unproved.
pub mod preallocated_file;
pub mod preallocation_error;
pub(crate) mod preallocation;
