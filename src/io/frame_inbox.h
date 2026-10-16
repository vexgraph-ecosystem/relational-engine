#ifndef IO_FRAME_INBOX_H
#define IO_FRAME_INBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "type/type.h"

// io/frame_inbox.h — engine-owned bounded caller-fed byte inbox.
//
// A bounded, thread-free frame inbox: fixed rx buffer, state, cancel flag, and
// timeout. It is not a WebSocket implementation. Owns NO sockets and spawns NO
// threads — callers supply Bytes via FrameInbox_feed. FrameInbox_poll drains pending
// Bytes under the Bounded Wait Law.

#define FRAME_INBOX_RX_CAP 4096u
#define FRAME_INBOX_POLL_MAX_NS 100000000ULL
#define TYPE_FRAME_INBOX_SINGLETON Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 1u)

typedef enum FrameInboxState {
    FRAME_INBOX_IDLE = 0,
    FRAME_INBOX_CONNECTING = 1,
    FRAME_INBOX_OPEN = 2,
    FRAME_INBOX_CLOSING = 3,
    FRAME_INBOX_CLOSED = 4
} FrameInboxState;

typedef struct FrameInbox {
    FrameInboxState state;            // caller-managed state marker
    uint8_t rxBuf[FRAME_INBOX_RX_CAP]; // fixed pending-byte buffer (no alloc)
    uint32_t rxLen;                   // pending Bytes in rxBuf (0..RX_CAP)
    uint64_t timeoutNs;               // default budget capped by the Bounded Wait Law
    bool cancelled;                   // cancel flag; poll drops on sight
} FrameInbox;

// Idle client, zero budget override (timeoutNs 0 = poll arg governs).
FrameInbox *FrameInbox_0(void);

// Client with a default poll budget in nanoseconds (clamped to MAX_NS).
FrameInbox *FrameInbox_1(uint64_t timeoutNs);

#define FrameInbox(...) CONSTRUCTOR_DISPATCH(FrameInbox, __VA_ARGS__)

// Release the client block (null-safe no-op).
void FrameInbox_free(FrameInbox *self);

// Append caller Bytes into the fixed rx buffer. False on NULL self/Bytes,
// zero len, or overflow (buffer keeps old Bytes, caller retries after poll).
bool FrameInbox_feed(FrameInbox *self, const uint8_t *Bytes, uint32_t len);

// Drain pending Bytes into dest (Semantic Consistency Law, Argument order). Waits at
// most timeoutNs (clamped to FRAME_INBOX_POLL_MAX_NS) in ~1ms slices,
// re-checking the cancel flag each slice; returns false (drop-degrade,
// *outLen 0) on timeout, cancel, empty buffer, or NULL args.
bool FrameInbox_poll(FrameInbox *self, uint64_t timeoutNs, uint8_t *dest,
                     uint32_t destCap, uint32_t *outLen);

// Raise the cancel flag (in-flight poll drops at the next slice).
void FrameInbox_cancel(FrameInbox *self);

// Discard pending Bytes without delivering (core reset, keeps state).
void FrameInbox_clear(FrameInbox *self);

// Symmetric accessors (Single Class Per File Law, symmetric-accessors clause).
void FrameInbox_setState(FrameInbox *self, FrameInboxState state);
FrameInboxState FrameInbox_getState(const FrameInbox *self);
void FrameInbox_setTimeoutNs(FrameInbox *self, uint64_t timeoutNs);
uint64_t FrameInbox_getTimeoutNs(const FrameInbox *self);
void FrameInbox_setCancelled(FrameInbox *self, bool cancelled);
bool FrameInbox_isCancelled(const FrameInbox *self);
uint32_t FrameInbox_getPending(const FrameInbox *self);

#endif
