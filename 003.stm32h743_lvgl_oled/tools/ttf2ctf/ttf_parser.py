"""Minimal TrueType parser - just enough to build a CTF index.

Deliberately dependency free (no fontTools) so the tool runs anywhere Python
does.  Only the tables the CTF generator needs are decoded:

    head  unitsPerEm, indexToLocFormat
    maxp  numGlyphs
    hhea  ascender / descender / lineGap / numberOfHMetrics
    hmtx  advance width + left side bearing per glyph
    loca  glyph -> byte range inside 'glyf'
    glyf  bounding box and simple/composite classification
    cmap  Unicode -> glyph id (formats 0, 4, 6 and 12)
"""

from __future__ import annotations

import struct
from typing import Dict, List, Optional, Tuple


class TtfError(Exception):
    """Raised when the input is not a TTF we can index."""


def _u16(b: bytes, o: int) -> int:
    return struct.unpack_from(">H", b, o)[0]


def _i16(b: bytes, o: int) -> int:
    return struct.unpack_from(">h", b, o)[0]


def _u32(b: bytes, o: int) -> int:
    return struct.unpack_from(">I", b, o)[0]


class TtfFont:
    """A parsed TrueType font."""

    def __init__(self, data: bytes, font_index: int = 0):
        self.data = data
        self.tables: Dict[bytes, Tuple[int, int]] = {}

        tag = data[0:4]
        if tag == b"ttcf":
            n = _u32(data, 8)
            if font_index >= n:
                raise TtfError("font-index %d out of range (%d fonts)" % (font_index, n))
            base = _u32(data, 12 + 4 * font_index)
        elif tag in (b"\x00\x01\x00\x00", b"true", b"typ1", b"OTTO"):
            base = 0
        else:
            raise TtfError("not a TrueType/OpenType file (tag=%r)" % (tag,))

        self.base = base
        num_tables = _u16(data, base + 4)
        for i in range(num_tables):
            rec = base + 12 + 16 * i
            name = data[rec:rec + 4]
            off = _u32(data, rec + 8)
            length = _u32(data, rec + 12)
            self.tables[name] = (off, length)

        self._parse_head()
        self._parse_maxp()
        self._parse_hhea()
        self._parse_loca()
        self._cmap: Optional[Dict[int, int]] = None

    # -- required tables ----------------------------------------------------

    def _need(self, name: bytes) -> int:
        if name not in self.tables:
            raise TtfError("missing required table %r" % (name.decode("latin1"),))
        return self.tables[name][0]

    def _parse_head(self) -> None:
        head = self._need(b"head")
        self.units_per_em = _u16(self.data, head + 18)
        self.index_to_loc_format = _i16(self.data, head + 50)
        if self.units_per_em == 0:
            raise TtfError("head.unitsPerEm is zero")

    def _parse_maxp(self) -> None:
        maxp = self._need(b"maxp")
        self.num_glyphs = _u16(self.data, maxp + 4)

    def _parse_hhea(self) -> None:
        hhea = self._need(b"hhea")
        self.ascent = _i16(self.data, hhea + 4)
        self.descent = _i16(self.data, hhea + 6)
        self.line_gap = _i16(self.data, hhea + 8)
        self.number_of_h_metrics = _u16(self.data, hhea + 34)
        if self.number_of_h_metrics == 0:
            raise TtfError("hhea.numberOfHMetrics is zero")

    def _parse_loca(self) -> None:
        loca = self._need(b"loca")
        glyf_off, glyf_len = self.tables.get(b"glyf", (0, 0))
        self.glyf_offset = glyf_off
        self.glyf_length = glyf_len

        count = self.num_glyphs + 1
        if self.index_to_loc_format == 0:
            self.loca = [_u16(self.data, loca + 2 * i) * 2 for i in range(count)]
        else:
            self.loca = [_u32(self.data, loca + 4 * i) for i in range(count)]

    # -- cmap ---------------------------------------------------------------

    def cmap(self) -> Dict[int, int]:
        """Unicode (BMP) -> glyph id.  Built once, on demand."""
        if self._cmap is not None:
            return self._cmap

        if b"cmap" not in self.tables:
            raise TtfError("missing required table 'cmap'")

        cmap_off = self.tables[b"cmap"][0]
        mapping: Dict[int, int] = {}
        num_sub = _u16(self.data, cmap_off + 2)

        best: Optional[Tuple[int, int]] = None  # (score, subtable offset)
        for i in range(num_sub):
            rec = cmap_off + 4 + 8 * i
            plat = _u16(self.data, rec)
            enc = _u16(self.data, rec + 2)
            off = cmap_off + _u32(self.data, rec + 4)
            fmt = _u16(self.data, off)

            # Prefer a full repertoire subtable: (3,10) format 12, then
            # (3,1)/(0,x) format 4, then anything we can decode.
            if fmt == 12:
                score = 3
            elif fmt in (4, 6):
                score = 2 if plat == 3 else 1
            elif fmt == 0:
                score = 0
            else:
                continue

            if best is None or score > best[0]:
                best = (score, off)

        if best is None:
            raise TtfError("no supported cmap subtable (need format 0/4/6/12)")

        fmt = _u16(self.data, best[1])
        if fmt == 4:
            self._read_cmap4(best[1], mapping)
        elif fmt == 12:
            self._read_cmap12(best[1], mapping)
        elif fmt == 6:
            self._read_cmap6(best[1], mapping)
        elif fmt == 0:
            self._read_cmap0(best[1], mapping)

        self._cmap = mapping
        return mapping

    def _read_cmap4(self, off: int, out: Dict[int, int]) -> None:
        seg_x2 = _u16(self.data, off + 6)
        seg = seg_x2 // 2
        end_base = off + 14
        start_base = end_base + seg_x2 + 2
        delta_base = start_base + seg_x2
        range_base = delta_base + seg_x2

        for s in range(seg):
            end = _u16(self.data, end_base + 2 * s)
            start = _u16(self.data, start_base + 2 * s)
            delta = _i16(self.data, delta_base + 2 * s)
            range_off = _u16(self.data, range_base + 2 * s)

            if start > end or end == 0xFFFF and start == 0xFFFF:
                # The final sentinel segment; still harmless to walk.
                pass
            for c in range(start, min(end, 0xFFFF) + 1):
                if range_off == 0:
                    gid = (c + delta) & 0xFFFF
                else:
                    addr = range_base + 2 * s + range_off + 2 * (c - start)
                    if addr + 2 > len(self.data):
                        continue
                    gid = _u16(self.data, addr)
                    if gid != 0:
                        gid = (gid + delta) & 0xFFFF
                if gid:
                    out[c] = gid

    def _read_cmap12(self, off: int, out: Dict[int, int]) -> None:
        ngroups = _u32(self.data, off + 12)
        for g in range(ngroups):
            rec = off + 16 + 12 * g
            start = _u32(self.data, rec)
            end = _u32(self.data, rec + 4)
            gid = _u32(self.data, rec + 8)
            if start > 0xFFFF:
                continue
            last = min(end, 0xFFFF)
            for c in range(start, last + 1):
                out[c] = gid + (c - start)

    def _read_cmap6(self, off: int, out: Dict[int, int]) -> None:
        first = _u16(self.data, off + 6)
        count = _u16(self.data, off + 8)
        for i in range(count):
            gid = _u16(self.data, off + 10 + 2 * i)
            if gid:
                out[first + i] = gid

    def _read_cmap0(self, off: int, out: Dict[int, int]) -> None:
        for c in range(256):
            gid = self.data[off + 6 + c]
            if gid:
                out[c] = gid

    # -- per glyph ----------------------------------------------------------

    def glyph_id(self, unicode_cp: int) -> int:
        return self.cmap().get(unicode_cp, 0)

    def metrics(self, glyph_id: int) -> Tuple[int, int]:
        """(advanceWidth, lsb) in font units."""
        if glyph_id >= self.num_glyphs:
            return (0, 0)
        idx = min(glyph_id, self.number_of_h_metrics - 1)
        hmtx = self._need(b"hmtx")
        adv = _u16(self.data, hmtx + 4 * idx)
        lsb = _i16(self.data, hmtx + 4 * idx + 2)
        return (adv, lsb)

    def glyph_range(self, glyph_id: int) -> Tuple[int, int]:
        """Absolute (offset, length) of the glyph inside the TTF file."""
        if glyph_id == 0 or glyph_id >= self.num_glyphs:
            return (0, 0)
        start = self.loca[glyph_id]
        end = self.loca[glyph_id + 1]
        if end < start:
            return (0, 0)
        # loca is relative to the start of the glyf table.
        return (self.glyf_offset + start, end - start)

    def glyph_header(self, glyph_id: int):
        """(numberOfContours, xMin, yMin, xMax, yMax) or None when empty."""
        off, length = self.glyph_range(glyph_id)
        if length < 2:
            return None
        return (
            _i16(self.data, off),
            _i16(self.data, off + 2),
            _i16(self.data, off + 4),
            _i16(self.data, off + 6),
            _i16(self.data, off + 8),
        )


def crc32_file(path: str, chunk: int = 1 << 20) -> int:
    """CRC32 of a file, streamed so a multi-MB TTF does not blow up RAM."""
    import zlib

    value = 0
    with open(path, "rb") as fh:
        while True:
            block = fh.read(chunk)
            if not block:
                break
            value = zlib.crc32(block, value)
    return value & 0xFFFFFFFF
