#!/usr/bin/env python3
"""Audit a git repo for source directories that exist on disk but are not tracked.

Motivation: in this multi-project repo the root .gitignore (written for STM32
projects) silently swallows several common directory names. Because `git add`
skips ignored files *without any error*, self-written code can live on disk for
months and never enter version control -- then vanish on a disk incident.

This script reports, for every top-level project directory, each sub-directory
with files on disk but zero files tracked, and whether it is git-ignored.

Usage:  python audit_untracked.py [repo_root]
Exit  : 0 always (read-only report)
"""
import os
import subprocess
import sys

# Directories that are expected to be untracked / ignored (build output, vendored
# code, caches). Reported separately so real problems stand out.
VENDOR_OR_BUILD = {
    "build", "out", ".build", ".cache", "Debug", "Release", "obj", "Listings",
    "Objects", "DebugConfig", "Testing", "node_modules", "Drivers", "third_party",
    "zephyr", "temp_hal_all", "th", ".pio", ".vscode-test", "sys_startup",
}

SKIP_TOP = {".git", ".vscode", ".workbuddy", "__pycache__", ".github"}

# Probe file name used for `git check-ignore`. It must NOT match any rule by
# itself -- a name like "probe.tmp" is useless because `*.tmp` is ignored, which
# makes every directory look ignored.
PROBE = "__audit_probe__"

# Untracked content that is regenerable from something already tracked
# (e.g. extracted zip archives whose .zip is committed).
REGENERABLE = {"env_support_for_stm32f429", "env_support_for_stm32h743",
               "env_support_for_zephyr"}


def git(*args, cwd):
    r = subprocess.run(["git"] + list(args), cwd=cwd, capture_output=True)
    return r.stdout.decode("utf-8", "replace")


def git_rc(*args, cwd):
    """Return True when git exits 0 (for check-ignore: 0 == path IS ignored)."""
    return subprocess.run(["git"] + list(args), cwd=cwd,
                          capture_output=True).returncode == 0


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    root = os.path.abspath(root)

    tracked = set()
    for line in git("ls-files", cwd=root).splitlines():
        tracked.add(line.replace("\\", "/"))

    # Map "<project>/<lower dir>" -> set of casings actually present in git.
    # git paths ARE case-sensitive while the Windows filesystem is NOT, so a
    # directory stored as "Bsp" looks like zero tracked files when queried as
    # "bsp" -- a false "untracked" alarm. Detect the mismatch explicitly.
    tracked_casing = {}
    for t in tracked:
        parts = t.split("/")
        if len(parts) >= 2:
            tracked_casing.setdefault("%s/%s" % (parts[0], parts[1].lower()), set()).add(parts[1])

    problems, expected, regenerable, casefix = [], [], [], []

    for proj in sorted(os.listdir(root)):
        pdir = os.path.join(root, proj)
        if not os.path.isdir(pdir) or proj in SKIP_TOP or proj.startswith("."):
            continue
        for sub in sorted(os.listdir(pdir)):
            sdir = os.path.join(pdir, sub)
            if not os.path.isdir(sdir) or sub.startswith("."):
                continue

            on_disk = 0
            for _, _, files in os.walk(sdir):
                on_disk += len(files)
            if on_disk == 0:
                continue

            prefix = "%s/%s/" % (proj, sub)
            n_tracked = sum(1 for t in tracked if t.startswith(prefix))
            if n_tracked > 0:
                continue

            rel = "%s/%s" % (proj, sub)

            # Same directory tracked under a different casing?
            casings = tracked_casing.get("%s/%s" % (proj, sub.lower()), set())
            if casings and sub not in casings:
                casefix.append((rel, sorted(casings), on_disk))
                continue

            # check-ignore exits 0 when the path IS ignored; it also honours
            # negation rules, so this is authoritative. Never pass -q (it prints
            # nothing, so ignored could not be told apart from not-ignored).
            ignored = (git_rc("check-ignore", rel, cwd=root)
                       or git_rc("check-ignore", rel + "/" + PROBE, cwd=root))
            row = (rel, on_disk, ignored)
            if sub in REGENERABLE:
                regenerable.append(row)
            elif sub in VENDOR_OR_BUILD or ignored:
                expected.append(row)
            else:
                problems.append(row)

    print("=" * 78)
    print("A. UNTRACKED AND NOT IGNORED  <-- real risk, will not survive a disk loss")
    print("=" * 78)
    if not problems:
        print("  (none)")
    for rel, n, _ in problems:
        print("  %-52s %5d files on disk, 0 tracked" % (rel, n))

    print()
    print("=" * 78)
    print("B. UNTRACKED BUT IGNORED / known vendored-build dirs  (likely intentional)")
    print("=" * 78)
    if not expected:
        print("  (none)")
    for rel, n, ign in expected:
        print("  %-52s %5d files, ignored=%s" % (rel, n, ign))

    print()
    print("=" * 78)
    print("C. UNTRACKED BUT REGENERABLE (extracted from a tracked archive)")
    print("=" * 78)
    if not regenerable:
        print("  (none)")
    for rel, n, ign in regenerable:
        print("  %-52s %5d files, ignored=%s" % (rel, n, ign))

    print()
    print("=" * 78)
    print("D. TRACKED UNDER A DIFFERENT CASING  (false alarm source: git paths are")
    print("   case-sensitive, Windows FS is not -- query with the git spelling)")
    print("=" * 78)
    if not casefix:
        print("  (none)")
    for rel, casings, n in casefix:
        print("  %-46s on-disk %-10s git has %s" % (rel, repr(rel.split('/')[1]), casings))

    print()
    print("Summary: %d at-risk, %d ignored/vendored, %d regenerable, %d case-mismatch"
          % (len(problems), len(expected), len(regenerable), len(casefix)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
