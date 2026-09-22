#!/usr/bin/env python3
"""Launch ESP-IDF's idf.py with a deterministic PATH.

Why this exists
---------------
The Windows environment block handed down from the host shell can contain two
variables that differ only in case (``Path`` and ``PATH``).  cmd.exe resolves
``%PATH%`` to one of them, CPython's ``os.environ`` keeps the other, and every
process python spawns (cmake -> ninja -> the Xtensa toolchain) inherits
python's choice.  The symptom is a perfectly fine ``%PATH%`` in the batch
console while CMake reports::

    The C compiler identification is unknown
    The CMAKE_C_COMPILER: xtensa-esp32s3-elf-gcc
      is not a full path and was not found in the PATH.

The wrapper batch file therefore exports the PATH *it* actually resolved as
``IDF_LAUNCH_PATH``; this launcher pushes that exact string into
``os.environ`` before idf.py starts, so the whole process tree agrees.
"""

import os
import runpy
import sys


def _fix_path() -> None:
    fixed = os.environ.pop('IDF_LAUNCH_PATH', None)
    if fixed:
        os.environ['PATH'] = fixed


def main() -> int:
    _fix_path()

    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        sys.stderr.write('idf_launcher: IDF_PATH is not set\n')
        return 1

    idf_py = os.path.join(idf_path, 'tools', 'idf.py')
    if not os.path.isfile(idf_py):
        sys.stderr.write(f'idf_launcher: {idf_py} not found\n')
        return 1

    sys.argv = [idf_py] + sys.argv[1:]
    # Running idf.py through runpy does not put its own directory on sys.path
    # (unlike `python idf.py`), and idf.py imports sibling helpers such as
    # python_version_checker from there.
    sys.path.insert(0, os.path.dirname(idf_py))
    runpy.run_path(idf_py, run_name='__main__')
    return 0


if __name__ == '__main__':
    sys.exit(main())
