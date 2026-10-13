//! Private allocation row: owner-local handle plus its stored value.
use crate::annotation::overview;
use super::value::Value;

#[overview]
pub(super) struct Block {
    pub(super) id: u64,
    pub(super) value: Value,
}
