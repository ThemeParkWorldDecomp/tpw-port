#!/usr/bin/env python3
"""Linux: tpw-decomp's slicer (tpw-decomp/devtools/slicer.py) run on a libclang the build found, for the platform tests
(CMakeLists.txt). Takes the slicer's own arguments.

The slicer indexes each source with libclang, which it finds beside the compiler's own resource folder: that is Apple's
toolchain and a clang toolchain. A Linux build is usually made with GCC, which has no -print-resource-dir and no libclang
beside it, so this runs the slicer itself, unchanged, with two things put around it:

- libclang: the one TPW_LIBCLANG names (CMakeLists.txt finds it among the distribution's LLVM libraries, such as
  /usr/lib/llvm-18/lib/libclang-18.so.1; the package is libclang-NN-dev or libclang1-NN on Debian and Ubuntu).
- The resource folder (clang's own headers, stddef.h and the like, which the parse needs): the one of the clang beside that
  libclang (<llvm root>/lib/clang/<version>), found by looking next to the library; a clang on the PATH is asked instead
  when there is none there.
- Symbol names: the keep and drop lists, and the slicer's own probes, spell symbols as Mach-O does, with one leading
  underscore more than ELF; the names libclang gives are spelled that way (devtools/slicer_win.py's Library, which does
  the same for COFF).

The game library itself needs no libclang on Linux: devtools/slice_win.py makes its one sliced copy (CMakeLists.txt).
"""
import ctypes.util
import glob
import os
import pathlib
import shutil
import subprocess
import sys

SLICER = pathlib.Path(__file__).resolve().parent.parent / "tpw-decomp" / "devtools" / "slicer.py"


def resource_dir(libclang):
    """clang's resource folder: the clang beside the library, else the one on the PATH."""
    lib = pathlib.Path(libclang).resolve()
    for root in (lib.parent, lib.parent.parent):
        found = [f for f in sorted(glob.glob(str(root / "clang" / "*")) + glob.glob(str(root / "lib" / "clang" / "*")))
                 if pathlib.Path(f, "include").is_dir()]
        if found:
            return found[-1]
    clang = shutil.which("clang")
    if clang:
        return subprocess.run([clang, "-print-resource-dir"], capture_output=True, text=True, check=True).stdout.strip()
    sys.exit(f"slicer_linux: no clang resource folder beside {libclang}, and no clang on the PATH")


def main():
    libclang = os.environ.get("TPW_LIBCLANG", "")
    if not pathlib.Path(libclang).is_file():
        sys.exit(f"slicer_linux: no libclang at TPW_LIBCLANG ({libclang or 'not set'})")
    res = resource_dir(libclang)
    sys.path.insert(0, str(SLICER.parent))
    import slicer

    # The slicer asks the compiler for its resource folder and looks for libclang beside it, then asks find_library:
    # here the compiler's question is answered with the folder found above, and find_library with TPW_LIBCLANG.
    original_run = subprocess.run

    def run(cmd, *args, **kwargs):
        if isinstance(cmd, list) and len(cmd) == 2 and cmd[1] == "-print-resource-dir":
            return subprocess.CompletedProcess(cmd, 0, stdout=res + "\n", stderr="")
        return original_run(cmd, *args, **kwargs)

    find_library = ctypes.util.find_library
    subprocess.run = run
    slicer.subprocess.run = run
    ctypes.util.find_library = lambda name: libclang if name == "clang" else find_library(name)
    # The names libclang gives, respelled as Mach-O's (slicer_win.py's wrapper of the library).
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    from slicer_win import Library
    load_libclang = slicer.load_libclang

    def load(compiler):
        cl, res_dir = load_libclang(compiler)
        return Library(cl), res_dir

    slicer.load_libclang = load
    sys.argv[0] = str(SLICER)
    slicer.main()


if __name__ == "__main__":
    main()
