#!/usr/bin/env python3
"""Windows: tpw-decomp's slicer (tpw-decomp/devtools/slicer.py) run on libclang with a MinGW toolchain, for the
platform tests (CMakeLists.txt). Takes the slicer's own arguments.

The slicer is written for macOS and Linux, and the game library does without it on Windows (devtools/slice_win.py).
The platform tests keep only some functions of their src files, which needs the slicer's index of each file. This
runs the slicer itself, unchanged, with what differs on Windows put around it:

- libclang: llvm-mingw ships none, so the one TPW_LIBCLANG names (CMakeLists.txt sets it).
- The target and headers: a MinGW driver takes its target from a config file beside it (llvm-mingw's
  x86_64-w64-windows-gnu.cfg) and its headers from the toolchain's root; libclang reads neither and would look for
  MSVC's, so the parse gets them as flags.
- Symbol names: the keep and drop lists, and the slicer's own probes, spell symbols as Mach-O does, with one leading
  underscore more than 64-bit COFF; the names libclang gives are spelled that way.
- The copy's #line directive: a Windows path's backslashes would be escapes in its string; '/' works there too.
"""
import ctypes
import ctypes.util
import os
import pathlib
import shutil
import subprocess
import sys

SLICER = pathlib.Path(__file__).resolve().parent.parent / "tpw-decomp" / "devtools" / "slicer.py"


def macho(name):
    """A symbol as Mach-O spells it."""
    return b"_" + name if name else name


class Name:
    """A symbol name libclang gave, respelled: stands in for its CXString."""

    def __init__(self, text):
        self.text = text


class NameSet:
    """A CXStringSet of respelled names: what the slicer reads of one (contents.Strings, contents.Count)."""

    def __init__(self, names):
        self.contents = self
        self.Strings = names
        self.Count = len(names)


class Library:
    """libclang, with the symbol names it gives respelled; every other call goes to it as it is."""

    def __init__(self, cl):
        self._cl = cl

    def __getattr__(self, name):
        return getattr(self._cl, name)

    def clang_Cursor_getMangling(self, cursor):
        cl = self._cl
        s = cl.clang_Cursor_getMangling(cursor)
        text = cl.clang_getCString(s) or b""
        cl.clang_disposeString(s)
        return Name(macho(text))

    def clang_Cursor_getCXXManglings(self, cursor):
        cl = self._cl
        ss = cl.clang_Cursor_getCXXManglings(cursor)
        names = [Name(macho(cl.clang_getCString(ss.contents.Strings[i]) or b"")) for i in range(ss.contents.Count)]
        cl.clang_disposeStringSet(ss)
        return NameSet(names)

    def clang_getCString(self, s):
        return s.text if isinstance(s, Name) else self._cl.clang_getCString(s)

    def clang_disposeString(self, s):
        if not isinstance(s, Name):
            self._cl.clang_disposeString(s)

    def clang_disposeStringSet(self, ss):
        if not isinstance(ss, NameSet):
            self._cl.clang_disposeStringSet(ss)


def main():
    libclang = os.environ.get("TPW_LIBCLANG", "")
    if not pathlib.Path(libclang).is_file():
        sys.exit(f"slicer_win: no libclang.dll at TPW_LIBCLANG ({libclang or 'not set'})")
    sys.path.insert(0, str(SLICER.parent))
    import slicer

    # Beside the compiler's resource folder the slicer looks for libclang.so, and then asks find_library.
    find_library = ctypes.util.find_library
    ctypes.util.find_library = lambda name: libclang if name == "clang" else find_library(name)
    load_libclang = slicer.load_libclang

    def load(compiler):
        cl, res = load_libclang(compiler)
        return Library(cl), res

    slicer.load_libclang = load

    init, parse = slicer.Parser.__init__, slicer.Parser.parse

    def parser_init(self, compiler):
        init(self, compiler)
        target = subprocess.run([compiler, "-dumpmachine"], capture_output=True, text=True, check=True).stdout.strip()
        root = pathlib.Path(shutil.which(compiler) or compiler).resolve().parent.parent
        self.mingw_flags = [f"--target={target}", f"--sysroot={root}"]
        if (root / "include" / "c++" / "v1").is_dir():
            self.mingw_flags.append("-stdlib=libc++")   # llvm-mingw's mingw32-common.cfg; it has no libstdc++

    def parser_parse(self, src, flags, contents=None):
        if not any(f.startswith(("--target", "-target")) for f in flags):
            flags = self.mingw_flags + list(flags)
        return parse(self, src, flags, contents)

    slicer.Parser.__init__, slicer.Parser.parse = parser_init, parser_parse

    apply = slicer.apply
    slicer.apply = lambda text, items, keep, src_name: apply(text, items, keep, str(src_name).replace("\\", "/"))

    sys.argv[0] = str(SLICER)
    slicer.main()


if __name__ == "__main__":
    main()
