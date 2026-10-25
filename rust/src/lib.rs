//! Learning storage layer, not a replacement for Vexspoke's slab allocator.
//!
//! ```
//! use relational_engine_scratchpad::{Memory, Bytes};
//! let mut memory = Memory!();
//! let hello = memory.copy_bytes(Bytes!("hello"))?;
//! assert_eq!(memory.get(hello)?, b"hello");
//! # Ok::<(), relational_engine_scratchpad::MemoryError>(())
//! ```
pub mod annotation;
pub mod nio;
pub mod primitives;
pub mod io;
pub mod compress;
pub mod variable;
pub mod r#struct;
pub mod r#virtual;
pub mod text;
pub mod ffi;
// Preserve existing short client paths while exposing organized modules.
pub use nio::mem;
pub use primitives::string;
pub use nio::mem::Memory;
pub use nio::memory_error::MemoryError;
pub use nio::fixed_mapped_file::FixedMappedFile;
pub use nio::growable_mapped_file::GrowableMappedFile;
pub use nio::mapping_error::MappingError;
pub use io::preallocated_file::PreallocatedFile;
pub use io::preallocation_error::PreallocationError;
pub use nio::chunk::Chunk;
pub use nio::handle::Handle;
pub use nio::typed_chunk::TypedChunk;
pub use nio::storage_error::StorageError;
pub use r#struct::chunked_list::ChunkedList;
pub use r#struct::typed_pool::TypedPool;
pub use r#struct::row_pool::RowPool;
pub use nio::row_handle::RowHandle;
pub use nio::row_chunk::RowChunk;
pub use variable::variable_slot::VariableSlot;
pub use variable::variable_registry::VariableRegistry;

/// Canonical constructor convenience, sharing the type's name in the macro namespace.
// INTENTIONAL(vex): CamelCase constructor macros preserve class-like construction;
// ordinary Rust methods remain snake_case.
#[macro_export]
macro_rules! Memory {
    () => { $crate::Memory::new() };
}

/// Compatibility spelling; new construction uses Memory!().
#[macro_export]
macro_rules! memory {
    () => { $crate::Memory!() };
}

/// Borrow UTF-8 Bytes without allocating or constructing a character array.
#[macro_export]
macro_rules! Bytes {
    ($text:expr) => { ($text).as_bytes() };
}

/// Compatibility spelling; new byte-view construction uses Bytes!().
#[macro_export]
macro_rules! bytes {
    ($text:expr) => { $crate::Bytes!($text) };
}
