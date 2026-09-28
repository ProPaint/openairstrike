#!/usr/bin/env python3
"""Independent, pure-Python reference decoder for AirStrike 3D's .tga textures.

See docs/spec/tga.md for the format subset this covers and the reasoning behind the
choices below (in particular how `has_alpha` is derived, and the vertical-flip-to-
top-down orientation rule). No third-party dependencies: stdlib only.

Usage (manual inspection only; the real entry point is test_tga.py):
  tga.py info <file>...   print header fields and decoded size/hasAlpha per file
  tga.py hash <file>...   print sha1 of the decoded top-down RGBA8 bytes per file
"""
import struct
import sys


class TgaError(ValueError):
    """Raised by decode_tga() on any malformed or unsupported input."""


class TgaImage:
    __slots__ = ("width", "height", "has_alpha", "rgba")

    def __init__(self, width, height, has_alpha, rgba):
        self.width = width
        self.height = height
        self.has_alpha = has_alpha
        self.rgba = rgba

    def __repr__(self):
        return f"TgaImage({self.width}x{self.height}, has_alpha={self.has_alpha})"


_COLOUR_ENTRY_SIZE = {15: 2, 16: 2, 24: 3, 32: 4}

# Image types this decoder understands, and the "colour source" each uses.
#   index     -- type 1/9: pixel byte is an index into the colour map
#   truecolor -- type 2/10: pixel bytes directly encode a colour
#   grey      -- type 3/11: pixel byte is a grey level
_TYPE_CLASS = {1: "index", 2: "truecolor", 3: "grey", 9: "index", 10: "truecolor", 11: "grey"}
_RLE_TYPES = (9, 10, 11)

MAX_DIMENSION = 8192


def _expand5(v):
    """Expands a 5-bit channel to 8 bits the usual way (replicate the top 3 bits)."""
    return (v << 3) | (v >> 2)


def _read_colour(buf, off, depth):
    """Reads one BGR(A)-ish colour entry of `depth` bits starting at `buf[off]`.

    Returns (r, g, b, a). Alpha is only ever taken from real data at depth 32; every
    other depth yields a=255, even 16-bit true colour's spare high bit (see tga.md).
    Caller guarantees off + entry size for `depth` fits in buf.
    """
    if depth == 32:
        b, g, r, a = buf[off], buf[off + 1], buf[off + 2], buf[off + 3]
        return r, g, b, a
    if depth == 24:
        b, g, r = buf[off], buf[off + 1], buf[off + 2]
        return r, g, b, 255
    if depth in (15, 16):
        v = buf[off] | (buf[off + 1] << 8)
        r = _expand5(v & 0x1F)
        g = _expand5((v >> 5) & 0x1F)
        b = _expand5((v >> 10) & 0x1F)
        return r, g, b, 255
    raise TgaError(f"unsupported colour depth {depth}")


def decode_tga(data):
    """Decodes a TGA file held in `data` (bytes-like) into a TgaImage.

    Row 0 of the result is always the TOP row of the image, regardless of the
    source's origin. Raises TgaError on any malformed or unsupported input; never
    raises anything else, and never reads past the end of `data`.
    """
    data = memoryview(data)
    n = len(data)
    if n < 18:
        raise TgaError("truncated header (need 18 bytes)")

    id_len = data[0]
    cmap_type = data[1]
    img_type = data[2]
    cmap_first, cmap_len, cmap_depth = struct.unpack_from("<HHB", data, 3)
    _x_origin, _y_origin, width, height = struct.unpack_from("<HHHH", data, 8)
    pixel_depth = data[16]
    descriptor = data[17]

    if width == 0 or height == 0:
        raise TgaError("zero-sized image")
    if width > MAX_DIMENSION or height > MAX_DIMENSION:
        raise TgaError(f"dimensions too large: {width}x{height}")

    off = 18
    if off + id_len > n:
        raise TgaError("truncated image ID field")
    off += id_len

    if img_type not in _TYPE_CLASS:
        raise TgaError(f"unsupported image type {img_type}")
    type_class = _TYPE_CLASS[img_type]
    is_rle = img_type in _RLE_TYPES

    # A colour map, if present (cmap_type == 1), always occupies space between the ID
    # field and the pixel data -- even for a true-colour or greyscale image that
    # doesn't use it -- so it must always be skipped to find the right pixel offset,
    # not just when type_class == "index" actually reads it.
    if cmap_type not in (0, 1):
        raise TgaError(f"bad colour map type {cmap_type}")
    entry_size = 0
    if cmap_type == 1:
        if cmap_depth not in _COLOUR_ENTRY_SIZE:
            raise TgaError(f"unsupported colour map depth {cmap_depth}")
        entry_size = _COLOUR_ENTRY_SIZE[cmap_depth]
    cmap_total_bytes = cmap_len * entry_size
    if off + cmap_total_bytes > n:
        raise TgaError("truncated colour map")
    cmap_base = off

    cmap = []
    if type_class == "index":
        if cmap_type != 1:
            raise TgaError("colour-mapped image type without a colour map")
        cmap = [_read_colour(data, cmap_base + i * entry_size, cmap_depth) for i in range(cmap_len)]
        if pixel_depth != 8:
            raise TgaError(f"unsupported colour-mapped pixel depth {pixel_depth}")
        has_alpha = cmap_depth == 32
        bpp = 1
    elif type_class == "truecolor":
        if pixel_depth not in (15, 16, 24, 32):
            raise TgaError(f"unsupported true-colour pixel depth {pixel_depth}")
        has_alpha = pixel_depth == 32
        bpp = _COLOUR_ENTRY_SIZE[pixel_depth]
    else:  # grey
        if pixel_depth != 8:
            raise TgaError(f"unsupported greyscale pixel depth {pixel_depth}")
        has_alpha = False
        bpp = 1

    off += cmap_total_bytes

    def decode_pixel(buf, o):
        if type_class == "index":
            idx = buf[o]
            rel = idx - cmap_first
            if rel < 0 or rel >= cmap_len:
                raise TgaError(f"colour map index {idx} out of range")
            return cmap[rel]
        if type_class == "truecolor":
            return _read_colour(buf, o, pixel_depth)
        g = buf[o]
        return (g, g, g, 255)

    total_pixels = width * height
    src = bytearray(total_pixels * 4)

    if not is_rle:
        needed = total_pixels * bpp
        if off + needed > n:
            raise TgaError("truncated pixel data")
        for i in range(total_pixels):
            r, g, b, a = decode_pixel(data, off + i * bpp)
            o = i * 4
            src[o], src[o + 1], src[o + 2], src[o + 3] = r, g, b, a
    else:
        i = 0
        pos = off
        while i < total_pixels:
            if pos >= n:
                raise TgaError("truncated RLE stream (no packet header)")
            header = data[pos]
            pos += 1
            count = (header & 0x7F) + 1
            if i + count > total_pixels:
                raise TgaError("RLE packet overruns image")
            if header & 0x80:
                if pos + bpp > n:
                    raise TgaError("truncated RLE run packet")
                r, g, b, a = decode_pixel(data, pos)
                pos += bpp
                for _ in range(count):
                    o = i * 4
                    src[o], src[o + 1], src[o + 2], src[o + 3] = r, g, b, a
                    i += 1
            else:
                if pos + count * bpp > n:
                    raise TgaError("truncated RLE raw packet")
                for _ in range(count):
                    r, g, b, a = decode_pixel(data, pos)
                    pos += bpp
                    o = i * 4
                    src[o], src[o + 1], src[o + 2], src[o + 3] = r, g, b, a
                    i += 1

    top_down = bool((descriptor >> 5) & 1)
    right_to_left = bool((descriptor >> 4) & 1)

    out = bytearray(total_pixels * 4)
    if not right_to_left:
        # Common case (every shipped file): whole rows can be copied verbatim, just
        # possibly reordered top-to-bottom.
        for y in range(height):
            dst_row = y if top_down else (height - 1 - y)
            src_off = y * width * 4
            dst_off = dst_row * width * 4
            out[dst_off:dst_off + width * 4] = src[src_off:src_off + width * 4]
    else:
        for y in range(height):
            dst_row = y if top_down else (height - 1 - y)
            for x in range(width):
                si = (y * width + (width - 1 - x)) * 4
                di = (dst_row * width + x) * 4
                out[di:di + 4] = src[si:si + 4]

    return TgaImage(width, height, has_alpha, bytes(out))


def _main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "info":
        for path in argv[2:]:
            with open(path, "rb") as f:
                data = f.read()
            try:
                img = decode_tga(data)
                print(f"{path}: {img.width}x{img.height} hasAlpha={img.has_alpha}")
            except TgaError as e:
                print(f"{path}: ERROR: {e}")
        return 0
    if cmd == "hash":
        import hashlib
        for path in argv[2:]:
            with open(path, "rb") as f:
                data = f.read()
            img = decode_tga(data)
            digest = hashlib.sha1(img.rgba).hexdigest()
            print(f"{digest}  {img.width}x{img.height}  {path}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
