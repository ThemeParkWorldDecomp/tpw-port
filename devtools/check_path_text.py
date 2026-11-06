#!/usr/bin/env python3
"""No std::filesystem::path string() in the launcher and the platform layer: ctest runs this ("path_text").

usage: python3 -I devtools/check_path_text.py [--root DIR]

  --root DIR   the tree to check (default: the one this script is in)

On Windows the launcher and the game run in the UTF-8 process code page (launcher/win/tpw_launcher.manifest), where
libc++'s string() and generic_string() may throw on every path, whatever its letters, and take the whole launcher down on
that system only. tpw::PathText and tpw::GenericPathText (platform/path_text.h) are the same elsewhere.

The files of launcher/, assets/ and platform/ are read with their comments taken out; tests (no manifest) and
launcher/game_app_mac.cpp (no Windows) are left out, and so is path_text.h itself. Exit status 0 with no findings, 1 otherwise.
"""
import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm", ".inc"}
FOLDERS = ("launcher", "assets", "platform")
SKIPPED_NAMES = {"game_app_mac.cpp", "path_text.h"}
CALL = re.compile(r"\.(?:generic_)?string\s*\(\s*\)")
# Strings and character literals are matched too, so that a "//" or "/*" inside one is not taken for a comment.
TOKENS = re.compile(r"\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'|//[^\n]*|/\*.*?\*/", re.S)


def sources(root):
    for folder in FOLDERS:
        for path in sorted((root / folder).rglob("*")):
            if "tests" in path.relative_to(root).parts or path.name in SKIPPED_NAMES:
                continue
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                yield path


def without_comments(text):
    # A comment's newlines stay, so that the line numbers do.
    return TOKENS.sub(lambda m: "\n" * m.group().count("\n") if m.group().startswith("/") else m.group(), text)


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--root", type=pathlib.Path, default=HERE.parent)
    args = parser.parse_args()
    findings = 0
    for path in sources(args.root):
        text = without_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in CALL.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            print("%s:%d: a path's string(): tpw::PathText or tpw::GenericPathText (platform/path_text.h)" %
                  (path.relative_to(args.root), line))
            findings += 1
    if findings:
        print("%d finding(s)" % findings)
        return 1
    print("no path string() in %s" % ", ".join(FOLDERS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
