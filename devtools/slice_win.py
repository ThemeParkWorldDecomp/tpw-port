#!/usr/bin/env python3
"""Windows: a copy of a tpw-decomp src file without some of its definitions, for the game library (CMakeLists.txt).

usage: slice_win.py --src FILE --out FILE [--drop SYMBOL ...]

tpw-decomp's CMakeLists.txt builds src/sound/CSprocketsModule.cpp for the game library without the methods the
platform layer's sound module defines (tpw_slice ... DROP, which runs the decomp's slicer on libclang). The Windows
build makes that copy here instead, from the same list, with no libclang: a definition is found by its qualified name
in the text (one only, or the build stops), and its lines are blanked, so the copy keeps the original's line numbers.
The copy also leaves out the file's Mach-O assembler directives (asm(".weak_definition ...")), which COFF's assembler
does not know.

A SYMBOL is a mangled name in the Mach-O spelling the decomp's lists use (__ZN16CSprocketsModule7StopAllEv): a member
function, constructor or destructor of a class at namespace scope. Its parameters are not compared, so a name with
two definitions in the file is refused rather than guessed at.
"""
import argparse
import pathlib
import re
import sys


def qualified_name(symbol):
    """'A::B' for _ZN1A1BE..., 'A::A' and 'A::~A' for the constructors and destructors."""
    name = symbol[1:] if symbol.startswith("__Z") else symbol
    i = 4 if name.startswith("_ZNK") else 3
    parts = []
    while name.startswith("_ZN") and i < len(name) and name[i].isdigit():
        digits = re.match(r"\d+", name[i:]).group()
        i += len(digits)
        parts.append(name[i:i + int(digits)])
        i += int(digits)
    if name[i:i + 2] in ("C1", "C2", "C3", "D0", "D1", "D2"):
        parts.append(("~" if name[i] == "D" else "") + parts[-1])
        i += 2
    if len(parts) < 2 or name[i:i + 1] != "E":
        sys.exit(f"slice_win: {symbol}: not a member of a class at namespace scope")
    return "::".join(parts)


def masked(text):
    """The text with its comments and string and character literals blanked (newlines kept), for searching."""
    out = list(text)
    i, n = 0, len(text)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isdigit())):
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j += 1
        else:
            i += 1
            continue
        blank(i, j)
        i = j
    return "".join(out)


def matching(text, i, opening, closing):
    """The index just past the bracket that closes the one at i."""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == opening:
            depth += 1
        elif text[j] == closing:
            depth -= 1
            if depth == 0:
                return j + 1
    sys.exit("slice_win: unbalanced brackets")


def definition_span(text, search, name):
    """The span from the start of the line of the one definition of name to its closing brace."""
    spans = []
    for m in re.finditer(r"(?<![\w:~])" + re.escape(name) + r"\s*\(", search):
        line_start = search.rfind("\n", 0, m.start()) + 1
        if not re.fullmatch(r"[\w\s:*&<>,()]*", search[line_start:m.start()]):
            continue   # a use inside an expression
        after = matching(search, m.end() - 1, "(", ")")
        head = re.match(r"(?:\s|\b(?:const|noexcept|override|final)\b)*", search[after:]).end()
        if search[after + head:after + head + 1] != "{":
            continue   # a declaration or a call
        spans.append((line_start, matching(search, after + head, "{", "}")))
    if len(spans) != 1:
        sys.exit(f"slice_win: {len(spans)} definitions of {name} in the file, not one")
    return spans[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--drop", nargs="*", default=[])
    args = ap.parse_args()
    src = pathlib.Path(args.src)
    text = src.read_text(encoding="utf-8")
    search = masked(text)
    spans = [definition_span(text, search, qualified_name(symbol)) for symbol in args.drop]
    for m in re.finditer(r'^[ \t]*(?:asm|__asm__)[ \t]*\([ \t]*"\.weak_definition [^"\n]*"[ \t]*\);[ \t]*$', text, re.M):
        if search[m.start():m.end()].strip().startswith(("asm", "__asm__")):   # not inside a comment
            spans.append((m.start(), m.end()))
    out = list(text)
    for a, b in spans:
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "
    result = f'#line 1 "{src.as_posix()}"\n' + re.sub(r"[ \t]+\n", "\n", "".join(out))
    path = pathlib.Path(args.out)
    if not path.exists() or path.read_text(encoding="utf-8") != result:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(result, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
