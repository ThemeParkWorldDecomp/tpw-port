"""Package and sign the Android launcher built by CMake."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile


ANDROID = Path(__file__).resolve().parent
SOURCE = ANDROID.parents[1]
PACKAGE = "org.tpwport.android"
LIBRARIES = ("SDL3/libSDL3.so", "libtpw_launcher.so")


def run(*args):
    subprocess.run(args, check=True)


def jdk_tool(name):
    if home := os.environ.get("JAVA_HOME"):
        return Path(home) / "bin" / (name + (".exe" if os.name == "nt" else ""))
    return name


def build_config(build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        key_type, sep, value = line.partition("=")
        if sep and ":" in key_type and not line.startswith(("#", "//")):
            cache[key_type.split(":", 1)[0]] = value
    return cache


def assemble(output, base, dex, libraries, abi, sdl, ndk):
    third_party = SOURCE / "third_party"
    decomp_vendor = SOURCE / "tpw-decomp" / "third_party"
    notices = {
        "SDL.txt": sdl / "LICENSE.txt",
        "SDL-HIDAPI.txt": sdl / "src/hidapi/LICENSE-bsd.txt",
        "SDL-YUV2RGB.txt": sdl / "src/video/yuv2rgb/LICENSE",
        "zlib.txt": decomp_vendor / "zlib/LICENSE",
        "libarchive.txt": decomp_vendor / "libarchive/COPYING",
        "liblzma.txt": decomp_vendor / "xz/COPYING.0BSD",
        "libjpeg.txt": third_party / "libjpeg-6b/README",
        "stb.txt": third_party / "stb/LICENSE",
        "Fredoka.txt": third_party / "fonts/fredoka/OFL.txt",
        "Nunito.txt": third_party / "fonts/nunito/OFL.txt",
        "Vulkan-Headers.md": third_party / "vulkan/LICENSE.md",
        "Android-NDK.txt": ndk / "NOTICE",
        "Android-NDK-toolchain.txt": ndk / "NOTICE.toolchain",
    }
    with ZipFile(output, "w") as apk, ZipFile(base) as resources, ZipFile(dex) as bytecode:
        for entry in resources.infolist():
            apk.writestr(entry, resources.read(entry))
        for entry in bytecode.infolist():
            if entry.filename.endswith(".dex"):
                apk.writestr(entry, bytecode.read(entry))
        for library in libraries:
            apk.write(library, f"lib/{abi}/{library.name}", compress_type=ZIP_STORED)
        for name, path in notices.items():
            apk.write(path, f"assets/licenses/{name}", compress_type=ZIP_DEFLATED)
        apk.writestr("assets/licenses/NOTICE.txt",
                     "This software is based in part on the work of the Independent JPEG Group.\n")


def package(args):
    build = args.build_dir.resolve()
    tools = args.build_tools_dir.resolve()
    platform = args.android_jar.resolve()
    output = args.output_apk.resolve()
    cache = build_config(build)
    abi = cache["ANDROID_ABI"]
    if abi not in ("arm64-v8a", "x86_64"):
        raise ValueError(f"Unsupported Android ABI: {abi}")
    sdl = Path(cache["TPW_ANDROID_SDL_SOURCE_DIR"])
    ndk = Path(cache["CMAKE_TOOLCHAIN_FILE"]).resolve().parents[2]
    suffix = ".exe" if os.name == "nt" else ""
    aapt = tools / f"aapt2{suffix}"
    zipalign = tools / f"zipalign{suffix}"
    apksigner = (jdk_tool("java"), "-jar", tools / "lib/apksigner.jar")
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="tpw-apk-", dir=output.parent) as tmp:
        work = Path(tmp)
        libraries = []
        for name in LIBRARIES:
            stripped = work / Path(name).name
            run(cache["CMAKE_STRIP"], "--strip-debug", "-o", stripped, build / name)
            libraries.append(stripped)

        base, resources, generated = work / "base.apk", work / "res.zip", work / "generated"
        run(aapt, "compile", "--dir", ANDROID / "res", "-o", resources)
        link = [aapt, "link", "-I", platform, "--manifest", ANDROID / "AndroidManifest.xml",
                "--java", generated, "--custom-package", PACKAGE, "-o", base, resources]
        if args.application_id != PACKAGE:
            link.extend(("--rename-manifest-package", args.application_id))
        if args.version_code or args.version_name:
            link.append("--replace-version")
        if args.version_code:
            link.extend(("--version-code", str(args.version_code)))
        if args.version_name:
            link.extend(("--version-name", args.version_name))
        if not args.release_keystore:
            link.append("--debug-mode")
        run(*link)

        classes = work / "classes"
        classes.mkdir()
        java = sorted((sdl / "android-project/app/src/main/java").rglob("*.java"))
        if not java:
            raise ValueError(f"SDL Android Java sources missing from {sdl}")
        java.extend(sorted(ANDROID.glob("*.java")))
        java.append(generated / "org/tpwport/android/R.java")
        run(jdk_tool("javac"), "--release", "8", "-encoding", "UTF-8", "-classpath", platform, "-d", classes, *java)
        jar, dex = work / "classes.jar", work / "dex.zip"
        run(jdk_tool("jar"), "cf", jar, "-C", classes, ".")
        run(jdk_tool("java"), "-cp", tools / "lib/d8.jar", "com.android.tools.r8.D8", "--lib", platform,
            "--min-api", "33", "--output", dex, jar)

        unsigned, aligned, signed = work / "unsigned.apk", work / "aligned.apk", work / "signed.apk"
        assemble(unsigned, base, dex, libraries, abi, sdl, ndk)
        run(zipalign, "-P", "16", "-f", "4", unsigned, aligned)
        if args.release_keystore:
            signature = ("--ks", args.release_keystore.resolve(), "--ks-key-alias", args.release_key_alias,
                         "--ks-pass", "env:TPW_SIGNING_PASSWORD")
        else:
            key = build / "android-debug.jks"
            if not key.exists():
                run(jdk_tool("keytool"), "-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android",
                    "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
                    "-dname", "CN=Android Debug,O=Android,C=US")
            signature = ("--ks", key, "--ks-pass", "pass:android")
        run(*apksigner, "sign", *signature, "--v4-signing-enabled", "false", "--out", signed, aligned)
        run(*apksigner, "verify", "--verbose", signed)
        run(zipalign, "-c", "-P", "16", "4", signed)
        signed.replace(output)
    print(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--android-jar", type=Path, required=True)
    parser.add_argument("--build-tools-dir", type=Path, required=True)
    parser.add_argument("--output-apk", type=Path, required=True)
    parser.add_argument("--application-id", default=PACKAGE)
    parser.add_argument("--version-code", type=int)
    parser.add_argument("--version-name")
    parser.add_argument("--release-keystore", type=Path)
    parser.add_argument("--release-key-alias")
    args = parser.parse_args()
    if args.version_code is not None and not 1 <= args.version_code <= 2147483647:
        parser.error("--version-code must be a positive 32-bit integer")
    if args.release_keystore and not (args.release_key_alias and os.environ.get("TPW_SIGNING_PASSWORD")):
        parser.error("Release signing needs --release-key-alias and TPW_SIGNING_PASSWORD")
    if args.release_key_alias and not args.release_keystore:
        parser.error("--release-key-alias requires --release-keystore")
    try:
        package(args)
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Android packaging failed: {error}\n")


if __name__ == "__main__":
    main()
