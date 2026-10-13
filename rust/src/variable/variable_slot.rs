//! DEFINITION: VariableSlot is the 32-byte label on a separately owned value.
//! OVERVIEW: #[repr(C)] VariableSlot { name: [u8; 24], pointer: *const u8 }.
//! This pointer is a borrowed VALUE address, not StringSlot's intrusive self link.
//! API: new/zero/set_name/get_name/get_name_bytes/set_pointer/get_pointer/is_empty;
//! bounded to_string/to_string_struct. No function dereferences the value pointer.
//! Valid names are 1..23 ASCII bytes, folded lowercase, using a-z/0-9/_/$/- and
//! dots between nonempty segments. NUL/non-ASCII/overlong names reject, never cut.
//! Invalid setters preserve old state. Null value pointer means unbound and is legal.
//! Target lifetime/type/synchronization belong to its owner. Copying/moving a slot
//! does not change ownership. This initial plain pointer is not atomically replaced.
use crate::annotation::overview;
use crate::nio::{storage_error::StorageError, projection};

pub const VARIABLE_NAME_BYTES: usize = 24;
pub const VARIABLE_NAME_MAX: usize = VARIABLE_NAME_BYTES - 1;

#[repr(C)]
#[derive(Clone, Copy)]
#[overview]
pub struct VariableSlot {
    name: [u8; VARIABLE_NAME_BYTES],
    pointer: *const u8,
}

const _: () = assert!(std::mem::size_of::<VariableSlot>() == 32);
const _: () = assert!(std::mem::offset_of!(VariableSlot, name) == 0);
const _: () = assert!(std::mem::offset_of!(VariableSlot, pointer) == 24);
const _: () = assert!(std::mem::size_of::<*const u8>() == 8);

impl VariableSlot {
    /// Construct a slot after validating and folding its name; the value pointer is borrowed and opaque.
    pub fn new(name: &[u8], pointer: *const u8) -> Result<Self, StorageError> {
        let mut value = Self::zero();
        value.set_name(name)?;
        value.pointer = pointer;
        Ok(value)
    }

    /// Return an unnamed slot with a null value pointer.
    pub fn zero() -> Self { Self { name: [0; VARIABLE_NAME_BYTES], pointer: std::ptr::null() } }

    /// Validate and lowercase a segmented ASCII name; on rejection, preserve the previous name.
    pub fn set_name(&mut self, name: &[u8]) -> Result<(), StorageError> {
        if name.is_empty() || name.len() > VARIABLE_NAME_MAX { return Err(StorageError::InvalidName); }
        let mut folded = [0; VARIABLE_NAME_BYTES];
        let mut segment_length = 0;
        for (index, byte) in name.iter().copied().enumerate() {
            let byte = byte.to_ascii_lowercase();
            if byte == b'.' {
                if segment_length == 0 { return Err(StorageError::InvalidName); }
                segment_length = 0;
            } else {
                if !byte.is_ascii_lowercase() && !byte.is_ascii_digit() && !b"_$-".contains(&byte) {
                    return Err(StorageError::InvalidName);
                }
                segment_length += 1;
            }
            folded[index] = byte;
        }
        if segment_length == 0 { return Err(StorageError::InvalidName); }
        self.name = folded;
        Ok(())
    }

    /// Borrow the name bytes without its trailing NUL padding.
    pub fn get_name(&self) -> &[u8] {
        let length = self.name.iter().position(|byte| *byte == 0).unwrap_or(VARIABLE_NAME_BYTES);
        &self.name[..length]
    }
    /// Borrow the complete fixed-width, NUL-padded name storage.
    pub fn get_name_bytes(&self) -> &[u8; VARIABLE_NAME_BYTES] { &self.name }
    /// Store a borrowed opaque value pointer without dereferencing or taking ownership of it.
    pub fn set_pointer(&mut self, pointer: *const u8) { self.pointer = pointer; }
    /// Return the borrowed opaque value pointer without validating its lifetime or type.
    pub fn get_pointer(&self) -> *const u8 { self.pointer }
    /// Return whether the slot has no name and is therefore unbound.
    pub fn is_empty(&self) -> bool { self.name[0] == 0 }

    /// Write a bounded value summary and report whether the destination was truncated.
    pub fn to_string(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("VariableSlot(\"{}\", {:p})", String::from_utf8_lossy(self.get_name()), self.pointer), dest, out_truncated)
    }
    /// Write a bounded one-level field summary and report destination truncation.
    pub fn to_string_struct(&self, dest: &mut [u8], out_truncated: &mut bool) -> bool {
        projection::write(format!("VariableSlot {{ name: \"{}\", pointer: {:p} }}", String::from_utf8_lossy(self.get_name()), self.pointer), dest, out_truncated)
    }
}

#[macro_export]
macro_rules! VariableSlot {
    () => { $crate::variable::variable_slot::VariableSlot::zero() };
    ($name:expr) => { $crate::variable::variable_slot::VariableSlot::new($name, std::ptr::null()) };
    ($name:expr, $pointer:expr) => { $crate::variable::variable_slot::VariableSlot::new($name, $pointer) };
}
