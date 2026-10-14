//! Explicit foreign interfaces, separate from native storage behavior.
pub mod memory;
pub mod variable_registry;
pub mod row_pool;
pub mod fixed_mapped_file;
pub use variable_registry::{re_variables_new, re_variables_drop, re_variables_add,
    re_variables_find, re_variables_slot, re_variables_set_pointer};
// Preserve the original ffi::re_memory_* client paths.
pub use memory::{re_memory_copy, re_memory_drop, re_memory_new, re_memory_read};
pub use memory::{re_memory_get_byte, re_memory_new_atomic_byte, re_memory_get_atomic_byte,
    re_memory_set_atomic_byte, re_memory_new_atomic_string, re_memory_get_atomic_string,
    re_memory_set_atomic_string};
