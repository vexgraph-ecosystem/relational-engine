#include "io/frame_inbox.h"
// Production R2 I/O owner: Relational Engine; caller-fed bounded frame ABI preserved.

#include <string.h>
#include <time.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FrameInbox
 * ============================================================================
 * A bounded, thread-free frame inbox for the caller (legacy API name:
 * FrameInbox). This is not a WebSocket implementation. Fixed rx
 * buffer, connection state, cancel flag, and timeout — zero steady-state
 * allocation, no threads, no sockets. Bytes arrive via FrameInbox_feed from a
 * caller; no socket adapter is wired to this inbox in the current workspace.
 * FrameInbox_poll drains pending Bytes with a
 * bounded wait: timeoutNs clamped to FRAME_INBOX_POLL_MAX_NS (100ms), ~1ms
 * nanosleep slices, cancel flag re-checked each slice, false (drop-degrade)
 * on timeout per the Bounded Wait Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FrameInbox (io/frame_inbox.c)
 * ============================================================================
 * A bounded, thread-free frame inbox for the caller (legacy API name:
 * FrameInbox). This is not a WebSocket implementation.
 * Fixed rx buffer, connection state, cancel flag, and timeout — zero
 * steady-state allocation, no threads, no sockets. Bytes arrive via
 * FrameInbox_feed from its caller; a socket adapter is not implemented here.
 * FrameInbox_poll
 * drains pending Bytes with a bounded wait: timeoutNs clamped to
 * FRAME_INBOX_POLL_MAX_NS (100ms, Rule 27), ~1ms nanosleep slices,
 * cancel flag re-checked each slice, false (drop-degrade) on timeout.
 *
 * STRUCT FIELDS (Mirroring io/frame_inbox.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   FrameInbox {
 *     FrameInboxState state;             // IDLE/CONNECTING/OPEN/CLOSING/CLOSED
 *     uint8_t rxBuf[FRAME_INBOX_RX_CAP]; // fixed 4096B pending buffer, no alloc
 *     uint32_t rxLen;                  // pending Bytes (0..RX_CAP)
 *     uint64_t timeoutNs;              // default poll budget, clamped Rule 27
 *     bool cancelled;                  // cancel flag; poll drops on sight
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - FrameInbox()               : FrameInbox_0()
 *   - FrameInbox(timeoutNs)      : FrameInbox_1(timeoutNs)
 *
 * Core Functions:
 *   - FrameInbox_free(self)
 *   - FrameInbox_feed(self, Bytes, len)
 *   - FrameInbox_poll(self, timeoutNs, dest, destCap, outLen)
 *   - FrameInbox_cancel(self)
 *   - FrameInbox_clear(self)
 *
 * Setters:
 *   - FrameInbox_setState(self, state)
 *   - FrameInbox_setTimeoutNs(self, timeoutNs)
 *   - FrameInbox_setCancelled(self, cancelled)
 *
 * Getters:
 *   - FrameInbox_getState(self)
 *   - FrameInbox_getTimeoutNs(self)
 *   - FrameInbox_isCancelled(self)
 *   - FrameInbox_getPending(self)
 * ============================================================================
 */

// io/frame_inbox.c — legacy FrameInbox API for a caller-fed bounded frame inbox; never blocks
// past FRAME_INBOX_POLL_MAX_NS and never touches a socket or a thread.

/** Read the monotonic clock in nanoseconds for the poll timeout budget. */
static uint64_t frameInboxNowNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

/** Sleep for one cancel-check interval used by bounded polling. */
static void frameInboxSleepSlice(void) {
    struct timespec slice;
    slice.tv_sec = 0;
    slice.tv_nsec = 1000000L;
    nanosleep(&slice, nullptr);
}

/** Allocate and initialize a client slot, clamping its default timeout to the supported maximum. */
static FrameInbox *frameInboxCreate(uint64_t timeoutNs) {
    FrameInbox *self = (FrameInbox*) Memory_alloc(TYPE_FRAME_INBOX_SINGLETON, sizeof(FrameInbox));
    if (!self)
        return nullptr;
    (*self).state = FRAME_INBOX_IDLE;
    (*self).rxLen = 0;
    if (timeoutNs > FRAME_INBOX_POLL_MAX_NS)
        timeoutNs = FRAME_INBOX_POLL_MAX_NS;
    (*self).timeoutNs = timeoutNs;
    (*self).cancelled = false;
    return self;
}

// CONSTRUCTORS
/** Construct a client with a zero-duration default poll timeout. */
FrameInbox *FrameInbox_0(void) {
    return frameInboxCreate(0);
}

/** Construct a client with the supplied default timeout, clamped to the maximum. */
FrameInbox *FrameInbox_1(uint64_t timeoutNs) {
    return frameInboxCreate(timeoutNs);
}

// CORE FUNCTIONS
/** Release the arena-backed client slot; callers must exclude concurrent polling. */
void FrameInbox_free(FrameInbox *self) {
    if (!self)
        return;
    Memory_free(self);
}

/** Append caller-fed bytes to the fixed receive slot, rejecting empty or overflowing input. */
bool FrameInbox_feed(FrameInbox *self, const uint8_t *Bytes, uint32_t len) {
    if (!self || !Bytes)
        return false;
    if (len == 0)
        return false;
    if (len > FRAME_INBOX_RX_CAP - (*self).rxLen)
        return false;
    memcpy((*self).rxBuf + (*self).rxLen, Bytes, len);
    (*self).rxLen += len;
    return true;
}

/** Wait within the capped budget for buffered bytes, then copy and consume them if the destination fits. */
bool FrameInbox_poll(FrameInbox *self, uint64_t timeoutNs, uint8_t *dest,
                   uint32_t destCap, uint32_t *outLen) {
    if (outLen)
        (*outLen) = 0;
    if (!self || !dest || destCap == 0 || !outLen)
        return false;
    if (timeoutNs > FRAME_INBOX_POLL_MAX_NS)
        timeoutNs = FRAME_INBOX_POLL_MAX_NS;
    if ((*self).cancelled)
        return false;
    if ((*self).rxLen == 0 && timeoutNs == 0)
        return false;
    uint64_t start = frameInboxNowNs();
    for (;;) {
        if ((*self).cancelled)
            return false;
        if ((*self).rxLen > 0)
            break;
        if (frameInboxNowNs() - start >= timeoutNs)
            return false;
        frameInboxSleepSlice();
    }
    uint32_t n = (*self).rxLen;
    if (n > destCap)
        return false;
    memcpy(dest, (*self).rxBuf, n);
    (*self).rxLen = 0;
    (*outLen) = n;
    return true;
}

/** Set the cancellation flag so current or future polls return without waiting further. */
void FrameInbox_cancel(FrameInbox *self) {
    if (!self)
        return;
    (*self).cancelled = true;
}

/** Discard all pending receive bytes without changing connection or cancellation state. */
void FrameInbox_clear(FrameInbox *self) {
    if (!self)
        return;
    (*self).rxLen = 0;
}

// SETTERS
/** Replace the stored connection state when the client pointer is non-null. */
void FrameInbox_setState(FrameInbox *self, FrameInboxState state) {
    if (!self)
        return;
    (*self).state = state;
}

/** Set the default poll timeout, clamped to the maximum permitted interval. */
void FrameInbox_setTimeoutNs(FrameInbox *self, uint64_t timeoutNs) {
    if (!self)
        return;
    if (timeoutNs > FRAME_INBOX_POLL_MAX_NS)
        timeoutNs = FRAME_INBOX_POLL_MAX_NS;
    (*self).timeoutNs = timeoutNs;
}

/** Set or clear the poll cancellation flag. */
void FrameInbox_setCancelled(FrameInbox *self, bool cancelled) {
    if (!self)
        return;
    (*self).cancelled = cancelled;
}

// GETTERS
/** Return the stored state, using CLOSED as the null-pointer default. */
FrameInboxState FrameInbox_getState(const FrameInbox *self) {
    if (!self)
        return FRAME_INBOX_CLOSED;
    return (*self).state;
}

/** Return the stored default poll timeout, or zero for a null pointer. */
uint64_t FrameInbox_getTimeoutNs(const FrameInbox *self) {
    if (!self)
        return 0;
    return (*self).timeoutNs;
}

/** Return the cancellation flag, treating a null client as cancelled. */
bool FrameInbox_isCancelled(const FrameInbox *self) {
    if (!self)
        return true;
    return (*self).cancelled;
}

/** Return the number of buffered receive bytes, or zero for a null client. */
uint32_t FrameInbox_getPending(const FrameInbox *self) {
    if (!self)
        return 0;
    return (*self).rxLen;
}
