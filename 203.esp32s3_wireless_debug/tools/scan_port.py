#!/usr/bin/env python3
# Scan system COM ports and select the target port for flashing.
# The port list is printed to stderr (visible in console). Only the chosen
# port is printed to stdout so a .bat wrapper can capture it with for /f.
import sys


def main():
    arg_port = sys.argv[1] if len(sys.argv) > 1 else ""
    default_port = sys.argv[2] if len(sys.argv) > 2 else "COM21"
    try:
        import serial.tools.list_ports as lp
        ports = [p.device for p in lp.comports()]
        descs = {p.device: p.description for p in lp.comports()}
    except Exception as e:
        sys.stderr.write("ERR: port scan failed: %s\n" % e)
        sys.exit(2)

    if not ports:
        sys.stderr.write("ERR: no serial port detected\n")
        sys.exit(3)

    sys.stderr.write("Detected COM ports:\n")
    for d in ports:
        mark = "  (default)" if d == default_port else ""
        sys.stderr.write("  %-8s | %s%s\n" % (d, descs.get(d, ""), mark))

    chosen = None
    if arg_port:
        if arg_port in ports:
            chosen = arg_port
        else:
            sys.stderr.write("WARN: %s not in list, still trying\n" % arg_port)
            chosen = arg_port
    elif default_port in ports:
        chosen = default_port
    elif sys.stdin.isatty():
        try:
            sel = input("Select port [enter=default %s]: " % default_port)
        except EOFError:
            sel = ""
        sel = sel.strip()
        if sel == "":
            chosen = default_port if default_port in ports else ports[0]
        else:
            try:
                chosen = ports[int(sel) - 1]
            except (ValueError, IndexError):
                sys.stderr.write("ERR: invalid selection: %s\n" % sel)
                sys.exit(4)
    else:
        chosen = default_port if default_port in ports else ports[0]

    print(chosen)


if __name__ == "__main__":
    main()
