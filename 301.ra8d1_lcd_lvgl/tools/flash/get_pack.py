#!/usr/bin/env python3
"""Recreate the fixed Renesas RA_DFP pack and extract the RA8D1 FLM.

Renesas.RA_DFP.6.6.0.pack has a broken pdsc: it references 3 FLM files that
are missing from the archive, which makes pyOCD fail while loading the pack.
This script downloads the original pack, strips the missing <algorithm> lines
and all svd= attributes (pyOCD's SVD parser chokes on Renesas SVD dim format),
then extracts Flash/RA8D1_2M.FLM next to this script.

Usage:
    python get_pack.py
"""
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

PACK_URL = "https://www2.renesas.eu/Keil_MDK_Packs/Renesas.RA_DFP.6.6.0.pack"
PDSC_NAME = "Renesas.RA_DFP.pdsc"
FLM_ENTRY = "Flash/RA8D1_2M.FLM"


def main():
    out_dir = Path(__file__).resolve().parent
    flm_path = out_dir / "RA8D1_2M.FLM"
    if flm_path.is_file():
        print(f"FLM already present: {flm_path}")
        return

    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "ra_dfp.pack"
        print(f"Downloading {PACK_URL} ...")
        urllib.request.urlretrieve(PACK_URL, src)

        print("Patching pdsc (remove missing algorithm refs + svd attrs)...")
        zin = zipfile.ZipFile(src)
        names = set(zin.namelist())
        pdsc = zin.read(PDSC_NAME).decode("utf-8", "replace")

        lines = pdsc.split("\n")
        missing = [
            i for i, ln in enumerate(lines)
            if (m := re.search(r'<algorithm name="([^"]+)"', ln))
            and m.group(1) not in names
        ]
        lines = [ln for i, ln in enumerate(lines) if i not in missing]
        pdsc = re.sub(r"\s*svd=\"[^\"]*\"", "", "\n".join(lines))

        patched = Path(tmp) / "ra_dfp_fixed.pack"
        with zipfile.ZipFile(patched, "w", zipfile.ZIP_STORED) as zout:
            for item in zin.infolist():
                data = pdsc.encode("utf-8") if item.filename == PDSC_NAME else zin.read(item.filename)
                zout.writestr(item, data)
        zin.close()

        print(f"Extracting {FLM_ENTRY} ...")
        with zipfile.ZipFile(patched) as z:
            z.extract(FLM_ENTRY, Path(tmp) / "flm")

        shutil.copy(Path(tmp) / FLM_ENTRY, flm_path)
    print(f"Done: {flm_path}")
    print("Install into pyOCD cache if needed:")
    print(f"  pyocd pack install <this dir>/Renesas.RA_DFP.6.6.0.fixed.pack")


if __name__ == "__main__":
    sys.exit(main())
