//! Storage ownership and allocation vocabulary.
pub mod handle;
pub mod mem;
pub mod memory_error;
pub mod fixed_mapped_file;
pub mod mapping_error;
pub mod storage_error;
pub mod chunk;
pub mod typed_chunk;
pub mod row_chunk;
pub mod row_handle;
pub(crate) mod projection;
mod block;
mod value;
