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

A second, quieter problem is handled here too: the ``MSYSTEM`` variable.  Git
Bash sets it, and it is inherited by every child process - including this one.
ESP-IDF's ``idf.py`` top-level block is a single ``if/elif/elif`` chain whose
*first* arm is::

    if 'MSYSTEM' in os.environ:
        print_warning('MSys/Mingw is no longer supported...')
    elif os.name == 'posix' and not _valid_unicode_config():
        ...
    elif os.name == 'nt' and not _windows_unicode_satisfactory():
        ...
    else:
        <calls main()>

Taking the MSYSTEM arm means ``main()`` is never called.  The process prints one
warning, exits 0, and does nothing at all - a build system that reports success
without building, which is the worst possible failure mode.  So the variable is
removed before idf.py is loaded.

``env -u MSYSTEM`` from the caller is not enough: the tool shell re-exports it,
and the batch file's ``call export.bat`` can put it back.  Deleting it here, in
the one process that is guaranteed to sit between the shell and idf.py, is the
only placement that holds for every way this project is built.
"""

import os
import runpy
import sys


def _fix_path() -> None:
    fixed = os.environ.pop('IDF_LAUNCH_PATH', None)
    if fixed:
        os.environ['PATH'] = fixed


def _drop_msystem() -> None:
    """Remove the MSYS marker so idf.py's entry point reaches main().

    See the module docstring: with MSYSTEM set, idf.py takes a warning-only
    branch and never calls main(), so it exits 0 having done nothing.
    """
    if os.environ.pop('MSYSTEM', None) is not None:
        sys.stderr.write('idf_launcher: dropped MSYSTEM (idf.py would have done nothing)\n')


def _warn_stale_config() -> None:
    """Warn when sdkconfig is older than sdkconfig.defaults.

    This project treats ``sdkconfig.defaults`` as the single source of truth and
    gitignores ``sdkconfig``, but Kconfig only *seeds* sdkconfig from the
    defaults - once sdkconfig exists, editing the defaults changes nothing.  The
    failure is silent and extremely confusing: the source says
    ``CONFIG_LV_DEF_REFR_PERIOD=16`` and the running firmware reports 33, with a
    green build in between.

    Fix: delete ``sdkconfig`` and rebuild (it is gitignored, so nothing is lost
    that was not already written down in the defaults).

    Only a warning, never a build failure: a *deliberate* local override in
    sdkconfig is legitimate, it just has to be a conscious choice.
    """
    defaults = 'sdkconfig.defaults'
    generated = 'sdkconfig'
    try:
        if os.path.getmtime(defaults) > os.path.getmtime(generated):
            sys.stderr.write(
                'idf_launcher: WARNING: sdkconfig.defaults is NEWER than sdkconfig.\n'
                'idf_launcher:   Kconfig will NOT apply the defaults changes.\n'
                'idf_launcher:   Run:  del sdkconfig  &&  rebuild\n'
            )
    except OSError:
        pass    # no sdkconfig yet: the defaults are about to be applied anyway


def main() -> int:
    _fix_path()
    _drop_msystem()
    _warn_stale_config()

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
