#ifndef IO_CLIPBOARD_H
#define IO_CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// io/clipboard.h — System Clipboard & Pasteboard Bridge.
//
// Provides zero-allocation bridging between off-heap engine buffers and the
// operating system clipboard (NSPasteboard on macOS, Wayland/X11 on Linux).
//
// Supports:
// 1. UTF-8 Plain Text copy/paste.
// 2. Raw RGBA8 2D Bitmap copy/paste.
// 3. Clear and format inspection.

typedef struct ClipboardImage {
    int32_t width;
    int32_t height;
    size_t  byteLength;
    uint8_t *pixels; // RGBA8 packed pixels
} ClipboardImage;

// Check if clipboard contains text or image data
bool   Clipboard_hasText(void);
bool   Clipboard_hasImage(void);

// Read UTF-8 text from clipboard into caller-owned dest buffer.
// Returns the number of Bytes written, or 0 if empty/no text.
size_t Clipboard_getText(char *dest, size_t maxBytes);

// Write UTF-8 text string to clipboard.
bool   Clipboard_setText(const char *text);

// Read raw RGBA8 image pixels from clipboard.
// Outputs-first: writes dimensions and pixel Bytes into caller storage.
bool   Clipboard_getImage(int32_t *outWidth, int32_t *outHeight, void *dest, size_t maxBytes);

// Write raw RGBA8 image pixels to clipboard.
bool   Clipboard_setImage(const void *pixels, int32_t width, int32_t height);

// Clear clipboard contents.
void   Clipboard_clear(void);

#endif
