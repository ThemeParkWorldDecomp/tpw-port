#!/usr/bin/env python3
"""gen_config.py: writes config/ for the trimmed FFmpeg beside it (third_party/ffmpeg), in place of FFmpeg's configure.

usage: python3 gen_config.py [FFMPEG_DIR]     (default: the directory of this script)

FFmpeg's sources read their build settings from config.h and config_components.h, two public settings from
libavutil/avconfig.h, and the lists of decoders, demuxers, protocols and bitstream filters they register from small .c
files that configure writes. This script writes all of them into config/ (config/public/ holds avconfig.h, the one the
library's users see too; CMakeLists.txt puts both on the include path), so neither configure nor FFmpeg's makefiles
are ever run. Its output is kept in the tree: building needs no Python.

Every CONFIG_*, HAVE_* and ARCH_* name that the kept sources test is defined: to 1 when it is in ON below, else to 0.
The settings are the conservative ones that hold on macOS (arm64 and x86_64) and on Linux with gcc or clang: no
assembly or intrinsics of any architecture, no threads, no hardware decoding, no external library, and only the C99
and POSIX functions every one of those systems has; on Windows, the Win32 ones in WINDOWS below. Endianness comes from
the compiler. Run it again after the kept file set changes (a new name is then defined as 0) and look at the list it
prints. The files are written with LF line ends on every host.
"""
import pathlib
import re
import sys


# What the game's films need (QuickTime .mov, Sorenson Video 1, IMA4 ADPCM): the components registered and built.
DECODERS = ["svq1", "adpcm_ima_qt"]
DEMUXERS = ["mov"]
PROTOCOLS = ["file"]
BSFS = ["null"]                      # a decoder without bitstream filters of its own gets the null filter
COMPONENTS = ([d + "_decoder" for d in DECODERS] + [d + "_demuxer" for d in DEMUXERS] +
              [p + "_protocol" for p in PROTOCOLS] + [b + "_bsf" for b in BSFS])

ON = {
    # the libraries kept
    "CONFIG_AVUTIL", "CONFIG_AVCODEC", "CONFIG_AVFORMAT", "CONFIG_SWSCALE",
    # subsystems the components above select (configure's *_select)
    "CONFIG_HPELDSP", "CONFIG_ISO_MEDIA", "CONFIG_RIFFDEC", "CONFIG_MPEG4AUDIO",
    # configure's defaults that change what code is built
    "CONFIG_STATIC", "CONFIG_SAFE_BITSTREAM_READER", "CONFIG_SWSCALE_ALPHA", "CONFIG_UNSTABLE",
    # zlib (the game links it anyway): the films' movie headers are compressed (a cmov atom)
    "CONFIG_ZLIB",
    # C99 <math.h>
    "HAVE_ATANF", "HAVE_ATAN2F", "HAVE_CBRT", "HAVE_CBRTF", "HAVE_COPYSIGN", "HAVE_COSF", "HAVE_ERF", "HAVE_EXP2",
    "HAVE_EXP2F", "HAVE_EXPF", "HAVE_HYPOT", "HAVE_ISFINITE", "HAVE_ISINF", "HAVE_ISNAN", "HAVE_LDEXPF", "HAVE_LLRINT",
    "HAVE_LLRINTF", "HAVE_LOG2", "HAVE_LOG2F", "HAVE_LOG10F", "HAVE_LRINT", "HAVE_LRINTF", "HAVE_POWF", "HAVE_RINT",
    "HAVE_ROUND", "HAVE_ROUNDF", "HAVE_SINF", "HAVE_TRUNC", "HAVE_TRUNCF",
    # POSIX headers and functions (macOS and Linux)
    "HAVE_UNISTD_H", "HAVE_FCNTL", "HAVE_ISATTY", "HAVE_GETTIMEOFDAY", "HAVE_CLOCK_GETTIME", "HAVE_NANOSLEEP",
    "HAVE_USLEEP", "HAVE_POSIX_MEMALIGN", "HAVE_SYSCONF", "HAVE_ACCESS", "HAVE_GMTIME_R", "HAVE_LOCALTIME_R",
    "HAVE_STRERROR_R", "HAVE_DIRENT_H", "HAVE_LSTAT", "HAVE_MKSTEMP", "HAVE_SYS_TIME_H", "HAVE_SYS_SELECT_H",
    "HAVE_POLL_H", "HAVE_TERMIOS_H", "HAVE_SYS_RESOURCE_H", "HAVE_GETRUSAGE", "HAVE_SETRLIMIT", "HAVE_MMAP",
    "HAVE_SYS_PARAM_H", "HAVE_DLFCN_H", "HAVE_ARPA_INET_H", "HAVE_INET_ATON",
    # compiler features of gcc and clang
    "HAVE_ATTRIBUTE_PACKED", "HAVE_ATTRIBUTE_MAY_ALIAS", "HAVE_PRAGMA_DEPRECATED",
}
# Off, among others: every ARCH_* and every CPU extension (no assembly, no intrinsics), HAVE_THREADS and HAVE_PTHREADS
# (the game decodes on one thread), CONFIG_NETWORK, every hardware API and every external library but zlib.
# Names that are not settings (a define the sources make themselves) are found and left out below.

# Where Windows (MinGW-w64 with the UCRT, as FFmpeg's configure finds it there) differs: the POSIX functions and headers
# it lacks are off, and the Win32 ones FFmpeg uses in their place are on. config.h writes these names in an #ifdef _WIN32.
WINDOWS = {
    **{name: 0 for name in (
        "HAVE_FCNTL", "HAVE_CLOCK_GETTIME", "HAVE_NANOSLEEP", "HAVE_POSIX_MEMALIGN", "HAVE_SYSCONF", "HAVE_GMTIME_R",
        "HAVE_LOCALTIME_R", "HAVE_STRERROR_R", "HAVE_LSTAT", "HAVE_MKSTEMP", "HAVE_POLL_H",
        "HAVE_MMAP")},
    **{name: 1 for name in (
        "HAVE_ALIGNED_MALLOC", "HAVE_IO_H", "HAVE_DIRECT_H", "HAVE_WINDOWS_H", "HAVE_DOS_PATHS", "HAVE_SETMODE",
        "HAVE_GETSYSTEMTIMEASFILETIME", "HAVE_GETPROCESSAFFINITYMASK", "HAVE_VIRTUALALLOC", "HAVE_SLEEP")},
}


def tokens(root):
    """(the names the kept sources test, the names they define themselves). The CPU extension tests (have_neon() and
    the like, libavutil/cpu_internal.h's CPUEXT macros) paste HAVE_<EXT>, HAVE_<EXT>_INLINE and HAVE_<EXT>_EXTERNAL
    together, so those are added for every extension such a macro names."""
    names, defined = set(), set()
    cpuext = re.compile(rb"\bCPUEXT\w*\(([^)]*)\)")
    pat = re.compile(rb"\b((?:CONFIG|HAVE|ARCH)_[A-Z0-9_]+)\b")
    define = re.compile(rb"^\s*#\s*(?:define|undef)\s+((?:CONFIG|HAVE|ARCH)_[A-Z0-9_]+)", re.M)
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".c", ".h") or "config" in path.relative_to(root).parts[:1]:
            continue
        data = path.read_bytes()
        names.update(m.decode() for m in pat.findall(data))
        defined.update(m.decode() for m in define.findall(data))
        for args in cpuext.findall(data):
            for arg in args.split(b",")[1:]:
                arg = arg.strip().decode()
                if re.fullmatch(r"[A-Z][A-Z0-9_]*", arg) and arg not in ("SUFFIX", "CPUEXT"):
                    names.update("HAVE_" + arg + s for s in ("", "_INLINE", "_EXTERNAL"))
    return names, defined


# Every component FFmpeg has, from the lists that register them: some sources build a component's name by pasting tokens
# (adpcm.c's CASE()), so each one is defined, not only those spelled out somewhere.
REGISTRIES = ["libavcodec/allcodecs.c", "libavcodec/parsers.c", "libavcodec/bitstream_filters.c",
              "libavformat/allformats.c", "libavformat/protocols.c"]


def all_components(root):
    names = set()
    pat = re.compile(rb"\bff_(\w+?_(?:decoder|encoder|parser|bsf|demuxer|muxer|protocol))\b")
    for rel in REGISTRIES:
        names.update("CONFIG_" + m.decode().upper() for m in pat.findall((root / rel).read_bytes()))
    return names


def is_component(name):
    return re.search(r"_(DECODER|ENCODER|PARSER|BSF|DEMUXER|MUXER|PROTOCOL|INDEV|OUTDEV|FILTER|HWACCEL)$", name) is not None


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else __file__).resolve()
    if root.is_file():
        root = root.parent
    out = root / "config"
    names, defined = tokens(root)
    names -= defined
    names -= {"HAVE_AV_CONFIG_H", "HAVE_BIGENDIAN"}
    components_on = {"CONFIG_" + c.upper() for c in COMPONENTS}
    names |= components_on | all_components(root)

    head = "/* Written by gen_config.py (in place of FFmpeg's configure); run it again rather than edit this file. */"
    config = [head, "#ifndef FFMPEG_CONFIG_H\n#define FFMPEG_CONFIG_H",
              '#define FFMPEG_CONFIGURATION "tpw: gen_config.py, ' + " ".join(COMPONENTS) + '"',
              '#define FFMPEG_LICENSE "LGPL version 2.1 or later"',
              "#define CONFIG_THIS_YEAR 2026",
              '#define FFMPEG_DATADIR ""', '#define AVCONV_DATADIR ""', '#define CC_IDENT "tpw"', '#define OS_NAME tpw',
              '#define EXTERN_PREFIX ""', "#define EXTERN_ASM", '#define BUILDSUF ""', '#define SLIBSUF ""',
              "#define SWS_MAX_FILTER_SIZE 256", "#define av_restrict restrict",
              "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__",
              "#define HAVE_BIGENDIAN 1", "#else", "#define HAVE_BIGENDIAN 0", "#endif"]
    components = [head, "#ifndef FFMPEG_CONFIG_COMPONENTS_H\n#define FFMPEG_CONFIG_COMPONENTS_H"]
    for name in sorted(names):
        value = 1 if name in ON or name in components_on else 0
        lines = components if is_component(name) else config
        if WINDOWS.get(name, value) != value:
            lines += ["#ifdef _WIN32", "#define %s %d" % (name, WINDOWS[name]), "#else", "#define %s %d" % (name, value),
                      "#endif"]
        else:
            lines.append("#define %s %d" % (name, value))
    config.append("#endif /* FFMPEG_CONFIG_H */")
    components.append("#endif /* FFMPEG_CONFIG_COMPONENTS_H */")

    files = {
        "config.h": config,
        "config_components.h": components,
        "public/libavutil/avconfig.h": [head, "#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H",
                                 "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__",
                                 "#define AV_HAVE_BIGENDIAN 1", "#else", "#define AV_HAVE_BIGENDIAN 0", "#endif",
                                 "#define AV_HAVE_FAST_UNALIGNED 0", "#endif /* AVUTIL_AVCONFIG_H */"],
    }

    def component_list(path, struct, name, items):
        lines = [head, "static const %s * const %s[] = {" % (struct, name)]
        lines += ["    &ff_%s," % i for i in items]
        lines += ["    NULL };"]
        files[path] = lines

    component_list("libavcodec/codec_list.c", "FFCodec", "codec_list", [d + "_decoder" for d in DECODERS])
    component_list("libavcodec/parser_list.c", "FFCodecParser", "parser_list", [])
    component_list("libavcodec/bsf_list.c", "FFBitStreamFilter", "bitstream_filters", [b + "_bsf" for b in BSFS])
    component_list("libavformat/demuxer_list.c", "FFInputFormat", "demuxer_list", [d + "_demuxer" for d in DEMUXERS])
    component_list("libavformat/muxer_list.c", "FFOutputFormat", "muxer_list", [])
    component_list("libavformat/protocol_list.c", "URLProtocol", "url_protocols", [p + "_protocol" for p in PROTOCOLS])

    for rel, lines in files.items():
        path = out / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(("\n".join(lines) + "\n").encode())   # LF on every host
    on = sorted(n for n in names if n in ON or n in components_on)
    print("gen_config: %d names, %d on: %s" % (len(names), len(on), " ".join(on)))
    unused = sorted(ON - names)
    if unused:
        print("gen_config: on but not tested by any kept file: " + " ".join(unused))
    unused = sorted(set(WINDOWS) - names)
    if unused:
        print("gen_config: set for Windows but not tested by any kept file: " + " ".join(unused))


if __name__ == "__main__":
    main()
