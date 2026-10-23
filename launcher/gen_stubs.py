#!/usr/bin/env python3
"""gen_stubs.py: writes stubs_generated.cpp, the stand-ins that let the launcher link while
some of what it calls is not wired up yet.

usage: gen_stubs.py --out FILE --include-dir DIR [--syslib LIB ...] [--provides ARCHIVE ...] [--c-function NAME ...]
                    [--nm NM] [--cxxfilt C++FILT] OBJECT_OR_ARCHIVE ...

The inputs are everything the launcher links that we build ourselves: libtpw.a (the
translated game code), libtpw_platform.a (the platform layer) and the launcher's objects.
Every symbol those inputs need and none of them defines, and that neither the host's system
libraries nor the static libraries given with --provides (SDL3, zlib and FFmpeg, built from
source with the launcher) define, gets a stand-in:

- a function becomes a stub that logs "unimplemented platform call: <name>" (or "not
  wired up yet (does nothing): <name>" for the rest) the first time it is called and returns zero in the
  integer and floating-point return registers;
- a variable becomes a zero-initialised definition of its declared type (the generated file
  includes every header in --include-dir, so `decltype(name) name{};` is enough). A variable
  that cannot be defined that way (a reference, an incomplete type, an array without a
  bound) fails to compile here: define it by hand in launcher/pending_manual.cpp, whose
  object is one of the inputs, and it disappears from the generated file.
- a vtable or typeinfo symbol cannot be stood in for (it needs the class's key function
  defined in C++): those are listed at the top of the generated file and stay undefined, so
  the link names them.

Nothing here is kept between runs: when a unit lands and defines a symbol, the next build
drops its stand-in. Standard library only, so any Python 3.8+ runs it.
"""
import argparse
import ctypes
import ctypes.util
import pathlib
import platform
import re
import subprocess
import sys

DARWIN = sys.platform == "darwin"
WINDOWS = sys.platform == "win32"
# Scopes whose functions belong to the platform layer rather than to the game.
PLATFORM_SCOPES = ("NS_MacDoze::", "SamsUtilities::", "IDirectDraw", "IdxDirectDraw::", "IDirect3D")
SPECIAL_PREFIXES = ("vtable for ", "typeinfo for ", "typeinfo name for ", "VTT for ", "guard variable for ",
                    "construction vtable for ", "thunk to ", "non-virtual thunk to ", "virtual thunk to ")


def run(cmd, text_in=None):
    return subprocess.run(cmd, input=text_in, capture_output=True, text=True, check=True).stdout


def symbols(nm, files):
    """(undefined, defined) external symbol names, as the object files spell them."""
    undefined, defined = set(), set()
    for f in files:
        for line in run([nm, "-g", f]).splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "U":
                undefined.add(parts[1])
            elif len(parts) == 3 and parts[1] not in "Uu":
                defined.add(parts[2])
                if WINDOWS and parts[2].startswith("__imp_"):
                    defined.add(parts[2][6:])   # an import library's data: the linker auto-imports the plain name
    return undefined, defined


def c_name(symbol):
    """The name as C source spells it (Mach-O adds a leading underscore)."""
    return symbol[1:] if DARWIN and symbol.startswith("_") else symbol


class SystemLibraries:
    """Answers whether the host's libraries define a symbol (dlsym over the given list)."""

    def __init__(self, extra):
        names = [None]
        if WINDOWS:
            # Nothing is dlsym'ed: the C runtime and the Win32 import libraries come as --provides (CMakeLists.txt).
            self.handles = []
            return
        if DARWIN:
            names += ["/usr/lib/libSystem.B.dylib", "/usr/lib/libc++.1.dylib", "/usr/lib/libc++abi.dylib"]
        else:
            names += [ctypes.util.find_library(n) for n in ("c", "m", "stdc++", "gcc_s")]
        self.handles = []
        for name in names + list(extra):
            if name is None and names.index(None) != 0:
                continue
            try:
                self.handles.append(ctypes.CDLL(name))
            except OSError as error:
                print(f"gen_stubs: note: could not load {name}: {error}", file=sys.stderr)

    def has(self, name):
        for handle in self.handles:
            try:
                getattr(handle, name)
                return True
            except AttributeError:
                pass
        return False


def reserved(name):
    """Names the implementation owns (the compiler, linker or C runtime provides them)."""
    return name.startswith("__") or re.match(r"_[A-Z]", name) is not None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--include-dir", required=True)
    ap.add_argument("--syslib", action="append", default=[], help="a library the launcher links (dlsym'ed)")
    ap.add_argument("--provides", action="append", default=[], help="a static library the launcher links; the symbols it defines get no stand-in")
    ap.add_argument("--c-function", action="append", default=[], help="an unmangled name that is a function")
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--cxxfilt", default="c++filt")
    ap.add_argument("inputs", nargs="+")
    args = ap.parse_args()

    undefined, defined = symbols(args.nm, args.inputs)
    labels = []
    if WINDOWS:
        # An asm label in the Mach-O spelling (__asm__("__ZN..."), on a declaration, weak or not) names a C++ symbol
        # with one underscore more than COFF gives it. Each gets a thunk to COFF's name (label_thunks), which is stood in
        # for in turn when nothing defines it.
        labels = sorted(s for s in undefined | defined if s.startswith("__Z"))
        undefined = (undefined - set(labels)) | {s[1:] for s in labels}
    defined |= symbols(args.nm, args.provides)[1]
    needed = sorted(undefined - defined)
    system = SystemLibraries(args.syslib)
    names = [c_name(s) for s in needed]
    demangled = run([args.cxxfilt], "\n".join(names) + "\n").splitlines()

    functions, variables, special = [], [], []
    for symbol, name, pretty in zip(needed, names, demangled):
        mangled = name.startswith("_Z")
        if system.has(name) or (not mangled and reserved(name)):
            continue
        if pretty.startswith("tpw_stub_"):   # defined by the generated file itself
            continue
        if not mangled:
            (functions if name in args.c_function else variables).append((symbol, name, name))
        elif pretty.startswith(SPECIAL_PREFIXES):
            special.append((symbol, name, pretty))
        elif "(" in pretty:
            functions.append((symbol, name, pretty))
        else:
            variables.append((symbol, name, pretty))

    include_dir = pathlib.Path(args.include_dir)
    # include/helpers holds only file-local helpers (static inline), each header for the few src files that share
    # them: nothing there to stand in for, and two of them may define one name.
    headers = sorted(str(p.relative_to(include_dir)) for p in include_dir.rglob("*.h")
                     if "generated" not in p.parts and "helpers" not in p.parts)
    # A function is the platform layer's when its scope is one of the shims' or, for a
    # function at global scope (the Mac Toolbox, OpenGL, DirectDrawCreate), when one of the
    # include/platform headers declares it.
    platform_text = "\n".join(p.read_text() for p in sorted((include_dir / "platform").glob("*.h")))

    def is_platform(pretty):
        name = pretty.split("(")[0]
        if name.startswith(PLATFORM_SCOPES):
            return True
        return "::" not in name and re.search(r"\b%s\s*\(" % re.escape(name), platform_text) is not None
    out = []
    w = out.append
    w("// GENERATED by launcher/gen_stubs.py at build time. Do not edit and do not commit.")
    w("// Stand-ins for what the launcher links but that nothing defines: not wired up yet, or a platform call not implemented yet.")
    w("#include <cstdio>")
    w("#include <cstdlib>")
    w("")
    for h in headers:
        w(f'#include "{h}"')
    w('#include "stubs.h"')
    w("")
    if special:
        w("// Vtables and type information that are still undefined (define the class's key")
        w("// function, by translating it or in launcher/pending_manual.cpp):")
        for _, _, pretty in special:
            w(f"//   {pretty}")
        w("")
    w(f"// {len(variables)} variables that nothing defines yet (zero-initialised).")
    for _, name, pretty in variables:
        if name.startswith("_Z"):
            w(f"decltype({pretty}) {pretty}{{}};")
        else:
            w(f"decltype(::{name}) {name}{{}};")
    w("")
    w(f"// {len(functions)} functions.")
    w("namespace {")
    w("struct StubInfo { const char* name; bool platform; };")
    w("const StubInfo kStubs[] = {")
    for _, _, pretty in functions:
        text = pretty.replace("\\", "\\\\").replace('"', '\\"')
        w(f'    {{"{text}", {"true" if is_platform(pretty) else "false"}}},')
    if not functions:
        w('    {"", false},')
    w("};")
    w(f"const int kStubCount = {len(functions)};")
    w("bool gStubHit[sizeof kStubs / sizeof kStubs[0]];")
    w("}  // namespace")
    w("")
    w('extern "C" void tpw_stub_hit(int index) {')
    w("    if (index < 0 || index >= kStubCount) return;")
    w("    const bool first = !gStubHit[index];")
    w("    gStubHit[index] = true;")
    w("    tpw_stub_called(kStubs[index].name, kStubs[index].platform, first);")
    w("}")
    w("")
    w("int tpw_stub_count() { return kStubCount; }")
    w("int tpw_stub_hits(const char** names, int capacity) {")
    w("    int n = 0;")
    w("    for (int i = 0; i < kStubCount; ++i) {")
    w("        if (!gStubHit[i]) continue;")
    w("        if (n < capacity) names[n] = kStubs[i].name;")
    w("        ++n;")
    w("    }")
    w("    return n;")
    w("}")
    w("")
    out.extend(trampolines(functions))
    if labels:
        out.extend(label_thunks(labels))
    text = "\n".join(out) + "\n"
    path = pathlib.Path(args.out)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
    platform_count = sum(1 for _, _, p in functions if is_platform(p))
    print(f"gen_stubs: {len(functions)} function stubs ({platform_count} platform calls, "
          f"{len(functions) - platform_count} game functions), {len(variables)} variables, "
          f"{len(special)} vtable or typeinfo symbols left undefined")
    for _, _, pretty in special:
        print(f"gen_stubs: undefined: {pretty}")


def returns_this(pretty):
    """Whether a demangled name is a constructor or destructor. On AArch64 those return
    `this`, and a derived class's constructor goes on using the value its base's
    constructor returned: a stand-in that returned zero there made the caller write
    through a null pointer."""
    head = pretty.split("(", 1)[0]
    depth, parts, part = 0, [], ""
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == ":" and depth == 0:
            if part:
                parts.append(part)
            part = ""
        else:
            part += ch
    parts.append(part)
    if len(parts) < 2:
        return False
    last, owner = parts[-1], parts[-2].split("<", 1)[0]
    return last == owner or last == "~" + owner


def trampolines(functions):
    """One entry point per stub: it passes its index to tpw_stub_hit and returns zero in
    the integer and floating-point return registers, so callers expecting an int, a
    pointer, a bool, a float or a double all read zero. (A caller expecting a structure
    through a hidden pointer gets its buffer back untouched.) Constructors and
    destructors return their first argument instead (see returns_this)."""
    machine = platform.machine().lower()
    prefix = "_" if DARWIN else ""
    lines = []
    w = lines.append
    if machine in ("arm64", "aarch64"):
        w("__asm__(")
        w('    ".text\\n"')
        w('    ".p2align 2\\n"')
        w(f'    "{prefix}tpw_stub_common:\\n"')
        w('    "    stp x29, x30, [sp, #-16]!\\n"')
        w('    "    mov x29, sp\\n"')
        w('    "    mov w0, w16\\n"')
        w(f'    "    bl {prefix}tpw_stub_hit\\n"')
        w('    "    mov x0, #0\\n"')
        w('    "    mov x1, #0\\n"')
        w('    "    fmov d0, xzr\\n"')
        w('    "    fmov d1, xzr\\n"')
        w('    "    fmov d2, xzr\\n"')
        w('    "    fmov d3, xzr\\n"')
        w('    "    ldp x29, x30, [sp], #16\\n"')
        w('    "    ret\\n"')
        w(f'    "{prefix}tpw_stub_common_this:\\n"')
        w('    "    stp x29, x30, [sp, #-32]!\\n"')
        w('    "    mov x29, sp\\n"')
        w('    "    str x0, [sp, #16]\\n"')
        w('    "    mov w0, w16\\n"')
        w(f'    "    bl {prefix}tpw_stub_hit\\n"')
        w('    "    ldr x0, [sp, #16]\\n"')
        w('    "    ldp x29, x30, [sp], #32\\n"')
        w('    "    ret\\n"')
        for i, (symbol, _, pretty) in enumerate(functions):
            w(f'    ".globl {symbol}\\n"')
            w('    ".p2align 2\\n"')
            w(f'    "{symbol}:\\n"')
            w(f'    "    mov w16, #{i & 0xffff}\\n"')
            if i > 0xffff:
                w(f'    "    movk w16, #{i >> 16}, lsl #16\\n"')
            w(f'    "    b {prefix}tpw_stub_common{"_this" if returns_this(pretty) else ""}\\n"')
        w(");")
    elif machine in ("x86_64", "amd64"):
        w("__asm__(")
        w('    ".text\\n"')
        w(f'    "{prefix}tpw_stub_common:\\n"')
        w('    "    pushq %rbp\\n"')
        w('    "    movq %rsp, %rbp\\n"')
        if WINDOWS:
            # The Windows x64 convention: the first argument in %ecx, and 32 bytes of shadow space for the callee.
            w('    "    movl %eax, %ecx\\n"')
            w('    "    subq $32, %rsp\\n"')
        else:
            w('    "    movl %eax, %edi\\n"')
        w(f'    "    call {prefix}tpw_stub_hit\\n"')
        w('    "    xorl %eax, %eax\\n"')
        w('    "    xorl %edx, %edx\\n"')
        w('    "    xorps %xmm0, %xmm0\\n"')
        w('    "    xorps %xmm1, %xmm1\\n"')
        if WINDOWS:
            w('    "    movq %rbp, %rsp\\n"')
        w('    "    popq %rbp\\n"')
        w('    "    ret\\n"')
        for i, (symbol, _, _) in enumerate(functions):
            w(f'    ".globl {symbol}\\n"')
            w(f'    "{symbol}:\\n"')
            w(f'    "    movl ${i}, %eax\\n"')
            w(f'    "    jmp {prefix}tpw_stub_common\\n"')
        w(");")
    else:
        # Any other processor: plain C functions under the symbols' names. Integer and
        # pointer results read zero; floating-point results are undefined there.
        for i, (symbol, _, _) in enumerate(functions):
            w(f'extern "C" long tpw_stub_fn_{i}() __asm__("{symbol}");')
            w(f'extern "C" long tpw_stub_fn_{i}() {{ tpw_stub_hit({i}); return 0; }}')
    return lines


def label_thunks(labels):
    """Windows: a jump under each asm label in the Mach-O spelling to the symbol it means, under COFF's name."""
    lines = ["__asm__(", '    ".text\\n"']
    for label in labels:
        lines += [f'    ".globl {label}\\n"', f'    "{label}:\\n"', f'    "    jmp {label[1:]}\\n"']
    lines.append(");")
    return lines


if __name__ == "__main__":
    main()
