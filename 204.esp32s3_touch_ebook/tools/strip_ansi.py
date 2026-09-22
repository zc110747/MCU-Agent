#!/usr/bin/env python3
"""Strip ANSI escape sequences from a log file in place (keeps it readable)."""
import re
import sys

ANSI = re.compile(rb'\x1b(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')


def main() -> int:
    if len(sys.argv) < 2:
        return 1
    path = sys.argv[1]
    with open(path, 'rb') as fh:
        data = fh.read()
    with open(path, 'wb') as fh:
        fh.write(ANSI.sub(b'', data))
    return 0


if __name__ == '__main__':
    sys.exit(main())
