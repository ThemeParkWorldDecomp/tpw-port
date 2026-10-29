#!/usr/bin/env python3
"""Make the game data folders the port reads, from the original downloads of the game.

usage: python3 devtools/import_originals.py --from DOWNLOADS --to WORK/extracted [--whole-disc] [--replace]
       python3 devtools/import_originals.py --from DOWNLOADS --find
       python3 devtools/import_originals.py --check WORK/extracted [--whole-disc]

The originals are found in DOWNLOADS (searched with its subfolders) by their content, never by their names, sizes or
hashes of the downloads: whatever file looks like one of these is taken, and every file written is then checked against
the SHA-256 list at the end of this script, so a wrong or damaged one is found there.

  cd     the US Mac CD image (SimTheme Park for the Mac, 2.0.1): a zip holding the disc image ("Theme Park.toast", a
         hybrid ISO 9660 and HFS disc whose first bytes are an Apple partition map). This is all the US edition needs.
  bonus  optional: the Theme Park World Bonus Stuff zip (a "Theme Park World Bonus Stuff/tpwfnt" folder of TrueType
         fonts, which serve only the platform tests)
  eur    optional: the Theme Park World European PC CD, an ISO 9660 image with Joliet names (raw 2352-byte sectors or
         cooked 2048; for tpw --eur). The European edition also needs the Mac CD above.
  Any patch file (.sea.sit) is not needed: the data tables the build reads come from the CD's own program.

and unpacked into WORK/extracted as

  disc-hfs/Theme Park Data/                      the CD's game folder: each file's data fork as the file itself, its
                                                 resource fork as NAME.rsrc (when it has one) and its type and creator
                                                 as NAME.finfo ("TYPE<tab>CREATOR"); --whole-disc writes the rest of
                                                 the disc beside it (manuals, installers, extras), which nothing reads.
                                                 "Theme Park Data/SimTheme Park" is the program the build reads its
                                                 data tables from.
  Theme_Park_World_Bonus_Stuff/Theme Park World Bonus Stuff/tpwfnt/
                                                 the bonus fonts, when the bonus zip is there
  tpw-eur/Theme Park Data/                       the Theme Park World edition's game folder, when the European PC image
                                                 is there (written by devtools/import_eur.py, which says what it holds)

which is where the build (TPW_GAME_PROGRAM's default, WORK/tpw-port/../extracted/...) and the launcher
(--game "extracted/disc-hfs/Theme Park Data", from WORK) look. Every file written is then checked against the SHA-256
list at the end of this script; the result is OK or the list of the files that differ.

Files whose names a Windows file system cannot hold are not written and not checked: the two with a ':' in the name
(the Mac spelled "/" that way: a shared library named "c/c++ shared") and the folder icons ("Icon" followed by a carriage
return, with their .rsrc and .finfo). The game does not read them.

Needs Python 3.8 or later and nothing else: the HFS reader (machfs, with macresources) is in devtools/third_party and
is imported from there. Nothing is written outside --to; the work folder it unpacks into (--to/.import_originals-*)
is deleted at the end. A folder that is already there is left alone unless --replace is given, which deletes and
rewrites it.

--check DIR checks an existing layout against the list without the downloads (a folder SimTPPatch_204b3 left from
earlier imports is checked too, when it is there).
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys
import tempfile
import zipfile
import importlib.util

_HERE = os.path.dirname(os.path.abspath(__file__))
for _lib in ("macresources", "machfs"):          # vendored, see devtools/third_party/*/NOTE.md
    sys.path.insert(0, os.path.join(_HERE, "third_party", _lib))

_spec = importlib.util.spec_from_file_location("import_eur", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                          "import_eur.py"))
import_eur = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(import_eur)

# What the importer looks for: (key, description, required)
ORIGINALS = [
    ("cd", "the US Mac CD image (a zip holding the disc image)", True),
    ("bonus", "the Theme Park World Bonus Stuff zip (optional; its fonts serve only the platform tests)", False),
    ("eur", "the European PC CD image (optional; for tpw --eur)", False),
]

DISC = "disc-hfs"
GAME_FOLDER = "Theme Park Data"
PATCH = "SimTPPatch_204b3"
PATCH_FOLDER = "Sim Theme Park Patch"      # only a layout from earlier imports has it (--check)
BONUS = "Theme_Park_World_Bonus_Stuff"
BONUS_FOLDER = "Theme Park World Bonus Stuff"
FONTS = BONUS_FOLDER + "/tpwfnt"


def portable(name):
    """Whether NAME (one path component, or a whole relative path) can be a file name on Windows as well: the Mac's
    '/' is written ':' by earlier imports, and the folder icon files are called "Icon" and a carriage return."""
    return ":" not in name and "\r" not in name


def fail(msg):
    sys.exit("import_originals: " + msg)


# ---- finding the originals

def probe_zip(path):
    """"cd" when PATH is a zip whose largest file starts like an Apple-partitioned disc image, "bonus" when it holds the
    bonus fonts folder, else None. Only the zip's directory and the first bytes of that file are read."""
    try:
        with zipfile.ZipFile(path) as z:
            names = [i for i in z.infolist() if not i.is_dir() and not i.filename.startswith("__MACOSX/")]
            if any("/".join(i.filename.split("/")[:-1]) == FONTS and i.filename.upper().endswith(".TTF") for i in names):
                return "bonus"
            if names:
                with z.open(max(names, key=lambda i: i.file_size)) as f:
                    head = f.read(4096)
                if head[:2] == b"ER" and any(head[n:n + 2] == b"PM" for n in (512, struct.unpack_from(">H", head, 2)[0])
                                              if n < len(head) - 1):
                    return "cd"
    except (zipfile.BadZipFile, OSError, NotImplementedError, struct.error, RuntimeError, ValueError, EOFError):
        pass
    return None


def find_originals(root):
    """{key: [paths]} of the originals in ROOT and its subfolders, recognised by their content (see the top of this file),
    each key's paths in the order the folders are walked."""
    found = {}
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(folder, name)
            try:
                size = os.path.getsize(path)
                with open(path, "rb") as f:
                    magic = f.read(4)
            except OSError:
                continue
            if size < (1 << 20) and magic[:2] != b"PK":
                continue
            if magic[:2] == b"PK":
                key = probe_zip(path)
            else:
                key = "eur" if import_eur.looks_like_eur(path) else None
            if key:
                found.setdefault(key, []).append(path)
    for key, what, required in ORIGINALS:
        if key in found:
            print("found %-5s %s%s" % (key, found[key][0], "" if len(found[key]) == 1 else
                                       " (and %d more like it)" % (len(found[key]) - 1)))
        else:
            print("%s %-5s %s" % ("MISSING" if required else "missing", key, what))
    return found


# ---- the CD: the HFS partition of the Toast image

def read_toast(zip_path):
    """The disc image: the largest file of the CD zip (the zip's CRC is checked as it is read)."""
    with zipfile.ZipFile(zip_path) as z:
        member = max((i for i in z.infolist() if not i.is_dir() and not i.filename.startswith("__MACOSX/")),
                     key=lambda i: i.file_size)
        print("reading %s from the CD zip (%d bytes)" % (member.filename, member.file_size))
        with z.open(member) as f:
            return f.read()


def _mdb_fits(image, start, length):
    """Whether an HFS master directory block sits at START + 1024 and its allocation blocks fit in LENGTH bytes."""
    if start + length > len(image) or image[start + 1024:start + 1026] != b"BD":
        return False
    nblocks, blksize = struct.unpack_from(">HL", image, start + 1024 + 18)
    first = struct.unpack_from(">H", image, start + 1024 + 28)[0]
    return blksize > 0 and blksize % 512 == 0 and 512 * first + nblocks * blksize <= length


def find_hfs(image):
    """(byte offset, byte length, description) of the image's HFS volume, from its Apple partition map (the driver
    descriptor "ER" at 0, the map's "PM" entries from block 1), or the whole image when it is a bare HFS volume."""
    if image[:2] == b"ER":
        blk = struct.unpack_from(">H", image, 2)[0]
        for size in dict.fromkeys((blk, 512)):          # entries one device block apart, or 512 bytes on some CDs
            if not size or image[size:size + 2] != b"PM":
                continue
            count = struct.unpack_from(">L", image, size + 4)[0]
            for i in range(1, count + 1):
                e = i * size
                if image[e:e + 2] != b"PM":
                    break
                pstart, plen = struct.unpack_from(">LL", image, e + 8)
                name = image[e + 16:e + 48].split(b"\0")[0].decode("mac_roman")
                ptype = image[e + 48:e + 80].split(b"\0")[0].decode("mac_roman")
                if ptype == "Apple_HFS" and _mdb_fits(image, pstart * size, plen * size):
                    return pstart * size, plen * size, "partition %d \"%s\" (%s)" % (i, name, ptype)
        fail("the CD image has an Apple partition map but no Apple_HFS partition with a valid HFS volume in it")
    if _mdb_fits(image, 0, len(image)):
        return 0, len(image), "the whole image"
    fail("no HFS volume found in the CD image (no partition map and no HFS header at byte 1024)")


def _safe(part):
    if part in (".", "..", ""):
        fail("an HFS name %r cannot be written as a file name" % part)
    return part


def unpack_cd(zip_path, stage, whole):
    import machfs                                       # devtools/third_party/machfs
    image = read_toast(zip_path)
    start, length, where = find_hfs(image)
    vn = start + 1024 + 36                              # drVN, a Pascal string
    volume_name = image[vn + 1:vn + 1 + min(image[vn], 27)].decode("mac_roman")
    print("HFS volume \"%s\": %s, bytes %d to %d of the image" % (volume_name, where, start, start + length))
    vol = machfs.Volume()
    vol.read(memoryview(image)[start:start + length])
    del image
    out = os.path.join(stage, DISC)
    n = skipped = 0
    for path, obj in vol.iter_paths():
        if not isinstance(obj, machfs.File) or not (whole or path[0] == GAME_FOLDER):
            continue
        if not all(portable(p.replace("/", ":")) for p in path):
            skipped += 1
            continue
        dst = os.path.join(out, *[_safe(p) for p in path])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(obj.data)
        if obj.rsrc:
            with open(dst + ".rsrc", "wb") as fh:
                fh.write(obj.rsrc)
        with open(dst + ".finfo", "w", encoding="mac_roman", newline="\n") as fh:
            fh.write(obj.type.decode("mac_roman") + "\t" + obj.creator.decode("mac_roman") + "\n")
        n += 1
    if not n:
        fail("the CD's HFS volume has no \"%s\" folder" % GAME_FOLDER)
    print("CD: %d files written (data forks, with their .rsrc and .finfo); %d not written (names Windows cannot hold)"
          % (n, skipped))


# ---- the bonus fonts

def unpack_bonus(zip_path, stage):
    n = 0
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            parts = info.filename.split("/")
            if info.is_dir() or parts[0] == "__MACOSX" or "/".join(parts[:-1]) != FONTS:
                continue
            if not parts[-1].upper().endswith(".TTF") or parts[-1] in (".", ".."):
                continue
            dst = os.path.join(stage, BONUS, *parts)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(info) as f, open(dst, "wb") as fh:
                shutil.copyfileobj(f, fh)
            n += 1
    if not n:
        fail("the bonus zip has no fonts in \"%s\"" % FONTS)
    print("bonus: %d fonts" % n)


# ---- the checksum list

def expected(groups):
    """{relative path: (size, sha256)} of the list's entries in GROUPS."""
    out = {}
    folder = ""
    for line in CHECKSUMS.split("\n"):
        if not line:
            continue
        if line.startswith("= "):
            folder = line[2:]
            continue
        sha, size, name = line.split(" ", 2)
        path = folder + name
        if group_of(path) in groups and portable(path):
            out[path] = (int(size), sha)
    return out


def group_of(path):
    if path.startswith(DISC + "/" + GAME_FOLDER + "/"):
        return "game"
    if path.startswith(DISC + "/"):
        return "disc"
    if path.startswith(PATCH + "/"):
        return "patch"
    if path.startswith(BONUS + "/"):
        return "bonus"
    return None


def tree_root(group):
    return {"game": DISC + "/" + GAME_FOLDER, "disc": DISC, "patch": PATCH + "/" + PATCH_FOLDER,
            "bonus": BONUS + "/" + FONTS}[group]


def groups_of(whole, bonus, patch=False):
    """(the list's groups to check, the folders written): the disc's game folder or all of the disc, the bonus fonts,
    and the patch when an earlier import's folder is there (an import no longer writes it)."""
    groups = ["game"] + (["disc"] if whole else []) + (["bonus"] if bonus else []) + (["patch"] if patch else [])
    return groups, [tree_root(g) for g in groups if not (whole and g == "game")]


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def files_under(top, rel):
    """The relative paths of the files under TOP/REL."""
    out = []
    for folder, dirs, files in os.walk(os.path.join(top, rel)):
        for name in files:
            out.append(os.path.relpath(os.path.join(folder, name), top).replace(os.sep, "/"))
    return out


def check(top, groups):
    want = expected(groups)
    have = set()
    for g in groups:
        have.update(p for p in files_under(top, tree_root(g)) if group_of(p) in groups and portable(p))
    bad = []
    for path in sorted(want):
        size, sha = want[path]
        full = os.path.join(top, path)
        if path not in have:
            bad.append("missing: " + path)
        elif os.path.getsize(full) != size:
            bad.append("size %d, not %d: %s" % (os.path.getsize(full), size, path))
        elif sha256_of(full) != sha:
            bad.append("contents differ: " + path)
    for path in sorted(have - set(want)):
        bad.append("not in the list: " + path)
    names = ", ".join(groups_of("disc" in groups, "bonus" in groups, "patch" in groups)[1])
    if bad:
        print("CHECK FAILED: %d of %d files (%s):" % (len(bad), len(want), names))
        for b in bad[:60]:
            print("  " + repr(b)[1:-1])
        if len(bad) > 60:
            print("  ... and %d more" % (len(bad) - 60))
        return False
    print("CHECK OK: %d files match the list (%s)" % (len(want), names))
    return True


def check_eur(top):
    bad = import_eur.check(os.path.join(top, import_eur.EDITION, import_eur.GAME_FOLDER))
    if bad:
        print("CHECK FAILED: %d files (%s):" % (len(bad), import_eur.EDITION))
        for b in bad[:60]:
            print("  " + b)
        return False
    print("CHECK OK: %s matches its list" % import_eur.EDITION)
    return True


def make_list(top):
    """Print the CHECKSUMS text of a known-good layout TOP (the folders above, all of the disc)."""
    lines = []
    for group in ("disc", "patch", "bonus"):
        current = None
        for path in sorted(files_under(top, tree_root(group))):
            folder, name = path.rsplit("/", 1)
            if folder != current:
                lines.append("= " + folder + "/")
                current = folder
            lines.append("%s %d %s" % (sha256_of(os.path.join(top, path)), os.path.getsize(os.path.join(top, path)),
                                       name))
    sys.stdout.write("\n".join(l.encode("unicode_escape").decode("ascii") for l in lines) + "\n")


# ---- main

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--from", dest="src", metavar="DOWNLOADS", help="the folder holding the original downloads")
    ap.add_argument("--to", metavar="DIR", help="the extracted/ folder to write (made if it is not there)")
    ap.add_argument("--whole-disc", action="store_true", help="write all of the CD's HFS volume, not only its game folder")
    ap.add_argument("--replace", action="store_true", help="delete and rewrite folders that are already there")
    ap.add_argument("--check", metavar="DIR", help="only check an existing layout against the list")
    ap.add_argument("--find", action="store_true", help="only say which originals --from holds; write nothing")
    ap.add_argument("--make-list", metavar="DIR", help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    if a.make_list:
        make_list(a.make_list)
        return 0
    if a.check:
        groups, _ = groups_of(a.whole_disc, os.path.isdir(os.path.join(a.check, tree_root("bonus"))),
                              os.path.isdir(os.path.join(a.check, tree_root("patch"))))
        ok = check(a.check, groups)
        if os.path.isdir(os.path.join(a.check, import_eur.EDITION)):
            ok = check_eur(a.check) and ok
        return 0 if ok else 1
    if not a.src or not (a.to or a.find):
        ap.error("--from and --to are needed (or --from and --find, or --check DIR)")
    if not os.path.isdir(a.src):
        fail("%s is not a folder" % a.src)
    found = find_originals(a.src)
    missing = [k for k, _, req in ORIGINALS if req and k not in found]
    if a.find:
        return 1 if missing else 0
    if missing:
        fail("cannot go on without: %s (see the MISSING lines above)" % ", ".join(missing))
    groups, targets = groups_of(a.whole_disc, "bonus" in found)
    os.makedirs(a.to, exist_ok=True)
    there = [t for t in targets + ([import_eur.EDITION] if "eur" in found else [])
             if os.path.lexists(os.path.join(a.to, t))]
    if there and not a.replace:
        fail("already there (give --replace to rewrite them): " + ", ".join(os.path.join(a.to, t) for t in there))
    stage = tempfile.mkdtemp(prefix=".import_originals-", dir=a.to)
    try:
        unpack_cd(found["cd"][0], stage, a.whole_disc)
        if "bonus" in found:
            unpack_bonus(found["bonus"][0], stage)
        for t in targets:
            dst = os.path.join(a.to, t)
            if os.path.lexists(dst):
                shutil.rmtree(dst)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.rename(os.path.join(stage, t), dst)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    ok = check(a.to, groups)
    if ok and "eur" in found:
        for iso in found["eur"]:
            try:
                import_eur.build_eur(iso, a.to, replace=a.replace)
                break
            except import_eur.EurImportError as e:
                print("the European image %s: %s" % (iso, e))
        else:
            fail("none of the European images could be imported (see above)")
        ok = check_eur(a.to)
    if ok:
        print("Done. From the folder holding tpw-port and extracted: cmake -S tpw-port -B build, then\n"
              "build/tpw_launcher --game \"extracted/%s/%s\"" % (DISC, GAME_FOLDER))
        if "eur" in found:
            print("Theme Park World: build/tpw_launcher --game \"extracted/%s/%s\"" % (import_eur.EDITION, GAME_FOLDER))
    return 0 if ok else 1


# The files the importer writes, from a layout checked by hand against the game's discs (--make-list). "= FOLDER/" starts
# a folder; each line under it is "SHA-256 SIZE NAME". This is the list of the files, not the files themselves.
CHECKSUMS = """
= disc-hfs/
e6f57437cd2c69e1efa077479655ee45adc4a6e99cba65df14e0d75cf4e56c03 122880 AppleShare PDS
50194894d165ea193ed97da355956661b382a41aa8a57fe5c716e61cd46e4f5d 10 AppleShare PDS.finfo
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 DesktopPrinters DB
727461b34c0cf56493ab5460bfc2fe39a74a0c0a3aeeeade4af6ba6d737b829b 10 DesktopPrinters DB.finfo
b4f307f6a6f76db2f8a1ee0bbac64e1132dc6662f1afe869b7620a8c6ca17b3c 286 DesktopPrinters DB.rsrc
= disc-hfs/Electronic Manuals/
e3c6ce419c748f52777a1f8ed96d8153e6b9c135a30fac65ec06f4a9026bd70f 61900 STP Easy Guide.pdf
e1f18cd05ee91240eec016361c35d83257a507903957a6ef5496c606539f9376 10 STP Easy Guide.pdf.finfo
0bd911e353a9d587d640976b5adbb9261ca88daa47a8f6ab5028eee7c2a09f5f 16749342 STP Manual.pdf
e1f18cd05ee91240eec016361c35d83257a507903957a6ef5496c606539f9376 10 STP Manual.pdf.finfo
= disc-hfs/Extras/Acrobat Reader/
1dcf112050f18dc843dca6d48f08c82dddbafbc7534d92bbaf9604e96312a79f 5626922 Reader Installer
dd1a82cd0edadf195e2bb0ab22410de413092c41c79cfc025142ad596ad4a8ad 10 Reader Installer.finfo
df78e5cc1328adc447ce3cbce8df0d6ac2e542d5ad5f8ef6f5936084a1073719 438033 Reader Installer.rsrc
= disc-hfs/Extras/Game Sprockets/
5c3dcc56092427ab99fd7bba53574f7a378f624df0ad83211311526ef41be996 722581 GameSprockets 1.7.5.smi
3817b4152853a12d1613a7621685490557a8f42f227e9e5d1f4a0e76cd01b01b 10 GameSprockets 1.7.5.smi.finfo
e4006667ea98e07294b43858c949d1240d0f86e61c5e2ca5fd5071320e7902ea 175598 GameSprockets 1.7.5.smi.rsrc
= disc-hfs/Extras/OpenGL Installer/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Install OpenGL
7dda06099c68c8a72ffa8b9b56441c21224f1b69c35e6fefedff2f95fe02a2b0 10 Install OpenGL.finfo
81fb9abb2a97ed428fe46e6e57bfb143cd963699a28913a72a23ef0fea4115da 291988 Install OpenGL.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Installer
7881b5dde0f9ae9e687dfc5b21d8aef543584c1e339c70f0ab840a03d0773e47 10 Installer.finfo
f2962fdf1aca102abda662ba09d4b38aa6d8a84baba6a9207548d71f04904f65 197072 Installer.rsrc
= disc-hfs/Extras/OpenGL Installer/OpenGL Parts/
911801aba3f13540ddbb5be88d5c47befa82a393f7bd706c4b4f105012d504d3 3980163 Tome
a46e27a684b6cb9bdfbfc21deffabcd579749b086e4b96934612210f77e071b8 10 Tome.finfo
ef93353adbc0471562750dc23886e72a46064c00b947dc4c1f380cc0753fa265 2909 Tome.rsrc
= disc-hfs/Extras/OpenGL Installer/
934e23f7ac96dc48c43b7622e0ff98cdb45f75bb71f6f0e17dc1509c5d0614e0 3709 OpenGL Read Me
a6464cc4b36f83c772c835ecdc0b97aa4a3eca08d76734c7a8fa11efbd9412d0 10 OpenGL Read Me.finfo
aeb45548b485cf46d6bdd3e7aca7344a52177bd0aba415deaafdb4dced26e4c8 1156 OpenGL Read Me.rsrc
= disc-hfs/Extras/QuickTime Installer/
aed8283621435880d7a6185ab25a577adea43ed0edd04ef17e552cc1143de3bc 7623842 QuickTime Install Cache
769a7dc71378be7bb84130095b3072ea8c93f98111ed11c3e913768c00915281 10 QuickTime Install Cache.finfo
003f99518ff3ed5a9944990e90ed39ca309ba27c87f0f9b26637ef327ba6858a 10313 QuickTime Install Cache.rsrc
8afb6477f6c357a30f7cb9cb7fc02e62a4f7c1b5c3fc32120faf39a678fe0c04 32759 QuickTime Installer
dd1a82cd0edadf195e2bb0ab22410de413092c41c79cfc025142ad596ad4a8ad 10 QuickTime Installer.finfo
12a3727fd118d5c072f4ea9f18a76dcb029f7399af555980b2d9547baac258bd 355748 QuickTime Installer.rsrc
= disc-hfs/Extras/
96759d28bc4c83608fb83bd0282b9171e6d25bbc1cf416d2c17f098bc79292dd 23628766 STP Trailer.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 STP Trailer.mov.finfo
= disc-hfs/
c611c37ac33c33145c84920e6052e321e1dc24c473332a186e840392aaa83a67 495006 Install STP
dd1a82cd0edadf195e2bb0ab22410de413092c41c79cfc025142ad596ad4a8ad 10 Install STP.finfo
fb9a70bfd998029f582a6aae3ab1e1d33e807a7accc17bee26f654156dc455a9 214525 Install STP.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 OpenFolderListDF\r
aedd0d9d534021953a525c018c1cefbd0f7b3a307d17af01d87bbc8c4e0d05a1 10 OpenFolderListDF\r.finfo
7458777c75db561f70bbcad12616eeff916bf5201d505b436826177d9a6f1993 422 OpenFolderListDF\r.rsrc
0204b9ada39dafe80c397466b94a3e7161a4ae94642ad2225cd928bf3d13a4f8 9973 ReadMe
a6464cc4b36f83c772c835ecdc0b97aa4a3eca08d76734c7a8fa11efbd9412d0 10 ReadMe.finfo
46b09b7447908eb73b12b2e3896a4b8889fe8f6967151251300066a4cb4da499 332 ReadMe.rsrc
= disc-hfs/Theme Park Data/
0204b9ada39dafe80c397466b94a3e7161a4ae94642ad2225cd928bf3d13a4f8 9973 ReadMe
a6464cc4b36f83c772c835ecdc0b97aa4a3eca08d76734c7a8fa11efbd9412d0 10 ReadMe.finfo
46b09b7447908eb73b12b2e3896a4b8889fe8f6967151251300066a4cb4da499 332 ReadMe.rsrc
04809cd4ccee5433c7fb0b7c93d32f6a7aa629c1849181c0b7906415e5e295f5 2274758 SimTheme Park
ca6a56cc97fc8a9411b95871766626df75cc46c1f97d17b8f68af2022628a9ae 10 SimTheme Park.finfo
000601114927848b15b901bea71ba557b5a86408960191c00ee1f554a3269e89 130609 SimTheme Park.rsrc
= disc-hfs/Theme Park Data/data/2dmap/
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06 16428 esprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 esprite.tga.finfo
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06 16428 gsprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 gsprite.tga.finfo
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06 16428 lsprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 lsprite.tga.finfo
6b8edaa068d9b20f07151969f80d94668e224a9e4c79285fc442ddb74ba0d3d2 78 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
baa01e03d376300b2910bbccbe92ffb8fac8e2ace8d43b8d1cca4186c3cf9a5c 1068 rsprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 rsprite.tga.finfo
f4772f675811dd8f7dc09b9fa53b7683faa266e0c75362584208da244175f4b7 1068 ssprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 ssprite.tga.finfo
f4772f675811dd8f7dc09b9fa53b7683faa266e0c75362584208da244175f4b7 1068 vsprite.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 vsprite.tga.finfo
= disc-hfs/Theme Park Data/data/Advisor/
e905df3d0a12ea5d7580ce87a7dd3a6c7e690e2f072b9eafef8cbfdc88f00df3 19863 Advisor.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Advisor.sam.finfo
= disc-hfs/Theme Park Data/data/
120fff68adc08120134787cfa24e5e6851dc33388c145dd11ee8a40bc4f4edae 12603 Challenges.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Challenges.sam.finfo
= disc-hfs/Theme Park Data/data/Language/American/
eb1d49adbee61bb204de9174c9fc3ba5f6974c7d0870677b67aaf5f77c8708af 87736 CASHBIG.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 CASHBIG.bf4.finfo
d843c2a8dfeae5a049426dda2e5bf42ddb2d8d84e2e131949b8050e830b2ff0f 59744 CASHMED.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 CASHMED.bf4.finfo
b2765028a70cd044458ddf944ba9c1dd0be1a746f9507a4f0b792e1607426dda 43528 CASHSMALL.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 CASHSMALL.bf4.finfo
2867cc82aa650ec27e81ac8892cee642fa59d83d47cbd7e268089de10006ccbf 5812 CHAT_COMMANDS.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 CHAT_COMMANDS.str.finfo
989a8d25916398cd0274b8f2c8b1a4cb01df9112faa7cfc5dad7288e77428a68 8456 CONSOLE6.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 CONSOLE6.bf4.finfo
989a8d25916398cd0274b8f2c8b1a4cb01df9112faa7cfc5dad7288e77428a68 8456 COURIER8.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 COURIER8.bf4.finfo
6d75717c03ba981198d8c3a8ec91c7350e2402e65edaa38318af70dc84235e3b 16480 DATEBIG.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 DATEBIG.bf4.finfo
7c7768519496935294e37d26ddba9a381f9f833477d440f34d0d9b0e534e9051 14456 DATEMED.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 DATEMED.bf4.finfo
971dbdb3d2e731c8845872237e30aa38506c3b3b0c7211172888a03ea4c11ff6 11928 DATESMALL.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 DATESMALL.bf4.finfo
75bf0ade82fc44307070d6d0089393f4334f108a64b5e25208961c32de85a934 10488 DATETINY.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 DATETINY.bf4.finfo
1c268f5d7e416e4a11b66a4ef188cda97b8cd846138803d90ad28437a67d4cc0 904 ENTERTAINER_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ENTERTAINER_NAMES.str.finfo
8248558cb04111d384d4243bde07329128f67c265f06236d49af9a01ad3ba289 5352 ERRORMSG.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ERRORMSG.str.finfo
a6e7388d3fb2b884fd43e1bdd825a8724372f7c5ff988eb86379b29877aa4866 584 FEMALE_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 FEMALE_NAMES.str.finfo
9fd64d7d430d925da1f4d1a725537d08ce6318cdc6e2f4f0fb0cb9c06778c1ab 9056 GAME10.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME10.bf4.finfo
7c7768519496935294e37d26ddba9a381f9f833477d440f34d0d9b0e534e9051 14456 GAME10AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME10AA.bf4.finfo
bbdb5d50d6e430289ee5e7bd03804babdd3b60d8e11934e57934af508e2c1b1a 9904 GAME12.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME12.bf4.finfo
6d75717c03ba981198d8c3a8ec91c7350e2402e65edaa38318af70dc84235e3b 16480 GAME12AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME12AA.bf4.finfo
6a6efe54bc4cb81e6330f8f0b0a767ceb486e9eb6ff2fd91df47cbfca5931555 9672 GAME5AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME5AA.bf4.finfo
c307a110aa7643fcb3c577c53edb3b6f29a0aaaeec82eefbd2ab25a410b69fb6 8448 GAME6.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME6.bf4.finfo
75bf0ade82fc44307070d6d0089393f4334f108a64b5e25208961c32de85a934 10488 GAME6AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME6AA.bf4.finfo
256d468174713a802add99f08c91678d7e53fcc7e72e1523a26fd5913e2e5992 8472 GAME7.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME7.bf4.finfo
ed8194f2e7cd52b68739e8761663d9e68c9abfc4acb26c1f3021c9df9408c263 11040 GAME7AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME7AA.bf4.finfo
2fa1b3e497eaf3cf3ff9262542bd369a612448b0ffcd5260da83476357a62783 8560 GAME8.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME8.bf4.finfo
971dbdb3d2e731c8845872237e30aa38506c3b3b0c7211172888a03ea4c11ff6 11928 GAME8AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME8AA.bf4.finfo
283b86a1e31fb5609ba0fd39f2aa80f76ffc3cf332fe01ea1c55c3cd5b76d6b2 8848 GAME9.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME9.bf4.finfo
ddaa1e6089917cd601f9bcf3f9aee25d313826b51d7e3574aa1e5c3f6e297d9a 13264 GAME9AA.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GAME9AA.bf4.finfo
40fc5a618028115c1296519e6e35c9be4fd045d7ca008ef2a95b89fecf50baaa 908 GUARD_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 GUARD_NAMES.str.finfo
f71ebfe701d40c64162bf8520e2a64e23659037f294272e97fc21828b7d67ff8 904 HANDYMAN_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 HANDYMAN_NAMES.str.finfo
8bd5a2ffd0fd080da0ed4eb16962a70e21aeb5e2eb27767c972590ff11fc71ad 124 INGREDIENT.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 INGREDIENT.str.finfo
c003a132319f48ef2b3720e3c13a0d111b5d04c2cede8bb6a4f9a240e93959e0 676 ITEMTYPES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ITEMTYPES.str.finfo
091995f25e2901b77681ca363234b2a3bc1eac261800b54a681637b6c0d2d9d3 348 KEYBOARD.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 KEYBOARD.str.finfo
26e8f47921907b839e1d864802309287a6f1a30c8f60a433f0ede6368306c50f 684 KIDSTATES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 KIDSTATES.str.finfo
45d7c5de11afa6ea2a0fc827628138c0cf7cea89cc157f01fb2ab275dc762f59 496 LOANNAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LOANNAMES.str.finfo
f682ed03507d6ea20f5f1689c321f32b52f8a21e838e9440fbc3207e7236b67d 504 MBToUni.dat
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MBToUni.dat.finfo
c97056aa59868ae06e9ec5f3cd9634798a52af8abde00443a4d2d0cf1a2cd37a 912 MECHANIC_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MECHANIC_NAMES.str.finfo
bbed1c06bff5a4ee5e4bd28f64dd60172aa26d3c8a80d78f0684af0a4b55fef8 34240 MENUBIG.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MENUBIG.bf4.finfo
2c2dd3004cd1b186c10add65f6a62315b63c8d0b5add59bd6fa620809a35335d 30416 MENUMED.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MENUMED.bf4.finfo
7d398907667de0c8abcdb06b99127fa4120bea08672edb672e83bd6edcf7ab3f 24424 MENUSMALL.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MENUSMALL.bf4.finfo
5b2b81c97d3b39d53ddcfbc7896620a0e56a2e9afd2a6e332dc93c33a4abd307 8352 OBJECT_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 OBJECT_NAMES.str.finfo
0958ac356fff2191fb9cfb65a1de3155bd02e733d016dd4953e434744cda07b7 19376 POSTCARD.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 POSTCARD.bf4.finfo
a11869442a2ddf1f5944ef1ac49b56bc7361bf3afdd0f6ea22186460d209564e 908 RESEARCHER_NAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 RESEARCHER_NAMES.str.finfo
dcd660e87fc5841755f4e2febccd9a80d17f11bf561a9b33089962c2ee5d3f6d 26360 SESHBIG.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SESHBIG.bf4.finfo
6a458535fc37283fa2e6538868b0f7e806560ca08fc7678e9faffe634cf01794 20216 SESHMED.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SESHMED.bf4.finfo
e5f991818f2bd1c7a3d0418f407489235722605d31dd7b93b7548a041e7bbf25 15560 SESHSMALL.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SESHSMALL.bf4.finfo
691573f2369eab936dc616b5f93c5ab32fc3ed17ca1f26fe8660adb15170b6d1 140 STAFFSTATES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 STAFFSTATES.str.finfo
5079ca9ad1443e8c3ae321e1c113c1ac05fdb15ca32f2d56713673b818fac039 232 STAFF_TYPES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 STAFF_TYPES.str.finfo
bcffebab7e1627219953982d8f081c41ae3e7b65dd91f2427bcd4a0b38b3ac16 35628 TAG_SYSTEM.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 TAG_SYSTEM.str.finfo
77c9fce57d5654c26d815f0d33be625d56eaf4e62c42b8f65c6592abd9945462 112 THEMENAMES.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 THEMENAMES.str.finfo
c218be46a1a8f2a260ebf545f34ab86f2bd2f5775159485af9ea1eab1e905fab 416 THOUGHTS.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 THOUGHTS.str.finfo
bd9bd19d37195d3c7f8de476cf6ff7403065f2ab18dc036e2285746258f94c60 18184 TITLEBIG.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 TITLEBIG.bf4.finfo
b4a5902fd4e008b87fd9cf9748f42e05531d8997a37dab2863dddbe75bcbc37b 16272 TITLEMED.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 TITLEMED.bf4.finfo
7c7768519496935294e37d26ddba9a381f9f833477d440f34d0d9b0e534e9051 14456 TITLESMALL.bf4
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 TITLESMALL.bf4.finfo
adc16efc04bdb8c605e916a23b7794342d507d0a90cc3f8f635dadf20cf59b75 30604 UIHELPTEXT.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 UIHELPTEXT.str.finfo
c3768d1f448c0f85952ae3edda41da750081e85996eb76750df53fad7688bdef 17016 UITEXT.str
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 UITEXT.str.finfo
54e45b589f445e30b87dcfd28682a78720c02607af7c8430927a29577e64c372 516 UniToMB.dat
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 UniToMB.dat.finfo
d672c8c9cc02ebda7a6cdc807683438b41b8ed521536aa1a2b1db4d8d4b9d135 1044 alloweds.txt
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 alloweds.txt.finfo
5be0255d211accd2fba0b7432e9273d72fa4856d3fba4b6337e870e091a9a72a 26072 bankrupt.MD2
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 bankrupt.MD2.finfo
453955609fcb4a9911fc0e1b6bb4e6978c41ebd1a4457a1d9f66a54d28dc529c 41992 congrats.MD2
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 congrats.MD2.finfo
7c43ed882485dc85758285a2f28c97a5d2e6c2d31b745c047990990a3636c5ed 18288 paused.MD2
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 paused.MD2.finfo
02b937519e897a9fc7f36fec29197bc1abe40350979800306062c56f039174f0 1442 residx.dat
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 residx.dat.finfo
91f36b6228190ad0014b3205e609edb8b62ad51a827f1ae9f1e408769ceb3bfb 2566 swears.txt
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 swears.txt.finfo
= disc-hfs/Theme Park Data/data/Movies/
3381369c59ccb1a16007f74b3a9b8227da21f8325f0879018e6c860f3c3698ae 1202700 BFj2.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 BFj2.mov.finfo
3229854fe27d0412cd5ba64eb70b6b83d5e22db8ba5310cae6f33fe8a2d24fb3 1952476 bf.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 bf.mov.finfo
7b6b97d5c750766bd5d5dfb77d48bb0fa6c3a1db78ccb6622350c8c78bb518a9 3494 bf.mov.rsrc
89142ee0873d2752a543d6f5a364fdc9e58e6b94688a00af75edcce6ea3ba92d 1890868 bfj.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 bfj.mov.finfo
e872fdd5d334dd364d635c180f9109cf554395ff08ae3585f72028f0576fed34 3494 bfj.mov.rsrc
b271c871a46ebc6a1debe01d9c7592b836f9b8ae1c452b7c9eb5e816f8268285 8695617 bub.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 bub.mov.finfo
ab70c921f5ce07914047f1745a7b9543a14497c289b46cf7c650346a71f39198 8740263 buc.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 buc.mov.finfo
e2ec6320ee7fe8aa8025dcec23bd06ead6c4b94c05b1e82af77d339bf28b0244 9607333 grav.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 grav.mov.finfo
e2c8339b4e4669555a55f9be2253924279c29d3de5768e0e06657cc3b9908363 9780091 jug.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 jug.mov.finfo
1e9e75253932a55ed6cf51b20d8da4850d0e52f3b3cd744e1dbe776eee8d06fc 9568897 mir.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 mir.mov.finfo
9a4efbe0d2922e07db242fd22217b1da5ab01dae821fa963be721663449668e3 7966605 plan.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 plan.mov.finfo
d7d293775d6ebe26c75fd05e57a14926f55c3fe29867cadec5b668eeeaecca91 6950151 roc.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 roc.mov.finfo
95612a5bc2e86ae7d188275e054d027f2299d4b719d887a8f74928ee8af51f77 8373263 roll.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061 10 roll.mov.finfo
= disc-hfs/Theme Park Data/data/Particle/
6564736ad4d12d27634be0af1f23ef6f3ce4259b9d5b2f577e57fd8a68113210 35704 Tp2.plb
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Tp2.plb.finfo
d9fed850f3a778af15a3d988245aab097a79b806f61a2c858a519336f3fbb96d 3373 par_lib.h
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 par_lib.h.finfo
= disc-hfs/Theme Park Data/data/
30e98c3c013f825a22f2198f79a002f60b4eb454f54ee47c1786de158e27763c 424 _Resolution.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _Resolution.sam.finfo
= disc-hfs/Theme Park Data/data/debug/
75f73fdc44d5755735c17d97636bd372f078bd1f933de98d080bba9b9d005e21 197147 font.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 font.tga.finfo
b82e3ee0852d8847b981fc09ac7479c127dbc9552cb5b1a8c198d7b0ff1e4b2a 49691 xfont.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 xfont.tga.finfo
= disc-hfs/Theme Park Data/data/
00e02624405005b14ebf1bb1c13d09d6fa5b7c3e6f3c76e28dc3e532728f5ade 10448287 esprites.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 esprites.wad.finfo
= disc-hfs/Theme Park Data/data/fonts/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 BIGLA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 BIGLA___.TTF.finfo
3d9d27868b9f1f9b7872d47df653970fc2ed3b201c43eaa1b2d1a0dcdf199134 54340 BIGLA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 BIGLOA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 BIGLOA__.TTF.finfo
2dce96e5b5ea9ea562ea398a6bfe92c105edaed400515123be0db32c2379c42f 87106 BIGLOA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 CLUNAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 CLUNAB__.TTF.finfo
3e032164858709fd99f77ae17b3ae8db2fece6ef3a05626868f7290dc63da331 50318 CLUNAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 CLUNA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 CLUNA___.TTF.finfo
37a1d4bdda3df48b556409ed8a84d72a0e017f930023489ff7ea0b2bd5be55af 66831 CLUNA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 EGGITAOE.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 EGGITAOE.TTF.finfo
b34eb7b82b87982ae8e2577ebdb49ba816890b3e148c6a15abd2634bc3039db0 55909 EGGITAOE.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 GARGA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 GARGA___.TTF.finfo
f9d5d8be1dbfd966decffd7a4bfdabe0a17266d3881cfcb9f70b869554f5eafd 50822 GARGA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 GARGSAI_.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 GARGSAI_.TTF.finfo
f4732b39c8552a414acb152e116c3a520f05ffac1826f77a09dd9d4d42ce038a 51928 GARGSAI_.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 GARGSA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 GARGSA__.TTF.finfo
aefb479b33862ef8a392716c647730938e1d431b3c10bfd3047520258ff6a033 50167 GARGSA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 HAUNTAOE.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 HAUNTAOE.TTF.finfo
8a9f18df9f8d347654f5d5e45ec320132ab1e4b90e91af5a042bc22e784adf8b 119413 HAUNTAOE.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 INTRAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 INTRAB__.TTF.finfo
8eb152a86d2a76fca3475675d57cd667e5d4289c88636cbfbdd3690c2ba7ba1e 61865 INTRAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 INTRA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 INTRA___.TTF.finfo
169b27aed6f8fa8ad2e2d12ef0c62527422b829bc9eef3264681d006bfd3ed1c 123770 INTRA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 KRELAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 KRELAB__.TTF.finfo
bb6973a44fca200701d480a4466fdddd6c70232a8680de59ab91d5ea42c5f57d 67903 KRELAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 KRELA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 KRELA___.TTF.finfo
fb0699323cbdd5a51ae52f07f6c3ac40f7d827477fc2a721955e51626ced805e 68676 KRELA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 LINUPA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 LINUPA__.TTF.finfo
52743da6cc55018ab581a42f3e858106c0e00869b06517fcc48a102faa053e52 69917 LINUPA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 TANNAO__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 TANNAO__.TTF.finfo
fbe99800ebf5cec92e3762c7b26a911f871f0c143784e3370986ad41a5aef949 67018 TANNAO__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 TANNA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 TANNA___.TTF.finfo
0d1b777935807b4854d954d7dc8bf152f5ee0a1d3c8a866d74335976fb14105b 65674 TANNA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 YOUNIA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d 10 YOUNIA__.TTF.finfo
40406125ba87ada85b3620fbe2753ba523461d1bc2093187cfec447175b9c110 116319 YOUNIA__.TTF.rsrc
= disc-hfs/Theme Park Data/data/generic/defaulttexture/
a4060947603f4d31e7def1bed746963f5f54834afde05d5f5f9b2d401f42a082 12332 NotFound.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 NotFound.tga.finfo
c7d38296c36af26b90c73e47fdc728ffbb4ccba4809faa83aa47beb4e4d04f10 14 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
= disc-hfs/Theme Park Data/data/generic/dynamic/
d5154361929da3a835fee4a47a68ef9c717e18365e670902d423d4e8f480839f 1669 garrow.MD2
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 garrow.MD2.finfo
dd58973cbfec088f8c3343dbb192d57ecb83cb6f69556e61b49560b3f494d384 24 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
3e021b02eb830bcbd183286eff56775d4548ede57bf4713a7ac172395995825f 1669 rarrow.MD2
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 rarrow.MD2.finfo
= disc-hfs/Theme Park Data/data/generic/dynamic/stexture/
2088f54ef8143dcad2221da05fd88e22657d491cd3920c912fd354c18edb7ab3 211 blue.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 blue.wct.finfo
116a49c771a09c8ef537e49c6895f0d6da58c3fe980e9335f9a79c4fe581122d 720 gby_sur1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur1.wct.finfo
4388c1e6bbf5b460e1f72f9f82fae1846666ea94046f7e075ed24918524f179c 844 gby_sur2.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur2.wct.finfo
9c2c1bb6c3dace834e8b8a52c85400c78149cdfd6a17bb9cc5dfd8a2b9839f27 848 gby_sur3.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur3.wct.finfo
70ec227bfa34cf3bf05e53ff81527de99f2c189292ac1f0ef340720d7b864803 50 green.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 green.wct.finfo
2e55bdf5aec3c6430badc72044fb183457ebd8b70285a0cedac9669bf8128147 6252 gte_lgo1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gte_lgo1.wct.finfo
a1359aebacd6f0bc145d9a6ec8dadeb62ebfeeeaf47f24ef43de5238f1a494ff 1007 jri_lak1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 jri_lak1.wct.finfo
330c92d8e897778796440b9773785c64ee209488254e40d7afe8daecb0d3e018 2311 m_break.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_break.wct.finfo
3896b36626a845c70702742c7f137a7262d3f2e1bf6e3133aab38588c329ef8a 154 m_cross.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_cross.wct.finfo
ecb5ac2592d55842d8aa3998bc13817739129b6a6f7a82b0906cf33eb5315690 1075 m_direct.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_direct.wct.finfo
101c73c53f2696ad259ad49516d0c0755ed422cbf4aa4a00cd8d48c99717da04 1939 m_end.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_end.wct.finfo
eba23d87f3cef6365a02a6e372bb5d050ba096057d4a70f908671cecf7eb9efc 1094 m_enter.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_enter.wct.finfo
60b2b85c3e0166067788c4d43aed2bf1a1085787975e1ebe3f9a114c8865cb84 2013 m_erase.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_erase.wct.finfo
2a3254fd4af5b1049429885ea71c3a59906f9f55537ab909582eab4a4da458b3 1275 m_exit.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_exit.wct.finfo
85ce2af1ac81a3e7a0d70ae850764d03eabc7c30f1b2dde9dfdf45fe539ee9a2 570 m_front.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_front.wct.finfo
9ec5139700417354307f7e138159043260856bb96c76e6e448ee17da1ac81b7d 1902 m_inout.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_inout.wct.finfo
21d97a3cf3fb81a61a3000bdd47005cc734c0b40df5a17cfdbbb4228207612f0 2210 m_link.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_link.wct.finfo
1510eb461d8d6962316609f00700ae14a5ec5f2ac3da446a62a1ee30001b96c7 1908 m_nocash.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_nocash.wct.finfo
4981f6de21f46f0774c25a715a5f8433a09338400e79acecf2b92c49b763bfc8 2139 m_nopath.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_nopath.wct.finfo
12de99789487441fc2363ea978f657685f1227f7a9507d6399c12a85a4c9d2da 255 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
b6867c06600939bc10bedf041d74959cb53429b070e7581948471606ee383e54 122 red.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 red.wct.finfo
= disc-hfs/Theme Park Data/data/generic/dynamic/textures/
881f18334e8056f882dfe2174d0885a2e0f403fb5f54d4b5dec5130dc4ae152c 493 blue.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 blue.wct.finfo
5a84687b61681019f7cb3cd0805566bacd7dc959f98412b591589782a72dd453 4849 gby_sur1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur1.wct.finfo
d975240e4a2204892bc9c04f2144ed7e6645dea7c287a57c545a565f1250fc77 5372 gby_sur2.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur2.wct.finfo
5ed24cc085bd9682bf9268147bc3ac257c4d2ede1a5bc8f01e3a965eaabdef93 5804 gby_sur3.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gby_sur3.wct.finfo
70ec227bfa34cf3bf05e53ff81527de99f2c189292ac1f0ef340720d7b864803 50 green.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 green.wct.finfo
2e55bdf5aec3c6430badc72044fb183457ebd8b70285a0cedac9669bf8128147 6252 gte_lgo1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gte_lgo1.wct.finfo
fbb9c15a88d7e6116495810831030d5b5608423e4c156f94abf1a54efa7b5003 6202 jri_lak1.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 jri_lak1.wct.finfo
330c92d8e897778796440b9773785c64ee209488254e40d7afe8daecb0d3e018 2311 m_break.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_break.wct.finfo
3896b36626a845c70702742c7f137a7262d3f2e1bf6e3133aab38588c329ef8a 154 m_cross.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_cross.wct.finfo
ecb5ac2592d55842d8aa3998bc13817739129b6a6f7a82b0906cf33eb5315690 1075 m_direct.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_direct.wct.finfo
101c73c53f2696ad259ad49516d0c0755ed422cbf4aa4a00cd8d48c99717da04 1939 m_end.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_end.wct.finfo
eba23d87f3cef6365a02a6e372bb5d050ba096057d4a70f908671cecf7eb9efc 1094 m_enter.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_enter.wct.finfo
60b2b85c3e0166067788c4d43aed2bf1a1085787975e1ebe3f9a114c8865cb84 2013 m_erase.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_erase.wct.finfo
2a3254fd4af5b1049429885ea71c3a59906f9f55537ab909582eab4a4da458b3 1275 m_exit.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_exit.wct.finfo
85ce2af1ac81a3e7a0d70ae850764d03eabc7c30f1b2dde9dfdf45fe539ee9a2 570 m_front.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_front.wct.finfo
9ec5139700417354307f7e138159043260856bb96c76e6e448ee17da1ac81b7d 1902 m_inout.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_inout.wct.finfo
21d97a3cf3fb81a61a3000bdd47005cc734c0b40df5a17cfdbbb4228207612f0 2210 m_link.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_link.wct.finfo
1510eb461d8d6962316609f00700ae14a5ec5f2ac3da446a62a1ee30001b96c7 1908 m_nocash.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_nocash.wct.finfo
4981f6de21f46f0774c25a715a5f8433a09338400e79acecf2b92c49b763bfc8 2139 m_nopath.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 m_nopath.wct.finfo
12de99789487441fc2363ea978f657685f1227f7a9507d6399c12a85a4c9d2da 255 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
1a1f9b386ac7ee96840b36c9f88f4075746b7b2da9b886f02676dc12c87b19c1 340 red.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 red.wct.finfo
= disc-hfs/Theme Park Data/data/generic/shadow/
a577b0136de97c4b7a3cb2cf663904598296385af99cf7298dbabe2d9e12c923 790 alphkid.wct
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 alphkid.wct.finfo
= disc-hfs/Theme Park Data/data/generic/sweather/
e3212d1f1c33799c92be48c98d769d66c199d271d209d883c684ae819d2440b2 812 Raindrop.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Raindrop.tga.finfo
81ba618fad5396e6183a1e83ee9cd098c5c2438c756b66f27661b8023d3c43a3 4140 lightning.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 lightning.tga.finfo
a515e50fb815caaf13deae1e8c5f8a22249813efb48d7f78821492c8571f427b 39 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
05f7c67481b9d1200b03be7c812202ba2e680a8e0215500b5230be448536c976 16428 snow.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 snow.tga.finfo
= disc-hfs/Theme Park Data/data/generic/weather/
71e55c003ed99c2fcb81b58633222feb45f4f9dcf8eb93d0d1585fa8c0fbf1f2 3116 Raindrop.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Raindrop.tga.finfo
b039e417a281771d2643aacb2c0588e5fb09173f1ec5d3d2f3704a8cb96f797f 65580 lightning.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 lightning.tga.finfo
a515e50fb815caaf13deae1e8c5f8a22249813efb48d7f78821492c8571f427b 39 qickload.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 qickload.txt.finfo
8ee9710f0cfc0c08692c4b70bdc21c2e0fdb2acc449ef4311bac032de9c47de1 65580 snow.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 snow.tga.finfo
= disc-hfs/Theme Park Data/data/global/Speech/
bb70bc8479a9563cf3a8fca8fa0d506887fb913585771287e27d86fd7028d478 57 cat_speechBANK.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechBANK.map.finfo
8ace9703326862e49b58ace6bc1862b355f8b19662f0684d36043571c7be92a0 50050 cat_speechSFX.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechSFX.map.finfo
14abb33f93b623fe7e3d6d0aff9bbfa23d66991246f5efca68b6c8d453bfa216 49031 lips.wad
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 lips.wad.finfo
d92289144867fba62b955f6d6d4ca40c024d525bebd0f48d7dfd889ece730f5e 25773480 speechHD.SDT
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 speechHD.SDT.finfo
= disc-hfs/Theme Park Data/data/global/
d3fe56921b1b3998d6019c3c870e8d936948fb642f0272d35a749c5a6d1f033e 180685 advisor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 advisor.wad.finfo
= disc-hfs/Theme Park Data/data/global/sound/
e7c0286cf926f0d1f0072b1bf5b9eccd00d4473a9a020bbdf0f365b08ead5733 1261518 AmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 AmbientHD.sdt.finfo
c95c11ed15ad10fc82a1b9db259edb3fcbe4fe8e4cc938beff20b0e0f1af838f 3579087 KidsHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 KidsHD.sdt.finfo
36c035524e8c5b5c45ee7f4740ee4f4e0aaee38531ade4126e17078cc054b354 507706 MusicHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 MusicHD.sdt.finfo
0ee2116967c5e9dbf1298dcfe285fbec3a2116107a77ccf002eeac65d28b77a0 476746 RideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 RideHD.sdt.finfo
2e375003eef4d2025dbcfe5101a47654aa8ea05edca31d250b5554145b96e465 147285 SfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SfxHD.sdt.finfo
b2023110d052290010cb60ff09eb44294ca5481a6ea5ee3a1af94601b4ab5eb4 688020 StaffHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 StaffHD.sdt.finfo
565fa2aa98935268b03db3dbc3f903d2f65d48005241691b0172414f60372cb7 666164 UIHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 UIHD.sdt.finfo
55736321a428b98a72502174037b5e18a38943a7e8e36233f5bb7d85a1a08c2c 57 cat_ambientBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientBANK.map.finfo
713793b8f3c86bf96c5affa9ace435bac49016f2fbc9a604810bffb50e9e20b3 3960 cat_ambientSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientSFX.map.finfo
af380d6c90ec075434056eee39fa46c922aeee571091d278f74b48f5a50c88f0 106 cat_globallobbysfxBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_globallobbysfxBANK.map.finfo
34440e8b6a688869fc6678512e8f065ec9f5cfd7beadff97044ad6f1ffad673f 698 cat_globallobbysfxSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_globallobbysfxSFX.map.finfo
4cb16ac91cc753d0edbc87de4088e583f23a72a305ab8e97bb37547dcb81a75b 105 cat_kidsBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_kidsBANK.map.finfo
13bdb903dc3a780cac0d09709036c52888b14a35f68cc9f5257c0625312cba5e 14712 cat_kidsSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_kidsSFX.map.finfo
1a802bbb8cbe3e3f51b62602b30c921ae4865b97b466c5a99659912749f05002 160 cat_ridesBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesBANK.map.finfo
094242d35a8b0e31feee7415a5bb0d083d16289639607b96fef78e891d807182 3358 cat_ridesSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesSFX.map.finfo
359422b404bb1c457b3b25e63fdaa1407b47d100522f8aafc6f666764527dfe7 55 cat_staffBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_staffBANK.map.finfo
096710285d64c0a67f07180c1471ea550cbb6e0a331f710d00eca71a497d6eb8 4188 cat_staffSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_staffSFX.map.finfo
73cb613fe511c47697f4610d393bbf1fc9f4fa83350c7d1d073481586d3c3cb6 129 cat_uiBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_uiBANK.map.finfo
0d1a6c2c0a7733eded894fc172fa372bbee6bed18e966c48384c0c020e89cf9f 5928 cat_uiSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_uiSFX.map.finfo
ad3d4efd5080592948999e4df816cce9d3796e78527db5b146f6ac8a9fda6132 122932 sSfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sSfxHD.sdt.finfo
bd2f8aae3b808e42414a22635c88be049f7a6a9da0133c58f42e45dae92b3ddd 472598 sUiHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sUiHD.sdt.finfo
6bf3986e4be32f30fff97d3efa31392c223aa9ae5cc85599afaae82521666945 514982 sfUIHD.SF2
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sfUIHD.SF2.finfo
deb420063cbe73f2f2e6927837d5f97728e13160d3f34195208722c73a3b4acf 99132 sfUiHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sfUiHD.sdt.finfo
4c7e7cddf622680fdf71fd7166f44b44e8cb30b2d50135a5ecdc47a36bd8282e 551196 xKidsHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xKidsHD.sdt.finfo
= disc-hfs/Theme Park Data/data/
707eeef1168e77e9cf10b168ef8959419dba8a72bb2581a98be3d7ef08ad23c6 2123 high.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 high.sam.finfo
= disc-hfs/Theme Park Data/data/init/1024/
628b0e062b9ada3f50e764ac497c8adaa729639a52e6f9f0e2e460236a060611 2359340 Splash_American.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Splash_American.tga.finfo
589cd767952ddfa370c760ba2463a1517cd127865a0cbca0e7073c85d04682e4 405548 legal.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 legal.tga.finfo
fd979e767a8b78eef4eedce8b2fc5e5fac6bd437028717701f9086d74553c45a 7955 legal.tga.rsrc
daf18cbd82da3718619795936c0cc0bd38ec5e4d3b9a53602c4226f204353ca8 3145772 welcome.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 welcome.tga.finfo
263b1ea7d1238d5cbc7cc22a15f7d9b928c6d0f0ee7ef3c0dc28feb18e641bec 331 welcome.tga.rsrc
= disc-hfs/Theme Park Data/data/init/640/
f3c92202e230f99c73c6b0804322c28badc140712006713cc0f6ba0cd1e694c6 921644 Splash_American.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Splash_American.tga.finfo
02054bd11f89c3ed51aac420083597c8c9141c60378a7f4ac8ca1c178cf812f5 158764 legal.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 legal.tga.finfo
6440b161494b4831d3e3601214846a349a782dd6c1896faee45a8fc81d8e472d 7877 legal.tga.rsrc
982820c7999eadaa6f5ce9a493fe9db6844f3454e0a4d05da282f5d117317c9d 1228844 welcome.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 welcome.tga.finfo
152afe5786f6eb38f9ac6c90fea0c37eca29c6770f36835d8aa131b214554e13 331 welcome.tga.rsrc
= disc-hfs/Theme Park Data/data/init/800/
1c06228cae5b033aaaf9cc2db3d8399d8161a303be0f7a6454276c8be971651d 1440044 Splash_American.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Splash_American.tga.finfo
49d367460017e7637157cceedabc040f6909e431129e1f67220bfa1a55567179 249644 legal.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 legal.tga.finfo
2a8bcc22f508962f5456bfd169a751c20b16ac97f2377a0308b71b4c29a6ac67 7859 legal.tga.rsrc
1dcd69f73dd9157f0f5cde81e37480c03039b96655db7cda82ec1d77d4ce4383 1920044 welcome.tga
6e235bfd6cb68ce0f9af5a757ff58459216f489e75e9e5c67ce833d138ddb319 10 welcome.tga.finfo
f52a8723243d96fb2e157575934d52dd3337881e5fd8aa01923ea8ec12db07a7 331 welcome.tga.rsrc
= disc-hfs/Theme Park Data/data/levels/
b88d2955e719de6aa10d70fd8520aa60500956eab486702799d068b6e23f5e58 19730 Online_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Standard.sam.finfo
3d39433641df70dba73e6ce0f4ae576a5a334ed93e61adc1cdd570b751a8bf4b 20630 Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Standard.sam.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
a0464c5bac42ec5fe006d56d7e191585ebb68209535bf258747196c406259a9c 786476 2dmap.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 2dmap.tga.finfo
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01 879 EndHoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 EndHoardings.sam.finfo
ef93aba09c2f7513d528763c9d8073659796ebeec8b50da07bcc79a9a418a795 967 Hoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Hoardings.sam.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/Music/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Icon\r
46d5376e26029f5739e0994c5c62cf646518334e2bdaee711818042afaa20ed0 10 Icon\r.finfo
40dc81615e0c31b2e4fe653a3c1a41e50530dcf64a4b2428ff8ae6b5770863b6 286 Icon\r.rsrc
6d35cd515ba59027be57d492eb9803554e1ff51ffc231381a5f09e0ca07a0852 31674907 MusicHD.sdt
4265db2aa4ae0c9cf96fc97b33b7fa1c2ae52eb9fde2595bf9f8390eb5dfe55a 10 MusicHD.sdt.finfo
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4 55 cat_musicBANK.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicBANK.map.finfo
bdd080f8bece1d03df33cf2868ef82d4433b62543f0994b9d24e59fefda8b65e 2182 cat_musicSFX.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
e25770aeb7a19e2688d37bced2488c4f42b5a46887adb38dda32150cb5439901 4002 Online_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Standard.sam.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/Sound/
4228b7d58e7807f0d7d641cb812767e4eba3c787482022f77baf2b1853e00621 810086 AmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 AmbientHD.sdt.finfo
b86a6ec4a52bd2e0d68fae7a43b7fc9bbe035fb7cbbcf1bcd94f38b6519b8ea7 724255 LobbyMusicHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbyMusicHD.sdt.finfo
73cfdd25d21b1b70a86fac90637a6f8f5f8c42ebebe59c02db437599dec65ea5 35850 LobbySfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbySfxHD.sdt.finfo
807d3ce648e3f10fbc394351562c7bc8bcf7557c4cb110233a6e9de4dde0c492 1131131 RideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 RideHD.sdt.finfo
2fc8e636b95c96a9a4a3c03f9aab8c9e4895f0a20c10ba85ea98e67ab9b51eb3 57 cat_ambientBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientBANK.map.finfo
65c9bed8ae42e63a7c2ecfec8bacdcc1d8a0723e26f3af089ac52b818293d7b4 16834 cat_ambientSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientSFX.map.finfo
f05b6523280ff15b3050dfcb6fbc669e8aed4e558a2c723cd466e4fce012d59f 60 cat_locallobbymusicBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicBANK.map.finfo
74c2e2590c10db47602291db09d9860cc87fd83a9bc31d26bee962e977d99fc7 162 cat_locallobbymusicSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicSFX.map.finfo
f139dc1ab3183120437cb78479b9e2c7560b6229222038da07842dcc4a394295 58 cat_locallobbysfxBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxBANK.map.finfo
bfe3af27e41a7bb054e48a3a8dc039a7846ca3487e097ce37da6d2ad1604d84e 492 cat_locallobbysfxSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxSFX.map.finfo
010d122e94840753c4545562cb3a9f5639425aef9239864d5bb66c9434752e34 108 cat_ridesBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesBANK.map.finfo
8a2fef253f6d5c54422f3c51ae69708034257a3bde8ab58dff6ac895632c871a 10790 cat_ridesSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesSFX.map.finfo
1a4a224ca6f92de93d56987366a78db185c267b6eac9b70ea1107e4d3536d047 116241 sRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sRideHD.sdt.finfo
847e140b592e09da5df3d568139d07918f6c045e96dd7295380bb0a1b6794d1d 359433 xRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xRideHD.sdt.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/Speech/
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa 57 cat_speechBANK.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechBANK.map.finfo
c199cd736fb1243cd5d1814cbf270e52ac5444210e70a9bc7a81ab2d9aac77b1 130 cat_speechSFX.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/Speech/lips/
38aa5b4ffdc2e7d5e15a7a4627b0bab310a975740202a315227ef755e5f4eced 56 sp_001.LIP
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 sp_001.LIP.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/Speech/
3b27e98ed19bf91c2accfc4ba1a9189d54eb43180cfa9fa9f2e8f8eba853b255 98792 speechHD.SDT
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 speechHD.SDT.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
f90fc9b7dc26d80f48c2cd50c18d7d12fafc85779fb4d22b5597e4b3a9d4d104 4970 Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Standard.sam.finfo
354dbd216561345046f3986ebe489340211681219fc93a43b1a27bb01d9be791 99140 advisor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 advisor.wad.finfo
86565a3111b7329e0ab2263ca48e5bf7d3e8df9043fd7162810ddbb3cbc8f4ca 1101 dynamic.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 dynamic.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/features/
0a487bdc5710c1870202bcc7910f93b38dc04512e0b2cfcc8d1c4ff1735309b9 2678 Features.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Features.sam.finfo
5923a2babc3feb760c8b99b4c1ab61475af4b9f1d42a7a802c6edb7dd7496403 35152 _cards_19.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _cards_19.wad.finfo
a46b304ceca910740468042838629cd7cf07ebbe50da1f3d9be26e231b9a582e 17077 _coin_20.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _coin_20.wad.finfo
f1f32c042c4758a0e800b6879c16f079be6510b0a1dd869a541d38880bd40724 11687 _crayon_21.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _crayon_21.wad.finfo
ed934e5945df0470aab096089fc187949de5097de4c6ea04275b0ef34d4e2f52 8282 _sweet_22.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _sweet_22.wad.finfo
cab7927ea0d5e4b6061f112e62c5fc454c6550fd0db61c68d84e30e46ed5957a 22843 bfbin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bfbin.wad.finfo
f3547fd7185aeb81675a42f89084714bae70d2a05fdae78a767078e323219263 38398 bricks.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bricks.wad.finfo
7ba8f59f4b89c1c2ede458f394f5ef2e3564f18011e6be665c022c5643ca189d 29079 bus.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bus.wad.finfo
8603f593a68d05cbd1cda2109bb7ed543d141ffd94e2b5b2e39bee691313e37e 12546 c_cane.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 c_cane.wad.finfo
05c8e6dc4d3fb579340f1f40d63f51eda9161604046f004cbcfed91025ec1369 61965 cardgard.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cardgard.wad.finfo
26a4050395234fc43b4f86dc01929cced0920e76b5a8e9ea48453e3197e1ec6d 36123 donut.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 donut.wad.finfo
2efaf470cd6c5f6769a64b779caac607ef1a4b5fbb472cde24acf56611ec5ee8 58011 end.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 end.wad.finfo
2fb940a31e5ad1eb1e50e86ea40e8557bb06af3e40b89893943b86a67799a7d0 27916 ferry.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ferry.wad.finfo
dcb65f110d764e32511165515c501e588db1cbb93d32fd1dbbe8ef809925fa23 4617 flower.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 flower.wad.finfo
65362975dda62185e055b4e4041ef4bb6edd8ce984924c40b09bd7512d0cafec 4444 flower1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 flower1.wad.finfo
efc19aa8abbdeb6bd3f1a5903c5f12255374e45cf1de2130c76ee06cb88c2628 4223 flower2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 flower2.wad.finfo
bbbe145ec9ac9b478ba988a2c3029938e9176f84fcdb5785734ec657852192ed 16012 fork.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fork.wad.finfo
a86c0239089fd5ff24e00db163cd9f848aeb6bb46004d453be0f7437d412f466 76398 fountain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fountain.wad.finfo
f3a4aa8315d9aa392418c6ab2286edc9f207577f030d4eb06bbbfd4b5da37384 83309 gates.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gates.wad.finfo
c4a000f43f6ebf991a7f9db6818c0e108b67161c76e1082f663a7095f8d9fe58 8676 grass.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 grass.wad.finfo
6d68b5121cf654ea4d6de3e812dd11b2d85dfe451a00076f3dd73c62060bd22b 999 ground.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ground.wad.finfo
225ce76a897bf1afb05b51fa7248633efd85c8768bd5e2080e167b1afc8971f5 6011 groundc.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 groundc.wad.finfo
78ebdaf502a1f518c7541326b6f99baccee01cd5b4cccf150ebed088ed8201c0 10281 lights.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lights.wad.finfo
95b330567649d6087acbe087d1aae5bbf04fc8a736fbfcd4da47a237059df96e 19913 lollipop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lollipop.wad.finfo
5bb82cf06660c663f9619a413e9714231a654f3047df8b58edee28f4fb1c228d 42708 loo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 loo.wad.finfo
974e23b0a424e9b036e5a4a12708a9933466fb49d12053f92f58eb66aa6b1f85 7691 mushroom.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mushroom.wad.finfo
d6f0c90532b22e4b5c0d8da5e930a14270fabd735bfff102d64c36bc88bf9c8b 8113 pencil.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 pencil.wad.finfo
14f7a82d5d00038d14fd42561bbc19260aca009695b7ea1a2e633a9f6c648d50 15322 plntpot.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 plntpot.wad.finfo
ac2b50d61d92cfa1f38d4772cba5950870ea66b6adc107144236d2e1434a5239 80097 royaloo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 royaloo.wad.finfo
2f13e5e9cd2d2eacf5fce944264556779ce437fc65fdc3d17a692599aa580791 29331 seaplane.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 seaplane.wad.finfo
c5971872e4c75d12c31d36cfa59932b5c9435162db234f0e9c6f93fb612858a5 39886 seccam.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 seccam.wad.finfo
de9b32322ef41e90ad959548ce6b1d33c58a667bc7eb9c14a871bbe236ed560f 20619 sflower.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sflower.wad.finfo
3d238ec8c7cd0264bd57b7e4db6c7c6df23a0db507eea39855c22b4081589130 56081 sign1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sign1.wad.finfo
7b49806d8527fe47d0cb2eaad42313dc013ad8187c95297375c2b3adeca8c9a0 7865 speaker.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker.wad.finfo
2deb0324910542ed3b735d84c063b3754f073497ab6b51e477c1cbd53539faaa 7872 speaker2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker2.wad.finfo
ebffcd6b428522e01423b62510bf04857118b7a4ed45576fba3cf41460288ee2 7872 speaker3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker3.wad.finfo
3fb088ecee9f194f5330aa916c3592e9f6fa729b2a718eb0c9a5823d1070d986 7872 speaker4.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker4.wad.finfo
93840aa6e032edf1ed8f330249fd26ef63588bb3e5399688d849d55456683684 19539 staffrm.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 staffrm.wad.finfo
16078df3257e502ec5430ac03c028925828857199927e79f04aa87a714a252b9 28578 teapot.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 teapot.wad.finfo
20ccc115019964d4c6fa850c1c192bf058b453db0381a4d5d92566150bd057dc 15377 trowel.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 trowel.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
9410dd7840f379b9a85bfe46515a3629ff1ef6d26fbb95147f16d575acfcc9f3 304 global.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 global.sam.finfo
9845cf09d0653b1978dea8a0d79468b8a3800e09bac0a202ebd1a9fd2214e8a7 278714 hoarding.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hoarding.wad.finfo
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510 35202 miscmesh.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 miscmesh.wad.finfo
2b51f5e5de61052ee36123cf3ee926be5f987ee0bbe6f3f3820904d81e6e72c5 150264 queue.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 queue.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/rides/
95f5f5bd57cd9ef28ff8cc4ba66e9a16897bb2947a2a65b517eb2d08389ade44 233 Online_Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Rides.sam.finfo
e72531e0fc9b54fe2722a6aec913ae3951f0f12c4bd199bedf04e8ce92cd01c9 3566 Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Rides.sam.finfo
313342851f117bc0cb4c1f86029f7d2e4e208ac88f1fff49747c8bc9a6c35c8f 236529 _acorn_23.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _acorn_23.wad.finfo
b82049694bd69484fbfe7dea7e9d0cdd89b0dd659673293b874a10c786f4ad19 264780 _flyfoun_39.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _flyfoun_39.wad.finfo
22b4917f70e8b4cfcd0f2353a4b7766072cf5353345b8d1348787f761ccd329b 252273 b_drip.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 b_drip.wad.finfo
a5d7270299dc35b95f79e9650e4dfc69e003681ccc8a33e8df6cc990bdf22f62 163972 bbugs.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bbugs.wad.finfo
260b28e9785c8576bf3310fd07fbe53864da8960df0e9c06bd2876a49ac7a8f8 217846 bigapple.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bigapple.wad.finfo
c9ffef096d0ee1a6c15f963c9df2c6d6967ab2f2f0c5292d9cb91205da761589 131197 bugstv.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bugstv.wad.finfo
49318a91169607e71c5324752fe849e522e292565aad4a723febac2c3e502883 279818 candy_c.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 candy_c.wad.finfo
cc244b28c09ac899705e2ad640479e8f4840b314abb6f8909bfcebcf3ef743a2 249902 cat_co.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_co.wad.finfo
291233c18b4985cbf8f0a3c27d8cced417a0d95292bb57476670a82b5be4811b 392688 ccride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ccride.wad.finfo
81b8e5d2cf43d7a022c0b1a1c91511c7494df21c445369cf87d28dcb8a0de95a 247504 dragfly.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 dragfly.wad.finfo
5146e580c014381083df621b84dc6cb19bc1c6d446f69a8e8dba25438ec0600b 191676 flamingo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 flamingo.wad.finfo
7331f2efa392a2b8d387fbcf8a7455f82ec70636d596fcbf1220413c9ff1278a 130608 flowspin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 flowspin.wad.finfo
e9c7dddc86b0687df8cb26f02cc44bc8d1c8a0b60a726af43d9cb74b207b8ada 199015 fwheel.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fwheel.wad.finfo
dfdbb4534ce6da783fd431dee72c03e1ae8372f423c26750e2d7d4146ecc51ec 407294 gokarts.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gokarts.wad.finfo
0cfd85b64a3a3ca4a33c8694367e3bd8ad0a7a991822ab6ef91e419786fb764b 104324 jelly.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 jelly.wad.finfo
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5 23163 mystery.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mystery.wad.finfo
7fab84f3585e2cd59e0dd38986fdbf2210995f7fe252088a990f6d6588e0aa6e 119745 twetours.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 twetours.wad.finfo
047e92524c0e88d32fedd0bc257ea12cf7bf6e4a22c484f615eaeeae5661d056 394424 wateride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 wateride.wad.finfo
6a4a2695cbf6e0c9bc6591947c33685fd19a61f1350613b77a9353a070b44268 192326 welldrop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 welldrop.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
57224eaaf1f2894673e9e893e08d6972e02e48ec530e8e412ba769e6fd60bdb8 411 scape.omp
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scape.omp.finfo
fecb402d9eb4d5d3bb924432853e0ceefb4affc7458d43c495c882be6498dfc9 572814 sharetex.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sharetex.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/shops/
f22eb1e93f9dad617c398c8cb4992510b27bddb9feade9f3e0ae523aa33a64f3 3176 Shops.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Shops.sam.finfo
ec2ffe6471f5e7493ad6d49384444f582aaf2ebc087e676d1709b1a02ec4a1d0 28852 bee.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bee.wad.finfo
cdbca057918f8ea6a482492e0562f5d4d1b16588d710f02258b2e64fdb33963f 36631 drinks.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 drinks.wad.finfo
48ba55f9dfad14c7ff6d1ee3f5f9a7bb0c0acaeeee3fa5363f4361973c01ec77 48491 fatfairy.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fatfairy.wad.finfo
d5464964e7137d70b83a57c43ecb0a02c733d3c3bd87b8e0a3524fdae2dc62e2 34037 fries.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fries.wad.finfo
b5ef861af30ece1fac02d9ca7353d5ed567b0daf138fd43c5b4fadf886e0d8e4 104522 gingerho.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gingerho.wad.finfo
9909e9d0b22aa9b20cb1ca1bc84307f3580b7c9d72fc98b0ef66a6db0ef31e18 26691 icecream.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 icecream.wad.finfo
396222ed752ff862e76d1986b2a7bc66480b5ac1d1af6ff50998c3ffa3e6faf0 45508 purse.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 purse.wad.finfo
d57217555b48b8e980e7f87a5db3e6a211eb3c764a4f2d6733ac26ac7eb504d3 45210 sburger.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sburger.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/sideshow/
ff46e41ae1a8b203a26b322c6965c8836e1eaa791d6e0fdd9981a487248267d9 3022 SideShow.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SideShow.sam.finfo
63ea3df54af138df47e8f9972d85e698778e5864ed10ea390408eeb3c9019721 61855 arcade.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 arcade.wad.finfo
70539b05af38ae0c68533654e55a6e0cff8d342b8b942f14e50828a340261b6e 78050 frushy.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 frushy.wad.finfo
be2db97b76bd916e002c8e35e9bd9ac8f6f362bd741c71d6de3882877097a42c 78720 squirtem.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 squirtem.wad.finfo
f95db66e38dff3b598a27b39a5693466cdf81008b8aa80542f7e11e070e244d6 178813 strength.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 strength.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/sky/
8b9d1562bc082e25d250dddf0bef33ffbda98b35c7c388a282716029b93ac353 262188 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
eae11045eb295c0102ff23fc2e6aa5f30ecd39d7414a240866faed4adbe07ed9 262188 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
cd1ce80746ce8dcf2e943fce1dc6b2540a51a8d628c9f9f542a455c4b1afd332 196652 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
e2627c153388819a827b403320e9e5ad5546848889e12501f1e2d39515c20103 71776 ssharete.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ssharete.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/ssky/
2c29537b547399d29c2a764185ebb3b5102482b0ce4f8f36f01ccf1764203cf9 4140 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
20da9d9b49d13599825ffd5a53250fdb01a271d752f1677144ba34fc78af9ed1 4140 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
3bcca26d1bf04f2ae106ad14edee967047e961ebd203c0295562fc257b9f63c9 3116 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/
980bfec26866fee118e9b0271592b080ad24bff14138c6098d270d84d1e115fb 2004500 terrain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 terrain.wad.finfo
= disc-hfs/Theme Park Data/data/levels/fantasy/upgrades/
f60ce2e38f25be4cc586f13d704f7a0d42b5439bffb191acc724a9748c987a19 2496 Upgrades.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Upgrades.sam.finfo
82fefebcb0d36d48d6bdef14765b0ae104e193fd18283d9e2f96632abaca5853 75551 beejump.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 beejump.wad.finfo
6c66137220e2595fd16912a2ea8348ff5eded55f8fffb38676665adc991973ab 102629 honeypot.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 honeypot.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
b6fbf2f444517e4534c0e9c07ea308b467c1aa572b4e093b9ba7105efef45876 786476 2dmap.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 2dmap.tga.finfo
ea671487b4d47e6c06190ce31d9732ec593b84c5c6aaf2257a8abfe904adafab 569 EndHoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 EndHoardings.sam.finfo
de19d199eb50792e3e802831f2bf8bc62c5a20527e81dd19d9a91f66fa71de43 763 Hoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Hoardings.sam.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/Music/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Icon\r
46d5376e26029f5739e0994c5c62cf646518334e2bdaee711818042afaa20ed0 10 Icon\r.finfo
40dc81615e0c31b2e4fe653a3c1a41e50530dcf64a4b2428ff8ae6b5770863b6 286 Icon\r.rsrc
aa32f7cda25879750b9f04cceab799fd1e0ba46a7bed8e9159e74ecdb63b3878 25290836 MusicHD.sdt
4265db2aa4ae0c9cf96fc97b33b7fa1c2ae52eb9fde2595bf9f8390eb5dfe55a 10 MusicHD.sdt.finfo
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4 55 cat_musicBANK.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicBANK.map.finfo
70000d62c4d9d7bb06b6fa643ab736fcca3037b75588468a233feef000cef71b 1858 cat_musicSFX.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
10e4e5b00b02bd8ada59fd6e63f9d5c33e70a38c5801cf3d3ac9e68806bf98f0 4364 Online_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Standard.sam.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/Sound/
b5979baadb698d97148ba2b6c795939621f823eb89c0600cc1a21d99cbc0d157 1420624 AmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 AmbientHD.sdt.finfo
cb31f10a2b1f185043e620e770dc62610eea6b9f3cc7b55985e19c5d27c80c65 880048 LobbyMusicHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbyMusicHD.sdt.finfo
6fe2d506ea852dbc892a70db331ac89fb4733e711be87740eaf8569be2683202 413590 LobbySfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbySfxHD.sdt.finfo
27813f0cd37596637b81086e8c34eaaf51726c3c8b85d07c29b437d539d6e26c 57 cat_ambientBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientBANK.map.finfo
3be6a1b5e995ae6b54645922704412f1b10184ec4ceb20498d33deaf1fcc7796 4602 cat_ambientSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientSFX.map.finfo
f0affb542195f0b4a0ba65a90139ea8166a8be4224e845d1614201ad869b1b10 60 cat_locallobbymusicBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicBANK.map.finfo
0e81b0f9d8afff11b10df4d3517c2e1179a2474ca0422686574eaae02c01195d 162 cat_locallobbymusicSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicSFX.map.finfo
589a4c72bc3ac0cdf25b645d88818ea7ab66cb9fd9018ce5a81fec9fee3a5cf5 58 cat_locallobbysfxBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxBANK.map.finfo
84d74b26b22e6f33f10b16ce289b19c1a45e2c43d23ddf2ffeafa98524544968 740 cat_locallobbysfxSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxSFX.map.finfo
9819b1e14294594630276d6735976205cae4f6335ee100e9f26785a59939a2e9 108 cat_ridesBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesBANK.map.finfo
9f4eb46edc2ef19c7d36d8b98a7d3bee5176ea129b758a35d3c643c77b9fd66d 13184 cat_ridesSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesSFX.map.finfo
b60aea06d34218fe83f05c4282e4f69dcca9c9d5b1195ad22874139f1fe2807d 2256628 rideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 rideHD.sdt.finfo
a1f233e18644b5043dd892277b55419fafcd73f02685423836ef7017c91f2768 255701 sRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sRideHD.sdt.finfo
4bcd7427130b79848709037c2982f1ff2ae9413d1d89993b5dbc01e34ea55702 247473 xRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xRideHD.sdt.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/Speech/
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa 57 cat_speechBANK.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechBANK.map.finfo
aaf8055ac9b8924bfcafef4984a003c81b6dc63bcbb4cc3d8c95e6a8836289f5 130 cat_speechSFX.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/Speech/lips/
8d3555971c46ad7ff7d37e3d5e84c77738de37b3b2b3cf0cc43a8955530a7863 88 sp_001.LIP
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 sp_001.LIP.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/Speech/
3a0128ef0b9b5e0258599d5ea31a2a0f2397c3edb38496e39832946ff4b6346b 162113 speechHD.SDT
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 speechHD.SDT.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
ff9dc009c0c608f55b99f70777af736887a5d0b3e5892af19b43b080d25a9733 5406 Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Standard.sam.finfo
ddb04b09eaac414fe5b66f84a16538059a60ebe70b5a2716688073fa2911d1e5 135822 advisor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 advisor.wad.finfo
668417dd0aeeb262a0571a51dc89b0aab030a34e2fc2ccea080763ae7e80128d 1123 dynamic.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 dynamic.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/features/
ca51cac74cabd4d5cad047a9887dcf29516329b3ddb2a4e8ef4f024abe6ae398 2678 1x1rck1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 1x1rck1.wad.finfo
9e247114011fb1d5abaee82f42484dd235bc85f525f618d67124a98b3a37d023 3048 1x1rck2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 1x1rck2.wad.finfo
57244b2e1955286b9fab860f5db58f542d9bb49958cea1215c5526b08f790024 29394 2x2rck.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 2x2rck.wad.finfo
568df9310768e9148332c0e7d1f7eb8d49d01bf64ab24655764dcb3b49e121fd 29600 4x4rock.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 4x4rock.wad.finfo
a128b331b8250d7d0be88d766361f9a57642ecc7b66f52b1f8a22a02f616f840 21590 5x5rck.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 5x5rck.wad.finfo
45e564f37e5d207da8ec4340e529f705a9d3dfe710d4ff8414817b14b7979bf0 2680 Features.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Features.sam.finfo
506eef30f8dc628c5e02c69ad7c4fd4c9f814007e67a86d983085c07f4ef8df3 28224 bin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bin.wad.finfo
7b80404596ad68c20575b1c3b2edfe0b0017be169e49aa8170fbf21c8812c360 30086 bus.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bus.wad.finfo
bcb21be67ade7b4875766e3d4d34fa1ba34e6cb261f63bc98ecce6f33417b266 3924 bush.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush.wad.finfo
5606ecfa64679634358e8074ea4374199904759899321c44ee958ec3d9d32d17 19882 bush02.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush02.wad.finfo
f3f5daf7594df1418d0e13ba84b3b312caae8e8acca5a383302b311e835f2ab9 38978 demstat.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 demstat.wad.finfo
3d662ee2fe0a5ad35229b068926565aefbb43b5d8ff8da7ea765c1eb658a703f 58013 end.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 end.wad.finfo
cbefcb97eb0099989c31efcc785df1afdac44634b6f27f413039ecf635ad862a 27916 ferry.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ferry.wad.finfo
4ee267d23ac7fac688d55c12f12d1aea32fca5cd4dac4efbe4a49ce14040a644 22784 firework.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 firework.wad.finfo
b04b7bd10094547b963ef203cca035bae064b4d5ef19b4990840ebe5cda227f0 22152 gargoyle.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gargoyle.wad.finfo
17cfe422d714a4f741e4e76b272b7b39ba09a14ad716948143c1ce395530d4de 72064 gates.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gates.wad.finfo
5fa46c003ce1c1771b4b840e6a122bc1903c37a217c567e6c97bb1b36355c4be 3400 grave.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 grave.wad.finfo
d177331768fbce2c8612cf99152a8c9eeec5d425c23bd546efd224e4dea69238 998 ground.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ground.wad.finfo
e7e2b389c28a2dc58e375fcb441cc3f668bf83390e562fdd4387c957b5f45012 6011 groundc.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 groundc.wad.finfo
36ce7093b8704c3735dc36fe323d2082c62291069dbcfdaeaae33f43c0a72636 24393 horcam.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horcam.wad.finfo
a7077331fe45415175df0be3cd902b9a723a4e7c8c87fb7024b64dc85b3c096f 91464 horfount.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horfount.wad.finfo
57637ca20102f7d6714a4cd4a6cf32b5f48fae00d964b758a35907ffe2a9920f 26499 horloo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horloo.wad.finfo
aa192cfff6c42002caab2fcc2dafb9d7c4efdc7cd9ee8ce71900d0d910e38470 3747 horspek1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horspek1.wad.finfo
66476ff4dba84234be58ba50db94406376dd853cdedce60608343d98159cb655 3747 horspek2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horspek2.wad.finfo
6c745c1b8534be2b394ca4084680800ba3f63c0c12a13576ed554671a132eb9f 3747 horspek3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horspek3.wad.finfo
3cdd45e70fa19be9b14117019555454199aae259c6b6470af9f27eca41846a6c 3748 horspek4.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horspek4.wad.finfo
b45fa99855f7db9baee1c434fa760179dc9be293cc2e63c961a0de878c8550d1 28344 horstaff.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horstaff.wad.finfo
c61b31007ba29536623ce1c5416ab4a2b8326952d02f801d3e9df50eb5221cff 42701 horsuloo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 horsuloo.wad.finfo
4885882ff90c97c710bcd6e90d5034d4781a6fb0a738fd495d202e1fd79e8233 10281 lights.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lights.wad.finfo
ea893be439998b37fae8026d1061c2d539e50e08712e32325438b875a706a773 21039 ltree.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ltree.wad.finfo
b95fa3cc638eae9935ac5090d6933caa3a2a81ce8b5dcce2db96f9668c27adab 20234 mtree.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mtree.wad.finfo
cc944e8e12880c739580482b9ff0ea18f8bef99a616f11637913af2fbf68088f 21636 pumpkin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 pumpkin.wad.finfo
21043aef9b221ecb1367518284e23dad2a9f875645ae99d57fe9add653bff26f 24073 s_tree.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 s_tree.wad.finfo
c2ec0bb397dd6e56e35aaa671be2ad10a0fe89e645844455cc7f16e779ca25ce 29856 seaplane.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 seaplane.wad.finfo
31e1443632409d3698cefa7f8966df1c34f4c79604ac7775641a40e6973db290 47690 sign1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sign1.wad.finfo
e005c7a8e35af239f5e61bb56df432fd48de4adaae48d43a044faf0bdfd38d93 43448 tower1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tower1.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
b4e5f4e2d34edfbe18d249b050a8253d45c873fcc6314167b6afb1dc18286aca 306 global.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 global.sam.finfo
332cd0115bdd7413e971b2c7e019bf46ebbcfc9730c045657ccf4c8efe8043d3 133029 hoarding.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hoarding.wad.finfo
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510 35202 miscmesh.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 miscmesh.wad.finfo
06dc7df1b7d50e0f7515a73ff26f166c894ad2692d20db61e04a30275a770ec2 71938 queue.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 queue.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/rides/
95f5f5bd57cd9ef28ff8cc4ba66e9a16897bb2947a2a65b517eb2d08389ade44 233 Online_Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Rides.sam.finfo
d6c4782462110ef40bdef9acff1476f97b74187ff1c9651d8d032a3c9fe29174 3708 Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Rides.sam.finfo
63adc19f6dcc8158540c278b24769ef204855e957d75ae7e9158d0f0e7bf89a0 231417 _bellt_10.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _bellt_10.wad.finfo
7ac1567b7a505f4ca0a217d92c807dcedc1a0150025bf1dcb16d421bf6473ed3 139187 brainb.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 brainb.wad.finfo
628d5bfd226edcce95f273406811bf063475223c0bc100d272f0ac70fd8c5949 272532 bug.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bug.wad.finfo
cb423962417c15aa353b47f1233a31ce8f60fbded9aed9763c8dafd69f3c1e23 113692 bumper.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bumper.wad.finfo
afba96b507eee15dc7ab2fc894b930a94538c79ba8aec5a0019a5be3b51d820a 240765 c_hade.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 c_hade.wad.finfo
a117e7d916f59212f8f9971fe181245d877fca36a2eb39a28e350396ca5e205b 258147 c_scat.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 c_scat.wad.finfo
2332d9549817ed36a74dfddbb22515edb026d73d5eee4ff75b02cc23351a2902 148848 candle.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 candle.wad.finfo
8b08cf45ad95e1d64041036b51a26954513eb410e3c6f647be5bae746add420a 179912 coasta.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 coasta.wad.finfo
740a8b8199c424585c3d205dd5041d92c2c8131fa7c97087b2e6e88d0d79d96e 211061 demon.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 demon.wad.finfo
a9d5ee54b646b502e4e65eac0c39a5285b052c554444d3cba2c2e63cb7578983 179034 firepit.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 firepit.wad.finfo
c5cefdef95ceb9ab4173d9ffe79daafeb16078ccee4e9777172be3787d919ccc 264437 ghostshp.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ghostshp.wad.finfo
e3043e554dc892473ec176b9e354a70cf8eb47a5272beb013c529bd1e6af2792 201970 gokarts.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gokarts.wad.finfo
eae300bd3e27b91c7dccdef946925d065b104293c50d79d11926b1f4acc0cc96 139108 haunt.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 haunt.wad.finfo
87a655929a350b0dd0884a971fb6f7742122628fae28e88e7264a9ed6ce0bff3 150854 hcentro.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hcentro.wad.finfo
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5 23163 mystery.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mystery.wad.finfo
90a0867707206a76d29e7229c65ea4756bbb55397d7a5a7c3076e2f2bee59792 217840 phantom.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 phantom.wad.finfo
a07750b0782601f37e52672882434240437557b0bf4cb55541330cde54e9cea5 115020 pumpkin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 pumpkin.wad.finfo
b5e11e401561af28ed82bbe1d8fccf18373e86bbb2ed5fa82bb8097fa55d7c21 232116 ratrace.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ratrace.wad.finfo
23a3fb023ad3a9cd264e2f2e4a2b46969c9106bbe36efd1a8dfde2fb2f77bb41 233935 spider.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spider.wad.finfo
18622b40252ffac8b6bc8fe1ebe4f173c558e58a6ed7ee9978fad525a9ebdb50 154614 tourride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tourride.wad.finfo
eefe9044026e73a02788dbb4d9f840e5cdaeefe5dae7090c6340f659cb0b526c 186186 wateride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 wateride.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
554b9252aa67c85041ac5fae5482ebbb8e1a7192ca8e9e81c23bca2fbd139949 413 scape.omp
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scape.omp.finfo
e2adc1375a1e896ed4911ea9a60869b7c7fbb496cd88adefa14732a9f121b754 561947 sharetex.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sharetex.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/shops/
cfee8adad2867d755362fa8a3cc8cde79a7eec443c8b2caf6948d68db2676122 3178 Shops.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Shops.sam.finfo
c0cce66e2c9a3e0a2a5a6a40cd1ff714a289d44d416f121123e4381f3832db56 50056 burger.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 burger.wad.finfo
6d56622089277b96446c7e3bb96a430180e9136d81d99eb0a70ab7ced285bd76 105816 cauldron.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cauldron.wad.finfo
b87c3bc2ca38b6cf49435ab5c136a53deb0ac09c66bf6fdd72b51af76bc38f03 36347 gchips.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gchips.wad.finfo
abd6d4809356fedd3eee1cfa7bdb3062b621e7db10b8f636ae3c8cc75e7922d6 84433 giftshop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 giftshop.wad.finfo
389d418443080af3e87e0e5f4ff4260d6c88469ef4a5b38c500b49700a87c44e 22168 ices.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ices.wad.finfo
ac784169f5c13eb3654a0a5493be7141f2bde1e746cfb5dbd044cc81d8995396 40622 pumpkin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 pumpkin.wad.finfo
8402e8f54d293e53675154db8ff1277c5ae6b49cb16fb48b62e460476184c098 64636 vampshop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 vampshop.wad.finfo
af5867c3a30542f813524ba6e4d1cc2cf51416f571f1dc68158990c531fd77c2 77168 witch.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 witch.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/sideshow/
ab58248b468e43c7f9d9612f2c7cb76e3b7f447d79f9f03510b1c80d30626f6d 3022 SideShow.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SideShow.sam.finfo
ad8252dce8a0e549ccdc5180d7a6750669ad41036b89d5fbe16751707af0095b 154102 _shooter_13.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _shooter_13.wad.finfo
d4a05e18557e2073d25dcc66ffcd927a1ed67ada89096fbc33fb4f8ff1b3451b 69001 arcade.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 arcade.wad.finfo
12408345e30d89c7c2ae55b4125f0c07389efd1467fe03cab2fb2d88a6ac5eec 110702 ball.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ball.wad.finfo
b9a4ff64e0b3dc8fad2167030f722876ba1923d6ce027bb6a50efe48e055637b 141952 puzzle.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 puzzle.wad.finfo
168c15ac0aee291f1e922306f4bfae31fd99ae3e1547c58eea3c3605044c9556 103950 skull.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 skull.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/sky/
fd7abee273f5b35f0164f23f56faff4162dc8390f3c89b45b451d3c98f01e25f 65580 moon.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon.tga.finfo
7c1dc892a3f25623947b55a48601e2c6484da48c84c03cddacb6848feb11e533 262188 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
3a6a5dd239d24f7fb7970249ca21e09b53a51afe4f91312fc4485802c07f68f5 262188 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
7f2a880ca26dacdb965f63a67086ba39bc04663786828ed8097a35c7bc6f4e22 196652 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
07c51ed18fb87b27c99174ebb5e4124afed614aeeb60c0e1db4e1b345b29bc1f 77047 ssharete.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ssharete.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/ssky/
737f1528e33e0775b16a966d2a7d4f19aef9a44919501f982e53863379fe13a4 4140 moon.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon.tga.finfo
e47f1bad97d0cb0aa11de8bfee7a53d665fe3b5f2bc3113527de3a1b01116de4 16428 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
be8e3e97d9bb328a96a56503d999dc8b5f16db9440d691848cc6f1f6c551423b 4140 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
f2ee928cf04d1b1bd3c310b62cf2f6703c8b6b829041bd593f7be5cfbd5205c1 3116 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/
8c475de0e9c5ff204b70d8efbfdb41ebbccc430ebf50896ffac4e2eefdb90754 2358066 terrain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 terrain.wad.finfo
= disc-hfs/Theme Park Data/data/levels/hallow/upgrades/
0e1d537dfca3695a75a71b9720d7b0a9ef5466aaa22f5c0c877e9b2e6f844911 2498 Upgrades.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Upgrades.sam.finfo
04dc07884e7fec7fdc4ec528fe6e795921b1e84780130be0f0b6d520a04eef0d 66968 chopper.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 chopper.wad.finfo
e590d3ad3eba86ea63d754ac21c49b36c64186d0bbe68c7f6d73b3ff07525f37 32942 firepit.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 firepit.wad.finfo
c3aa27fea0e43eeead3560b64335b8ceb7b9a45689085feb419589b2fe5fac78 61130 ogre.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ogre.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
0a6e93abc2aa03c355629c939627f18e43fcae1fceb0dacef7de5a3f4d4a2403 786476 2dmap.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 2dmap.tga.finfo
855ca41c493cb91ea41b2ac2d673e6b0163e4ac9b8e8e4ecfa159c310dfbcea0 3487 Easy_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Easy_Standard.sam.finfo
6d89303d098900364bf5e80b236b64bd85976fb947e9e4609d088547f430b39a 38479 Easymode.TPWI
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Easymode.TPWI.finfo
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01 879 EndHoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 EndHoardings.sam.finfo
6c45aa5313609f9fab6fb3160345f4c2a6cb39b5689f45a4ce764b473c006ed9 967 Hoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Hoardings.sam.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/Music/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Icon\r
46d5376e26029f5739e0994c5c62cf646518334e2bdaee711818042afaa20ed0 10 Icon\r.finfo
40dc81615e0c31b2e4fe653a3c1a41e50530dcf64a4b2428ff8ae6b5770863b6 286 Icon\r.rsrc
0bcd58d1e1df303fda652a9ab70af2b6f0a37f4ebcab476824eabdefe453f0ad 31614550 MusicHD.sdt
4265db2aa4ae0c9cf96fc97b33b7fa1c2ae52eb9fde2595bf9f8390eb5dfe55a 10 MusicHD.sdt.finfo
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4 55 cat_musicBANK.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicBANK.map.finfo
efcaf43ce41231e9b5cec11f0b913cc8e364797c9ae3783d7532c05751f1c68a 2676 cat_musicSFX.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
652de797d4a679bc7ce2ba4240d4a11c19fc5f09c08fd2ffa31463c305dadc28 3585 Online_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Standard.sam.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/Sound/
43493cda17efff7d89c933ad72bfc210ebfdebc260c2a71c03e97e8dfb3b32db 522468 AmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 AmbientHD.sdt.finfo
93fd93030d5f6ded0d6bac0d8e6733a66319cc75360bd4d562be07ed0acf01e2 735226 LobbyMusicHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbyMusicHD.sdt.finfo
905ac080822fe120b6f0f4e54dc5524ac024609d96d8571eb38ace950c94a1df 129371 LobbySfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbySfxHD.sdt.finfo
c533fd7d1c1aac985851e31b15afda5975d65a11ad438d39a11bf853f034f93e 1986253 RideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 RideHD.sdt.finfo
64dc53f8abacb589ea06ea95c9ad85a81c332d52bc5b6755fb8c9475819e815f 88 cat_ambientBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientBANK.map.finfo
09befc06792ba20f5ee0b5eeb8ec0fe28f728b1ac3da074b4a3bf3d25bcafee6 3226 cat_ambientSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientSFX.map.finfo
332aeb206da290b8f75486481d57e7a1bf38d91977416a07ded6f236093b1fc1 60 cat_locallobbymusicBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicBANK.map.finfo
65ec4f7a954ea59f1366472ac46a86cad028c7feba0b4c31dc841437583c3af5 162 cat_locallobbymusicSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicSFX.map.finfo
bc7ec091839ad99b2bd98a6e261844e607559fd57c0911911c3e16d03fc47559 89 cat_locallobbysfxBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxBANK.map.finfo
d56bb63191d6c325a6093993eacfcffbf308bce62b89280c6d8c693aa4f0aa3a 492 cat_locallobbysfxSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxSFX.map.finfo
58db355488ba95e0b26d937b0533d8453c5ee8fe0d1504d13b5209e76e35f3a3 108 cat_ridesBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesBANK.map.finfo
c6ebbdbaa12a210dfc7443ddf17fff501024ec021810559f57d30e7fb11d3c8d 13722 cat_ridesSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesSFX.map.finfo
77d2164d0e5d3ccc714a7d52716ac5ea9d6b18ff543790d8579f96f8195af280 731201 sLobbySfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sLobbySfxHD.sdt.finfo
d1883119806336eddcfa87ac5727ea264e23d2ed093e80f20a719df3779d053f 88241 sRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sRideHD.sdt.finfo
32e835ec4757fc856c5aff1bde87bb503247c8c9c061b41819cb7822f6cb80cd 209528 xRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xRideHD.sdt.finfo
77d2164d0e5d3ccc714a7d52716ac5ea9d6b18ff543790d8579f96f8195af280 731201 xsAmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xsAmbientHD.sdt.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/Speech/
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa 57 cat_speechBANK.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechBANK.map.finfo
1ad086ced2fa6f4c265a68ee1803997e24b24d36e130c29db927553b1de5d4a8 130 cat_speechSFX.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/Speech/lips/
d19355c3001d8f17fbe6470e30dcfff012f2fd6e86db81e84458ec06d2f4d974 80 sp_001.LIP
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 sp_001.LIP.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/Speech/
7038e828ad7b4d908a4321ec7ab65f6b0436daf7d6cd4a1811334ad0feb69132 153963 speechHD.SDT
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 speechHD.SDT.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
16ed44c82c7508a77cd71bd0a4757ba70dd78e833972481f5aa3b65413303494 4624 Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Standard.sam.finfo
18f2ae759adb4c8692f672f7db6eecc7004edd23a66fd4d1460722744cbfceca 100142 advisor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 advisor.wad.finfo
86565a3111b7329e0ab2263ca48e5bf7d3e8df9043fd7162810ddbb3cbc8f4ca 1101 dynamic.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 dynamic.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/features/
8b44a231b128683dae6079ce67130be31e75c4cde5a2464a9f837738fbc02383 15646 1x1east.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 1x1east.wad.finfo
f0365995619751be0102eddf87ce2fd79a0abd0fe8473a19e9b95d734162fe90 2481 1x1rck1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 1x1rck1.wad.finfo
3eb1c8bc800cf335f5526eadbdb4c24766cbdcc534a034b9e480552b535c54ca 3459 1x1rck2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 1x1rck2.wad.finfo
4e5a3c84e463a2b6d1523497c243b94f3d17ec40ae8df8cab5ecf0005498a630 7015 2x1log.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 2x1log.wad.finfo
6b106cc3f660a3d970476fca6839961ce12c4861a2980f975f4385f1c879087a 5279 2x2rck.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 2x2rck.wad.finfo
2f42f0814f01a92e15c7d7897556684fff126d9dc47fdbdc049992a4e6bcdc81 29682 4x4rock.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 4x4rock.wad.finfo
1bdea3dd3839e751fcd6bd28c59ffd92941181c870485fca9ee847c6064cdca2 76011 5x5rck.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 5x5rck.wad.finfo
32e39f5cb55fb573211b9aec05b5b594f4d1702cc16823d478de9aad2f7cb8b6 76258 5x5rck2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 5x5rck2.wad.finfo
d983f687b9bd4844a8060c37417417a37309581277efeb92c0b31d0e076a2616 2886 Features.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Features.sam.finfo
105050dc81e73106980b89b833fbc76debc089ce96900874bde17f382499253c 18050 _bushiii_2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _bushiii_2.wad.finfo
c56700ccbc06eea90befe0b99a6999714a4843a9dae2b91fd53fd3c1889a2718 16808 _bushiv_3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _bushiv_3.wad.finfo
6510d4be0de6d7899493ae514b8eee4aa3005a3de70173e9de0f29f7aa68b86e 68656 _gidol_5.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _gidol_5.wad.finfo
dfd4322ce646c42a049647431b83e9d118803810ae48d73417238fe3fd594090 10718 bigpalm.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bigpalm.wad.finfo
3d542d580bb259eb342cc3775d12f56a0c6bd927bbaa9567c0f2d90cd74f8894 30330 bus.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bus.wad.finfo
46e344d0bb3642aa15e463dd251134d7dd9c2b47c6ce79609e277ebd6b5e8dd3 3981 bush.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush.wad.finfo
302e2e91ab2dc221acafd0c64ccb5e54784c690be245cc63692a32669dc5b3b9 26146 bush02.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush02.wad.finfo
a926798b61755c222f51484a143301fefa9be6d34ae8621661e08615d8c59927 13457 camera.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 camera.wad.finfo
a1898729b27235fd1d4387366311be744e54584c9aa65db964eab7bfbba63bfe 58072 end.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 end.wad.finfo
c5efde57bf036becae1c99422055ba22471c61441d795cbd59daa1229a7d6b13 27978 ferry.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ferry.wad.finfo
5d67be2b5df22015cdf36048c22158b056c7005b4d41a7296bb33eb0a2b063ec 34835 fountain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fountain.wad.finfo
1972df27aa799b0c8b70d276f3b4d26bd1f4de6ae059e1e5875cb79f8356d521 95387 gates.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gates.wad.finfo
aa60fc70a48ed812b8c55f93f2f4d92a4bb6607caf6c6751c2109ddba899f769 1061 ground.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ground.wad.finfo
1b45e3499899f05d1d4bc89485b3debbfec02b48bdac76e5f4114c45f570db24 6074 groundc.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 groundc.wad.finfo
d2882157544a270a341eb54d2253724d6f5efa198b743b402a60e981c0df64fe 33400 lavspurt.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lavspurt.wad.finfo
e6fc6727edf390d8f993fbb4c71306b95442bbaad2ae67bf7a0d4c57bff6b452 10344 lights.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lights.wad.finfo
5751c2401a6c931fd66ca637e2f886a4bb5d207b6dfad426549065b05e581fdd 23180 lure.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lure.wad.finfo
089754fe419fad24196b89b5fbb1ed504ef08b006b14ae4f0a3e0a21ab18c0c1 128697 mamfount.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mamfount.wad.finfo
b676619630921c21f122166af311fc1757044067d17b68dd99477a1c867c01c6 23368 pelbin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 pelbin.wad.finfo
2555b58b3a6a611e030afab303c5892741a77b532c99c539b00fa030d91ff988 27271 s_plant.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 s_plant.wad.finfo
b94ece97883c4517ff8769006cf5672e3f408c9fc1ecf0238bb35d83e722ca51 30656 seaplane.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 seaplane.wad.finfo
fc40f9d9c69932552eac8c311d60007bf4a873b71c04b12354f1527ce70f3ef0 43774 sign1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sign1.wad.finfo
cb87059fe35788d79a71439031925424d62ffd563218b5ccf4f6d5f1e6c24231 188635 speaker1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker1.wad.finfo
782cbb34ac2b6dd22d87db0f104aeede6c8862e4acfa436a7b74b9760b512081 8889 speaker2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker2.wad.finfo
e00b898703b700810730a8e5c5d6fc2b7727ba976a5c215919aa06263b07771b 8889 speaker3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker3.wad.finfo
38df97a5c204e2dcca1233a224b8ff909b1aab1a9367bb265f5c281e6e76a578 8889 speaker4.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 speaker4.wad.finfo
fdbf63c0750184b3f52cc69eb751db3d6a662807c2e457b33f121dc1dafe73ff 17835 staff.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 staff.wad.finfo
351fce22743470b5646f046974587453fb8bf6d74947bc1b2008f370b8349ce4 8430 statue1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 statue1.wad.finfo
64888cde8b290acd279a23bbd10e54c0aea7a3fb79c36808f339287ee8f41eca 9102 statue2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 statue2.wad.finfo
a4f458672e24e343b084ec6604732fc17e207e26dd7c59960a58586eba4ccc83 27902 supbog.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 supbog.wad.finfo
5c81c263fc4ccda08fb3c29ea7608f5d5ed3970ff9e3590cd332374c93abe527 19985 toilet.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 toilet.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
a46eff78223f380d49b100b5002690b6552e52e1390fa34c6406cd7f96b68406 307 global.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 global.sam.finfo
242a944a56790d50675e16e01b5d2ba05f5979cafb861c67a0622ab36a1ffc96 225984 hoarding.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hoarding.wad.finfo
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510 35202 miscmesh.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 miscmesh.wad.finfo
d4955c8b99187af0d356b3bad9c39c87a4f07f4607824a2b058e8b699bc27770 99066 queue.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 queue.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/rides/
cdd38445f8c17b936691acc2eab119c8aafcb5ed2461ccedeec1b42abff715fc 178 Online_Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Rides.sam.finfo
41efb301738461f0215f17b45318ab0113fe42d4d5f9f3ad9e698c662f6f7c00 3596 Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Rides.sam.finfo
3a92cc91a8bc32ec8d5e6359b07d79a126ef973387d32ee06a4681d272bbddee 105748 _snake_1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _snake_1.wad.finfo
d15c4b99a3f6180926d47ca188ca645711ab2a57947e18204ce4fb614986ecf0 95723 bouncy.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bouncy.wad.finfo
2d18beeed28a70fe42860c1c0ef6af81300e5d913e1ab6086414624362a2952f 81145 bumper.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bumper.wad.finfo
f6867d29c998aba2f8755102a079242fcb6f96773a7667ebf5d07db41275f61c 335179 coaster1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 coaster1.wad.finfo
2b3d165caff3881b0fdbcaeb1abeb8028bcbc752e1726f1be18190fbb64dc83e 251570 coaster3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 coaster3.wad.finfo
f6c5e454c864c86e0ea946a00e2ca962f20d4f067b26b37fb28805782b392c8a 195682 gokarts.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gokarts.wad.finfo
966537d08258bff92eff1292da62ee4befa903028c1381e2ce9d2168166671c9 188447 incagod.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 incagod.wad.finfo
2f71e7c76333a6b58f6fface7e0f8bea1f994562448469fdfd6388cd0eda5969 132353 lookout.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lookout.wad.finfo
c3bd8da541d10e1d26ab46a8fd188387e427ce8557c5466fbfecc306cc903ef1 300458 minecart.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 minecart.wad.finfo
3178e6f7e2d84cfd61d26da7d6796e9ce5107b580ff7a6ceafa2b89b92b75f19 195920 monkey.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 monkey.wad.finfo
8290d3e111cfe9ba47d82e05401c2e71c34124a5b2e84fcbd10c5c6743f2ba91 201524 mumbo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mumbo.wad.finfo
c69aa17d2ce1941a6d896aa6071fc32f956e38a5ed78793078b560ba5c79a47e 23227 mystery.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mystery.wad.finfo
7e4d1a8e57d2d846780b63c33802cee83413b123beae2fdd2a61ebc8c3d23bef 88966 porkpie.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 porkpie.wad.finfo
8c1fdcb7c346a06b5d53b99c2e94ac36ad5cfc6b575e2a30b65dd85a952e0f25 246167 spider.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spider.wad.finfo
2a5bc310bd66aa031ffe2c70283b3034a39559bea31efa535d64938a0dcf217d 184857 totem.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 totem.wad.finfo
10ce16a9c78311530d7e2af1acefe5833c9bb8d958ed968d3630f5b4b6f3006e 114128 tourride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tourride.wad.finfo
1805fcab52d114a94482e76766020566af6ed30cc5915a6d7bc6c8580d20b5e5 152049 tvsim.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tvsim.wad.finfo
d0008845d3040e3a61a8672f65aa39508704a3f2a641e9f9363fecab5f48df67 139897 volcano.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 volcano.wad.finfo
77c6a09f429440b9caa317fad44d1bb95270a394cf77b4deb3a5c38a444e6d64 225488 wateride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 wateride.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
87387a9baa4ae69b5bd2c1da32941969c8f4042f59557a22005d195a17e56809 590 scape.omp
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scape.omp.finfo
78f1dd31aa7717cd6963721f492d25c2ffdf05e441f5e162ba203d3f7868d763 671422 sharetex.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sharetex.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/shops/
0ba53ae24c7785fe2d72cc59a6690e6ca5e7f4c11ffae78de452b12040f920cb 3273 Shops.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Shops.sam.finfo
0a18780822cf32c610f1eb6bd67811e28075ffb057dcd6f7ef628e9fe8de14cb 63037 balloon.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 balloon.wad.finfo
24209d9742288af5a5dc2f0074919f87b341e61f29dda026489544c111548309 43475 burger.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 burger.wad.finfo
f10702985f5a09beeefadaa10fa4bd630d7e889e1c1cfedd338dd412cde48ce2 39282 coconut.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 coconut.wad.finfo
77de76cac71e8ef8e7aae41382629adedda0cf7343323f4bd402cf22cdbdb1da 80726 cost_shp.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cost_shp.wad.finfo
af124c07768210ebd909d22f878cd0e6ae4491b8ae9b1d7f8afcb1b9f9ffa4e4 46788 fries.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fries.wad.finfo
00e4c3d3d14f98e7ba01777f58698b1715e2052a00240562bfa7832c67223968 77431 giftshop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 giftshop.wad.finfo
ad429e7405e67895db89a0a2b372d4890bad354c9d573c94f4289622c77262c0 24684 icecream.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 icecream.wad.finfo
47289afac2802a4b3f342bee2023af271edd41a2b55984ecefdaa2c8a1ef164a 92953 steak.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 steak.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/sideshow/
35cb198555cff48c55588c8cd74734ab7b3fd5c463310c43fdef9bd2d7fc8ed4 3041 SideShow.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SideShow.sam.finfo
7c1ea99dbc427f85aa949f5aa7ffc8d40a6d09e45331b71255680dfa9ba0aee7 71644 arc2x3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 arc2x3.wad.finfo
850f718bff894d8f31b000675e92a0d3c9e3defc712eb1d7c4afe72594ab2dec 149768 hyenas.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hyenas.wad.finfo
dcc57666313a2835845e4f3011cf8701f05a3bdd08eb988a7a71f2df31e3fb70 106471 junspray.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 junspray.wad.finfo
5a5f62dcdaafda66bab46e0bc4c32d302210a614ca96b26d590943264c332b9f 119994 puzzle.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 puzzle.wad.finfo
27480d8ecec9c0e9b170f8d31d0740f1bfdb77666ce217981a379c7a71a086a2 82500 squark.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 squark.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/sky/
fddc53e0fca678139f80837d7a457caee4d116227370fd6eba49d162fe114758 262188 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
eae11045eb295c0102ff23fc2e6aa5f30ecd39d7414a240866faed4adbe07ed9 262188 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
cd1ce80746ce8dcf2e943fce1dc6b2540a51a8d628c9f9f542a455c4b1afd332 196652 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
2b2c59a447b13a50f3000761750fbadc66581f5e31abe9169a10f1c0227ece2e 91981 ssharete.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ssharete.wad.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/ssky/
37633644515a99440052eb492406bae5b5d14edb040cb280aed18082031cc4b7 4140 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
20da9d9b49d13599825ffd5a53250fdb01a271d752f1677144ba34fc78af9ed1 4140 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
67639c744f9267596a2285f0886b19fb8a03eb3649f6743a21a4b696d074d078 3116 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/
3bdc4181f6b0fcee8f5cd2d34a93a736d69604203a7a3d1969187dc8eb67b448 2516332 terrain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 terrain.wad.finfo
7dd227b4e5faa5c8cb3195e0afd00b7e7a69a83ee84b9da0b89be2e8dc6bfd6f 236 test.tpt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 test.tpt.finfo
= disc-hfs/Theme Park Data/data/levels/jungle/upgrades/
cdaa9edf248dbbdce84be30fe5682421ea69e5611956b6293e16b586940f76b6 2497 Upgrades.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Upgrades.sam.finfo
f62040454630857352e0dbd54707f129dfdd4f50e559316064f9871ae0614339 69119 lavajump.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lavajump.wad.finfo
cc62a4b88f5faf3d41fd9f1bebfd1c870d64cbbfea5e4acd022a26c798e25151 30959 mammtunn.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mammtunn.wad.finfo
9a431cb1501872c5dbe376202c8495c49bcdf3c11ddd3a04fee5b6abba04d46d 32881 watertun.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 watertun.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/
598d9795ea7eef59bb133f1c2570c0a5561dbf33fe8f00d213adcab4abc06f55 786476 2dmap.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 2dmap.tga.finfo
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01 879 EndHoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 EndHoardings.sam.finfo
f6e90e49fd1ab6e9c0f1cf6ab4e549e5569e215ab81ddd0ec0d1bd1f4c8fa311 859 Hoardings.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Hoardings.sam.finfo
= disc-hfs/Theme Park Data/data/levels/space/Music/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Icon\r
46d5376e26029f5739e0994c5c62cf646518334e2bdaee711818042afaa20ed0 10 Icon\r.finfo
40dc81615e0c31b2e4fe653a3c1a41e50530dcf64a4b2428ff8ae6b5770863b6 286 Icon\r.rsrc
2e41e7bb95bd12376acc1896745d5745d8aeb302582356037e69206be64f803a 31540476 MusicHD.sdt
4265db2aa4ae0c9cf96fc97b33b7fa1c2ae52eb9fde2595bf9f8390eb5dfe55a 10 MusicHD.sdt.finfo
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4 55 cat_musicBANK.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicBANK.map.finfo
6a7d513b3c49f5d88baf37e65f600bbb6dae6bf85a964c36ed80b1f7dddbdc18 2482 cat_musicSFX.map
c3b1fcc785c1dc9c676cb717a78188353631ca5c7a68e163d8408f5d73602eed 10 cat_musicSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/space/
2f58f53e5def0bf769da47d935e0a28f0a698abb62031301d20f268f2baaaa25 4369 Online_Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Standard.sam.finfo
= disc-hfs/Theme Park Data/data/levels/space/Sound/
d242028241e61b0d94ea45d8c783ddf140937bdd515e9f74bc789964c415e541 577682 AmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 AmbientHD.sdt.finfo
10044d56235dc8cddf342caf1e09e7d05a46a352b682161020a4742cea073f46 761557 LobbyMusicHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbyMusicHD.sdt.finfo
3dafd9214141b992a4a97b4339f6270aece20c16d7cc9d14245ca9c8998d387d 330418 LobbySfxHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 LobbySfxHD.sdt.finfo
0ca239e43a2cb1cb7a389ac4ada0fd585d62800d34e159eca391bfc4da122d9e 2077382 RideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 RideHD.sdt.finfo
e7cf534b02a7ae0c131d17e6d0ddd9a1fe8e35084ee2484e944865055324fa1a 87 cat_ambientBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientBANK.map.finfo
f7388c3f3ac3fffea9aaeb6be4b3645168fc63f1088c43039955c27fdcbafe2c 8558 cat_ambientSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ambientSFX.map.finfo
17f75adeb56002dcb14a12885520e802dbbc972bad72c8d0da519c7be8f39989 60 cat_locallobbymusicBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicBANK.map.finfo
968e3b60b5b41a094049924fd47144347d1f43ddb5cd22c47331b8ae7133ca93 162 cat_locallobbymusicSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbymusicSFX.map.finfo
69557911148205e4794bf32e592bed562e3da9b9b21f7a326c06601c0d8b14f2 58 cat_locallobbysfxBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxBANK.map.finfo
d81e275f865a8b83376f02a143e322d7f61eaf697778c672fd5cf3604c56172d 922 cat_locallobbysfxSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_locallobbysfxSFX.map.finfo
63d787319bc87bb6a67731b3c1b031f6e26daeab03064b7d8252da0dcc695d39 108 cat_ridesBANK.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesBANK.map.finfo
f1d841f35651914fcd64bacf3ec84cc79953c4212b55e0e880e642fd2f903f39 11664 cat_ridesSFX.map
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 cat_ridesSFX.map.finfo
15fcadb10a8683e7582054f5730ba10483b9f04a8ebe990de961ae8564bba657 170717 sRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sRideHD.sdt.finfo
a9f1cef630d9129b6e0d410dedb17eb8b357efb43af8507b92ec36008a919ca1 316905 xAmbientHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xAmbientHD.sdt.finfo
e9dd8685a323c9420f85e127553a06ec9709c86fe61e2840d259292f1c5e7365 398925 xRideHD.sdt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 xRideHD.sdt.finfo
= disc-hfs/Theme Park Data/data/levels/space/Speech/
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa 57 cat_speechBANK.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechBANK.map.finfo
15d014182b52b344d3338d3880f95e1135626eb01ae70a5c3fde1a77a1402183 130 cat_speechSFX.map
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 cat_speechSFX.map.finfo
= disc-hfs/Theme Park Data/data/levels/space/Speech/lips/
84c4ea6db6c7c997b0f5188c2ea90e41e645c4897b8a1e1e7982c8cbf820b670 64 sp_001.LIP
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 sp_001.LIP.finfo
= disc-hfs/Theme Park Data/data/levels/space/Speech/
f34efc73e573866e7bf2e8da5c784f0d178c9d21361564a8edc83401e0ac3ea9 129199 speechHD.SDT
d01759f4c0816f61cf693132a3e01e2feb898afb0cfd85223cf4dd4e6e6341a1 10 speechHD.SDT.finfo
= disc-hfs/Theme Park Data/data/levels/space/
2f69d590da9d5b14c4b277189a89a95b6447587257c879e07aabab79438544f6 5251 Standard.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Standard.sam.finfo
a3acdc09357b2b5b5a26ea97d4167d83f816821c0b22ba081f57215cb5f77241 101222 advisor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 advisor.wad.finfo
668417dd0aeeb262a0571a51dc89b0aab030a34e2fc2ccea080763ae7e80128d 1123 dynamic.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 dynamic.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/features/
9c6ca1ba29d33b2fa3093d0cd393312690db479eb0613ed6f505b20adc947115 2676 Features.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Features.sam.finfo
65338eb9b7eea63f9e00a2f88e58e6608fe6235b4f54eb9aff48302aa9b533fb 71097 _morpher_27.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _morpher_27.wad.finfo
e7a680f1aa75a1d2b613c0b5b7f45dc3ca20741e85309bb456c175312d3e56b2 19503 _pulsar_29.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _pulsar_29.wad.finfo
28b9df39b8eb3a0d9c1df6355136cec2f3c515c3c315e1afd7881a786bcbf081 155408 _robosta_30.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _robosta_30.wad.finfo
ccb0de6ffde6be826a2e0e8794b29d27099b9c4139313d59290e41c4fc7e3a05 97372 alien.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 alien.wad.finfo
8096bd7431d8888418e57cde5c23ebfd658807284f4ee267043128484a885602 74872 antenna.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 antenna.wad.finfo
7d9f5dcd0ee1ca75f63f28896fdf81481f5e707c55627be42ce86c75a1590d3b 29982 bus.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bus.wad.finfo
2412dd3bb953918434d6cd8242352ee9f77ab8d694f9baeb841b1b3c7d119226 4626 bush01.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush01.wad.finfo
4dfc566d106acd301205f2d48f28288c779826615e73c0eeec6aba3716d7dc87 24554 bush02.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bush02.wad.finfo
b794d3db3d07cd84764ce87f0827d7b990dad46e05db79be3753a908acb9172b 15451 crys_a.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 crys_a.wad.finfo
f42a48ee99f7b413628a56af0ad0571a1adabeebc785a7a8a8773d3c565f8e2d 39534 crys_b.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 crys_b.wad.finfo
4aa636170895a090e24d27f92dec3284e1a563eb74bd415c06ac122e310f5fc2 58011 end.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 end.wad.finfo
9d3e69d61180c05ec402209cea6760aa563121616f7b5601c0e8bc3798d92341 27916 ferry.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ferry.wad.finfo
a22c9b8acfe8fb73f3ee212d09b3ca34d7779a03626a0fba66f7232986d6aa12 82695 gates.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gates.wad.finfo
85c663dc7242ca91e7ef0b57435e01039bd83427917ee4cca4c0a7d3dc640ecc 998 ground.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ground.wad.finfo
b6f88896ba7cb21f2e97cf4017899ba85e2197195c6802b705c830aa96608dd7 6047 groundc.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 groundc.wad.finfo
bdc690cd960491dd6b8ee04703a14f4d901cd276cc1a1714ac763b1ca980bd98 22587 growth.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 growth.wad.finfo
78b77cdc2db470805a839f6e1f26052c1aaa8bb339a6a397801d4cf1d97faed8 67570 gyrotron.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gyrotron.wad.finfo
539b73d625bb9e926956814889fc2eaadce9acfd18a360d469f8739974e0f779 10281 lights.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lights.wad.finfo
3e9a40a8bcb2fe96f49f81958e3c28c56b5a164851325f9f926659c82c37d131 14171 loo.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 loo.wad.finfo
fce0eaa350b366a70f14c44381a68c7c08ab9c11d791be69e95a4246c36bdecf 52922 loo_big.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 loo_big.wad.finfo
092bc76dc08c7083dfe3f32cfe1751ffc9d4231d7b66f1f11c50325fb34929e0 24567 ltree.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ltree.wad.finfo
08e1bba9e6fc0a0d94bd60ed436379c0218be356dd3cdbd77890541c0b6177dd 26761 plasma.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 plasma.wad.finfo
80b57a42a7499524f47ff59e9d08244c78c25ef2b13d48577735acde42339f08 30883 rocks.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 rocks.wad.finfo
f84a0a3f36d300f9bc427babe7fc32b6f2bf55053a84b83a8177381e815926f8 9648 scrater.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scrater.wad.finfo
27e6f81b807b3b43aba74e314825ce4bd80c306785bb242914591a4836383f7a 30284 seaplane.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 seaplane.wad.finfo
0cb4799b972af5ec31ed55bcb2354db2e1b24f7b011fd7436792b50008885291 41961 sign1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sign1.wad.finfo
6afc1b7cf38ebf2e38133e3e1be63701832a28c88cf4d813f3af4143e5f51bf7 29372 slaser.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 slaser.wad.finfo
41d187a273f0301c3067726bf9e6492d86cfea7547b99c1efed23b59a60ae764 19497 spacam.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacam.wad.finfo
75a9db82df18a9bf1e9c5d4f16680328c8d949b99ccacee33b1a9d114245dcab 35460 spacbin.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacbin.wad.finfo
2d50b4fb3c0a683280d277b1be34cf6e41d2dd2352b6c115a6124c1e207e4ae0 4166 spacspek.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacspek.wad.finfo
fc7cab49d248b32a4e45ada8b34fb4c148dd27a1f55c1040e1c54fd7bae70707 4166 spacspk2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacspk2.wad.finfo
54322f08b9b2c72697bf174105b9a6547b6433cfffcc6627b67ce759e44fc126 4166 spacspk3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacspk3.wad.finfo
98447904e99ec12e01edd1f4a24de59834564046cb8463c49a7944651e7e3b0f 4166 spacspk4.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spacspk4.wad.finfo
b828e2002bf519c5e68100f2e23ed41a3e6a14432492d0a43b67a59e367c3888 49342 staffrm.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 staffrm.wad.finfo
780c7b4fbaaec513344ce9d4833e1540027be139ace28439e6e0cd625b91996b 37187 tree.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tree.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/
98185d758b077ac56c1fb60f0ee0f6afd0e0378e0a0ebad534188507bc1ac6a2 309 global.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 global.sam.finfo
8f724b7514b864582d53a04752c2584a7c5cb415edf3dce06f56d0d799553020 169020 hoarding.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hoarding.wad.finfo
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510 35202 miscmesh.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 miscmesh.wad.finfo
bf645dd6592aefda4abbe0c44c4c75a5d4660e0a8ae3d96e7237cc640a47721a 64453 queue.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 queue.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/rides/
f0d5ba03b980a21cfc428a4e6f217522c5dc93fd7ebc09ab6324598e41bc4b17 179 Online_Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Online_Rides.sam.finfo
ec9af893baf7e386e2f2156d765cf458db53f415aca8fba15e524be463e539a6 3567 Rides.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Rides.sam.finfo
de42e572a02f750cd435d88c4ebbde685d052a7ed808a5ac4afb8f596f54a9cb 343994 _wormhol_33.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _wormhol_33.wad.finfo
071c966ba78007c134039562b1304ce4f5837636d22b77090a143c7d031c2b17 190393 bouncy.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bouncy.wad.finfo
b5b1c4e984159505bc8c1494ae42cf52b30518ddc896c620cc5d1951597f9a18 180530 bumper.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 bumper.wad.finfo
79a8896b15cc06c00cb493baf3c7740cbf8c9d514e651cb36a0ef43bc850af4b 261751 creature.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 creature.wad.finfo
bb6e89e2b93bd3445d750fea0f827c921e4108e7b983db2ae5419fe6189b7dd3 220062 gokarts.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 gokarts.wad.finfo
8c9682f1c71c7a4088b1e6649f307896c725b7943a7c2b0c28cda3b9cbcdbc99 162001 hoverbot.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 hoverbot.wad.finfo
32af166e2b447ba2ea6071cdb736bac492412fc82ddb16f09c19b71f64103129 266560 mbuggy.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mbuggy.wad.finfo
c3c6f34612c57d47987ca8000d72208fc03b6aa1c144538975a757b3ec68acce 159422 megacost.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 megacost.wad.finfo
dec8cebb7d3a57278a99ffb0375b78d5cd1bac8dd702a0c6838ceb91a0ac240f 170057 moonshot.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 moonshot.wad.finfo
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5 23163 mystery.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mystery.wad.finfo
5768b5cfa94c4cdbd6e3e5b27d0260da68dcc440fd3d4c7ee27281dba90727da 108394 orbiter.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 orbiter.wad.finfo
c7408e1f489a9fc478811dced987170b6ce72362ecefcd1e6ea5171bbcf13380 173565 rocket.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 rocket.wad.finfo
ab5cf43a949ed5501c5d478018e572c2af0a528bce8eb6140d172daf855a0413 164504 scentro.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scentro.wad.finfo
1d6a66eb387b5deb6d86fa77f4fa51e4a8d7329d8b6a4a155749486a0602096c 148493 scitour.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scitour.wad.finfo
4cad0835de37ecdb057c5dc60076b1bd08fa93afce03287b079edccde37ad8e8 86782 slide.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 slide.wad.finfo
dccd9e6a107bde3aa5e0cadd70dc79b521bc0dd2ca40db6668080f07c8306f1c 130418 spawheel.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 spawheel.wad.finfo
1c42b4bb8dae71b521282171874776336b8c7210509bbeac1f32efc81e269b92 118703 station.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 station.wad.finfo
ece8454f393b7c7f88b8b3c5193fbc69261ec6b22abb77e8b15754e94c2434fd 255942 tv_ride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 tv_ride.wad.finfo
f14937d7ed50bec1f68059b8ab720b782ed3a6ec32bf7fd25954617b2bbb0a7e 208890 wateride.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 wateride.wad.finfo
2a1a14a7176c5c40b9429fafc3db53c575afa356871d8df3d58698a646ee630a 167637 zerog.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 zerog.wad.finfo
c5e4c1d80367ca3f59fef91b312e6c8fffdee489c919a012a6b84120b7aa1927 76564 zob.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 zob.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/
e2f6a2ec89a43b14159386219e2026c915c48649a483faaad8fca9ce43d28bbb 409 scape.omp
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scape.omp.finfo
ae39ca6710984d355abb5ec774be92fa371b6b8203919c585dbd333205fad827 460262 sharetex.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 sharetex.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/shops/
7f9af9b71e13ec0be031de6c8da01f3c3a0f89411b7089a1bd9d06b0682ea60c 3174 Shops.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Shops.sam.finfo
5bee8c5280d07e877019341d9dd7cc09f04db8cb5628ed10216963e059eb2263 51920 burger.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 burger.wad.finfo
bd248eee5da8073d2ee74117b510e4c395bf0f072b225849d5060ff5afcb4cb7 74816 costume.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 costume.wad.finfo
f210d508b9162993afe75206a8e483e6800743fbaa6a80e0f5ab1a6f2e5dd812 68238 drinks.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 drinks.wad.finfo
c166c27607c9ff36fed1633e0da883855a904e060ea81b02262435ac0153a580 200244 droid.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 droid.wad.finfo
87911ed3d29ac875bcd52f573eb28a8c29a6b97af29fd41d133b020251eebe2d 66958 fries.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 fries.wad.finfo
a18c0902416c6fc738987520a573f070af68ebf62fb8a82f69606b0c60523aa0 84281 giftshop.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 giftshop.wad.finfo
14004dbc36cf16a1817f75e3ded51a7da59b069086ce45b0d83bd38e8511fe0e 27337 ices.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ices.wad.finfo
9e472e19dd3580f8fde9cdfcdc950df1c8ed9a2df3ad3e1ab35cb510b9d91b99 147078 mrcafe.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 mrcafe.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/sideshow/
95f9745960c12ae9cffcfd2b0c29c6144fe3116aa1aa3544ea64744849f386ca 3019 SideShow.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 SideShow.sam.finfo
0eaaf80f7d67e11f64772b57ce5959826db7e490156e429f6e7bf2b445c1ca2d 113068 _strengt_34.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 _strengt_34.wad.finfo
f2fd1a4daa3ad9871e2244013af3d1391b990ccdc1a1cf11e08a3139adff650c 73526 arcade.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 arcade.wad.finfo
9c1aeca04ee34d63c5bf96fdd9b619d3b4d4eb6e4059fa9d6d5d71b61f8ed415 142068 lunar.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lunar.wad.finfo
9b41332fd8df5dab5d9a065bf64b10e2701f67adb451480b944807c7c9764500 179926 marsmoon.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 marsmoon.wad.finfo
664a1e13897ce620705d556baf2c2016f3ed5970bdb1aa394ccc80feadb88dca 180598 scipuzz.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 scipuzz.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/sky/
3eef8a967dc3020e79b7620e3b20fcfa9ce1ca8bd9f421a2b887ab6583731fb5 65580 moon.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon.tga.finfo
f65a246a4ef8786d06e53d94ba9627e15414ecaa54d855f1ebff16bacc36ada6 65580 moon2.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon2.tga.finfo
cc8c403b8a6c8c52840181ea894a733e0f1fbd5f265b76fccdb5708884e107eb 262188 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
9f9e31235f5647735d166ddbb5cb7f17d8b0e38e0ba89dbbf50e8c6f9cc37a1c 262188 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
e268f84fe447189d5e4c7967b11a016f8a894e39586d994d4d44eb26da09b268 196652 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/space/
589b730dbf16a495ce6484c4d683b0e5881749b62b9b5f71b3d5b6771847ce56 80178 ssharete.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ssharete.wad.finfo
= disc-hfs/Theme Park Data/data/levels/space/ssky/
74f74f6fba7788f83ae802a520c94229df31d009675ee8a0a1feb81b94fda9c5 4140 moon.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon.tga.finfo
6249f645a312f6f2516d974fd2e7e98ec483d29a0c2fb749f97ff9302abdde1e 4140 moon2.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 moon2.tga.finfo
696787e6e7b519d4dadce89a38d0b644eee72c579ef3cdfa276f1a8da56e53d9 4140 sky.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky.tga.finfo
f5e245f1a7de4c118c603ba1e4d1e54fe9de40aa7ac23f5c59b2fc9cf6e12956 4140 sky_cyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_cyl.tga.finfo
2a6fae4e27d2b2dc1825955f21d6744e5ba81e8222f4d9143e1d6e20b0e734f7 3116 sky_rgb.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 sky_rgb.tga.finfo
= disc-hfs/Theme Park Data/data/levels/space/
6006ddc5cf9797e82da466fab123c62e10849e5cc65b3e10c23d7d2ccf171f88 1943592 terrain.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 terrain.wad.finfo
7dd227b4e5faa5c8cb3195e0afd00b7e7a69a83ee84b9da0b89be2e8dc6bfd6f 236 test.tpt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 test.tpt.finfo
= disc-hfs/Theme Park Data/data/levels/space/upgrades/
ad16bb1e2e50b87a3c5c8796237c8b2a24ba5043e397797e292be30a882d6496 2494 Upgrades.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Upgrades.sam.finfo
771d5813c2140e43e428271b4fddd09e59110c1982ea13e337842d2751ddba53 54102 meteor.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 meteor.wad.finfo
578d65157183b32ed1fec8c3a6c5e8638ab79f86b5d443397e5896047f83569e 33061 teleport.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 teleport.wad.finfo
b9cbd1acca710b78698939779901709431a10f13f2e99b23e58582095aa81092 42212 zob2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 zob2.wad.finfo
= disc-hfs/Theme Park Data/data/
b9afda6264961021aecaca882adf9ce25aa6973f48111415176ed45afd2c9ee7 1458164 lobby.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 lobby.wad.finfo
9506293a836d23481665080d1da00d7a449b200a6f69b92e5cccaf167c30b4bd 2122 low.sam
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 low.sam.finfo
b052aaf32721382b646fa4dcbbb6f506b0439231eb9cab836a33ead6e10bf084 2126 med.sam
868a783587a13610f00084933a98be7383e7b9c9fa97d5e9e619bed1e41b4bdc 10 med.sam.finfo
81adad48917cb0025d5bba9d7aa5b236e744fd9e926c2706a0079ced4b4128e8 410 med.sam.rsrc
abd85ba2883670995854fee2bf0bd744a27bb83fca5700fb3d17ba7ebef4837b 700 online.sam
868a783587a13610f00084933a98be7383e7b9c9fa97d5e9e619bed1e41b4bdc 10 online.sam.finfo
ccd9a9a41c82cf01781fc29bbddb32163ebccd8beded4054f8b5423070cb4216 410 online.sam.rsrc
= disc-hfs/Theme Park Data/data/postcard/
d04a3b8d2f22369981d7a9373be2ebcb4657f4352f63cc9400ba16e4faa7c763 15444 bullfrog.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 bullfrog.tga.finfo
c2a481b14bbaafd30ee4eacc050ceb13033b40c4298eb45cdbdc75372669b018 46524 frank.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 frank.tga.finfo
b225505df268a84604f9786ec32b1883ab0f1be7dbb760c6d8fe4b817fe17f67 24136 legal.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 legal.tga.finfo
2e1cd000dafdd767c1d6a8ad2ede666449cfc023c45d8854f1463360385d5b14 35764 stamp.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 stamp.tga.finfo
25b3ff1cc9df21710fcd3b9bd4eecd7cafc9dca29508717fe2076ae3fda639c6 33644 tpwlogo.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 tpwlogo.tga.finfo
= disc-hfs/Theme Park Data/data/
112e6b132ca7f0590aca02413d8f9bc2ae09bd4ba5eb565cc6051b27c3eeca5b 1099 sound.sam
868a783587a13610f00084933a98be7383e7b9c9fa97d5e9e619bed1e41b4bdc 10 sound.sam.finfo
32905f065abd6f127bc7fbc03a220919caaed09fb3ac5952310b359e9d7cc7f0 410 sound.sam.rsrc
5d710990242a9cc52656d282a50af77257dcc967e1d41985b130576150baa135 1697921 ui.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 ui.wad.finfo
= disc-hfs/Theme Park Data/data/ui/
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0 Here2000.txt
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 Here2000.txt.finfo
= disc-hfs/Theme Park Data/data/ui/cursors/
0aa332152fafd6dd97e48e4befab23baafda1d535566584374f15937d20f90e4 12332 Cash.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cash.tga.finfo
40cf8dad69971b7fcc1b507f47ac07cd4a7d0d6dcbd0e4d47a740ff6b2aacf4e 4908 Cbus.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cbus.tga.finfo
3d22001d36aace08bfd8877c96498f40a1a65715df9434add84c3b70f06c0976 12332 Ccam.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Ccam.tga.finfo
ca64c922f1ea86f58be886fcd45f1569ff3638f672343207a7e76cc555aff4f9 12332 Ccar.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Ccar.tga.finfo
81de379dc42e4b76e1b63f0f8b88319f5d8216166e958af61230322e90519acc 12332 Ccro.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Ccro.tga.finfo
b2cc119264c5d57b61c15e039ef751d8e2ee1fc6ef32ec85986c103e3d482339 12332 Cend.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cend.tga.finfo
7195d4eedeba61cd719535e2c2cb2eeb5694714fb3a12b7ff7418195a033474f 12332 Cera.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cera.tga.finfo
30a36dde49fe277380dcd3449a8255c9ee5aab28f44bb3e731a9f82d2fe2185b 12332 Chei.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Chei.tga.finfo
2decf58d5fd2dcf208b20fcb2154d3c8077a76eb894331cbe5f64d6b5dd5b311 12332 Clin.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Clin.tga.finfo
029446e318d64b083d47ae53edd5e9376f3087fb11749ee0655cf18a8244e72c 12332 Cmov.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cmov.tga.finfo
fb3c7dc5507951552fb45ac35c3a54f785e2d6366dc60cde8e683607c11c9b19 12332 Cnog.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cnog.tga.finfo
2a7dd336ff0793d782ed325ae4cda426feaee246e4990d0934acd68bee330fb7 12332 Cnor.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cnor.tga.finfo
8160275504151d7c9f675192a792f1815c8de1b5226cfe1b07b234e8236d3f0c 12332 Cpat.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cpat.tga.finfo
8671c316e9b5f80e77936a5eafd8ac020020bbc54951275f27c0804eb9704261 12332 Cpic.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cpic.tga.finfo
90ac2ac1885b861a45f038d43ce8f3b31c0cf98c70d634ad1e8bfcff04f8aef9 12332 Cpiv.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cpiv.tga.finfo
3debfcd6732b4932698ccbbc92c75097014c1f5f554a26a342e7ed5c5f0a0a5d 12332 Cpla.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cpla.tga.finfo
7540e077a11ad9afc3a626214c5c2123844110bd0061bd59ce8675c889119317 12332 Cpyl.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cpyl.tga.finfo
340c80a7cb17db2f37abb561404531443498f2877f274451cd892b8e99a32e8e 12332 Cque.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Cque.tga.finfo
e36c66e6c16cb3b0e72abfc197ed06ec51284626d6eee21a24a455c093b8b7ea 12332 Crot.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Crot.tga.finfo
22d99b9c70f71b8036e94234182a58a6969c1cbd3037093ee57a33d83f62a187 12332 Csdn.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Csdn.tga.finfo
01b33e79403ca483e7cf6607c58edbb0efc7396460aff194868858b69fae0773 12332 Csup.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Csup.tga.finfo
1c5d2847f9927fdd770ce3c3d9b9f19932d50bd2f2e6298299850d7af52b4dd4 12332 Ctra.tga
addaabec7ca0957189cfdd857ed33cd603deef8797d02f296e1c6cf52cc3c77e 10 Ctra.tga.finfo
0bd0129aa928786f6e144afb9dd599eb1b7dfbede5d170598721271bcc4971a5 1404 c_busy.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_busy.ani.finfo
7f729c0c536c3f17534b1f8641ad68e3c2bd7031f701fdea800f48113720e201 760 c_cam.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_cam.ani.finfo
d078cc47bf2861f84e328540c63842950f06e40f9bdb0630cd7850cb900695cf 1094 c_carry.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_carry.ani.finfo
efa535bad66a67e0ca842bd57482d5cb5f8acb5317a03e0a4efdabdd02904e13 760 c_cash.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_cash.ani.finfo
2d22656235fe43be3f26f9819be528d70655f7b85aab2ed7b2a8feaea8776d14 1094 c_crosshair.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_crosshair.ani.finfo
bfa2d43da2460f87d6ab0168c0a377211743964eab00f6e244d1355e83753422 426 c_end.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_end.ani.finfo
c534cb140d9a0fca73af3fabbb5fee2c6db6ac1a8f6094f7cf484fbb7e627517 1094 c_erase.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_erase.ani.finfo
266c49c40281ff199fa9ffffea45f02cb7721b81672775a7fe3ecb86da9453f9 426 c_height.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_height.ani.finfo
357203757e19fee4c03f32bd6208b6d843f68827f1899e212725ce89e0aace14 1094 c_link.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_link.ani.finfo
0dcbbac4223dbd1ac4d9135223047ab1472f22d3e0e55f19d4b2b70bdf78329a 426 c_move.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_move.ani.finfo
7a7089560d315c18b36a72b1c85e7126e5c487f97abe8b872b7d48137f3047bb 326 c_noplace.cur
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e 10 c_noplace.cur.finfo
e4e3d30d73547a546ef3766dd005495e3915466df7b80872a2b28901ff5085b3 1094 c_norm.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_norm.ani.finfo
6c7908a2a8a7ac4889b2e1bb66d09b775da3905750f6a06a631544f3675fb252 1404 c_path.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_path.ani.finfo
b66c4f72a012dd1505a1578f48ee6eab1acdf50e7e4bc5117da920c73a9ba260 1094 c_pick.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_pick.ani.finfo
54d4703230cc52b3ed4f0a86479fb893b22af974eb308915430d9c9a397e2553 426 c_pivot.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_pivot.ani.finfo
31f3316d722cdd78baee44db2274ff2332faa6ce578629dd4103a6c03a950fef 1094 c_place.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_place.ani.finfo
1123cdac8eb53613387caa60f6e2ffb71c6d5532d3439098e1df3fb3941fde37 426 c_pylon.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_pylon.ani.finfo
cc71fdddc085d4d4e710c1d3675906fdf873ccb5d2e8468471ab4d5ea9f37702 426 c_queue.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_queue.ani.finfo
aa28281e29358ce92f6dec96f0e34496849bdfa9efbe91b1c6ebe49b5c3d6687 426 c_rotate.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_rotate.ani.finfo
6f183263c59a41f293c88aa5539ef36781ac7bef05421e0d48b9b16755ac06e0 426 c_swapdown.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_swapdown.ani.finfo
545c52736b017dd497b0c9bd9eb1af1f92121387ae687326e688281ba0492e9b 426 c_swapup.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_swapup.ani.finfo
6396708e88e5f20be8c6f220d28280d3011fb050a2cff7496300e5a20617a3b7 1404 c_track.ani
65450dc60bf3b71d88ac8edb9e686ed67030e29089a4412771ce13c661c5e22e 10 c_track.ani.finfo
= disc-hfs/Theme Park Data/libraries/
b67b56b7b2b75962b8b34559e20fd97b623b2d0f7f08a0035f82072ffbf4ec06 433323 bullfrog shared
62bc3643e268b109a2eac528ca9b4a261735d6c6f7928ee27ab00c9d60aef05b 10 bullfrog shared.finfo
05573db7984df7b64e79686ceeccbaac99f39081e40a009e46f3cfadfe78d5ad 448 bullfrog shared.rsrc
5e04f9c00c922dc78a787d1b93067c75d37a3e65b0a0202e50c2f449f131b27f 243544 c:c++ shared
56f6bf57b8c41034961738874a98bfb087e3590ce188757d3c4daae943fe385b 10 c:c++ shared.finfo
515ab96d4abf84609c06cd74ad3538070915f526559225e38aa4f4ab4b1daec4 451 c:c++ shared.rsrc
c549123f647dcf33c3e2c3d515bffe2cfcb19d11680f743f02ca42bb09dbd73b 306581 engine shared
a133be405b9c1da329a59510fcc57fe1b22f3ed2278702ba8e01bf236526f658 10 engine shared.finfo
1a03b2fe82abee57165bbfaf6a680bd5cb8cc1328b2f7ac63bbdf725558c20ee 448 engine shared.rsrc
cb5473b799b4c6d4effd11e00a8eaa8fdc6aae87a00caaadd4368de5d56574a4 30141 indirectx shared
24d4a83cdba10d858bf47639be6bc20a5e559df5304281328a1855983032771f 10 indirectx shared.finfo
910886e82169d5ed4caad2f5ca25ea3383cb305ace04f96d1d6e66c2f8397aed 494 indirectx shared.rsrc
72603ebc8a31b5cf17a611410228a95538c63c3faa6d50b13fff6353594057eb 153012 libjpeg shared
d127ff3c8d93aa69dd281644bf4df0086210d3d83256754cc0c06e3a406692c5 10 libjpeg shared.finfo
5a5bb72ac5278c2fff7a1d529f8f8515bcd462deeb9f3034b08c521b70312e96 477 libjpeg shared.rsrc
2b0f7ac92c1f8b67761d271dd5832fca1bd19a8dd8d494b7b89b6ffa693e6ee8 71418 ltms shared
6f4022e4faa492d7ccf8b0d0dbfb361041dbacbda9be79d7db83bd9f46d7db88 10 ltms shared.finfo
f993c35bdba293a8798f78bc272469d40fc87a64345bbf53718b137f47f3fa09 444 ltms shared.rsrc
ba11331a70bce77140ae6e7fe73145fe4e13cce5f042707a494cb77654b32f0d 68604 macdoze shared
fc54dbc7b8634ebc2b97ffddb314f9a8db99a8c26f1e72885441b936dff8161f 10 macdoze shared.finfo
c57a5016d868bdbf62bfffeb5117dc2377e5e99f833ce04b6f14362c0b85ae0f 494 macdoze shared.rsrc
c27a22a457469077e5c832e2f29424ba3b79fa815c62a37d0f34e337f327ba67 15787 mail shared
a133be405b9c1da329a59510fcc57fe1b22f3ed2278702ba8e01bf236526f658 10 mail shared.finfo
e405759638fa5b4529c6842b803781588f51ed045a0abf7a93d47cf04dd89495 444 mail shared.rsrc
27f3e7c03e97ee1bc305cd767ce07bb04199574c2d64a84b9b7e3904767bf506 46299 more files shared
01c70c325d735fca4785a2e840d3a8752197f44a1e6ad275ccdd1be0a09ff7ec 10 more files shared.finfo
00ba9666e47a3440ef63d2b62577866834bf127ffcf75c1334bf24d5a1d66e62 534 more files shared.rsrc
05c21d2eda5fa4b800859a874ab986433adae2e303de230476329df6c019b2cb 69816 online shared
a133be405b9c1da329a59510fcc57fe1b22f3ed2278702ba8e01bf236526f658 10 online shared.finfo
56c5788ee3f343c066b238736422f4614edd07c8cfec61acccb36835c5a33227 444 online shared.rsrc
18af42bc9ffce8a11c0a03815ada9518386f7634dc09e860799093974ebc974d 17309 runtime shared
989e14a3035273139d88d6b60baf026a42ae0480da066eb2354ef57be0e293e6 10 runtime shared.finfo
ae1760f6504d807d22d66f4341da6e04d2fc057d6cd0f08858454debe0d2da1e 455 runtime shared.rsrc
1959a54b2280c95ddcc25ec77c070b4d609dcca7b7e59683b460c92c7297bcd6 196487 sams utils shared
2585c8fabb1ee4fb93385893f583e693a6d8628a4fac6fe4f1fe9302a90c5ecf 10 sams utils shared.finfo
ef0c76df7eb31438856ea4557e60e8fa0ba8732b825d464de9318bd1a5be7f2c 119559 sams utils shared.rsrc
7132c2f1d772de25b458b9c6e0303e130e6c536d7650a9d2cde5c8cacd6bb94f 232766 sound shared
2d01c394e6a320c4315997dc2e37dfd3bddbd1580dfa5a084e56f84fdf9f1404 10 sound shared.finfo
55eff90c442131210c047942756a1fc4a801c9f0dda5536fc5906658b18c36a8 448 sound shared.rsrc
459f84d3980c3abe89b058472779424afea6ad4c42e6985c17f914f93c0dc55b 61415 winsock shared
8e8860f4d232c0b8d3f428f5c8e75b464bfa911548949ac279aa0f2436a9a810 10 winsock shared.finfo
473d8ef4de50ceae272696cc5cef7431f6c874c42511cd768930c6aee6b649a4 448 winsock shared.rsrc
b5f3195f1e5d0022af5453b8c721f24e9aa4156d02b2323bb62ecf65dd52a895 49644 zlib shared
e250666ff6c55c172d14d3759a280ea7dc78dec6d0ef750c4d28ab0166a4a636 10 zlib shared.finfo
daa60516fa5a248010f10c614eb24c356fc6b5bc6b92dca2cef1f06ca9dd3a4e 495 zlib shared.rsrc
= SimTPPatch_204b3/Sim Theme Park Patch/
1a2272adbb4acf8cc5f785afc37c8950fc8e59ce44be216c76cf1ca77e84a9d9 3802 Read These Instructions!
= SimTPPatch_204b3/Sim Theme Park Patch/patch 2.04b3/
29da2392beefc1ee00b097536b7f1b202044cbf0743c0eb3a6235254fd318000 2746951 SimTheme Park
= SimTPPatch_204b3/Sim Theme Park Patch/patch 2.04b3/libraries/
34f9f29f3bb55c437afbd186e66841e026c35eeae8bd534c988ea299a0314833 502655 bullfrog symbols shared
e1d5ea9d2842426b58848bf4557c22c91b7c95f4a43f7bb1934761785fce25a5 287108 c:c++ symbols shared
a949b616f7074293607d290d031ef226d83ba1ca1377f2f017709030e49ad9fa 347129 engine symbols shared
d62003f3c77a9defe11529dc9fc19843a3b5992b8a6694b8b83f91b8795923cd 40732 indirectx symbols shared
16839910d6a1dda14139042c474da08bd8a14165d64d0791a36f77e155854e3f 175049 libjpeg symbols shared
1d1cee35232991308a12d97fdfbaa0faf2d9358ac97af5118362a85685ac075e 86530 ltms symbols shared
ddc5e725ed78a72fedbf27a0c5875146b5f3f86ae14ec7b97611ee3c19878e85 89682 macdoze symbols shared
80b0d60096344164fbfdb751579e6c92582ab035b465042f99d3d7efb5bfdf2f 19547 mail symbols shared
4e1cd8c262ce11d58ef47beddb7ac0d8565fa9bd939157f64a24cad21e141e66 52795 more files symbols shared
ad7f9b06c136f0ab94554fbeeb2c580f927de04df4a684b22557e388582ed90c 91647 online symbols shared
12ad8b375f9283145248c58384489cd9a60f7bfd5f6e7eca56b3102149b03ae1 19596 runtime symbols shared
cd6635b77797be9b07d35c882da29846c53ee8e2087783c003e6bcfa99c827b8 190814 sams utils shared
3fcec0506e59b8228adc59e32c75460677cb28dbd9b5dc6324f2c005040b093f 247430 sams utils symbols shared
b251858a70721b14c944cd946cc19f450dde42dd1a03fa3fe06050dcd392e4fc 317587 sound symbols shared
43ffc8066b29998cf8d95f5cd98758c9fdb2db67eeb4f4dac9ab25704140fec4 69239 winsock symbols shared
f55ce39c8bcb78cd3b71a9e45a4ff8e9bb29913aa829dce7de47fd69dd385fe2 52492 zlib symbols shared
= Theme_Park_World_Bonus_Stuff/Theme Park World Bonus Stuff/tpwfnt/
b0ec3340b2521e59e8fe67534508f6284a7cdfba13c191272f4cc3970ac72c60 60332 BIGLA___.TTF
03d234a8e80bf8ccf3b1da4b2909a821fc1143a666b615818000c6fbcbe11299 93368 BIGLOA__.TTF
3f3f3934b9dbdf45fad5ff92fe59b2d5b4157345a219be3fb3f1d449985519b8 59836 CLUNAB__.TTF
583e9201f0aeead00284e0bbe474887328faf816457c8767a97810636ed2703f 75052 CLUNA___.TTF
72d4ab6117f8f2d6c9a0a5a243ed91eb532327309e245e1cfd13e14d8fed5ffe 64884 EGGITAOE.TTF
03e930b9f91de5eea7c62d438a76573b8c1f25f8bf74ba96575373759bf31d68 52184 GARGA___.TTF
68b3e8ae58d97a611cc5e5adf4b2a0459502385bdce70022b7064575b920aec2 54436 GARGSAI_.TTF
011545802224ef6e22f024bbaf6e761437e68a9e7ca6a7b023150ed2109a69bf 52288 GARGSA__.TTF
9f829459ae2e1ad4c352d725523fa376ec61ff3fbbac808de83732597b8f992a 125568 HAUNTAOE.TTF
b5d80c96377170a5f145dea96d5326eae1e93fc3e5f113dfcf7a6c1839f087b8 66652 INTRAB__.TTF
c3946e29d0505cc07fce7f0cc4f232e23f04701d15701e1f2f41ee748ca0a0d6 128712 INTRA___.TTF
f3b744fb958623baa95bf5d1527051c48cdca243e878c4bc89cb6d31941f7a52 72884 KRELAB__.TTF
ac7f4ab9b38440a9b60a2fa3ae3f5ab034ead0d2ad026ee2f35aefef0a60d63c 73088 KRELA___.TTF
76af5878f5446f9e03c744fc791ab27acbda6b89613bc42d02914c0cfa2d8bed 145604 LINUPA__.TTF
9cc389f75ce034df3970c8f38cd0c19e167a64579d77f1fe93a1b194f04b5097 77964 TANNAO__.TTF
10ea17e010c03076f8476b67e91249e27235f09c227ffeedefb8d36dfdd642af 76104 TANNA___.TTF
939ae2c9095f54152f5ac2b25c459ee8cf52c477bb1df83f231defa3c2de7da1 127456 YOUNIA__.TTF
"""

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
