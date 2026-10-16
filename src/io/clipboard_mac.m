#import <AppKit/AppKit.h>
#include "io/clipboard.h"
// Production R2 I/O owner: Relational Engine; native pasteboard ABI preserved.
#include "annotation/platform_exclusive.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Clipboard (macOS bridge)
 * ============================================================================
 * Native NSPasteboard bridge supporting UTF-8 text and raw RGBA8 bitmaps:
 * Clipboard_getText copies into a caller-owned buffer (truncation-safe),
 * Clipboard_getImage decodes TIFF/PNG pasteboard data into caller-owned
 * pixel memory with outputs-first width/height outputs, and Clipboard_setImage
 * encodes RGBA8 through NSBitmapImageRep. Zero engine allocation — all
 * buffers are the caller's. This is the L4 platform seam; Linux/Wayland
 * builds provide a sibling implementation of the same io/clipboard.h
 * contract. Lives at R2 as a leaf I/O behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Clipboard_mac (io/clipboard_mac.m)
 * ============================================================================
 * Native macOS NSPasteboard bridge supporting UTF-8 text and raw RGBA bitmaps.
 *
 * STRUCT FIELDS (Mirroring io/clipboard.h):
 * ----------------------------------------------------------------------------
 *   ClipboardImage {
 *     int32_t width;      // pixel width of the RGBA8 bitmap
 *     int32_t height;     // pixel height of the RGBA8 bitmap
 *     size_t byteLength;  // width * height * 4
 *     uint8_t *pixels;    // RGBA8 packed pixels
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Core Functions: (.h)
 *   - Clipboard_hasText(void)
 *   - Clipboard_hasImage(void)
 *   - Clipboard_getText(dest, maxBytes)
 *   - Clipboard_setText(text)
 *   - Clipboard_getImage(outWidth, outHeight, dest, maxBytes)
 *   - Clipboard_setImage(pixels, width, height)
 *   - Clipboard_clear(void)
 * ============================================================================
 */

;;PLATFORM_EXCLUSIVE("Mac")

/** Report whether the system pasteboard advertises its plain-text format. */
bool Clipboard_hasText(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSArray *types = [pb types];
        return [types containsObject:NSPasteboardTypeString];
    }
}

/** Report whether the system pasteboard advertises TIFF or PNG image data. */
bool Clipboard_hasImage(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSArray *types = [pb types];
        return [types containsObject:NSPasteboardTypeTIFF] ||
               [types containsObject:NSPasteboardTypePNG];
    }
}

/** Copy pasteboard UTF-8 text into the caller buffer, NUL-terminating and truncating to its capacity. */
size_t Clipboard_getText(char *dest, size_t maxBytes) {
    if (dest == nullptr || maxBytes == 0) {
        return 0;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *str = [pb stringForType:NSPasteboardTypeString];
        if (str == nil) {
            dest[0] = '\0';
            return 0;
        }

        const char *utf8 = [str UTF8String];
        if (utf8 == nullptr) {
            dest[0] = '\0';
            return 0;
        }

        size_t len = strlen(utf8);
        if (len >= maxBytes) {
            len = maxBytes - 1;
        }
        memcpy(dest, utf8, len);
        dest[len] = '\0';
        return len;
    }
}

/** Replace pasteboard contents with the supplied UTF-8 text. */
bool Clipboard_setText(const char *text) {
    if (text == nullptr) {
        return false;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSString *str = [NSString stringWithUTF8String:text];
        if (str == nil) {
            return false;
        }
        return [pb setString:str forType:NSPasteboardTypeString];
    }
}

/** Decode pasteboard TIFF/PNG into caller RGBA storage when it fits, then return dimensions. */
bool Clipboard_getImage(int32_t *outWidth, int32_t *outHeight, void *dest, size_t maxBytes) {
    if (dest == nullptr || outWidth == nullptr || outHeight == nullptr) {
        return false;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSData *data = [pb dataForType:NSPasteboardTypeTIFF];
        if (data == nil) {
            data = [pb dataForType:NSPasteboardTypePNG];
        }
        if (data == nil) {
            return false;
        }

        NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:data];
        if (rep == nil) {
            return false;
        }

        int32_t w = (int32_t) [rep pixelsWide];
        int32_t h = (int32_t) [rep pixelsHigh];
        size_t needed = (size_t) w * (size_t) h * 4;
        if (needed > maxBytes) {
            return false;
        }

        *outWidth = w;
        *outHeight = h;

        unsigned char *bitmapData = [rep bitmapData];
        if (bitmapData != nullptr) {
            memcpy(dest, bitmapData, needed);
            return true;
        }
        return false;
    }
}

/** Encode caller-provided RGBA pixels as TIFF and replace pasteboard image contents. */
bool Clipboard_setImage(const void *pixels, int32_t width, int32_t height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    @autoreleasepool {
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:width * 4
                        bitsPerPixel:32];

        if (rep == nil) {
            return false;
        }

        unsigned char *destData = [rep bitmapData];
        if (destData == nullptr) {
            return false;
        }
        memcpy(destData, pixels, (size_t) width * (size_t) height * 4);

        NSData *tiffData = [rep TIFFRepresentation];
        if (tiffData == nil) {
            return false;
        }

        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return [pb setData:tiffData forType:NSPasteboardTypeTIFF];
    }
}

/** Clear all items from the system pasteboard. */
void Clipboard_clear(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
    }
}
