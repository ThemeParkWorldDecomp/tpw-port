#!/usr/bin/env python3
"""No function of the game or the launcher is looked up by its symbol at run time: ctest runs this ("own_dlsym").

usage: python3 -I devtools/check_own_dlsym.py [--root DIR]

  --root DIR   the tree to check (default: the one this script is in)

A dlsym of a C++ name ("_Z...") finds the launcher's own function on a Mac (an executable exports its symbols) and
on Windows (the launcher is linked with --export-all-symbols), but not on Linux, where an executable exports none of
its own: the answer is NULL and the code that called it goes on without the function. The save panel's name box was
built that way, with no constructor run on it, and Save crashed the Linux launcher (Discord report by xoob,
2026-11-01). Call the function directly instead. Lookups of the system's functions by their C names (glDrawBuffer,
NSApp) are not findings.

Every C, C++ and Objective-C file of the tree (build folders, third_party/ and hidden folders left out) is read with
its comments taken out. Exit status 0 with no findings, 1 otherwise.
"""
import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm", ".inc"}
SKIPPED_DIRS = {"third_party", "node_modules"}
LOOKUP = re.compile(r"\bdlsym\s*\(\s*[^,()]+,\s*\"(_Z[^\"]*)\"")
# Strings and character literals are matched too, so that a "//" or "/*" inside one is not taken for a comment.
TOKENS = re.compile(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|//[^\n]*|/\*.*?\*/", re.S)


def sources(root):
    for path in sorted(root.rglob("*")):
        parts = path.relative_to(root).parts
        if any(p in SKIPPED_DIRS or p.startswith((".", "build")) for p in parts[:-1]):
            continue
        if path.suffix in SOURCE_SUFFIXES and path.is_file():
            yield path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", type=pathlib.Path, default=HERE.parent)
    root = ap.parse_args().root.resolve()
    findings = 0
    files = 0
    for path in sources(root):
        files += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        code = TOKENS.sub(lambda m: m.group(0) if m.group(0)[0] in "\"'" else "\n" * m.group(0).count("\n"), text)
        for match in LOOKUP.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            print("%s:%d: dlsym of the program's own %s: call it directly (Linux finds nothing)"
                  % (path.relative_to(root).as_posix(), line, match.group(1)))
            findings += 1
    print("check_own_dlsym: %d files read, %d findings" % (files, findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
