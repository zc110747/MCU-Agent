"""CTF (Character Table Font) binary format - constants and (de)serialisation.

This module is the single source of truth for the on-disk layout.  The
firmware side (Bsp/font/ctf_format.h) mirrors it field by field; both sides
use explicit little-endian serialisation and never rely on struct padding.

Design rules (from the architecture spec):

  * CTF is an *index* into an existing TTF - never a replacement for it.
  * CTF holds no bitmaps and no copy of the glyf data.
  * Every field has a fixed width and an explicit endianness.
  * Lookups are multi-level direct addressing (O(1)) - no scans, no binary
    search, and never a trip into the TTF just to discover a character is
    absent.

File layout
-----------
    +----------------------------------------+
    | CTF Header                 (80 bytes)  |
    +----------------------------------------+
    | TTF table index       (N * 12 bytes)   |  tag/offset/length per table
    +----------------------------------------+
    | Level-1 plane index   (256 * 8 bytes)  |  plane = (u >> 16) & 0xFF
    +----------------------------------------+
    | Page index            (M * 40 bytes)   |  page  = (u >>  8) & 0xFF
    +----------------------------------------+
    | Entry table           (K * 24 bytes)   |  low   =  u        & 0xFF
    +----------------------------------------+

Addressing a codepoint costs three small reads, all cacheable:

    plane = u >> 16   -> L1 record   (8 B,  page_offset + page_count)
    page  = u >> 8    -> page record (40 B, entry_offset + 256-bit bitmap)
    low   = u & 0xFF  -> bit test + popcount rank -> entry index

A cleared bit means "this Unicode is not in the font": the lookup returns
CTF_NOT_FOUND immediately and the TTF is never touched.

The 256-bit bitmap per page is what turns a sparse Unicode space into a dense
one: the entry table only stores codepoints the font actually has, while the
rank of a set bit gives the entry position in O(1) (popcount over at most 32
bytes).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Identity
# ---------------------------------------------------------------------------

#: 'CTF1' as a little-endian uint32 (bytes 43 54 46 31 on disk).
CTF_MAGIC = 0x31465443

CTF_VERSION_MAJOR = 1
CTF_VERSION_MINOR = 0

#: The only version this tool writes and the firmware accepts.
CTF_SUPPORTED_VERSION = 1

#: Byte sizes of every on-disk record.  Asserted at (un)pack time.
CTF_HEADER_SIZE = 80
CTF_TABLE_SIZE = 12
CTF_L1_SIZE = 8
CTF_PAGE_SIZE = 40
CTF_ENTRY_SIZE = 24

#: Number of Level-1 slots: (u >> 16) & 0xFF, i.e. Unicode planes 0..255.
CTF_L1_ENTRIES = 256
#: Number of pages per plane: (u >> 8) & 0xFF -> 256 pages of 256 codepoints.
CTF_PAGES_PER_PLANE = 256
#: Codepoints per page: u & 0xFF.
CTF_PAGE_SPAN = 256
#: Presence bitmap bytes per page (256 bits).
CTF_PAGE_BITMAP_BYTES = CTF_PAGE_SPAN // 8

#: Marker for "this plane/page has no glyphs at all".
CTF_OFFSET_NONE = 0

# ---------------------------------------------------------------------------
# Unicode modes / encodings
# ---------------------------------------------------------------------------

#: Sparse Unicode, multi-level direct addressing.  The only mode defined so
#: far; GBK is handled on the firmware side by mapping GBK -> Unicode first.
CTF_MODE_UNICODE = 0

CTF_MODE_NAME = {CTF_MODE_UNICODE: "unicode"}

# ---------------------------------------------------------------------------
# Header flags
# ---------------------------------------------------------------------------

CTF_FLAG_HAS_COMPOSITE = 0x00000001  # at least one composite glyph indexed
CTF_FLAG_HAS_KERN = 0x00000002  # TTF carries a legacy 'kern' table
CTF_FLAG_HAS_GPOS = 0x00000004  # TTF carries a 'GPOS' table

# ---------------------------------------------------------------------------
# Glyph flags
# ---------------------------------------------------------------------------

CTF_GLYPH_EMPTY = 0x0001  # real glyph, no outline (e.g. space)
CTF_GLYPH_SIMPLE = 0x0002  # numberOfContours > 0
CTF_GLYPH_COMPOSITE = 0x0004  # numberOfContours < 0
CTF_GLYPH_VALID = 0x0008  # entry was produced from a real glyph
CTF_GLYPH_MISSING = 0x0010  # mapped to .notdef / unusable - treat as absent

# ---------------------------------------------------------------------------
# TTF tables worth recording in the table index
# ---------------------------------------------------------------------------

CTF_TABLE_TAGS = (
    b"cmap", b"head", b"hhea", b"hmtx", b"maxp",
    b"loca", b"glyf", b"kern", b"GPOS", b"name",
    b"OS/2", b"post", b"cvt ", b"fpgm", b"prep",
)

# ---------------------------------------------------------------------------
# Serialised records
#
# Every row is (name, struct code, byte offset).  Fields are packed one by one
# so neither Python nor the C compiler can insert padding, and the offsets are
# the wire format - changing a row is a format version bump.
# ---------------------------------------------------------------------------

_HEADER_FIELDS = (
    ("magic", "I", 0),
    ("version", "H", 4),
    ("header_size", "H", 6),
    ("flags", "I", 8),
    ("ttf_size", "I", 12),
    ("ttf_crc32", "I", 16),
    ("unicode_mode", "I", 20),
    ("table_index_offset", "I", 24),
    ("table_index_count", "I", 28),
    ("l1_index_offset", "I", 32),
    ("l1_index_count", "I", 36),
    ("page_index_offset", "I", 40),
    ("page_index_count", "I", 44),
    ("entry_offset", "I", 48),
    ("entry_count", "I", 52),
    ("entry_size", "I", 56),
    ("units_per_em", "I", 60),
    ("ascent", "h", 64),
    ("descent", "h", 66),
    ("line_gap", "h", 68),
    ("num_glyphs", "H", 70),
    ("char_count", "I", 72),
    ("reserved0", "I", 76),
)

_TABLE_FIELDS = (
    ("tag", "I", 0),
    ("offset", "I", 4),
    ("length", "I", 8),
)

_L1_FIELDS = (
    ("page_offset", "I", 0),
    ("page_count", "H", 4),
    ("reserved", "H", 6),
)

_PAGE_FIELDS = (
    ("entry_offset", "I", 0),
    ("entry_count", "H", 4),
    ("flags", "H", 6),
    ("bitmap", "32s", 8),
)

_ENTRY_FIELDS = (
    ("glyf_offset", "I", 0),
    ("glyf_length", "I", 4),
    ("glyph_id", "H", 8),
    ("advance_width", "H", 10),
    ("bearing_x", "h", 12),
    ("x_min", "h", 14),
    ("y_min", "h", 16),
    ("x_max", "h", 18),
    ("y_max", "h", 20),
    ("flags", "H", 22),
)


def _pack_fields(obj, fields, total_size):
    buf = bytearray(total_size)
    for name, code, off in fields:
        value = getattr(obj, name, 0)
        size = struct.calcsize("<" + code)
        assert off + size <= total_size, (name, off, size, total_size)
        struct.pack_into("<" + code, buf, off, value)
    return bytes(buf)


def _unpack_fields(blob, fields, total_size, cls):
    if len(blob) < total_size:
        raise ValueError("truncated CTF record: %d < %d" % (len(blob), total_size))
    kwargs = {}
    for name, code, off in fields:
        (kwargs[name],) = struct.unpack_from("<" + code, blob, off)
    if "reserved" in kwargs:
        kwargs.pop("reserved")
    return cls(**kwargs)


# ---------------------------------------------------------------------------
# Records
# ---------------------------------------------------------------------------


@dataclass
class CtfHeader:
    magic: int = CTF_MAGIC
    version: int = CTF_SUPPORTED_VERSION
    header_size: int = CTF_HEADER_SIZE
    flags: int = 0
    ttf_size: int = 0
    ttf_crc32: int = 0
    unicode_mode: int = CTF_MODE_UNICODE
    table_index_offset: int = CTF_HEADER_SIZE
    table_index_count: int = 0
    l1_index_offset: int = 0
    l1_index_count: int = CTF_L1_ENTRIES
    page_index_offset: int = 0
    page_index_count: int = 0
    entry_offset: int = 0
    entry_count: int = 0
    entry_size: int = CTF_ENTRY_SIZE
    units_per_em: int = 1000
    ascent: int = 0
    descent: int = 0
    line_gap: int = 0
    num_glyphs: int = 0
    char_count: int = 0
    reserved0: int = 0

    def pack(self) -> bytes:
        return _pack_fields(self, _HEADER_FIELDS, CTF_HEADER_SIZE)

    @staticmethod
    def unpack(blob: bytes) -> "CtfHeader":
        return _unpack_fields(blob, _HEADER_FIELDS, CTF_HEADER_SIZE, CtfHeader)


@dataclass
class CtfTable:
    tag: int = 0
    offset: int = 0
    length: int = 0

    @property
    def tag_str(self) -> str:
        return struct.pack("<I", self.tag).decode("latin1")

    def pack(self) -> bytes:
        return _pack_fields(self, _TABLE_FIELDS, CTF_TABLE_SIZE)

    @staticmethod
    def unpack(blob: bytes, offset: int = 0) -> "CtfTable":
        return _unpack_fields(blob[offset:], _TABLE_FIELDS, CTF_TABLE_SIZE, CtfTable)


@dataclass
class CtfL1:
    """One Unicode plane.  ``page_offset`` is a *file* offset (0 = empty)."""

    page_offset: int = CTF_OFFSET_NONE
    page_count: int = 0

    def pack(self) -> bytes:
        return _pack_fields(self, _L1_FIELDS, CTF_L1_SIZE)

    @staticmethod
    def unpack(blob: bytes, offset: int = 0) -> "CtfL1":
        return _unpack_fields(blob[offset:], _L1_FIELDS, CTF_L1_SIZE, CtfL1)


@dataclass
class CtfPage:
    """One 256-codepoint slice of a plane.

    ``bitmap`` is the raw 32-byte presence map; bit ``i`` set means the
    codepoint ``(plane << 16) | (page << 8) | i`` exists in this font.
    """

    entry_offset: int = 0
    entry_count: int = 0
    flags: int = 0
    bitmap: bytes = field(default_factory=lambda: bytes(CTF_PAGE_BITMAP_BYTES))

    def pack(self) -> bytes:
        return _pack_fields(self, _PAGE_FIELDS, CTF_PAGE_SIZE)

    @staticmethod
    def unpack(blob: bytes, offset: int = 0) -> "CtfPage":
        return _unpack_fields(blob[offset:], _PAGE_FIELDS, CTF_PAGE_SIZE, CtfPage)

    def has(self, low: int) -> bool:
        return (self.bitmap[low >> 3] >> (low & 7)) & 1 != 0

    def rank(self, low: int) -> int:
        """Number of set bits strictly below ``low`` -> entry index in page."""
        full, bits = divmod(low, 8)
        n = sum(bin(b).count("1") for b in self.bitmap[:full])
        if bits:
            n += bin(self.bitmap[full] & ((1 << bits) - 1)).count("1")
        return n


@dataclass
class CtfEntry:
    """One character record.

    ``glyf_offset`` is an *absolute* byte offset into the original TTF, so the
    firmware can hand it straight to its TTF reader.  ``advance_width`` and
    the bounding box are in font units; divide by ``units_per_em``.

    The Unicode is not stored: it is implied by the L1/page/entry position,
    which is what keeps the entry down to 24 bytes.
    """

    glyf_offset: int = 0
    glyf_length: int = 0
    glyph_id: int = 0
    advance_width: int = 0
    bearing_x: int = 0
    x_min: int = 0
    y_min: int = 0
    x_max: int = 0
    y_max: int = 0
    flags: int = 0

    def pack(self) -> bytes:
        return _pack_fields(self, _ENTRY_FIELDS, CTF_ENTRY_SIZE)

    @staticmethod
    def unpack(blob: bytes, offset: int = 0) -> "CtfEntry":
        if offset < 0 or (offset + CTF_ENTRY_SIZE) > len(blob):
            raise ValueError("CTF entry out of range: %d" % offset)
        return _unpack_fields(blob[offset:], _ENTRY_FIELDS, CTF_ENTRY_SIZE, CtfEntry)

    @property
    def is_missing(self) -> bool:
        """No usable glyph - the font backend reports NOT_FOUND."""
        return (self.flags & CTF_GLYPH_MISSING) != 0 or (self.flags & CTF_GLYPH_VALID) == 0

    @property
    def is_empty(self) -> bool:
        """Real glyph with no outline (space).  NOT the same as missing."""
        return (self.flags & CTF_GLYPH_EMPTY) != 0

    @property
    def is_composite(self) -> bool:
        return (self.flags & CTF_GLYPH_COMPOSITE) != 0

    def kind(self) -> str:
        if self.is_missing:
            return "missing"
        if self.is_empty:
            return "empty"
        return "composite" if self.is_composite else "simple"


# ---------------------------------------------------------------------------
# File level access
# ---------------------------------------------------------------------------


@dataclass
class CtfFile:
    """A CTF file mapped back from disk, with the firmware lookup semantics.

    The whole file is held in RAM *on the PC only* - the firmware reads the
    same records directly from the SD card through its block cache.
    """

    header: CtfHeader
    tables: List[CtfTable] = field(default_factory=list)
    l1: List[CtfL1] = field(default_factory=list)
    pages: Dict[int, List[CtfPage]] = field(default_factory=dict)
    entries: List[CtfEntry] = field(default_factory=list)
    blob: bytes = b""

    @staticmethod
    def load(path: str) -> "CtfFile":
        with open(path, "rb") as fh:
            blob = fh.read()

        header = CtfHeader.unpack(blob)
        if header.magic != CTF_MAGIC:
            raise ValueError("bad magic 0x%08X" % header.magic)
        if header.version != CTF_SUPPORTED_VERSION:
            raise ValueError("unsupported version %u" % header.version)
        if header.header_size != CTF_HEADER_SIZE:
            raise ValueError("unexpected header size %u" % header.header_size)
        if header.entry_size != CTF_ENTRY_SIZE:
            raise ValueError("unexpected entry size %u" % header.entry_size)
        if header.unicode_mode != CTF_MODE_UNICODE:
            raise ValueError("unsupported unicode mode %u" % header.unicode_mode)

        tables = [
            CtfTable.unpack(blob, header.table_index_offset + i * CTF_TABLE_SIZE)
            for i in range(header.table_index_count)
        ]
        l1 = [
            CtfL1.unpack(blob, header.l1_index_offset + i * CTF_L1_SIZE)
            for i in range(header.l1_index_count)
        ]

        pages: Dict[int, List[CtfPage]] = {}
        for plane, rec in enumerate(l1):
            if rec.page_count == 0 or rec.page_offset == CTF_OFFSET_NONE:
                continue
            pages[plane] = [
                CtfPage.unpack(blob, rec.page_offset + i * CTF_PAGE_SIZE)
                for i in range(rec.page_count)
            ]

        entries = [
            CtfEntry.unpack(blob, header.entry_offset + i * CTF_ENTRY_SIZE)
            for i in range(header.entry_count)
        ]

        return CtfFile(
            header=header, tables=tables, l1=l1, pages=pages,
            entries=entries, blob=blob,
        )

    # -- lookup ------------------------------------------------------------

    def find_unicode(self, cp: int) -> Optional[CtfEntry]:
        """Entry for a Unicode codepoint, or ``None`` when NOT_FOUND.

        Mirrors the firmware path exactly: plane -> page -> bitmap -> rank.
        """
        if cp < 0 or cp > 0xFFFFFF:
            return None
        plane = (cp >> 16) & 0xFF
        if plane >= len(self.l1):
            return None

        rec = self.l1[plane]
        if rec.page_count == 0 or rec.page_offset == CTF_OFFSET_NONE:
            return None

        page_no = (cp >> 8) & 0xFF
        if page_no >= rec.page_count:
            return None

        by_plane = self.pages.get(plane)
        if by_plane is None or page_no >= len(by_plane):
            return None

        page = by_plane[page_no]
        low = cp & 0xFF
        if not page.has(low):
            return None

        idx = page.entry_offset + page.rank(low) * self.header.entry_size
        if idx + CTF_ENTRY_SIZE > len(self.blob):
            return None

        entry = CtfEntry.unpack(self.blob, idx)
        return None if entry.is_missing else entry

    @staticmethod
    def gbk_to_unicode(code: int) -> Optional[int]:
        if code <= 0x7F:
            return code
        try:
            return ord(bytes([(code >> 8) & 0xFF, code & 0xFF]).decode("gbk"))
        except (UnicodeDecodeError, ValueError):
            return None

    def find_gbk(self, code: int) -> Optional[CtfEntry]:
        cp = self.gbk_to_unicode(code)
        return None if cp is None else self.find_unicode(cp)

    # -- inspection --------------------------------------------------------

    def iter_unicode(self):
        """Yield ``(codepoint, entry)`` for everything the index contains."""
        for plane, rec in enumerate(self.l1):
            if rec.page_count == 0:
                continue
            by_plane = self.pages.get(plane, [])
            for page_no, page in enumerate(by_plane):
                base = (plane << 16) | (page_no << 8)
                for low in range(CTF_PAGE_SPAN):
                    if not page.has(low):
                        continue
                    idx = page.entry_offset + page.rank(low) * self.header.entry_size
                    yield base | low, CtfEntry.unpack(self.blob, idx)

    def table(self, tag: bytes) -> Optional[CtfTable]:
        want = struct.unpack("<I", tag)[0]
        for t in self.tables:
            if t.tag == want:
                return t
        return None

    def size_breakdown(self) -> List[Tuple[str, int, int]]:
        """(name, offset, bytes) for each region, for --info output."""
        h = self.header
        return [
            ("header", 0, h.header_size),
            ("table index", h.table_index_offset, h.table_index_count * CTF_TABLE_SIZE),
            ("L1 index", h.l1_index_offset, h.l1_index_count * CTF_L1_SIZE),
            ("page index", h.page_index_offset, h.page_index_count * CTF_PAGE_SIZE),
            ("entry table", h.entry_offset, h.entry_count * h.entry_size),
        ]
