#!/usr/bin/env python3
"""Make the Theme Park World (UK/European edition) game folder the port reads with `tpw --eur`.

usage: python3 devtools/import_eur.py --iso ISO --to WORK/extracted [--mac "WORK/extracted/disc-hfs/Theme Park Data"] [--replace]
       python3 devtools/import_eur.py --check WORK/extracted

--to is the folder tpw-eur is written into. In a fresh setup that is WORK/extracted, beside the folders
import_originals.py writes (which also runs this import when the ISO is among the downloads). In the monorepo it is
ROOT/game-eur, with --mac "ROOT/extracted/disc-hfs/Theme Park Data": ROOT/extracted is read-only there, and devtools/tpw
looks in ROOT/game-eur/tpw-eur first, then ROOT/extracted/tpw-eur.

The original is the UK/European PC CD, an ISO 9660 image with Joliet names, found by its content (looks_like_eur), never
by its name, size or a hash of the file. The image may be raw (2352-byte sectors) or cooked (2048-byte sectors). The 2000
and 2001 pressings differ only in the copy protection's files, which are not read, so they write the same folder. The
older first pressing holds version 1.0 data, which is not the data the port reads: the SHA-256 list at the end of this
script finds the difference, and the import stops with the list of files that differ. The game code is the same for both
editions and only the data differs, so the folder is the PC disc's Data/ tree with the edition's own Language/English, splash, legal
and welcome art, speech (the British advisor) and logos, and the US Mac data only where the PC disc cannot supply a file
the Mac code reads:

  ISO Data/**                         data/**  (the whole tree, Joliet names, "Data" written as "data")
  ISO English/Meshes/English/*.MD2    data/Language/English/  (what the PC installer adds; the .mtr files are left out)
  ISO English/Filter/English/*.txt    data/Language/English/  (the name filter, also from the installer)
  data/init/W/Legal_English.tga       data/init/W/legal.tga   (the Mac code reads the unsuffixed name)
  Mac data/** the PC tree lacks       data/**  with their .rsrc/.finfo side files: fonts/*.TTF (TrueType in the
                                      resource fork), Movies/*.mov (the PC films are EA TGQ with the same pictures;
                                      the Mac code opens Data:Movies:*.mov), the Mac release's extra "_" ride and
                                      feature wads. Not Language/American, the init art or the empty Finder Icon files.
  Mac data/med.sam, data/sound.sam    replace the PC copies: the Mac release's tuning (no procedural texturing, the
                                      Mac's sound channel counts), which is what the Mac code was shipped and tested with

and written as WORK/extracted/tpw-eur/Theme Park Data/data. The Mac data folder defaults to the one import_originals.py
writes (WORK/extracted/disc-hfs/Theme Park Data). Every file written is then checked against the SHA-256 list at the
end of this script. Nothing is written outside --to; the work folder (--to/.import_eur-*) is deleted at the end. A
folder that is already there is left alone unless --replace is given.

Python 3.8 or later and nothing else: the image is read directly. A file name Windows cannot hold (a ':' or a carriage
return in it) is not written and not checked; the game does not read any.

Entry point for other scripts: build_eur(iso_path, extracted_dir, mac_game_dir=None, replace=False) returns the path of
the written game folder (or raises EurImportError).
"""
import argparse
import hashlib
import os
import shutil
import struct
import sys
import tempfile

EDITION = "tpw-eur"
GAME_FOLDER = "Theme Park Data"
MAC_DEFAULT = os.path.join("disc-hfs", "Theme Park Data")
SECTOR = 2048
RAW_SECTOR = 2352                       # a raw CD sector: 12 sync bytes, 4 header bytes, then the data
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
SIDE_FILES = (".rsrc", ".finfo")
MAC_TUNING = ("med.sam", "sound.sam")
INSTALLER_FILES = (("English/Meshes/English", (".md2",)), ("English/Filter/English", (".txt",)))


class EurImportError(Exception):
    pass


def portable(name):
    """Whether NAME can be a file name on Windows too (the Mac's '/' in a name is written ':', and the folder icon
    files are called "Icon" and a carriage return)."""
    return ":" not in name and "\r" not in name


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---- reading the image

class Iso:
    """An ISO 9660 image read through its Joliet supplementary descriptor (the long, mixed case names). The file is
    either cooked (2048 bytes a sector) or a raw dump of one data track (2352 bytes a sector: 12 sync bytes
    00 FF*10 00, the minutes, seconds and frame, the mode byte, then 2048 data bytes after 16 bytes in mode 1 or after
    the 8-byte subheader, 24 bytes in all, in mode 2 form 1). Which one is decided from the first sector's sync bytes."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.stride, self.skip = SECTOR, 0
        head = self.f.read(RAW_SECTOR)
        if head[:12] == SYNC:
            size = os.path.getsize(path)
            if size % RAW_SECTOR:
                raise EurImportError("%s has sync bytes but its size is not a whole number of 2352-byte sectors" % path)
            self.stride = RAW_SECTOR
            self.skip = {1: 16, 2: 24}.get(head[15])
            if self.skip is None:
                raise EurImportError("%s is a raw image of mode %d sectors, which are not read" % (path, head[15]))
        self.root = None
        for sector in range(16, 64):
            d = self._sector(sector)
            if d[1:6] != b"CD001" or d[0] == 255:
                break
            if d[0] == 2 and d[88:90] == b"%/" and d[90:91] in (b"@", b"C", b"E"):
                self.root = self._record(d[156:190])
        if self.root is None:
            raise EurImportError("%s has no Joliet volume descriptor" % path)

    def close(self):
        self.f.close()

    def _sector(self, n, count=1):
        """The data of COUNT sectors from sector N (2048 bytes each; fewer at the end of the image)."""
        self.f.seek(n * self.stride)
        raw = self.f.read(count * self.stride)
        if self.stride == SECTOR:
            return raw
        out = []
        for i in range(0, len(raw) - self.skip + 1, RAW_SECTOR):
            if raw[i:i + 12] != SYNC:
                raise EurImportError("sector %d of the image has no sync bytes" % (n + i // RAW_SECTOR))
            out.append(raw[i + self.skip:i + self.skip + SECTOR])
        return b"".join(out)

    @staticmethod
    def _record(r):
        extent, size, flags, nlen = struct.unpack_from("<I", r, 2)[0], struct.unpack_from("<I", r, 10)[0], r[25], r[32]
        name = r[33:33 + nlen]
        return extent, size, bool(flags & 2), name

    def listdir(self, entry):
        extent, size = entry[0], entry[1]
        data = self._sector(extent, (size + SECTOR - 1) // SECTOR)
        out, pos = [], 0
        while pos < size:
            n = data[pos]
            if n == 0:                         # records do not cross sectors: skip to the next one
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            ext, sz, is_dir, raw = self._record(data[pos:pos + n])
            pos += n
            if raw in (b"\x00", b"\x01"):
                continue
            name = raw.decode("utf-16-be")
            if not is_dir and ";" in name:
                name = name[:name.rindex(";")]
            out.append((name, (ext, sz, is_dir)))
        return out

    def find(self, path):
        entry = self.root
        for part in [p for p in path.split("/") if p]:
            hits = [e for n, e in self.listdir(entry) if n.lower() == part.lower()]
            if not hits:
                return None
            entry = hits[0]
        return entry

    def walk(self, entry, prefix=""):
        for name, e in self.listdir(entry):
            rel = prefix + name
            if e[2]:
                yield from self.walk(e, rel + "/")
            else:
                yield rel, e

    def copy(self, entry, dest):
        extent, size = entry[0], entry[1]
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as out:
            left, sector = size, extent
            while left:
                count = min((left + SECTOR - 1) // SECTOR, 512)
                chunk = self._sector(sector, count)[:left]
                if not chunk:
                    raise EurImportError("the image ends inside %s" % dest)
                out.write(chunk)
                left -= len(chunk)
                sector += count
                if left and len(chunk) < count * SECTOR:
                    raise EurImportError("the image ends inside %s" % dest)


def looks_like_eur(path):
    """Whether PATH is an ISO 9660 image (raw or cooked, with Joliet names) shaped like the European PC CD: a Data folder
    and the installer's two English folders. Only the volume's directories are read, so it is quick; the SHA-256 list
    decides whether the files in it are the right ones."""
    try:
        iso = Iso(path)
    except (EurImportError, OSError, struct.error, IndexError, UnicodeDecodeError):
        return False
    try:
        data = iso.find("Data")
        return (data is not None and bool(data[2]) and iso.find("English/Meshes/English") is not None
                and iso.find("English/Filter/English") is not None)
    except (EurImportError, OSError, struct.error, IndexError, UnicodeDecodeError):
        return False
    finally:
        iso.close()


# ---- building the tree

def _folded(top):
    """Every file under top as {case-folded relative path: relative path}, side files left out."""
    out = {}
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(SIDE_FILES):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), top).replace(os.sep, "/")
            out[rel.lower()] = rel
    return out


def _copy_with_sides(src_top, rel, dst_top, dst_rel=None):
    dst_rel = dst_rel or rel
    for suffix in ("",) + SIDE_FILES:
        src = os.path.join(src_top, rel + suffix)
        if os.path.exists(src):
            dst = os.path.join(dst_top, dst_rel + suffix)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)


def _mac_fill(rel):
    """Whether a Mac data file the PC tree lacks goes into the EUR tree."""
    low = rel.lower()
    if not portable(rel):
        return False
    if low.startswith("language/") or low.startswith("init/"):
        return False                        # the edition's own text and art come from the PC disc
    if os.path.basename(rel).startswith("Icon"):
        return False                        # the empty Finder custom icon files
    return True


def _write_tree(iso, mac_data, data):
    pc_data = iso.find("Data")
    if pc_data is None or not pc_data[2]:
        raise EurImportError("the ISO has no Data folder")
    for rel, e in iso.walk(pc_data):
        if portable(rel):
            iso.copy(e, os.path.join(data, rel))
    lang = os.path.join(data, "Language", "English")
    for folder, exts in INSTALLER_FILES:
        entry = iso.find(folder)
        if entry is None:
            raise EurImportError("the ISO has no %s folder" % folder)
        for rel, e in iso.walk(entry):
            if "/" not in rel and rel.lower().endswith(exts):
                iso.copy(e, os.path.join(lang, rel))
    # the tree spells the folder "Init" (the disc's own name); look it up whatever the case
    for init_name in sorted(n for n in os.listdir(data) if n.lower() == "init"):
        init = os.path.join(data, init_name)
        for width in sorted(os.listdir(init)):
            legal = os.path.join(init, width, "Legal_English.tga")
            if os.path.isfile(legal):
                shutil.copyfile(legal, os.path.join(init, width, "legal.tga"))
    have = _folded(data)
    for low, rel in sorted(_folded(mac_data).items()):
        if low not in have and _mac_fill(rel):
            _copy_with_sides(mac_data, rel, data)
    for name in MAC_TUNING:
        mac = _folded(mac_data).get(name)
        if mac is None:
            raise EurImportError("the Mac data has no %s" % name)
        os.remove(os.path.join(data, have[name]))
        _copy_with_sides(mac_data, mac, data)


def build_eur(iso_path, extracted_dir, mac_game_dir=None, replace=False):
    """Write extracted_dir/tpw-eur/Theme Park Data from the UK PC ISO and the US Mac game folder; return its path."""
    if not looks_like_eur(iso_path):
        raise EurImportError("%s is not the European PC CD (an ISO 9660 image with a Data folder and the installer's "
                             "English/Meshes/English and English/Filter/English folders)" % iso_path)
    mac_game_dir = mac_game_dir or os.path.join(extracted_dir, MAC_DEFAULT)
    mac_data = os.path.join(mac_game_dir, "data")
    if not os.path.isdir(mac_data):
        raise EurImportError("no Mac data folder at %s (run import_originals.py first)" % mac_data)
    final = os.path.join(extracted_dir, EDITION, GAME_FOLDER)
    if os.path.exists(final) and not replace:
        return final
    os.makedirs(extracted_dir, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=".import_eur-", dir=extracted_dir)
    try:
        iso = Iso(iso_path)
        try:
            _write_tree(iso, mac_data, os.path.join(stage, "data"))
        finally:
            iso.close()
        bad = check(stage)
        if bad:
            raise EurImportError("%d files differ from the list (an image of another pressing, such as the first one, "
                                 "holds other data and is not read): %s" % (len(bad), ", ".join(bad[:10])))
        if os.path.exists(final):
            shutil.rmtree(final)
        os.makedirs(os.path.dirname(final), exist_ok=True)
        os.rename(stage, final)
    finally:
        if os.path.exists(stage):
            shutil.rmtree(stage)
    return final


# ---- checking

def listing(game_dir):
    out = []
    for dirpath, dirnames, filenames in os.walk(game_dir):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            rel = os.path.relpath(p, game_dir).replace(os.sep, "/")
            if portable(rel):
                out.append((rel, sha256_of(p)))
    return sorted(out)


def check(game_dir):
    """The relative paths that are missing, extra or different against EXPECTED (empty when the folder is right)."""
    want = dict(_expected())
    have = dict(listing(game_dir))
    bad = sorted(p for p in want if have.get(p) != want[p])
    return bad + sorted(p for p in have if p not in want)


def _expected():
    for line in EXPECTED.strip().splitlines():
        digest, rel = line.split("  ", 1)
        yield rel, digest


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--iso", help="the European PC CD image (raw or cooked ISO 9660)")
    ap.add_argument("--to", metavar="DIR", help="the folder to write tpw-eur into: WORK/extracted in a fresh setup, "
                    "ROOT/game-eur in the monorepo")
    ap.add_argument("--mac", metavar="DIR", help="the US Mac game folder (default DIR/disc-hfs/Theme Park Data)")
    ap.add_argument("--replace", action="store_true", help="delete and rewrite the folder if it is already there")
    ap.add_argument("--check", metavar="DIR", help="only check DIR/tpw-eur/Theme Park Data against the list")
    ap.add_argument("--make-list", metavar="DIR", help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    if a.make_list:
        for rel, digest in listing(a.make_list):
            print("%s  %s" % (digest, rel))
        return 0
    if a.check:
        bad = check(os.path.join(a.check, EDITION, GAME_FOLDER))
        print("OK" if not bad else "\n".join(bad))
        return 1 if bad else 0
    if not a.iso or not a.to:
        ap.error("--iso and --to are needed")
    try:
        print(build_eur(a.iso, a.to, a.mac, a.replace))
    except EurImportError as e:
        sys.exit("import_eur: %s" % e)
    return 0


# SHA-256 of every file under tpw-eur/Theme Park Data (python3 devtools/import_eur.py --make-list THAT_FOLDER)
EXPECTED = """
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06  data/2dmap/esprite.tga
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06  data/2dmap/gsprite.tga
88c36d45c47b7c8594b8210e04e577ea7138c3e56cf268f174acee7c059e1c06  data/2dmap/lsprite.tga
6b8edaa068d9b20f07151969f80d94668e224a9e4c79285fc442ddb74ba0d3d2  data/2dmap/qickload.txt
baa01e03d376300b2910bbccbe92ffb8fac8e2ace8d43b8d1cca4186c3cf9a5c  data/2dmap/rsprite.tga
f4772f675811dd8f7dc09b9fa53b7683faa266e0c75362584208da244175f4b7  data/2dmap/ssprite.tga
f4772f675811dd8f7dc09b9fa53b7683faa266e0c75362584208da244175f4b7  data/2dmap/vsprite.tga
e905df3d0a12ea5d7580ce87a7dd3a6c7e690e2f072b9eafef8cbfdc88f00df3  data/Advisor/Advisor.sam
120fff68adc08120134787cfa24e5e6851dc33388c145dd11ee8a40bc4f4edae  data/Challenges.sam
9a95b317d703fceaf80a90ff2998e669264dcc45aa2af0b02751de48843ecbb5  data/Init/1024/Legal_Danish.tga
c9e095e781b0491e2b93c7cbb37463050510cf104e028711804eb187c706846e  data/Init/1024/Legal_English.tga
99ceab20e491f96c3fb06cc2302757dd7c0af3c4c422bfa553dc624d8e1e9b09  data/Init/1024/Legal_French.tga
a513c9520c974964482a09cc5fa730b408238ebfb06b3a3bd33a686ae7f2190a  data/Init/1024/Legal_German.tga
7b4d8594e663ac80ff0e0eab7b405cda2aba54aa7dab898083678b0649fe5446  data/Init/1024/Legal_Swedish.tga
64a05e3811900a627c030412a5b5772cda226b5aa8b724ead2d65bf8635a60e7  data/Init/1024/Splash_Danish.tga
628b0e062b9ada3f50e764ac497c8adaa729639a52e6f9f0e2e460236a060611  data/Init/1024/Splash_English.tga
83d7e8fa9a25c5c6d6cbd292aca279cd03c920d976df023764326804b22612f6  data/Init/1024/Splash_French.tga
4e4c893a10e15c54426c7babbe806fb954ad187cd4087f0acaea25a6d44c4af3  data/Init/1024/Splash_German.tga
b4a049b6655112ac2d7103a02321bcd8728b41b1aba8cdda1e5c12bb56b718e5  data/Init/1024/Splash_Swedish.tga
3f1bb44a00ce5a1ad78656c4b99ee05cd50c7042ab9112ac8cc104f96ae9a2fd  data/Init/1024/Welcome.tga
2d62e90ec8760495c82da04978ebbb12caee8b0cab93bd5de4e71579a807d577  data/Init/1024/Welcome_Danish.tga
f267e18a1accad27839bab9bfa2a9258d1a773e91c1d890260035e91e4830d86  data/Init/1024/Welcome_English.tga
de67ef2e0aeb55c531955d5e1f083748080c89eea77e84a5625d5e3cbaedc1c8  data/Init/1024/Welcome_French.tga
b402c10d69bfcd6be35e9f57e453451b47d76e477cc767e767ea45af30caf696  data/Init/1024/Welcome_German.tga
92e1b1a3b6179790af154d1fdc13044860d5328ec52984573a9a81991632b22b  data/Init/1024/Welcome_Swedish.tga
c9e095e781b0491e2b93c7cbb37463050510cf104e028711804eb187c706846e  data/Init/1024/legal.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Legal_Danish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Legal_English.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Legal_French.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Legal_German.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Legal_Swedish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Splash_Danish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Splash_English.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Splash_French.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Splash_German.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Splash_Swedish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome_Danish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome_English.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome_French.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome_German.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/Welcome_Swedish.tga
06d286b6f857d7177ee63350c2f929c92926ef33372bb905e6724fb4cfbdeae6  data/Init/400/legal.tga
095054c52c060c2f0b69e2ed050f5707ecbec30138d050442609650753cd6e75  data/Init/512/Legal_Danish.tga
c01e088f7c986008a487c62ca97cd41febbfef5eeeda2561448c6c2450341b68  data/Init/512/Legal_English.tga
e17cba4736606800020f6d4847b538e7d9589034d5fbebef6df57fe5ff9668f3  data/Init/512/Legal_French.tga
e21d32ee70242bf66a2d19178ed70b5d40be5f3b4469511a513fccaff506c108  data/Init/512/Legal_German.tga
9d5ff3e417572ff560f12f756e5f7fc908a4c828b6d2c7437241ea9a1fc6d0e8  data/Init/512/Legal_Swedish.tga
2cb442b15fe26f2aac72c9844a819f601181435b0b004588117efd7fe16efe49  data/Init/512/Splash_Danish.tga
5c140e227e0b140d6073344ad17760e6051852e83c1c106896c9235505e69316  data/Init/512/Splash_English.tga
ba5e453d132c9f33e9cb335be3ee7849902ec9b5f9a65d8e070aadffe8e5b13d  data/Init/512/Splash_French.tga
0ca3549eb498d7f26ce165a63493e2e2c079220e6e43bab0daab23080b8b595f  data/Init/512/Splash_German.tga
4ae878843bbd8f46976fef3f66baa14375e39b7c26e95022671e7ca8886c071d  data/Init/512/Splash_Swedish.tga
1f33f62a1f3fadbd4f2dc89d09b2c4de0549abf93a4470e146be4f292d880480  data/Init/512/Welcome.tga
c8922ba1cdaacd357fd690e9137abed4c5a9b34db56c46dd45a2b95f85f34481  data/Init/512/Welcome_Danish.tga
dae3a78cf78cf1ff72f6f81b758ad86f4228e6b3a81bc8563ebbe5bdb4c62820  data/Init/512/Welcome_English.tga
eea457b4e7a9681226df04880087e40799098ec240d9ee737800bd203d2da6e7  data/Init/512/Welcome_French.tga
e63420a1eed25c73b8b19d03ad12af10c353641a830f462fc217540f7c566874  data/Init/512/Welcome_German.tga
f08fe2ab77091c73718397a6a5a028eb16c629add6d00bc55322200d94ca3d19  data/Init/512/Welcome_Swedish.tga
c01e088f7c986008a487c62ca97cd41febbfef5eeeda2561448c6c2450341b68  data/Init/512/legal.tga
4b12ebd5ab33fbf5e8b36ddbfda61af632f309c4a21f09346c624342bac91945  data/Init/640/Legal_Danish.tga
f95174ea12d2c24a83870ad85cd69fcbd7c97c56d544d560fc5daa79bf13d172  data/Init/640/Legal_English.tga
fc02ed48de37ce653b9426f1f218da89b1a55990c6940f2708e7ed0158523b0f  data/Init/640/Legal_French.tga
8f958bacab086c9548ce935e990eebfdb8329e8152afce68cd44a0ba814c0c54  data/Init/640/Legal_German.tga
281324a85f65cf78d407a20c1b4c18010b5435236c12c0ea59216a16e1ed404f  data/Init/640/Legal_Swedish.tga
6f49442acc33acb018e5b2a5d9976d381e6184e6f1da79a1890eeeb5a5b7805e  data/Init/640/Splash_Danish.tga
f3c92202e230f99c73c6b0804322c28badc140712006713cc0f6ba0cd1e694c6  data/Init/640/Splash_English.tga
c3edc9e6da7bb246082a6da1355571f858bbec2c7d01328298d40f8d84dbcbdc  data/Init/640/Splash_French.tga
0ad2c1b49f60be0874625bb223a69a58582e7b87b84e5ad65afaae234d137fe3  data/Init/640/Splash_German.tga
80465762074b6bad88b1f8fdda947252b9998e6d199d5a046d140b057cbea887  data/Init/640/Splash_Swedish.tga
cb3464468011fb1a63dbe8d3832d8ad0eb62c5a9a7d146413fba4c1107e9e512  data/Init/640/Welcome.tga
040df04d2b0ec28a69c250462b68180c8489337829f7902117cd68c9ef85f5da  data/Init/640/Welcome_Danish.tga
c421b337ab63764e778f6192ca3d0e6cb6a998d8966ae45d32c8170b9fbcd9ce  data/Init/640/Welcome_English.tga
31cf42a8ac9865c3c4c79f6e027f492b710e410950d7def3efff6656b52d6b0f  data/Init/640/Welcome_French.tga
08583b35ff35d93c784667c543edd0d08ea679bb30c1c06820b6f6f08b21ffcd  data/Init/640/Welcome_German.tga
299f83a3ac9a4b696cda102bd32d732adde482b2d3847fad1293aa0a48bc4a72  data/Init/640/Welcome_Swedish.tga
f95174ea12d2c24a83870ad85cd69fcbd7c97c56d544d560fc5daa79bf13d172  data/Init/640/legal.tga
61c5f8c3576f1ff8ae1d8470ba5f983cc2823918674c5879949a564de50b5437  data/Init/800/Legal_Danish.tga
d6d454fab92d2079ceb13ac9ab302637fb792d078bb5a181f2d3d50a2f3afa2c  data/Init/800/Legal_English.tga
702a0afcbd45a2aae9ceab2805631daf4d9808889d9cc5e437450e5df2da4974  data/Init/800/Legal_French.tga
b9faef35f8ce517b43b0171dbb7aab1a4659dd23e879f35e66ae5c0e1f8d9c13  data/Init/800/Legal_German.tga
c7bca37ab5eab6e380c2707bb2083eeb84cbc67952df5601ac6532e74ae90772  data/Init/800/Legal_Swedish.tga
58217fd8eef2ea5752de189fc53a04123bf89bd33732b87c9488eb1c3bab1c8a  data/Init/800/Splash_Danish.tga
1c06228cae5b033aaaf9cc2db3d8399d8161a303be0f7a6454276c8be971651d  data/Init/800/Splash_English.tga
1c78ce611049f7dc01c1ad55963889e14c4e3e6186dfa139b937775c903a4a7a  data/Init/800/Splash_French.tga
2851c002f7aff57a8087ebfe49cfbba9f1458674b8e4cc3ea1c0ead0fdb89231  data/Init/800/Splash_German.tga
6cb60b41c89c03e6d2daf94e4f26b620bc9e508ffe6066fa5f98cda528eb00f7  data/Init/800/Splash_Swedish.tga
862c88624ef0a6559603fa0727c1b8f2d62a5c22e46d55cc3f5e0952aa987472  data/Init/800/Welcome.tga
f342700853230d5602b0dbcb01763c9f2ec547aa14d6af847febdc25be42ca59  data/Init/800/Welcome_Danish.tga
c804852f2c73755a6a7048a77ab457fb481bc8b0fd332f67ca2d0ad0174532b2  data/Init/800/Welcome_English.tga
25cf06d27325c3362332d92004f8948225ff22d3f2280823118c4fed825e2d14  data/Init/800/Welcome_French.tga
7c8bea4aa6d82bb5730b3cfe46e3f323a98c7b0e0865416d2cb2710a3510591e  data/Init/800/Welcome_German.tga
e4bc9c53fb9976ffc4335a8d50ae050c9de43629e77e60264f13e5a4fb72aefe  data/Init/800/Welcome_Swedish.tga
d6d454fab92d2079ceb13ac9ab302637fb792d078bb5a181f2d3d50a2f3afa2c  data/Init/800/legal.tga
67b8ea25b3f405ce233dc3aa7b9ed796861cf4160fa30ecd39bcf262504954ed  data/Language/English/CASHBIG.bf4
1a63bd534a93e3bed1c4920e70bbf786cc9b3e97d3e6b940896221a8a682016b  data/Language/English/CASHMED.bf4
3a0e56be7a234ffb2228529ca4e15ac20e5576595eeb537619e521460f416e63  data/Language/English/CASHSMALL.bf4
80b704f2cded9d92095e03b675939a8aa3110b00757a0f3d6914c309c44663ec  data/Language/English/CHAT_COMMANDS.str
c2fa7246196fb43a17c07a3cfadcbd71183beb57099fecb2bcb06b65c185159e  data/Language/English/CONSOLE6.bf4
c2fa7246196fb43a17c07a3cfadcbd71183beb57099fecb2bcb06b65c185159e  data/Language/English/COURIER8.bf4
1e5bab6c50447002638aac6c636d12f9fd49493018dbf214adb32fc070d0951d  data/Language/English/DATEBIG.bf4
a9b6ee073567675f9b1799d8cc354e6a5bb65e525afbadb26f275b634f7f36bc  data/Language/English/DATEMED.bf4
15eb42cd95898a7cd87f7b4e15b4fabdfb0f321c092be257df13a23f71dd3fed  data/Language/English/DATESMALL.bf4
92211a2c337f8ad56fb2132181035c2c77a75e1f425eaa887341a6e7748886b5  data/Language/English/DATETINY.bf4
8849647e865f7fa82260c21c56bf4fdcf380b191739b55f091f077f279381d46  data/Language/English/ENTERTAINER_NAMES.str
f8122badae03b7c948f45547bb82ad70cf309d5c7895e228d484349a6bd45a7d  data/Language/English/ERRORMSG.str
32ad7dd26279e17e67edde38dbaf672692e0a11a435c765a93149f8e638a2357  data/Language/English/FEMALE_NAMES.str
daec7f9a1e9aec8019683b64f651d6df8d0f5e16cc316ecd491125eaaf3e1534  data/Language/English/GAME10.bf4
a9b6ee073567675f9b1799d8cc354e6a5bb65e525afbadb26f275b634f7f36bc  data/Language/English/GAME10AA.bf4
543c9b01d3789493eac1f8f2ff2f87b0857572cdcdba27fd273b84823ba6d06c  data/Language/English/GAME12.bf4
1e5bab6c50447002638aac6c636d12f9fd49493018dbf214adb32fc070d0951d  data/Language/English/GAME12AA.bf4
7f5a0d4f9d135ecd03657f4b2d0780f0f0b735f512a3fe56091dc18d97989db7  data/Language/English/GAME5AA.bf4
0321231c0b5a54f643c9cb21fd1842dfce3e9e732c3809a26632ad9442fcf7c4  data/Language/English/GAME6.bf4
92211a2c337f8ad56fb2132181035c2c77a75e1f425eaa887341a6e7748886b5  data/Language/English/GAME6AA.bf4
caf7f09965e2231a3aa51d71c4a5f200bef17dcb00688cb6f892b875544832d3  data/Language/English/GAME7.bf4
df1807834cc4faf374ebe14e2ac0e3eaa2ca9ec35964b3edd56485f193e38a31  data/Language/English/GAME7AA.bf4
a52c765c2c652f20eea95dd25f19fe595300b7e6b89e88abcf526889e226c8ea  data/Language/English/GAME8.bf4
15eb42cd95898a7cd87f7b4e15b4fabdfb0f321c092be257df13a23f71dd3fed  data/Language/English/GAME8AA.bf4
e72f4d8e72bf962b23eca89e706cfad4bbf6e944c27e1440e27c0897abda46ae  data/Language/English/GAME9.bf4
2c8e1f058c92a9935a3a777e17fad1b4133406457009deea6f3c9b61ad938dfc  data/Language/English/GAME9AA.bf4
976ee1c90b1af8d8099e89b99ffebafac9c61839cf9fbf4406cfaf15157c76c7  data/Language/English/GAMEBOLD10.bf4
f874a7ecba9caf0e07990d83e59c2c25f9350e0272896fc5476f52652e426a04  data/Language/English/GAMEBOLD12.bf4
976ee1c90b1af8d8099e89b99ffebafac9c61839cf9fbf4406cfaf15157c76c7  data/Language/English/GAMEBOLD9.bf4
1a0f73bb3658b5bd785618e05791955561b5c5f649437401f915db7472f75fc6  data/Language/English/GUARD_NAMES.str
533e3c648574075af4235006631feff65768609369e18e1c8add07e3a3d3ef08  data/Language/English/HANDYMAN_NAMES.str
5c99cce5756cca0758198054dbb4258ec51ffbd59995ca1cbff695ae833ea59e  data/Language/English/INGREDIENT.str
7a1896a3190f7795a3aca8bc793a714e29eec943acb234db51e6275cd35a54ac  data/Language/English/ITEMTYPES.str
3f3113272da6844ead179281d774f2b7a55f5f25f617509fb4bbc3f95f4bed50  data/Language/English/KEYBOARD.str
31004291979ba7d255633af3a87789d9dfbe9cd317b4742a17ea3beda2f1b9be  data/Language/English/KIDSTATES.str
369cde7f17cc4362d9aa42c0424fdc4ff937cb4f4c0655be9263efe137bfd737  data/Language/English/LOANNAMES.str
429db5d5503e725e6783d49bd9524dd8cb41ae030ece16ed2a85c77cddd0a6a2  data/Language/English/MATISSE18.bf4
e5f40e34093347160932da804c529bdc92692e384ec00cfef70caf2eb65f4f5d  data/Language/English/MATISSE36.bf4
69f23492ef61a27ed79dd4df67978536720d403f7e2733acb6b43e6f4f78c587  data/Language/English/MBToUni.dat
5278c6f50cc6e34538e824e59b5c74e1485fd39d70316572bc2b921385697806  data/Language/English/MECHANIC_NAMES.str
227bfbb78ba6cb60360bb663ee91ecda6acbaa6735070c2871ba40826f452411  data/Language/English/MENUBIG.bf4
4d53ea818dc5ba0e1b8b722a6f2af98a3faf17aa912204172272c45996dafc36  data/Language/English/MENUMED.bf4
4d8a729b02cad4b46971d982238ef32d2e880894c95bd2903b1ce0e5b6208a15  data/Language/English/MENUSMALL.bf4
d9a0248e9976885008ad8129d0cad9f73b64f117187723a981705ea5092139ba  data/Language/English/OBJECT_NAMES.str
088c8681b43a630653d951408ededf6135dcc472de778b64f1e12c5f4e0f30ad  data/Language/English/POSTCARD.bf4
d6a45444845c4903fb16f14e5e2d1c31d164e4c1b83b6719f82bcd3c3c1ffcb5  data/Language/English/RESEARCHER_NAMES.str
b8f4e71b76408032ed8ba9cc155ee70ea323c5c331c4919ea9fe700b9ed98993  data/Language/English/SESHBIG.bf4
429db5d5503e725e6783d49bd9524dd8cb41ae030ece16ed2a85c77cddd0a6a2  data/Language/English/SESHMED.bf4
ae3a42c34fd108b46af6267e35b48740891d62d1f82624ade4226da690725198  data/Language/English/SESHSMALL.bf4
1d0cfc617bd390f3ab0f30ec73a2a34e5559ef3f1dd6b56576bac1cc9ada0589  data/Language/English/STAFFSTATES.str
153ad8767b4af3d71079e26f91b6afdebc6448c88044bf75b2966c5efb6cbaed  data/Language/English/STAFF_TYPES.str
525780dbb028b5cca80ad52e628df1b74fa128a0b8b4f750cb35e3ed81283f1a  data/Language/English/TAG_SYSTEM.str
a97338edb4721e0c92f6d1822e1df167c26d4ce345c36e0da1ed7cdfa89b3992  data/Language/English/THEMENAMES.str
92c6a9c38cbc7bc9d3f02ac2a8ec020924650b06d6a23bc9b3c84a2ee9c1723f  data/Language/English/THOUGHTS.str
a4e36d3a4408fb36ad770f971b4f12ab4368b706621fbc804bcc1cdac78fe0bb  data/Language/English/TITLEBIG.bf4
5c4d77d5d0773676113a74e3d0d57df08ff7c39b3eea0f480062df151f10df2f  data/Language/English/TITLEMED.bf4
a9b6ee073567675f9b1799d8cc354e6a5bb65e525afbadb26f275b634f7f36bc  data/Language/English/TITLESMALL.bf4
784f507f8f995b05441442ae8afbe1f7f332ba06a9b3ceff78f6fbcfe541ed5f  data/Language/English/UIHELPTEXT.str
ca238a10cb31736cc68374378a996b3bcc9b65ed4edd70bb78adf39ac06381ab  data/Language/English/UITEXT.str
9188d053785dea0e633b500a6dd0fcbcd7b99a00ac35d64466f5af32f7c4348a  data/Language/English/UniToMB.dat
d672c8c9cc02ebda7a6cdc807683438b41b8ed521536aa1a2b1db4d8d4b9d135  data/Language/English/alloweds.txt
5be0255d211accd2fba0b7432e9273d72fa4856d3fba4b6337e870e091a9a72a  data/Language/English/bankrupt.MD2
453955609fcb4a9911fc0e1b6bb4e6978c41ebd1a4457a1d9f66a54d28dc529c  data/Language/English/congrats.MD2
7c43ed882485dc85758285a2f28c97a5d2e6c2d31b745c047990990a3636c5ed  data/Language/English/paused.MD2
02b937519e897a9fc7f36fec29197bc1abe40350979800306062c56f039174f0  data/Language/English/residx.dat
91f36b6228190ad0014b3205e609edb8b62ad51a827f1ae9f1e408769ceb3bfb  data/Language/English/swears.txt
3381369c59ccb1a16007f74b3a9b8227da21f8325f0879018e6c860f3c3698ae  data/Movies/BFj2.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/BFj2.mov.finfo
3229854fe27d0412cd5ba64eb70b6b83d5e22db8ba5310cae6f33fe8a2d24fb3  data/Movies/bf.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/bf.mov.finfo
7b6b97d5c750766bd5d5dfb77d48bb0fa6c3a1db78ccb6622350c8c78bb518a9  data/Movies/bf.mov.rsrc
174723130d5f3f03da58c26f6b162b886267f9ba694792df6bc534847704a32d  data/Movies/bf.tgq
89142ee0873d2752a543d6f5a364fdc9e58e6b94688a00af75edcce6ea3ba92d  data/Movies/bfj.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/bfj.mov.finfo
e872fdd5d334dd364d635c180f9109cf554395ff08ae3585f72028f0576fed34  data/Movies/bfj.mov.rsrc
b271c871a46ebc6a1debe01d9c7592b836f9b8ae1c452b7c9eb5e816f8268285  data/Movies/bub.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/bub.mov.finfo
338764e9d40e3e9b49cf8356ea62c4bea6e6701a79d74fcedd8fbb6f9683e5e1  data/Movies/bub.tgq
ab70c921f5ce07914047f1745a7b9543a14497c289b46cf7c650346a71f39198  data/Movies/buc.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/buc.mov.finfo
50f751e8195dbe6784037a29776b46480b75ccae542bf1ee77373dd3bbff6a44  data/Movies/buc.tgq
e2ec6320ee7fe8aa8025dcec23bd06ead6c4b94c05b1e82af77d339bf28b0244  data/Movies/grav.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/grav.mov.finfo
fe6531e19fa0a9aea2c9ea88428898e11fba91034515818076e036061e85ebfd  data/Movies/grav.tgq
e2c8339b4e4669555a55f9be2253924279c29d3de5768e0e06657cc3b9908363  data/Movies/jug.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/jug.mov.finfo
acbde3241eb1268f525c4e50af2fcefbfef057cb0583e8831c2d63bafa2d4e69  data/Movies/jug.tgq
1e9e75253932a55ed6cf51b20d8da4850d0e52f3b3cd744e1dbe776eee8d06fc  data/Movies/mir.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/mir.mov.finfo
ffbceb6864ddda516922aceaf4ed2c55e463e663d49230ea03a2e14e4bea55a7  data/Movies/mir.tgq
9a4efbe0d2922e07db242fd22217b1da5ab01dae821fa963be721663449668e3  data/Movies/plan.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/plan.mov.finfo
9ac41b21b05527b76be611bcb6951d05f461092a20906fa732701e864abd0ff1  data/Movies/plan.tgq
d7d293775d6ebe26c75fd05e57a14926f55c3fe29867cadec5b668eeeaecca91  data/Movies/roc.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/roc.mov.finfo
0906c4862f9fb594b60b9c11afd7f657fc97de82d60ef3cc4d7cd210d404e72d  data/Movies/roc.tgq
95612a5bc2e86ae7d188275e054d027f2299d4b719d887a8f74928ee8af51f77  data/Movies/roll.mov
9341c83288cdadf304d4737b4d1d96977357a44adab83719c2c95e45c5c41061  data/Movies/roll.mov.finfo
2bb0194447ae9dce24d6e15c7d9c53177fff1356e85bb6d8902dc68a69cb3fe0  data/Movies/roll.tgq
b6151046da72019652f0e260c5fcac5a8e663a46d9aa17957966716b1f93de41  data/Online.sam
6564736ad4d12d27634be0af1f23ef6f3ce4259b9d5b2f577e57fd8a68113210  data/Particle/Tp2.plb
d9fed850f3a778af15a3d988245aab097a79b806f61a2c858a519336f3fbb96d  data/Particle/par_lib.h
30e98c3c013f825a22f2198f79a002f60b4eb454f54ee47c1786de158e27763c  data/_Resolution.sam
75f73fdc44d5755735c17d97636bd372f078bd1f933de98d080bba9b9d005e21  data/debug/font.tga
b82e3ee0852d8847b981fc09ac7479c127dbc9552cb5b1a8c198d7b0ff1e4b2a  data/debug/xfont.tga
00e02624405005b14ebf1bb1c13d09d6fa5b7c3e6f3c76e28dc3e532728f5ade  data/esprites.wad
9f0a0ab5df7a9b79694ebcd47099e89b7c8ffe23e8108278f39ea6748b811b80  data/fonts.wad
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/BIGLA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/BIGLA___.TTF.finfo
3d9d27868b9f1f9b7872d47df653970fc2ed3b201c43eaa1b2d1a0dcdf199134  data/fonts/BIGLA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/BIGLOA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/BIGLOA__.TTF.finfo
2dce96e5b5ea9ea562ea398a6bfe92c105edaed400515123be0db32c2379c42f  data/fonts/BIGLOA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/CLUNAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/CLUNAB__.TTF.finfo
3e032164858709fd99f77ae17b3ae8db2fece6ef3a05626868f7290dc63da331  data/fonts/CLUNAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/CLUNA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/CLUNA___.TTF.finfo
37a1d4bdda3df48b556409ed8a84d72a0e017f930023489ff7ea0b2bd5be55af  data/fonts/CLUNA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/EGGITAOE.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/EGGITAOE.TTF.finfo
b34eb7b82b87982ae8e2577ebdb49ba816890b3e148c6a15abd2634bc3039db0  data/fonts/EGGITAOE.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/GARGA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/GARGA___.TTF.finfo
f9d5d8be1dbfd966decffd7a4bfdabe0a17266d3881cfcb9f70b869554f5eafd  data/fonts/GARGA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/GARGSAI_.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/GARGSAI_.TTF.finfo
f4732b39c8552a414acb152e116c3a520f05ffac1826f77a09dd9d4d42ce038a  data/fonts/GARGSAI_.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/GARGSA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/GARGSA__.TTF.finfo
aefb479b33862ef8a392716c647730938e1d431b3c10bfd3047520258ff6a033  data/fonts/GARGSA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/HAUNTAOE.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/HAUNTAOE.TTF.finfo
8a9f18df9f8d347654f5d5e45ec320132ab1e4b90e91af5a042bc22e784adf8b  data/fonts/HAUNTAOE.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/INTRAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/INTRAB__.TTF.finfo
8eb152a86d2a76fca3475675d57cd667e5d4289c88636cbfbdd3690c2ba7ba1e  data/fonts/INTRAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/INTRA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/INTRA___.TTF.finfo
169b27aed6f8fa8ad2e2d12ef0c62527422b829bc9eef3264681d006bfd3ed1c  data/fonts/INTRA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/KRELAB__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/KRELAB__.TTF.finfo
bb6973a44fca200701d480a4466fdddd6c70232a8680de59ab91d5ea42c5f57d  data/fonts/KRELAB__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/KRELA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/KRELA___.TTF.finfo
fb0699323cbdd5a51ae52f07f6c3ac40f7d827477fc2a721955e51626ced805e  data/fonts/KRELA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/LINUPA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/LINUPA__.TTF.finfo
52743da6cc55018ab581a42f3e858106c0e00869b06517fcc48a102faa053e52  data/fonts/LINUPA__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/TANNAO__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/TANNAO__.TTF.finfo
fbe99800ebf5cec92e3762c7b26a911f871f0c143784e3370986ad41a5aef949  data/fonts/TANNAO__.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/TANNA___.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/TANNA___.TTF.finfo
0d1b777935807b4854d954d7dc8bf152f5ee0a1d3c8a866d74335976fb14105b  data/fonts/TANNA___.TTF.rsrc
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/fonts/YOUNIA__.TTF
4db66d8e9a1caec9b88d1e904e4d5710d7577338b83723536cda4ba35224930d  data/fonts/YOUNIA__.TTF.finfo
40406125ba87ada85b3620fbe2753ba523461d1bc2093187cfec447175b9c110  data/fonts/YOUNIA__.TTF.rsrc
a4060947603f4d31e7def1bed746963f5f54834afde05d5f5f9b2d401f42a082  data/generic/defaulttexture/NotFound.tga
c7d38296c36af26b90c73e47fdc728ffbb4ccba4809faa83aa47beb4e4d04f10  data/generic/defaulttexture/qickload.txt
d5154361929da3a835fee4a47a68ef9c717e18365e670902d423d4e8f480839f  data/generic/dynamic/garrow.MD2
dd58973cbfec088f8c3343dbb192d57ecb83cb6f69556e61b49560b3f494d384  data/generic/dynamic/qickload.txt
3e021b02eb830bcbd183286eff56775d4548ede57bf4713a7ac172395995825f  data/generic/dynamic/rarrow.MD2
2088f54ef8143dcad2221da05fd88e22657d491cd3920c912fd354c18edb7ab3  data/generic/dynamic/stexture/blue.wct
116a49c771a09c8ef537e49c6895f0d6da58c3fe980e9335f9a79c4fe581122d  data/generic/dynamic/stexture/gby_sur1.wct
4388c1e6bbf5b460e1f72f9f82fae1846666ea94046f7e075ed24918524f179c  data/generic/dynamic/stexture/gby_sur2.wct
9c2c1bb6c3dace834e8b8a52c85400c78149cdfd6a17bb9cc5dfd8a2b9839f27  data/generic/dynamic/stexture/gby_sur3.wct
70ec227bfa34cf3bf05e53ff81527de99f2c189292ac1f0ef340720d7b864803  data/generic/dynamic/stexture/green.wct
a2e4478a6810259b44c288f51b2878cf95795d145ebfcd5e32fe5ab2d9b3803f  data/generic/dynamic/stexture/gte_lgo1.wct
a1359aebacd6f0bc145d9a6ec8dadeb62ebfeeeaf47f24ef43de5238f1a494ff  data/generic/dynamic/stexture/jri_lak1.wct
330c92d8e897778796440b9773785c64ee209488254e40d7afe8daecb0d3e018  data/generic/dynamic/stexture/m_break.wct
3896b36626a845c70702742c7f137a7262d3f2e1bf6e3133aab38588c329ef8a  data/generic/dynamic/stexture/m_cross.wct
ecb5ac2592d55842d8aa3998bc13817739129b6a6f7a82b0906cf33eb5315690  data/generic/dynamic/stexture/m_direct.wct
101c73c53f2696ad259ad49516d0c0755ed422cbf4aa4a00cd8d48c99717da04  data/generic/dynamic/stexture/m_end.wct
eba23d87f3cef6365a02a6e372bb5d050ba096057d4a70f908671cecf7eb9efc  data/generic/dynamic/stexture/m_enter.wct
60b2b85c3e0166067788c4d43aed2bf1a1085787975e1ebe3f9a114c8865cb84  data/generic/dynamic/stexture/m_erase.wct
2a3254fd4af5b1049429885ea71c3a59906f9f55537ab909582eab4a4da458b3  data/generic/dynamic/stexture/m_exit.wct
85ce2af1ac81a3e7a0d70ae850764d03eabc7c30f1b2dde9dfdf45fe539ee9a2  data/generic/dynamic/stexture/m_front.wct
9ec5139700417354307f7e138159043260856bb96c76e6e448ee17da1ac81b7d  data/generic/dynamic/stexture/m_inout.wct
21d97a3cf3fb81a61a3000bdd47005cc734c0b40df5a17cfdbbb4228207612f0  data/generic/dynamic/stexture/m_link.wct
1510eb461d8d6962316609f00700ae14a5ec5f2ac3da446a62a1ee30001b96c7  data/generic/dynamic/stexture/m_nocash.wct
4981f6de21f46f0774c25a715a5f8433a09338400e79acecf2b92c49b763bfc8  data/generic/dynamic/stexture/m_nopath.wct
12de99789487441fc2363ea978f657685f1227f7a9507d6399c12a85a4c9d2da  data/generic/dynamic/stexture/qickload.txt
b6867c06600939bc10bedf041d74959cb53429b070e7581948471606ee383e54  data/generic/dynamic/stexture/red.wct
881f18334e8056f882dfe2174d0885a2e0f403fb5f54d4b5dec5130dc4ae152c  data/generic/dynamic/textures/blue.wct
5a84687b61681019f7cb3cd0805566bacd7dc959f98412b591589782a72dd453  data/generic/dynamic/textures/gby_sur1.wct
d975240e4a2204892bc9c04f2144ed7e6645dea7c287a57c545a565f1250fc77  data/generic/dynamic/textures/gby_sur2.wct
5ed24cc085bd9682bf9268147bc3ac257c4d2ede1a5bc8f01e3a965eaabdef93  data/generic/dynamic/textures/gby_sur3.wct
70ec227bfa34cf3bf05e53ff81527de99f2c189292ac1f0ef340720d7b864803  data/generic/dynamic/textures/green.wct
2c78a6c957a6917fd0cb4d917ab2b57c9ed019a0e4f666d60baca060fffb620d  data/generic/dynamic/textures/gte_lgo1.wct
fbb9c15a88d7e6116495810831030d5b5608423e4c156f94abf1a54efa7b5003  data/generic/dynamic/textures/jri_lak1.wct
330c92d8e897778796440b9773785c64ee209488254e40d7afe8daecb0d3e018  data/generic/dynamic/textures/m_break.wct
3896b36626a845c70702742c7f137a7262d3f2e1bf6e3133aab38588c329ef8a  data/generic/dynamic/textures/m_cross.wct
ecb5ac2592d55842d8aa3998bc13817739129b6a6f7a82b0906cf33eb5315690  data/generic/dynamic/textures/m_direct.wct
101c73c53f2696ad259ad49516d0c0755ed422cbf4aa4a00cd8d48c99717da04  data/generic/dynamic/textures/m_end.wct
eba23d87f3cef6365a02a6e372bb5d050ba096057d4a70f908671cecf7eb9efc  data/generic/dynamic/textures/m_enter.wct
60b2b85c3e0166067788c4d43aed2bf1a1085787975e1ebe3f9a114c8865cb84  data/generic/dynamic/textures/m_erase.wct
2a3254fd4af5b1049429885ea71c3a59906f9f55537ab909582eab4a4da458b3  data/generic/dynamic/textures/m_exit.wct
85ce2af1ac81a3e7a0d70ae850764d03eabc7c30f1b2dde9dfdf45fe539ee9a2  data/generic/dynamic/textures/m_front.wct
9ec5139700417354307f7e138159043260856bb96c76e6e448ee17da1ac81b7d  data/generic/dynamic/textures/m_inout.wct
21d97a3cf3fb81a61a3000bdd47005cc734c0b40df5a17cfdbbb4228207612f0  data/generic/dynamic/textures/m_link.wct
1510eb461d8d6962316609f00700ae14a5ec5f2ac3da446a62a1ee30001b96c7  data/generic/dynamic/textures/m_nocash.wct
4981f6de21f46f0774c25a715a5f8433a09338400e79acecf2b92c49b763bfc8  data/generic/dynamic/textures/m_nopath.wct
12de99789487441fc2363ea978f657685f1227f7a9507d6399c12a85a4c9d2da  data/generic/dynamic/textures/qickload.txt
1a1f9b386ac7ee96840b36c9f88f4075746b7b2da9b886f02676dc12c87b19c1  data/generic/dynamic/textures/red.wct
a577b0136de97c4b7a3cb2cf663904598296385af99cf7298dbabe2d9e12c923  data/generic/shadow/alphkid.wct
e3212d1f1c33799c92be48c98d769d66c199d271d209d883c684ae819d2440b2  data/generic/sweather/Raindrop.tga
81ba618fad5396e6183a1e83ee9cd098c5c2438c756b66f27661b8023d3c43a3  data/generic/sweather/lightning.tga
a515e50fb815caaf13deae1e8c5f8a22249813efb48d7f78821492c8571f427b  data/generic/sweather/qickload.txt
05f7c67481b9d1200b03be7c812202ba2e680a8e0215500b5230be448536c976  data/generic/sweather/snow.tga
71e55c003ed99c2fcb81b58633222feb45f4f9dcf8eb93d0d1585fa8c0fbf1f2  data/generic/weather/Raindrop.tga
b039e417a281771d2643aacb2c0588e5fb09173f1ec5d3d2f3704a8cb96f797f  data/generic/weather/lightning.tga
a515e50fb815caaf13deae1e8c5f8a22249813efb48d7f78821492c8571f427b  data/generic/weather/qickload.txt
8ee9710f0cfc0c08692c4b70bdc21c2e0fdb2acc449ef4311bac032de9c47de1  data/generic/weather/snow.tga
bb70bc8479a9563cf3a8fca8fa0d506887fb913585771287e27d86fd7028d478  data/global/Speech/cat_speechBANK.map
348e421d4ba6b42d52e78f38f7487b308c7a0a117ff3744a08c12916731055a2  data/global/Speech/cat_speechSFX.map
f86d74c4b4356aeaa0e6ed2feb00e80450a9a643a37c7d108e0c2d65c19cc9e1  data/global/Speech/lips.wad
61e2d6a34c7eecb4569bcd8495c4515227f4404a5c787bd0181e2928f2b79fe8  data/global/Speech/speechHD.SDT
e949f70e4d668840bc96e4b7d3c7942802f393f1300e15f054504d2adbf4f962  data/global/advisor.wad
e7c0286cf926f0d1f0072b1bf5b9eccd00d4473a9a020bbdf0f365b08ead5733  data/global/sound/AmbientHD.sdt
c95c11ed15ad10fc82a1b9db259edb3fcbe4fe8e4cc938beff20b0e0f1af838f  data/global/sound/KidsHD.sdt
36c035524e8c5b5c45ee7f4740ee4f4e0aaee38531ade4126e17078cc054b354  data/global/sound/MusicHD.sdt
0ee2116967c5e9dbf1298dcfe285fbec3a2116107a77ccf002eeac65d28b77a0  data/global/sound/RideHD.sdt
2e375003eef4d2025dbcfe5101a47654aa8ea05edca31d250b5554145b96e465  data/global/sound/SfxHD.sdt
b2023110d052290010cb60ff09eb44294ca5481a6ea5ee3a1af94601b4ab5eb4  data/global/sound/StaffHD.sdt
565fa2aa98935268b03db3dbc3f903d2f65d48005241691b0172414f60372cb7  data/global/sound/UIHD.sdt
55736321a428b98a72502174037b5e18a38943a7e8e36233f5bb7d85a1a08c2c  data/global/sound/cat_ambientBANK.map
713793b8f3c86bf96c5affa9ace435bac49016f2fbc9a604810bffb50e9e20b3  data/global/sound/cat_ambientSFX.map
af380d6c90ec075434056eee39fa46c922aeee571091d278f74b48f5a50c88f0  data/global/sound/cat_globallobbysfxBANK.map
34440e8b6a688869fc6678512e8f065ec9f5cfd7beadff97044ad6f1ffad673f  data/global/sound/cat_globallobbysfxSFX.map
4cb16ac91cc753d0edbc87de4088e583f23a72a305ab8e97bb37547dcb81a75b  data/global/sound/cat_kidsBANK.map
13bdb903dc3a780cac0d09709036c52888b14a35f68cc9f5257c0625312cba5e  data/global/sound/cat_kidsSFX.map
1a802bbb8cbe3e3f51b62602b30c921ae4865b97b466c5a99659912749f05002  data/global/sound/cat_ridesBANK.map
094242d35a8b0e31feee7415a5bb0d083d16289639607b96fef78e891d807182  data/global/sound/cat_ridesSFX.map
359422b404bb1c457b3b25e63fdaa1407b47d100522f8aafc6f666764527dfe7  data/global/sound/cat_staffBANK.map
096710285d64c0a67f07180c1471ea550cbb6e0a331f710d00eca71a497d6eb8  data/global/sound/cat_staffSFX.map
73cb613fe511c47697f4610d393bbf1fc9f4fa83350c7d1d073481586d3c3cb6  data/global/sound/cat_uiBANK.map
0d1a6c2c0a7733eded894fc172fa372bbee6bed18e966c48384c0c020e89cf9f  data/global/sound/cat_uiSFX.map
ad3d4efd5080592948999e4df816cce9d3796e78527db5b146f6ac8a9fda6132  data/global/sound/sSfxHD.sdt
bd2f8aae3b808e42414a22635c88be049f7a6a9da0133c58f42e45dae92b3ddd  data/global/sound/sUiHD.sdt
6bf3986e4be32f30fff97d3efa31392c223aa9ae5cc85599afaae82521666945  data/global/sound/sfUIHD.SF2
deb420063cbe73f2f2e6927837d5f97728e13160d3f34195208722c73a3b4acf  data/global/sound/sfUiHD.sdt
4c7e7cddf622680fdf71fd7166f44b44e8cb30b2d50135a5ecdc47a36bd8282e  data/global/sound/xKidsHD.sdt
707eeef1168e77e9cf10b168ef8959419dba8a72bb2581a98be3d7ef08ad23c6  data/high.sam
b88d2955e719de6aa10d70fd8520aa60500956eab486702799d068b6e23f5e58  data/levels/Online_Standard.sam
3d39433641df70dba73e6ce0f4ae576a5a334ed93e61adc1cdd570b751a8bf4b  data/levels/Standard.sam
a0464c5bac42ec5fe006d56d7e191585ebb68209535bf258747196c406259a9c  data/levels/fantasy/2dmap.tga
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01  data/levels/fantasy/EndHoardings.sam
ef93aba09c2f7513d528763c9d8073659796ebeec8b50da07bcc79a9a418a795  data/levels/fantasy/Hoardings.sam
6d35cd515ba59027be57d492eb9803554e1ff51ffc231381a5f09e0ca07a0852  data/levels/fantasy/Music/MusicHD.sdt
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4  data/levels/fantasy/Music/cat_musicBANK.map
bdd080f8bece1d03df33cf2868ef82d4433b62543f0994b9d24e59fefda8b65e  data/levels/fantasy/Music/cat_musicSFX.map
e25770aeb7a19e2688d37bced2488c4f42b5a46887adb38dda32150cb5439901  data/levels/fantasy/Online_Standard.sam
4228b7d58e7807f0d7d641cb812767e4eba3c787482022f77baf2b1853e00621  data/levels/fantasy/Sound/AmbientHD.sdt
b86a6ec4a52bd2e0d68fae7a43b7fc9bbe035fb7cbbcf1bcd94f38b6519b8ea7  data/levels/fantasy/Sound/LobbyMusicHD.sdt
73cfdd25d21b1b70a86fac90637a6f8f5f8c42ebebe59c02db437599dec65ea5  data/levels/fantasy/Sound/LobbySfxHD.sdt
807d3ce648e3f10fbc394351562c7bc8bcf7557c4cb110233a6e9de4dde0c492  data/levels/fantasy/Sound/RideHD.sdt
2fc8e636b95c96a9a4a3c03f9aab8c9e4895f0a20c10ba85ea98e67ab9b51eb3  data/levels/fantasy/Sound/cat_ambientBANK.map
65c9bed8ae42e63a7c2ecfec8bacdcc1d8a0723e26f3af089ac52b818293d7b4  data/levels/fantasy/Sound/cat_ambientSFX.map
f05b6523280ff15b3050dfcb6fbc669e8aed4e558a2c723cd466e4fce012d59f  data/levels/fantasy/Sound/cat_locallobbymusicBANK.map
74c2e2590c10db47602291db09d9860cc87fd83a9bc31d26bee962e977d99fc7  data/levels/fantasy/Sound/cat_locallobbymusicSFX.map
f139dc1ab3183120437cb78479b9e2c7560b6229222038da07842dcc4a394295  data/levels/fantasy/Sound/cat_locallobbysfxBANK.map
bfe3af27e41a7bb054e48a3a8dc039a7846ca3487e097ce37da6d2ad1604d84e  data/levels/fantasy/Sound/cat_locallobbysfxSFX.map
010d122e94840753c4545562cb3a9f5639425aef9239864d5bb66c9434752e34  data/levels/fantasy/Sound/cat_ridesBANK.map
8a2fef253f6d5c54422f3c51ae69708034257a3bde8ab58dff6ac895632c871a  data/levels/fantasy/Sound/cat_ridesSFX.map
1a4a224ca6f92de93d56987366a78db185c267b6eac9b70ea1107e4d3536d047  data/levels/fantasy/Sound/sRideHD.sdt
847e140b592e09da5df3d568139d07918f6c045e96dd7295380bb0a1b6794d1d  data/levels/fantasy/Sound/xRideHD.sdt
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa  data/levels/fantasy/Speech/cat_speechBANK.map
5ebe532b5a0e952b0c5903a7c4096509476aa25ce712b402b9012b4ce5b966d7  data/levels/fantasy/Speech/cat_speechSFX.map
5b9010fbf818327d5de682626787786a869b54206ea8bedd2d188c5bc2cdd1b7  data/levels/fantasy/Speech/lips/sp_001.LIP
b82a724ee07ca21400ccb482ee9c6cee9980a7f1959b48a476e4333a1cedfaf2  data/levels/fantasy/Speech/speechHD.SDT
f90fc9b7dc26d80f48c2cd50c18d7d12fafc85779fb4d22b5597e4b3a9d4d104  data/levels/fantasy/Standard.sam
354dbd216561345046f3986ebe489340211681219fc93a43b1a27bb01d9be791  data/levels/fantasy/advisor.wad
86565a3111b7329e0ab2263ca48e5bf7d3e8df9043fd7162810ddbb3cbc8f4ca  data/levels/fantasy/dynamic.wad
0a487bdc5710c1870202bcc7910f93b38dc04512e0b2cfcc8d1c4ff1735309b9  data/levels/fantasy/features/Features.sam
5923a2babc3feb760c8b99b4c1ab61475af4b9f1d42a7a802c6edb7dd7496403  data/levels/fantasy/features/_cards_19.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/features/_cards_19.wad.finfo
a46b304ceca910740468042838629cd7cf07ebbe50da1f3d9be26e231b9a582e  data/levels/fantasy/features/_coin_20.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/features/_coin_20.wad.finfo
f1f32c042c4758a0e800b6879c16f079be6510b0a1dd869a541d38880bd40724  data/levels/fantasy/features/_crayon_21.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/features/_crayon_21.wad.finfo
ed934e5945df0470aab096089fc187949de5097de4c6ea04275b0ef34d4e2f52  data/levels/fantasy/features/_sweet_22.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/features/_sweet_22.wad.finfo
cab7927ea0d5e4b6061f112e62c5fc454c6550fd0db61c68d84e30e46ed5957a  data/levels/fantasy/features/bfbin.wad
f3547fd7185aeb81675a42f89084714bae70d2a05fdae78a767078e323219263  data/levels/fantasy/features/bricks.wad
7ba8f59f4b89c1c2ede458f394f5ef2e3564f18011e6be665c022c5643ca189d  data/levels/fantasy/features/bus.wad
8603f593a68d05cbd1cda2109bb7ed543d141ffd94e2b5b2e39bee691313e37e  data/levels/fantasy/features/c_cane.wad
05c8e6dc4d3fb579340f1f40d63f51eda9161604046f004cbcfed91025ec1369  data/levels/fantasy/features/cardgard.wad
26a4050395234fc43b4f86dc01929cced0920e76b5a8e9ea48453e3197e1ec6d  data/levels/fantasy/features/donut.wad
2efaf470cd6c5f6769a64b779caac607ef1a4b5fbb472cde24acf56611ec5ee8  data/levels/fantasy/features/end.wad
2fb940a31e5ad1eb1e50e86ea40e8557bb06af3e40b89893943b86a67799a7d0  data/levels/fantasy/features/ferry.wad
dcb65f110d764e32511165515c501e588db1cbb93d32fd1dbbe8ef809925fa23  data/levels/fantasy/features/flower.wad
65362975dda62185e055b4e4041ef4bb6edd8ce984924c40b09bd7512d0cafec  data/levels/fantasy/features/flower1.wad
efc19aa8abbdeb6bd3f1a5903c5f12255374e45cf1de2130c76ee06cb88c2628  data/levels/fantasy/features/flower2.wad
bbbe145ec9ac9b478ba988a2c3029938e9176f84fcdb5785734ec657852192ed  data/levels/fantasy/features/fork.wad
a86c0239089fd5ff24e00db163cd9f848aeb6bb46004d453be0f7437d412f466  data/levels/fantasy/features/fountain.wad
f3a4aa8315d9aa392418c6ab2286edc9f207577f030d4eb06bbbfd4b5da37384  data/levels/fantasy/features/gates.wad
c4a000f43f6ebf991a7f9db6818c0e108b67161c76e1082f663a7095f8d9fe58  data/levels/fantasy/features/grass.wad
6d68b5121cf654ea4d6de3e812dd11b2d85dfe451a00076f3dd73c62060bd22b  data/levels/fantasy/features/ground.wad
225ce76a897bf1afb05b51fa7248633efd85c8768bd5e2080e167b1afc8971f5  data/levels/fantasy/features/groundc.wad
78ebdaf502a1f518c7541326b6f99baccee01cd5b4cccf150ebed088ed8201c0  data/levels/fantasy/features/lights.wad
95b330567649d6087acbe087d1aae5bbf04fc8a736fbfcd4da47a237059df96e  data/levels/fantasy/features/lollipop.wad
5bb82cf06660c663f9619a413e9714231a654f3047df8b58edee28f4fb1c228d  data/levels/fantasy/features/loo.wad
974e23b0a424e9b036e5a4a12708a9933466fb49d12053f92f58eb66aa6b1f85  data/levels/fantasy/features/mushroom.wad
d6f0c90532b22e4b5c0d8da5e930a14270fabd735bfff102d64c36bc88bf9c8b  data/levels/fantasy/features/pencil.wad
14f7a82d5d00038d14fd42561bbc19260aca009695b7ea1a2e633a9f6c648d50  data/levels/fantasy/features/plntpot.wad
ac2b50d61d92cfa1f38d4772cba5950870ea66b6adc107144236d2e1434a5239  data/levels/fantasy/features/royaloo.wad
2f13e5e9cd2d2eacf5fce944264556779ce437fc65fdc3d17a692599aa580791  data/levels/fantasy/features/seaplane.wad
c5971872e4c75d12c31d36cfa59932b5c9435162db234f0e9c6f93fb612858a5  data/levels/fantasy/features/seccam.wad
de9b32322ef41e90ad959548ce6b1d33c58a667bc7eb9c14a871bbe236ed560f  data/levels/fantasy/features/sflower.wad
3d238ec8c7cd0264bd57b7e4db6c7c6df23a0db507eea39855c22b4081589130  data/levels/fantasy/features/sign1.wad
7b49806d8527fe47d0cb2eaad42313dc013ad8187c95297375c2b3adeca8c9a0  data/levels/fantasy/features/speaker.wad
2deb0324910542ed3b735d84c063b3754f073497ab6b51e477c1cbd53539faaa  data/levels/fantasy/features/speaker2.wad
ebffcd6b428522e01423b62510bf04857118b7a4ed45576fba3cf41460288ee2  data/levels/fantasy/features/speaker3.wad
3fb088ecee9f194f5330aa916c3592e9f6fa729b2a718eb0c9a5823d1070d986  data/levels/fantasy/features/speaker4.wad
93840aa6e032edf1ed8f330249fd26ef63588bb3e5399688d849d55456683684  data/levels/fantasy/features/staffrm.wad
16078df3257e502ec5430ac03c028925828857199927e79f04aa87a714a252b9  data/levels/fantasy/features/teapot.wad
20ccc115019964d4c6fa850c1c192bf058b453db0381a4d5d92566150bd057dc  data/levels/fantasy/features/trowel.wad
9410dd7840f379b9a85bfe46515a3629ff1ef6d26fbb95147f16d575acfcc9f3  data/levels/fantasy/global.sam
9845cf09d0653b1978dea8a0d79468b8a3800e09bac0a202ebd1a9fd2214e8a7  data/levels/fantasy/hoarding.wad
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510  data/levels/fantasy/miscmesh.wad
2b51f5e5de61052ee36123cf3ee926be5f987ee0bbe6f3f3820904d81e6e72c5  data/levels/fantasy/queue.wad
95f5f5bd57cd9ef28ff8cc4ba66e9a16897bb2947a2a65b517eb2d08389ade44  data/levels/fantasy/rides/Online_Rides.sam
e72531e0fc9b54fe2722a6aec913ae3951f0f12c4bd199bedf04e8ce92cd01c9  data/levels/fantasy/rides/Rides.sam
313342851f117bc0cb4c1f86029f7d2e4e208ac88f1fff49747c8bc9a6c35c8f  data/levels/fantasy/rides/_acorn_23.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/rides/_acorn_23.wad.finfo
b82049694bd69484fbfe7dea7e9d0cdd89b0dd659673293b874a10c786f4ad19  data/levels/fantasy/rides/_flyfoun_39.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/fantasy/rides/_flyfoun_39.wad.finfo
22b4917f70e8b4cfcd0f2353a4b7766072cf5353345b8d1348787f761ccd329b  data/levels/fantasy/rides/b_drip.wad
a5d7270299dc35b95f79e9650e4dfc69e003681ccc8a33e8df6cc990bdf22f62  data/levels/fantasy/rides/bbugs.wad
260b28e9785c8576bf3310fd07fbe53864da8960df0e9c06bd2876a49ac7a8f8  data/levels/fantasy/rides/bigapple.wad
c9ffef096d0ee1a6c15f963c9df2c6d6967ab2f2f0c5292d9cb91205da761589  data/levels/fantasy/rides/bugstv.wad
49318a91169607e71c5324752fe849e522e292565aad4a723febac2c3e502883  data/levels/fantasy/rides/candy_c.wad
cc244b28c09ac899705e2ad640479e8f4840b314abb6f8909bfcebcf3ef743a2  data/levels/fantasy/rides/cat_co.wad
291233c18b4985cbf8f0a3c27d8cced417a0d95292bb57476670a82b5be4811b  data/levels/fantasy/rides/ccride.wad
81b8e5d2cf43d7a022c0b1a1c91511c7494df21c445369cf87d28dcb8a0de95a  data/levels/fantasy/rides/dragfly.wad
5146e580c014381083df621b84dc6cb19bc1c6d446f69a8e8dba25438ec0600b  data/levels/fantasy/rides/flamingo.wad
7331f2efa392a2b8d387fbcf8a7455f82ec70636d596fcbf1220413c9ff1278a  data/levels/fantasy/rides/flowspin.wad
e9c7dddc86b0687df8cb26f02cc44bc8d1c8a0b60a726af43d9cb74b207b8ada  data/levels/fantasy/rides/fwheel.wad
dfdbb4534ce6da783fd431dee72c03e1ae8372f423c26750e2d7d4146ecc51ec  data/levels/fantasy/rides/gokarts.wad
0cfd85b64a3a3ca4a33c8694367e3bd8ad0a7a991822ab6ef91e419786fb764b  data/levels/fantasy/rides/jelly.wad
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5  data/levels/fantasy/rides/mystery.wad
7fab84f3585e2cd59e0dd38986fdbf2210995f7fe252088a990f6d6588e0aa6e  data/levels/fantasy/rides/twetours.wad
047e92524c0e88d32fedd0bc257ea12cf7bf6e4a22c484f615eaeeae5661d056  data/levels/fantasy/rides/wateride.wad
6a4a2695cbf6e0c9bc6591947c33685fd19a61f1350613b77a9353a070b44268  data/levels/fantasy/rides/welldrop.wad
57224eaaf1f2894673e9e893e08d6972e02e48ec530e8e412ba769e6fd60bdb8  data/levels/fantasy/scape.omp
fecb402d9eb4d5d3bb924432853e0ceefb4affc7458d43c495c882be6498dfc9  data/levels/fantasy/sharetex.wad
f22eb1e93f9dad617c398c8cb4992510b27bddb9feade9f3e0ae523aa33a64f3  data/levels/fantasy/shops/Shops.sam
ec2ffe6471f5e7493ad6d49384444f582aaf2ebc087e676d1709b1a02ec4a1d0  data/levels/fantasy/shops/bee.wad
cdbca057918f8ea6a482492e0562f5d4d1b16588d710f02258b2e64fdb33963f  data/levels/fantasy/shops/drinks.wad
48ba55f9dfad14c7ff6d1ee3f5f9a7bb0c0acaeeee3fa5363f4361973c01ec77  data/levels/fantasy/shops/fatfairy.wad
d5464964e7137d70b83a57c43ecb0a02c733d3c3bd87b8e0a3524fdae2dc62e2  data/levels/fantasy/shops/fries.wad
b5ef861af30ece1fac02d9ca7353d5ed567b0daf138fd43c5b4fadf886e0d8e4  data/levels/fantasy/shops/gingerho.wad
9909e9d0b22aa9b20cb1ca1bc84307f3580b7c9d72fc98b0ef66a6db0ef31e18  data/levels/fantasy/shops/icecream.wad
396222ed752ff862e76d1986b2a7bc66480b5ac1d1af6ff50998c3ffa3e6faf0  data/levels/fantasy/shops/purse.wad
d57217555b48b8e980e7f87a5db3e6a211eb3c764a4f2d6733ac26ac7eb504d3  data/levels/fantasy/shops/sburger.wad
ff46e41ae1a8b203a26b322c6965c8836e1eaa791d6e0fdd9981a487248267d9  data/levels/fantasy/sideshow/SideShow.sam
63ea3df54af138df47e8f9972d85e698778e5864ed10ea390408eeb3c9019721  data/levels/fantasy/sideshow/arcade.wad
70539b05af38ae0c68533654e55a6e0cff8d342b8b942f14e50828a340261b6e  data/levels/fantasy/sideshow/frushy.wad
be2db97b76bd916e002c8e35e9bd9ac8f6f362bd741c71d6de3882877097a42c  data/levels/fantasy/sideshow/squirtem.wad
f95db66e38dff3b598a27b39a5693466cdf81008b8aa80542f7e11e070e244d6  data/levels/fantasy/sideshow/strength.wad
8b9d1562bc082e25d250dddf0bef33ffbda98b35c7c388a282716029b93ac353  data/levels/fantasy/sky/sky.tga
eae11045eb295c0102ff23fc2e6aa5f30ecd39d7414a240866faed4adbe07ed9  data/levels/fantasy/sky/sky_cyl.tga
cd1ce80746ce8dcf2e943fce1dc6b2540a51a8d628c9f9f542a455c4b1afd332  data/levels/fantasy/sky/sky_rgb.tga
e2627c153388819a827b403320e9e5ad5546848889e12501f1e2d39515c20103  data/levels/fantasy/ssharete.wad
2c29537b547399d29c2a764185ebb3b5102482b0ce4f8f36f01ccf1764203cf9  data/levels/fantasy/ssky/sky.tga
20da9d9b49d13599825ffd5a53250fdb01a271d752f1677144ba34fc78af9ed1  data/levels/fantasy/ssky/sky_cyl.tga
3bcca26d1bf04f2ae106ad14edee967047e961ebd203c0295562fc257b9f63c9  data/levels/fantasy/ssky/sky_rgb.tga
386f146985ee1eefae080c106da19d5f1b16c8620886853975ac98ff4fbebe7d  data/levels/fantasy/terrain.wad
f60ce2e38f25be4cc586f13d704f7a0d42b5439bffb191acc724a9748c987a19  data/levels/fantasy/upgrades/Upgrades.sam
82fefebcb0d36d48d6bdef14765b0ae104e193fd18283d9e2f96632abaca5853  data/levels/fantasy/upgrades/beejump.wad
6c66137220e2595fd16912a2ea8348ff5eded55f8fffb38676665adc991973ab  data/levels/fantasy/upgrades/honeypot.wad
b6fbf2f444517e4534c0e9c07ea308b467c1aa572b4e093b9ba7105efef45876  data/levels/hallow/2dmap.tga
ea671487b4d47e6c06190ce31d9732ec593b84c5c6aaf2257a8abfe904adafab  data/levels/hallow/EndHoardings.sam
de19d199eb50792e3e802831f2bf8bc62c5a20527e81dd19d9a91f66fa71de43  data/levels/hallow/Hoardings.sam
aa32f7cda25879750b9f04cceab799fd1e0ba46a7bed8e9159e74ecdb63b3878  data/levels/hallow/Music/MusicHD.sdt
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4  data/levels/hallow/Music/cat_musicBANK.map
70000d62c4d9d7bb06b6fa643ab736fcca3037b75588468a233feef000cef71b  data/levels/hallow/Music/cat_musicSFX.map
10e4e5b00b02bd8ada59fd6e63f9d5c33e70a38c5801cf3d3ac9e68806bf98f0  data/levels/hallow/Online_Standard.sam
b5979baadb698d97148ba2b6c795939621f823eb89c0600cc1a21d99cbc0d157  data/levels/hallow/Sound/AmbientHD.sdt
cb31f10a2b1f185043e620e770dc62610eea6b9f3cc7b55985e19c5d27c80c65  data/levels/hallow/Sound/LobbyMusicHD.sdt
6fe2d506ea852dbc892a70db331ac89fb4733e711be87740eaf8569be2683202  data/levels/hallow/Sound/LobbySfxHD.sdt
27813f0cd37596637b81086e8c34eaaf51726c3c8b85d07c29b437d539d6e26c  data/levels/hallow/Sound/cat_ambientBANK.map
3be6a1b5e995ae6b54645922704412f1b10184ec4ceb20498d33deaf1fcc7796  data/levels/hallow/Sound/cat_ambientSFX.map
f0affb542195f0b4a0ba65a90139ea8166a8be4224e845d1614201ad869b1b10  data/levels/hallow/Sound/cat_locallobbymusicBANK.map
0e81b0f9d8afff11b10df4d3517c2e1179a2474ca0422686574eaae02c01195d  data/levels/hallow/Sound/cat_locallobbymusicSFX.map
589a4c72bc3ac0cdf25b645d88818ea7ab66cb9fd9018ce5a81fec9fee3a5cf5  data/levels/hallow/Sound/cat_locallobbysfxBANK.map
84d74b26b22e6f33f10b16ce289b19c1a45e2c43d23ddf2ffeafa98524544968  data/levels/hallow/Sound/cat_locallobbysfxSFX.map
9819b1e14294594630276d6735976205cae4f6335ee100e9f26785a59939a2e9  data/levels/hallow/Sound/cat_ridesBANK.map
9f4eb46edc2ef19c7d36d8b98a7d3bee5176ea129b758a35d3c643c77b9fd66d  data/levels/hallow/Sound/cat_ridesSFX.map
b60aea06d34218fe83f05c4282e4f69dcca9c9d5b1195ad22874139f1fe2807d  data/levels/hallow/Sound/rideHD.sdt
a1f233e18644b5043dd892277b55419fafcd73f02685423836ef7017c91f2768  data/levels/hallow/Sound/sRideHD.sdt
4bcd7427130b79848709037c2982f1ff2ae9413d1d89993b5dbc01e34ea55702  data/levels/hallow/Sound/xRideHD.sdt
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa  data/levels/hallow/Speech/cat_speechBANK.map
14bd978ff1f25ef46756b3430d1d49e0217eae4bb3c63178eaec94dd2910f26a  data/levels/hallow/Speech/cat_speechSFX.map
d6be53d0ed0e1f6d2de96983dcc24e62d1980ee72b6042b51e3198551898bec2  data/levels/hallow/Speech/lips/sp_001.LIP
6fd61aaf54aa4b6ffff6155dbcf83a0da08a85ae2afd462b6299a2950c86c11b  data/levels/hallow/Speech/speechHD.SDT
ff9dc009c0c608f55b99f70777af736887a5d0b3e5892af19b43b080d25a9733  data/levels/hallow/Standard.sam
ddb04b09eaac414fe5b66f84a16538059a60ebe70b5a2716688073fa2911d1e5  data/levels/hallow/advisor.wad
668417dd0aeeb262a0571a51dc89b0aab030a34e2fc2ccea080763ae7e80128d  data/levels/hallow/dynamic.wad
ca51cac74cabd4d5cad047a9887dcf29516329b3ddb2a4e8ef4f024abe6ae398  data/levels/hallow/features/1x1rck1.wad
9e247114011fb1d5abaee82f42484dd235bc85f525f618d67124a98b3a37d023  data/levels/hallow/features/1x1rck2.wad
57244b2e1955286b9fab860f5db58f542d9bb49958cea1215c5526b08f790024  data/levels/hallow/features/2x2rck.wad
568df9310768e9148332c0e7d1f7eb8d49d01bf64ab24655764dcb3b49e121fd  data/levels/hallow/features/4x4rock.wad
a128b331b8250d7d0be88d766361f9a57642ecc7b66f52b1f8a22a02f616f840  data/levels/hallow/features/5x5rck.wad
45e564f37e5d207da8ec4340e529f705a9d3dfe710d4ff8414817b14b7979bf0  data/levels/hallow/features/Features.sam
506eef30f8dc628c5e02c69ad7c4fd4c9f814007e67a86d983085c07f4ef8df3  data/levels/hallow/features/bin.wad
7b80404596ad68c20575b1c3b2edfe0b0017be169e49aa8170fbf21c8812c360  data/levels/hallow/features/bus.wad
bcb21be67ade7b4875766e3d4d34fa1ba34e6cb261f63bc98ecce6f33417b266  data/levels/hallow/features/bush.wad
5606ecfa64679634358e8074ea4374199904759899321c44ee958ec3d9d32d17  data/levels/hallow/features/bush02.wad
f3f5daf7594df1418d0e13ba84b3b312caae8e8acca5a383302b311e835f2ab9  data/levels/hallow/features/demstat.wad
3d662ee2fe0a5ad35229b068926565aefbb43b5d8ff8da7ea765c1eb658a703f  data/levels/hallow/features/end.wad
cbefcb97eb0099989c31efcc785df1afdac44634b6f27f413039ecf635ad862a  data/levels/hallow/features/ferry.wad
4ee267d23ac7fac688d55c12f12d1aea32fca5cd4dac4efbe4a49ce14040a644  data/levels/hallow/features/firework.wad
b04b7bd10094547b963ef203cca035bae064b4d5ef19b4990840ebe5cda227f0  data/levels/hallow/features/gargoyle.wad
17cfe422d714a4f741e4e76b272b7b39ba09a14ad716948143c1ce395530d4de  data/levels/hallow/features/gates.wad
5fa46c003ce1c1771b4b840e6a122bc1903c37a217c567e6c97bb1b36355c4be  data/levels/hallow/features/grave.wad
d177331768fbce2c8612cf99152a8c9eeec5d425c23bd546efd224e4dea69238  data/levels/hallow/features/ground.wad
e7e2b389c28a2dc58e375fcb441cc3f668bf83390e562fdd4387c957b5f45012  data/levels/hallow/features/groundc.wad
36ce7093b8704c3735dc36fe323d2082c62291069dbcfdaeaae33f43c0a72636  data/levels/hallow/features/horcam.wad
a7077331fe45415175df0be3cd902b9a723a4e7c8c87fb7024b64dc85b3c096f  data/levels/hallow/features/horfount.wad
57637ca20102f7d6714a4cd4a6cf32b5f48fae00d964b758a35907ffe2a9920f  data/levels/hallow/features/horloo.wad
aa192cfff6c42002caab2fcc2dafb9d7c4efdc7cd9ee8ce71900d0d910e38470  data/levels/hallow/features/horspek1.wad
66476ff4dba84234be58ba50db94406376dd853cdedce60608343d98159cb655  data/levels/hallow/features/horspek2.wad
6c745c1b8534be2b394ca4084680800ba3f63c0c12a13576ed554671a132eb9f  data/levels/hallow/features/horspek3.wad
3cdd45e70fa19be9b14117019555454199aae259c6b6470af9f27eca41846a6c  data/levels/hallow/features/horspek4.wad
b45fa99855f7db9baee1c434fa760179dc9be293cc2e63c961a0de878c8550d1  data/levels/hallow/features/horstaff.wad
c61b31007ba29536623ce1c5416ab4a2b8326952d02f801d3e9df50eb5221cff  data/levels/hallow/features/horsuloo.wad
4885882ff90c97c710bcd6e90d5034d4781a6fb0a738fd495d202e1fd79e8233  data/levels/hallow/features/lights.wad
ea893be439998b37fae8026d1061c2d539e50e08712e32325438b875a706a773  data/levels/hallow/features/ltree.wad
b95fa3cc638eae9935ac5090d6933caa3a2a81ce8b5dcce2db96f9668c27adab  data/levels/hallow/features/mtree.wad
cc944e8e12880c739580482b9ff0ea18f8bef99a616f11637913af2fbf68088f  data/levels/hallow/features/pumpkin.wad
21043aef9b221ecb1367518284e23dad2a9f875645ae99d57fe9add653bff26f  data/levels/hallow/features/s_tree.wad
c2ec0bb397dd6e56e35aaa671be2ad10a0fe89e645844455cc7f16e779ca25ce  data/levels/hallow/features/seaplane.wad
31e1443632409d3698cefa7f8966df1c34f4c79604ac7775641a40e6973db290  data/levels/hallow/features/sign1.wad
e005c7a8e35af239f5e61bb56df432fd48de4adaae48d43a044faf0bdfd38d93  data/levels/hallow/features/tower1.wad
b4e5f4e2d34edfbe18d249b050a8253d45c873fcc6314167b6afb1dc18286aca  data/levels/hallow/global.sam
332cd0115bdd7413e971b2c7e019bf46ebbcfc9730c045657ccf4c8efe8043d3  data/levels/hallow/hoarding.wad
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510  data/levels/hallow/miscmesh.wad
06dc7df1b7d50e0f7515a73ff26f166c894ad2692d20db61e04a30275a770ec2  data/levels/hallow/queue.wad
95f5f5bd57cd9ef28ff8cc4ba66e9a16897bb2947a2a65b517eb2d08389ade44  data/levels/hallow/rides/Online_Rides.sam
d6c4782462110ef40bdef9acff1476f97b74187ff1c9651d8d032a3c9fe29174  data/levels/hallow/rides/Rides.sam
63adc19f6dcc8158540c278b24769ef204855e957d75ae7e9158d0f0e7bf89a0  data/levels/hallow/rides/_bellt_10.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/hallow/rides/_bellt_10.wad.finfo
7ac1567b7a505f4ca0a217d92c807dcedc1a0150025bf1dcb16d421bf6473ed3  data/levels/hallow/rides/brainb.wad
628d5bfd226edcce95f273406811bf063475223c0bc100d272f0ac70fd8c5949  data/levels/hallow/rides/bug.wad
cb423962417c15aa353b47f1233a31ce8f60fbded9aed9763c8dafd69f3c1e23  data/levels/hallow/rides/bumper.wad
afba96b507eee15dc7ab2fc894b930a94538c79ba8aec5a0019a5be3b51d820a  data/levels/hallow/rides/c_hade.wad
a117e7d916f59212f8f9971fe181245d877fca36a2eb39a28e350396ca5e205b  data/levels/hallow/rides/c_scat.wad
2332d9549817ed36a74dfddbb22515edb026d73d5eee4ff75b02cc23351a2902  data/levels/hallow/rides/candle.wad
8b08cf45ad95e1d64041036b51a26954513eb410e3c6f647be5bae746add420a  data/levels/hallow/rides/coasta.wad
740a8b8199c424585c3d205dd5041d92c2c8131fa7c97087b2e6e88d0d79d96e  data/levels/hallow/rides/demon.wad
a9d5ee54b646b502e4e65eac0c39a5285b052c554444d3cba2c2e63cb7578983  data/levels/hallow/rides/firepit.wad
c5cefdef95ceb9ab4173d9ffe79daafeb16078ccee4e9777172be3787d919ccc  data/levels/hallow/rides/ghostshp.wad
e3043e554dc892473ec176b9e354a70cf8eb47a5272beb013c529bd1e6af2792  data/levels/hallow/rides/gokarts.wad
eae300bd3e27b91c7dccdef946925d065b104293c50d79d11926b1f4acc0cc96  data/levels/hallow/rides/haunt.wad
87a655929a350b0dd0884a971fb6f7742122628fae28e88e7264a9ed6ce0bff3  data/levels/hallow/rides/hcentro.wad
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5  data/levels/hallow/rides/mystery.wad
90a0867707206a76d29e7229c65ea4756bbb55397d7a5a7c3076e2f2bee59792  data/levels/hallow/rides/phantom.wad
a07750b0782601f37e52672882434240437557b0bf4cb55541330cde54e9cea5  data/levels/hallow/rides/pumpkin.wad
b5e11e401561af28ed82bbe1d8fccf18373e86bbb2ed5fa82bb8097fa55d7c21  data/levels/hallow/rides/ratrace.wad
23a3fb023ad3a9cd264e2f2e4a2b46969c9106bbe36efd1a8dfde2fb2f77bb41  data/levels/hallow/rides/spider.wad
18622b40252ffac8b6bc8fe1ebe4f173c558e58a6ed7ee9978fad525a9ebdb50  data/levels/hallow/rides/tourride.wad
eefe9044026e73a02788dbb4d9f840e5cdaeefe5dae7090c6340f659cb0b526c  data/levels/hallow/rides/wateride.wad
554b9252aa67c85041ac5fae5482ebbb8e1a7192ca8e9e81c23bca2fbd139949  data/levels/hallow/scape.omp
e2adc1375a1e896ed4911ea9a60869b7c7fbb496cd88adefa14732a9f121b754  data/levels/hallow/sharetex.wad
cfee8adad2867d755362fa8a3cc8cde79a7eec443c8b2caf6948d68db2676122  data/levels/hallow/shops/Shops.sam
c0cce66e2c9a3e0a2a5a6a40cd1ff714a289d44d416f121123e4381f3832db56  data/levels/hallow/shops/burger.wad
6d56622089277b96446c7e3bb96a430180e9136d81d99eb0a70ab7ced285bd76  data/levels/hallow/shops/cauldron.wad
b87c3bc2ca38b6cf49435ab5c136a53deb0ac09c66bf6fdd72b51af76bc38f03  data/levels/hallow/shops/gchips.wad
abd6d4809356fedd3eee1cfa7bdb3062b621e7db10b8f636ae3c8cc75e7922d6  data/levels/hallow/shops/giftshop.wad
389d418443080af3e87e0e5f4ff4260d6c88469ef4a5b38c500b49700a87c44e  data/levels/hallow/shops/ices.wad
ac784169f5c13eb3654a0a5493be7141f2bde1e746cfb5dbd044cc81d8995396  data/levels/hallow/shops/pumpkin.wad
8402e8f54d293e53675154db8ff1277c5ae6b49cb16fb48b62e460476184c098  data/levels/hallow/shops/vampshop.wad
af5867c3a30542f813524ba6e4d1cc2cf51416f571f1dc68158990c531fd77c2  data/levels/hallow/shops/witch.wad
ab58248b468e43c7f9d9612f2c7cb76e3b7f447d79f9f03510b1c80d30626f6d  data/levels/hallow/sideshow/SideShow.sam
ad8252dce8a0e549ccdc5180d7a6750669ad41036b89d5fbe16751707af0095b  data/levels/hallow/sideshow/_shooter_13.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/hallow/sideshow/_shooter_13.wad.finfo
d4a05e18557e2073d25dcc66ffcd927a1ed67ada89096fbc33fb4f8ff1b3451b  data/levels/hallow/sideshow/arcade.wad
12408345e30d89c7c2ae55b4125f0c07389efd1467fe03cab2fb2d88a6ac5eec  data/levels/hallow/sideshow/ball.wad
b9a4ff64e0b3dc8fad2167030f722876ba1923d6ce027bb6a50efe48e055637b  data/levels/hallow/sideshow/puzzle.wad
168c15ac0aee291f1e922306f4bfae31fd99ae3e1547c58eea3c3605044c9556  data/levels/hallow/sideshow/skull.wad
fd7abee273f5b35f0164f23f56faff4162dc8390f3c89b45b451d3c98f01e25f  data/levels/hallow/sky/moon.tga
7c1dc892a3f25623947b55a48601e2c6484da48c84c03cddacb6848feb11e533  data/levels/hallow/sky/sky.tga
3a6a5dd239d24f7fb7970249ca21e09b53a51afe4f91312fc4485802c07f68f5  data/levels/hallow/sky/sky_cyl.tga
7f2a880ca26dacdb965f63a67086ba39bc04663786828ed8097a35c7bc6f4e22  data/levels/hallow/sky/sky_rgb.tga
07c51ed18fb87b27c99174ebb5e4124afed614aeeb60c0e1db4e1b345b29bc1f  data/levels/hallow/ssharete.wad
737f1528e33e0775b16a966d2a7d4f19aef9a44919501f982e53863379fe13a4  data/levels/hallow/ssky/moon.tga
e47f1bad97d0cb0aa11de8bfee7a53d665fe3b5f2bc3113527de3a1b01116de4  data/levels/hallow/ssky/sky.tga
be8e3e97d9bb328a96a56503d999dc8b5f16db9440d691848cc6f1f6c551423b  data/levels/hallow/ssky/sky_cyl.tga
f2ee928cf04d1b1bd3c310b62cf2f6703c8b6b829041bd593f7be5cfbd5205c1  data/levels/hallow/ssky/sky_rgb.tga
76a1711d05aefc9faa34a666d1c781a04a0913945027129c3d0104b11c5cfad9  data/levels/hallow/terrain.wad
0e1d537dfca3695a75a71b9720d7b0a9ef5466aaa22f5c0c877e9b2e6f844911  data/levels/hallow/upgrades/Upgrades.sam
04dc07884e7fec7fdc4ec528fe6e795921b1e84780130be0f0b6d520a04eef0d  data/levels/hallow/upgrades/chopper.wad
e590d3ad3eba86ea63d754ac21c49b36c64186d0bbe68c7f6d73b3ff07525f37  data/levels/hallow/upgrades/firepit.wad
c3aa27fea0e43eeead3560b64335b8ceb7b9a45689085feb419589b2fe5fac78  data/levels/hallow/upgrades/ogre.wad
0a6e93abc2aa03c355629c939627f18e43fcae1fceb0dacef7de5a3f4d4a2403  data/levels/jungle/2dmap.tga
855ca41c493cb91ea41b2ac2d673e6b0163e4ac9b8e8e4ecfa159c310dfbcea0  data/levels/jungle/Easy_Standard.sam
6d89303d098900364bf5e80b236b64bd85976fb947e9e4609d088547f430b39a  data/levels/jungle/Easymode.TPWI
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01  data/levels/jungle/EndHoardings.sam
6c45aa5313609f9fab6fb3160345f4c2a6cb39b5689f45a4ce764b473c006ed9  data/levels/jungle/Hoardings.sam
0bcd58d1e1df303fda652a9ab70af2b6f0a37f4ebcab476824eabdefe453f0ad  data/levels/jungle/Music/MusicHD.sdt
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4  data/levels/jungle/Music/cat_musicBANK.map
efcaf43ce41231e9b5cec11f0b913cc8e364797c9ae3783d7532c05751f1c68a  data/levels/jungle/Music/cat_musicSFX.map
652de797d4a679bc7ce2ba4240d4a11c19fc5f09c08fd2ffa31463c305dadc28  data/levels/jungle/Online_Standard.sam
43493cda17efff7d89c933ad72bfc210ebfdebc260c2a71c03e97e8dfb3b32db  data/levels/jungle/Sound/AmbientHD.sdt
93fd93030d5f6ded0d6bac0d8e6733a66319cc75360bd4d562be07ed0acf01e2  data/levels/jungle/Sound/LobbyMusicHD.sdt
905ac080822fe120b6f0f4e54dc5524ac024609d96d8571eb38ace950c94a1df  data/levels/jungle/Sound/LobbySfxHD.sdt
c533fd7d1c1aac985851e31b15afda5975d65a11ad438d39a11bf853f034f93e  data/levels/jungle/Sound/RideHD.sdt
64dc53f8abacb589ea06ea95c9ad85a81c332d52bc5b6755fb8c9475819e815f  data/levels/jungle/Sound/cat_ambientBANK.map
09befc06792ba20f5ee0b5eeb8ec0fe28f728b1ac3da074b4a3bf3d25bcafee6  data/levels/jungle/Sound/cat_ambientSFX.map
332aeb206da290b8f75486481d57e7a1bf38d91977416a07ded6f236093b1fc1  data/levels/jungle/Sound/cat_locallobbymusicBANK.map
65ec4f7a954ea59f1366472ac46a86cad028c7feba0b4c31dc841437583c3af5  data/levels/jungle/Sound/cat_locallobbymusicSFX.map
bc7ec091839ad99b2bd98a6e261844e607559fd57c0911911c3e16d03fc47559  data/levels/jungle/Sound/cat_locallobbysfxBANK.map
d56bb63191d6c325a6093993eacfcffbf308bce62b89280c6d8c693aa4f0aa3a  data/levels/jungle/Sound/cat_locallobbysfxSFX.map
58db355488ba95e0b26d937b0533d8453c5ee8fe0d1504d13b5209e76e35f3a3  data/levels/jungle/Sound/cat_ridesBANK.map
c6ebbdbaa12a210dfc7443ddf17fff501024ec021810559f57d30e7fb11d3c8d  data/levels/jungle/Sound/cat_ridesSFX.map
77d2164d0e5d3ccc714a7d52716ac5ea9d6b18ff543790d8579f96f8195af280  data/levels/jungle/Sound/sLobbySfxHD.sdt
d1883119806336eddcfa87ac5727ea264e23d2ed093e80f20a719df3779d053f  data/levels/jungle/Sound/sRideHD.sdt
32e835ec4757fc856c5aff1bde87bb503247c8c9c061b41819cb7822f6cb80cd  data/levels/jungle/Sound/xRideHD.sdt
77d2164d0e5d3ccc714a7d52716ac5ea9d6b18ff543790d8579f96f8195af280  data/levels/jungle/Sound/xsAmbientHD.sdt
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa  data/levels/jungle/Speech/cat_speechBANK.map
452515cbe59473efb6e7afa98016e3326dc010ebe35348f48ef486670c7c1364  data/levels/jungle/Speech/cat_speechSFX.map
f3455533f1c2c3d214484a40c3a0b712cbbb97feb06ee877dd0d860e20c2ce2b  data/levels/jungle/Speech/lips/sp_001.LIP
4e20b65a14bcd245675f8313b0834b8c8799538232bb1d5c34527fdee8a99f68  data/levels/jungle/Speech/speechHD.SDT
16ed44c82c7508a77cd71bd0a4757ba70dd78e833972481f5aa3b65413303494  data/levels/jungle/Standard.sam
18f2ae759adb4c8692f672f7db6eecc7004edd23a66fd4d1460722744cbfceca  data/levels/jungle/advisor.wad
86565a3111b7329e0ab2263ca48e5bf7d3e8df9043fd7162810ddbb3cbc8f4ca  data/levels/jungle/dynamic.wad
8b44a231b128683dae6079ce67130be31e75c4cde5a2464a9f837738fbc02383  data/levels/jungle/features/1x1east.wad
f0365995619751be0102eddf87ce2fd79a0abd0fe8473a19e9b95d734162fe90  data/levels/jungle/features/1x1rck1.wad
3eb1c8bc800cf335f5526eadbdb4c24766cbdcc534a034b9e480552b535c54ca  data/levels/jungle/features/1x1rck2.wad
4e5a3c84e463a2b6d1523497c243b94f3d17ec40ae8df8cab5ecf0005498a630  data/levels/jungle/features/2x1log.wad
6b106cc3f660a3d970476fca6839961ce12c4861a2980f975f4385f1c879087a  data/levels/jungle/features/2x2rck.wad
2f42f0814f01a92e15c7d7897556684fff126d9dc47fdbdc049992a4e6bcdc81  data/levels/jungle/features/4x4rock.wad
1bdea3dd3839e751fcd6bd28c59ffd92941181c870485fca9ee847c6064cdca2  data/levels/jungle/features/5x5rck.wad
32e39f5cb55fb573211b9aec05b5b594f4d1702cc16823d478de9aad2f7cb8b6  data/levels/jungle/features/5x5rck2.wad
d983f687b9bd4844a8060c37417417a37309581277efeb92c0b31d0e076a2616  data/levels/jungle/features/Features.sam
105050dc81e73106980b89b833fbc76debc089ce96900874bde17f382499253c  data/levels/jungle/features/_bushiii_2.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/jungle/features/_bushiii_2.wad.finfo
c56700ccbc06eea90befe0b99a6999714a4843a9dae2b91fd53fd3c1889a2718  data/levels/jungle/features/_bushiv_3.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/jungle/features/_bushiv_3.wad.finfo
6510d4be0de6d7899493ae514b8eee4aa3005a3de70173e9de0f29f7aa68b86e  data/levels/jungle/features/_gidol_5.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/jungle/features/_gidol_5.wad.finfo
dfd4322ce646c42a049647431b83e9d118803810ae48d73417238fe3fd594090  data/levels/jungle/features/bigpalm.wad
3d542d580bb259eb342cc3775d12f56a0c6bd927bbaa9567c0f2d90cd74f8894  data/levels/jungle/features/bus.wad
46e344d0bb3642aa15e463dd251134d7dd9c2b47c6ce79609e277ebd6b5e8dd3  data/levels/jungle/features/bush.wad
302e2e91ab2dc221acafd0c64ccb5e54784c690be245cc63692a32669dc5b3b9  data/levels/jungle/features/bush02.wad
a926798b61755c222f51484a143301fefa9be6d34ae8621661e08615d8c59927  data/levels/jungle/features/camera.wad
a1898729b27235fd1d4387366311be744e54584c9aa65db964eab7bfbba63bfe  data/levels/jungle/features/end.wad
c5efde57bf036becae1c99422055ba22471c61441d795cbd59daa1229a7d6b13  data/levels/jungle/features/ferry.wad
5d67be2b5df22015cdf36048c22158b056c7005b4d41a7296bb33eb0a2b063ec  data/levels/jungle/features/fountain.wad
1972df27aa799b0c8b70d276f3b4d26bd1f4de6ae059e1e5875cb79f8356d521  data/levels/jungle/features/gates.wad
aa60fc70a48ed812b8c55f93f2f4d92a4bb6607caf6c6751c2109ddba899f769  data/levels/jungle/features/ground.wad
1b45e3499899f05d1d4bc89485b3debbfec02b48bdac76e5f4114c45f570db24  data/levels/jungle/features/groundc.wad
d2882157544a270a341eb54d2253724d6f5efa198b743b402a60e981c0df64fe  data/levels/jungle/features/lavspurt.wad
e6fc6727edf390d8f993fbb4c71306b95442bbaad2ae67bf7a0d4c57bff6b452  data/levels/jungle/features/lights.wad
5751c2401a6c931fd66ca637e2f886a4bb5d207b6dfad426549065b05e581fdd  data/levels/jungle/features/lure.wad
089754fe419fad24196b89b5fbb1ed504ef08b006b14ae4f0a3e0a21ab18c0c1  data/levels/jungle/features/mamfount.wad
b676619630921c21f122166af311fc1757044067d17b68dd99477a1c867c01c6  data/levels/jungle/features/pelbin.wad
2555b58b3a6a611e030afab303c5892741a77b532c99c539b00fa030d91ff988  data/levels/jungle/features/s_plant.wad
b94ece97883c4517ff8769006cf5672e3f408c9fc1ecf0238bb35d83e722ca51  data/levels/jungle/features/seaplane.wad
fc40f9d9c69932552eac8c311d60007bf4a873b71c04b12354f1527ce70f3ef0  data/levels/jungle/features/sign1.wad
cb87059fe35788d79a71439031925424d62ffd563218b5ccf4f6d5f1e6c24231  data/levels/jungle/features/speaker1.wad
782cbb34ac2b6dd22d87db0f104aeede6c8862e4acfa436a7b74b9760b512081  data/levels/jungle/features/speaker2.wad
e00b898703b700810730a8e5c5d6fc2b7727ba976a5c215919aa06263b07771b  data/levels/jungle/features/speaker3.wad
38df97a5c204e2dcca1233a224b8ff909b1aab1a9367bb265f5c281e6e76a578  data/levels/jungle/features/speaker4.wad
fdbf63c0750184b3f52cc69eb751db3d6a662807c2e457b33f121dc1dafe73ff  data/levels/jungle/features/staff.wad
351fce22743470b5646f046974587453fb8bf6d74947bc1b2008f370b8349ce4  data/levels/jungle/features/statue1.wad
64888cde8b290acd279a23bbd10e54c0aea7a3fb79c36808f339287ee8f41eca  data/levels/jungle/features/statue2.wad
a4f458672e24e343b084ec6604732fc17e207e26dd7c59960a58586eba4ccc83  data/levels/jungle/features/supbog.wad
5c81c263fc4ccda08fb3c29ea7608f5d5ed3970ff9e3590cd332374c93abe527  data/levels/jungle/features/toilet.wad
a46eff78223f380d49b100b5002690b6552e52e1390fa34c6406cd7f96b68406  data/levels/jungle/global.sam
242a944a56790d50675e16e01b5d2ba05f5979cafb861c67a0622ab36a1ffc96  data/levels/jungle/hoarding.wad
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510  data/levels/jungle/miscmesh.wad
d4955c8b99187af0d356b3bad9c39c87a4f07f4607824a2b058e8b699bc27770  data/levels/jungle/queue.wad
cdd38445f8c17b936691acc2eab119c8aafcb5ed2461ccedeec1b42abff715fc  data/levels/jungle/rides/Online_Rides.sam
41efb301738461f0215f17b45318ab0113fe42d4d5f9f3ad9e698c662f6f7c00  data/levels/jungle/rides/Rides.sam
3a92cc91a8bc32ec8d5e6359b07d79a126ef973387d32ee06a4681d272bbddee  data/levels/jungle/rides/_snake_1.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/jungle/rides/_snake_1.wad.finfo
d15c4b99a3f6180926d47ca188ca645711ab2a57947e18204ce4fb614986ecf0  data/levels/jungle/rides/bouncy.wad
2d18beeed28a70fe42860c1c0ef6af81300e5d913e1ab6086414624362a2952f  data/levels/jungle/rides/bumper.wad
f6867d29c998aba2f8755102a079242fcb6f96773a7667ebf5d07db41275f61c  data/levels/jungle/rides/coaster1.wad
2b3d165caff3881b0fdbcaeb1abeb8028bcbc752e1726f1be18190fbb64dc83e  data/levels/jungle/rides/coaster3.wad
f6c5e454c864c86e0ea946a00e2ca962f20d4f067b26b37fb28805782b392c8a  data/levels/jungle/rides/gokarts.wad
966537d08258bff92eff1292da62ee4befa903028c1381e2ce9d2168166671c9  data/levels/jungle/rides/incagod.wad
2f71e7c76333a6b58f6fface7e0f8bea1f994562448469fdfd6388cd0eda5969  data/levels/jungle/rides/lookout.wad
c3bd8da541d10e1d26ab46a8fd188387e427ce8557c5466fbfecc306cc903ef1  data/levels/jungle/rides/minecart.wad
3178e6f7e2d84cfd61d26da7d6796e9ce5107b580ff7a6ceafa2b89b92b75f19  data/levels/jungle/rides/monkey.wad
8290d3e111cfe9ba47d82e05401c2e71c34124a5b2e84fcbd10c5c6743f2ba91  data/levels/jungle/rides/mumbo.wad
c69aa17d2ce1941a6d896aa6071fc32f956e38a5ed78793078b560ba5c79a47e  data/levels/jungle/rides/mystery.wad
7e4d1a8e57d2d846780b63c33802cee83413b123beae2fdd2a61ebc8c3d23bef  data/levels/jungle/rides/porkpie.wad
8c1fdcb7c346a06b5d53b99c2e94ac36ad5cfc6b575e2a30b65dd85a952e0f25  data/levels/jungle/rides/spider.wad
2a5bc310bd66aa031ffe2c70283b3034a39559bea31efa535d64938a0dcf217d  data/levels/jungle/rides/totem.wad
10ce16a9c78311530d7e2af1acefe5833c9bb8d958ed968d3630f5b4b6f3006e  data/levels/jungle/rides/tourride.wad
1805fcab52d114a94482e76766020566af6ed30cc5915a6d7bc6c8580d20b5e5  data/levels/jungle/rides/tvsim.wad
d0008845d3040e3a61a8672f65aa39508704a3f2a641e9f9363fecab5f48df67  data/levels/jungle/rides/volcano.wad
77c6a09f429440b9caa317fad44d1bb95270a394cf77b4deb3a5c38a444e6d64  data/levels/jungle/rides/wateride.wad
87387a9baa4ae69b5bd2c1da32941969c8f4042f59557a22005d195a17e56809  data/levels/jungle/scape.omp
78f1dd31aa7717cd6963721f492d25c2ffdf05e441f5e162ba203d3f7868d763  data/levels/jungle/sharetex.wad
0ba53ae24c7785fe2d72cc59a6690e6ca5e7f4c11ffae78de452b12040f920cb  data/levels/jungle/shops/Shops.sam
0a18780822cf32c610f1eb6bd67811e28075ffb057dcd6f7ef628e9fe8de14cb  data/levels/jungle/shops/balloon.wad
24209d9742288af5a5dc2f0074919f87b341e61f29dda026489544c111548309  data/levels/jungle/shops/burger.wad
f10702985f5a09beeefadaa10fa4bd630d7e889e1c1cfedd338dd412cde48ce2  data/levels/jungle/shops/coconut.wad
77de76cac71e8ef8e7aae41382629adedda0cf7343323f4bd402cf22cdbdb1da  data/levels/jungle/shops/cost_shp.wad
af124c07768210ebd909d22f878cd0e6ae4491b8ae9b1d7f8afcb1b9f9ffa4e4  data/levels/jungle/shops/fries.wad
00e4c3d3d14f98e7ba01777f58698b1715e2052a00240562bfa7832c67223968  data/levels/jungle/shops/giftshop.wad
ad429e7405e67895db89a0a2b372d4890bad354c9d573c94f4289622c77262c0  data/levels/jungle/shops/icecream.wad
47289afac2802a4b3f342bee2023af271edd41a2b55984ecefdaa2c8a1ef164a  data/levels/jungle/shops/steak.wad
35cb198555cff48c55588c8cd74734ab7b3fd5c463310c43fdef9bd2d7fc8ed4  data/levels/jungle/sideshow/SideShow.sam
7c1ea99dbc427f85aa949f5aa7ffc8d40a6d09e45331b71255680dfa9ba0aee7  data/levels/jungle/sideshow/arc2x3.wad
850f718bff894d8f31b000675e92a0d3c9e3defc712eb1d7c4afe72594ab2dec  data/levels/jungle/sideshow/hyenas.wad
dcc57666313a2835845e4f3011cf8701f05a3bdd08eb988a7a71f2df31e3fb70  data/levels/jungle/sideshow/junspray.wad
5a5f62dcdaafda66bab46e0bc4c32d302210a614ca96b26d590943264c332b9f  data/levels/jungle/sideshow/puzzle.wad
27480d8ecec9c0e9b170f8d31d0740f1bfdb77666ce217981a379c7a71a086a2  data/levels/jungle/sideshow/squark.wad
fddc53e0fca678139f80837d7a457caee4d116227370fd6eba49d162fe114758  data/levels/jungle/sky/sky.tga
eae11045eb295c0102ff23fc2e6aa5f30ecd39d7414a240866faed4adbe07ed9  data/levels/jungle/sky/sky_cyl.tga
cd1ce80746ce8dcf2e943fce1dc6b2540a51a8d628c9f9f542a455c4b1afd332  data/levels/jungle/sky/sky_rgb.tga
2b2c59a447b13a50f3000761750fbadc66581f5e31abe9169a10f1c0227ece2e  data/levels/jungle/ssharete.wad
37633644515a99440052eb492406bae5b5d14edb040cb280aed18082031cc4b7  data/levels/jungle/ssky/sky.tga
20da9d9b49d13599825ffd5a53250fdb01a271d752f1677144ba34fc78af9ed1  data/levels/jungle/ssky/sky_cyl.tga
67639c744f9267596a2285f0886b19fb8a03eb3649f6743a21a4b696d074d078  data/levels/jungle/ssky/sky_rgb.tga
6a4443e7793122eb3a804eee3f4f1ebe19324199185b2562f5311d0f331c3300  data/levels/jungle/terrain.wad
7dd227b4e5faa5c8cb3195e0afd00b7e7a69a83ee84b9da0b89be2e8dc6bfd6f  data/levels/jungle/test.tpt
cdaa9edf248dbbdce84be30fe5682421ea69e5611956b6293e16b586940f76b6  data/levels/jungle/upgrades/Upgrades.sam
f62040454630857352e0dbd54707f129dfdd4f50e559316064f9871ae0614339  data/levels/jungle/upgrades/lavajump.wad
cc62a4b88f5faf3d41fd9f1bebfd1c870d64cbbfea5e4acd022a26c798e25151  data/levels/jungle/upgrades/mammtunn.wad
9a431cb1501872c5dbe376202c8495c49bcdf3c11ddd3a04fee5b6abba04d46d  data/levels/jungle/upgrades/watertun.wad
598d9795ea7eef59bb133f1c2570c0a5561dbf33fe8f00d213adcab4abc06f55  data/levels/space/2dmap.tga
29caa9ef1436e2c2b24c892778693273999f0c7aa56846e45d7e6d61ac733c01  data/levels/space/EndHoardings.sam
f6e90e49fd1ab6e9c0f1cf6ab4e549e5569e215ab81ddd0ec0d1bd1f4c8fa311  data/levels/space/Hoardings.sam
2e41e7bb95bd12376acc1896745d5745d8aeb302582356037e69206be64f803a  data/levels/space/Music/MusicHD.sdt
ad45ce6ab74cef5c284f2ae6e7be5780d99a3e9fae1608990672b305528328b4  data/levels/space/Music/cat_musicBANK.map
6a7d513b3c49f5d88baf37e65f600bbb6dae6bf85a964c36ed80b1f7dddbdc18  data/levels/space/Music/cat_musicSFX.map
2f58f53e5def0bf769da47d935e0a28f0a698abb62031301d20f268f2baaaa25  data/levels/space/Online_Standard.sam
d242028241e61b0d94ea45d8c783ddf140937bdd515e9f74bc789964c415e541  data/levels/space/Sound/AmbientHD.sdt
10044d56235dc8cddf342caf1e09e7d05a46a352b682161020a4742cea073f46  data/levels/space/Sound/LobbyMusicHD.sdt
3dafd9214141b992a4a97b4339f6270aece20c16d7cc9d14245ca9c8998d387d  data/levels/space/Sound/LobbySfxHD.sdt
0ca239e43a2cb1cb7a389ac4ada0fd585d62800d34e159eca391bfc4da122d9e  data/levels/space/Sound/RideHD.sdt
e7cf534b02a7ae0c131d17e6d0ddd9a1fe8e35084ee2484e944865055324fa1a  data/levels/space/Sound/cat_ambientBANK.map
f7388c3f3ac3fffea9aaeb6be4b3645168fc63f1088c43039955c27fdcbafe2c  data/levels/space/Sound/cat_ambientSFX.map
17f75adeb56002dcb14a12885520e802dbbc972bad72c8d0da519c7be8f39989  data/levels/space/Sound/cat_locallobbymusicBANK.map
968e3b60b5b41a094049924fd47144347d1f43ddb5cd22c47331b8ae7133ca93  data/levels/space/Sound/cat_locallobbymusicSFX.map
69557911148205e4794bf32e592bed562e3da9b9b21f7a326c06601c0d8b14f2  data/levels/space/Sound/cat_locallobbysfxBANK.map
d81e275f865a8b83376f02a143e322d7f61eaf697778c672fd5cf3604c56172d  data/levels/space/Sound/cat_locallobbysfxSFX.map
63d787319bc87bb6a67731b3c1b031f6e26daeab03064b7d8252da0dcc695d39  data/levels/space/Sound/cat_ridesBANK.map
f1d841f35651914fcd64bacf3ec84cc79953c4212b55e0e880e642fd2f903f39  data/levels/space/Sound/cat_ridesSFX.map
15fcadb10a8683e7582054f5730ba10483b9f04a8ebe990de961ae8564bba657  data/levels/space/Sound/sRideHD.sdt
a9f1cef630d9129b6e0d410dedb17eb8b357efb43af8507b92ec36008a919ca1  data/levels/space/Sound/xAmbientHD.sdt
e9dd8685a323c9420f85e127553a06ec9709c86fe61e2840d259292f1c5e7365  data/levels/space/Sound/xRideHD.sdt
ab004468e2a75d3b4201ee08f1a697c13b12a51c996c055ba32b076019fffcfa  data/levels/space/Speech/cat_speechBANK.map
8cc2819082f62595e5c6f0e087d029d475f024401f0b169c31406e1f317272f2  data/levels/space/Speech/cat_speechSFX.map
d7414e592f1b7f5a8e701018375851ed2b8a169e6de9764c041e9592b2702e6a  data/levels/space/Speech/lips/sp_001.LIP
474cb4d9f5a538313fb42ac4c5abc7e2f84204508a8f26ba8367c1044f3980da  data/levels/space/Speech/speechHD.SDT
2f69d590da9d5b14c4b277189a89a95b6447587257c879e07aabab79438544f6  data/levels/space/Standard.sam
a3acdc09357b2b5b5a26ea97d4167d83f816821c0b22ba081f57215cb5f77241  data/levels/space/advisor.wad
668417dd0aeeb262a0571a51dc89b0aab030a34e2fc2ccea080763ae7e80128d  data/levels/space/dynamic.wad
9c6ca1ba29d33b2fa3093d0cd393312690db479eb0613ed6f505b20adc947115  data/levels/space/features/Features.sam
65338eb9b7eea63f9e00a2f88e58e6608fe6235b4f54eb9aff48302aa9b533fb  data/levels/space/features/_morpher_27.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/space/features/_morpher_27.wad.finfo
e7a680f1aa75a1d2b613c0b5b7f45dc3ca20741e85309bb456c175312d3e56b2  data/levels/space/features/_pulsar_29.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/space/features/_pulsar_29.wad.finfo
28b9df39b8eb3a0d9c1df6355136cec2f3c515c3c315e1afd7881a786bcbf081  data/levels/space/features/_robosta_30.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/space/features/_robosta_30.wad.finfo
ccb0de6ffde6be826a2e0e8794b29d27099b9c4139313d59290e41c4fc7e3a05  data/levels/space/features/alien.wad
8096bd7431d8888418e57cde5c23ebfd658807284f4ee267043128484a885602  data/levels/space/features/antenna.wad
7d9f5dcd0ee1ca75f63f28896fdf81481f5e707c55627be42ce86c75a1590d3b  data/levels/space/features/bus.wad
2412dd3bb953918434d6cd8242352ee9f77ab8d694f9baeb841b1b3c7d119226  data/levels/space/features/bush01.wad
4dfc566d106acd301205f2d48f28288c779826615e73c0eeec6aba3716d7dc87  data/levels/space/features/bush02.wad
b794d3db3d07cd84764ce87f0827d7b990dad46e05db79be3753a908acb9172b  data/levels/space/features/crys_a.wad
f42a48ee99f7b413628a56af0ad0571a1adabeebc785a7a8a8773d3c565f8e2d  data/levels/space/features/crys_b.wad
4aa636170895a090e24d27f92dec3284e1a563eb74bd415c06ac122e310f5fc2  data/levels/space/features/end.wad
9d3e69d61180c05ec402209cea6760aa563121616f7b5601c0e8bc3798d92341  data/levels/space/features/ferry.wad
a22c9b8acfe8fb73f3ee212d09b3ca34d7779a03626a0fba66f7232986d6aa12  data/levels/space/features/gates.wad
85c663dc7242ca91e7ef0b57435e01039bd83427917ee4cca4c0a7d3dc640ecc  data/levels/space/features/ground.wad
b6f88896ba7cb21f2e97cf4017899ba85e2197195c6802b705c830aa96608dd7  data/levels/space/features/groundc.wad
bdc690cd960491dd6b8ee04703a14f4d901cd276cc1a1714ac763b1ca980bd98  data/levels/space/features/growth.wad
78b77cdc2db470805a839f6e1f26052c1aaa8bb339a6a397801d4cf1d97faed8  data/levels/space/features/gyrotron.wad
539b73d625bb9e926956814889fc2eaadce9acfd18a360d469f8739974e0f779  data/levels/space/features/lights.wad
3e9a40a8bcb2fe96f49f81958e3c28c56b5a164851325f9f926659c82c37d131  data/levels/space/features/loo.wad
fce0eaa350b366a70f14c44381a68c7c08ab9c11d791be69e95a4246c36bdecf  data/levels/space/features/loo_big.wad
092bc76dc08c7083dfe3f32cfe1751ffc9d4231d7b66f1f11c50325fb34929e0  data/levels/space/features/ltree.wad
08e1bba9e6fc0a0d94bd60ed436379c0218be356dd3cdbd77890541c0b6177dd  data/levels/space/features/plasma.wad
80b57a42a7499524f47ff59e9d08244c78c25ef2b13d48577735acde42339f08  data/levels/space/features/rocks.wad
f84a0a3f36d300f9bc427babe7fc32b6f2bf55053a84b83a8177381e815926f8  data/levels/space/features/scrater.wad
27e6f81b807b3b43aba74e314825ce4bd80c306785bb242914591a4836383f7a  data/levels/space/features/seaplane.wad
0cb4799b972af5ec31ed55bcb2354db2e1b24f7b011fd7436792b50008885291  data/levels/space/features/sign1.wad
6afc1b7cf38ebf2e38133e3e1be63701832a28c88cf4d813f3af4143e5f51bf7  data/levels/space/features/slaser.wad
41d187a273f0301c3067726bf9e6492d86cfea7547b99c1efed23b59a60ae764  data/levels/space/features/spacam.wad
75a9db82df18a9bf1e9c5d4f16680328c8d949b99ccacee33b1a9d114245dcab  data/levels/space/features/spacbin.wad
2d50b4fb3c0a683280d277b1be34cf6e41d2dd2352b6c115a6124c1e207e4ae0  data/levels/space/features/spacspek.wad
fc7cab49d248b32a4e45ada8b34fb4c148dd27a1f55c1040e1c54fd7bae70707  data/levels/space/features/spacspk2.wad
54322f08b9b2c72697bf174105b9a6547b6433cfffcc6627b67ce759e44fc126  data/levels/space/features/spacspk3.wad
98447904e99ec12e01edd1f4a24de59834564046cb8463c49a7944651e7e3b0f  data/levels/space/features/spacspk4.wad
b828e2002bf519c5e68100f2e23ed41a3e6a14432492d0a43b67a59e367c3888  data/levels/space/features/staffrm.wad
780c7b4fbaaec513344ce9d4833e1540027be139ace28439e6e0cd625b91996b  data/levels/space/features/tree.wad
98185d758b077ac56c1fb60f0ee0f6afd0e0378e0a0ebad534188507bc1ac6a2  data/levels/space/global.sam
8f724b7514b864582d53a04752c2584a7c5cb415edf3dce06f56d0d799553020  data/levels/space/hoarding.wad
c317f9a708409c102ec3b9c0e457f38031ebb1c243de0b37138a54fed20cd510  data/levels/space/miscmesh.wad
bf645dd6592aefda4abbe0c44c4c75a5d4660e0a8ae3d96e7237cc640a47721a  data/levels/space/queue.wad
f0d5ba03b980a21cfc428a4e6f217522c5dc93fd7ebc09ab6324598e41bc4b17  data/levels/space/rides/Online_Rides.sam
ec9af893baf7e386e2f2156d765cf458db53f415aca8fba15e524be463e539a6  data/levels/space/rides/Rides.sam
de42e572a02f750cd435d88c4ebbde685d052a7ed808a5ac4afb8f596f54a9cb  data/levels/space/rides/_wormhol_33.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/space/rides/_wormhol_33.wad.finfo
071c966ba78007c134039562b1304ce4f5837636d22b77090a143c7d031c2b17  data/levels/space/rides/bouncy.wad
b5b1c4e984159505bc8c1494ae42cf52b30518ddc896c620cc5d1951597f9a18  data/levels/space/rides/bumper.wad
79a8896b15cc06c00cb493baf3c7740cbf8c9d514e651cb36a0ef43bc850af4b  data/levels/space/rides/creature.wad
bb6e89e2b93bd3445d750fea0f827c921e4108e7b983db2ae5419fe6189b7dd3  data/levels/space/rides/gokarts.wad
8c9682f1c71c7a4088b1e6649f307896c725b7943a7c2b0c28cda3b9cbcdbc99  data/levels/space/rides/hoverbot.wad
32af166e2b447ba2ea6071cdb736bac492412fc82ddb16f09c19b71f64103129  data/levels/space/rides/mbuggy.wad
c3c6f34612c57d47987ca8000d72208fc03b6aa1c144538975a757b3ec68acce  data/levels/space/rides/megacost.wad
dec8cebb7d3a57278a99ffb0375b78d5cd1bac8dd702a0c6838ceb91a0ac240f  data/levels/space/rides/moonshot.wad
0a0f94a5e55dab5a9c70dc8ed900237c45b8e531b89053b4e4a2fb578e65a8e5  data/levels/space/rides/mystery.wad
5768b5cfa94c4cdbd6e3e5b27d0260da68dcc440fd3d4c7ee27281dba90727da  data/levels/space/rides/orbiter.wad
c7408e1f489a9fc478811dced987170b6ce72362ecefcd1e6ea5171bbcf13380  data/levels/space/rides/rocket.wad
ab5cf43a949ed5501c5d478018e572c2af0a528bce8eb6140d172daf855a0413  data/levels/space/rides/scentro.wad
1d6a66eb387b5deb6d86fa77f4fa51e4a8d7329d8b6a4a155749486a0602096c  data/levels/space/rides/scitour.wad
afba22ea9eed278911c83b2102a7b87c021ece227efe2d47a9fb70420d836d60  data/levels/space/rides/shocker.wad
4cad0835de37ecdb057c5dc60076b1bd08fa93afce03287b079edccde37ad8e8  data/levels/space/rides/slide.wad
dccd9e6a107bde3aa5e0cadd70dc79b521bc0dd2ca40db6668080f07c8306f1c  data/levels/space/rides/spawheel.wad
1c42b4bb8dae71b521282171874776336b8c7210509bbeac1f32efc81e269b92  data/levels/space/rides/station.wad
ece8454f393b7c7f88b8b3c5193fbc69261ec6b22abb77e8b15754e94c2434fd  data/levels/space/rides/tv_ride.wad
f14937d7ed50bec1f68059b8ab720b782ed3a6ec32bf7fd25954617b2bbb0a7e  data/levels/space/rides/wateride.wad
2a1a14a7176c5c40b9429fafc3db53c575afa356871d8df3d58698a646ee630a  data/levels/space/rides/zerog.wad
c5e4c1d80367ca3f59fef91b312e6c8fffdee489c919a012a6b84120b7aa1927  data/levels/space/rides/zob.wad
e2f6a2ec89a43b14159386219e2026c915c48649a483faaad8fca9ce43d28bbb  data/levels/space/scape.omp
ae39ca6710984d355abb5ec774be92fa371b6b8203919c585dbd333205fad827  data/levels/space/sharetex.wad
7f9af9b71e13ec0be031de6c8da01f3c3a0f89411b7089a1bd9d06b0682ea60c  data/levels/space/shops/Shops.sam
5bee8c5280d07e877019341d9dd7cc09f04db8cb5628ed10216963e059eb2263  data/levels/space/shops/burger.wad
bd248eee5da8073d2ee74117b510e4c395bf0f072b225849d5060ff5afcb4cb7  data/levels/space/shops/costume.wad
f210d508b9162993afe75206a8e483e6800743fbaa6a80e0f5ab1a6f2e5dd812  data/levels/space/shops/drinks.wad
c166c27607c9ff36fed1633e0da883855a904e060ea81b02262435ac0153a580  data/levels/space/shops/droid.wad
87911ed3d29ac875bcd52f573eb28a8c29a6b97af29fd41d133b020251eebe2d  data/levels/space/shops/fries.wad
a18c0902416c6fc738987520a573f070af68ebf62fb8a82f69606b0c60523aa0  data/levels/space/shops/giftshop.wad
14004dbc36cf16a1817f75e3ded51a7da59b069086ce45b0d83bd38e8511fe0e  data/levels/space/shops/ices.wad
9e472e19dd3580f8fde9cdfcdc950df1c8ed9a2df3ad3e1ab35cb510b9d91b99  data/levels/space/shops/mrcafe.wad
95f9745960c12ae9cffcfd2b0c29c6144fe3116aa1aa3544ea64744849f386ca  data/levels/space/sideshow/SideShow.sam
0eaaf80f7d67e11f64772b57ce5959826db7e490156e429f6e7bf2b445c1ca2d  data/levels/space/sideshow/_strengt_34.wad
33b658d9e77ca24032e65b089bb7307c7e18b8e3d1dbcbab46e75cd100e5997e  data/levels/space/sideshow/_strengt_34.wad.finfo
f2fd1a4daa3ad9871e2244013af3d1391b990ccdc1a1cf11e08a3139adff650c  data/levels/space/sideshow/arcade.wad
9c1aeca04ee34d63c5bf96fdd9b619d3b4d4eb6e4059fa9d6d5d71b61f8ed415  data/levels/space/sideshow/lunar.wad
9b41332fd8df5dab5d9a065bf64b10e2701f67adb451480b944807c7c9764500  data/levels/space/sideshow/marsmoon.wad
664a1e13897ce620705d556baf2c2016f3ed5970bdb1aa394ccc80feadb88dca  data/levels/space/sideshow/scipuzz.wad
3eef8a967dc3020e79b7620e3b20fcfa9ce1ca8bd9f421a2b887ab6583731fb5  data/levels/space/sky/moon.tga
f65a246a4ef8786d06e53d94ba9627e15414ecaa54d855f1ebff16bacc36ada6  data/levels/space/sky/moon2.tga
cc8c403b8a6c8c52840181ea894a733e0f1fbd5f265b76fccdb5708884e107eb  data/levels/space/sky/sky.tga
9f9e31235f5647735d166ddbb5cb7f17d8b0e38e0ba89dbbf50e8c6f9cc37a1c  data/levels/space/sky/sky_cyl.tga
e268f84fe447189d5e4c7967b11a016f8a894e39586d994d4d44eb26da09b268  data/levels/space/sky/sky_rgb.tga
589b730dbf16a495ce6484c4d683b0e5881749b62b9b5f71b3d5b6771847ce56  data/levels/space/ssharete.wad
74f74f6fba7788f83ae802a520c94229df31d009675ee8a0a1feb81b94fda9c5  data/levels/space/ssky/moon.tga
6249f645a312f6f2516d974fd2e7e98ec483d29a0c2fb749f97ff9302abdde1e  data/levels/space/ssky/moon2.tga
696787e6e7b519d4dadce89a38d0b644eee72c579ef3cdfa276f1a8da56e53d9  data/levels/space/ssky/sky.tga
f5e245f1a7de4c118c603ba1e4d1e54fe9de40aa7ac23f5c59b2fc9cf6e12956  data/levels/space/ssky/sky_cyl.tga
2a6fae4e27d2b2dc1825955f21d6744e5ba81e8222f4d9143e1d6e20b0e734f7  data/levels/space/ssky/sky_rgb.tga
a681acc1504697ea31c177625ad36ef48a847ae2d29c041bd147585bb30495f5  data/levels/space/terrain.wad
7dd227b4e5faa5c8cb3195e0afd00b7e7a69a83ee84b9da0b89be2e8dc6bfd6f  data/levels/space/test.tpt
ad16bb1e2e50b87a3c5c8796237c8b2a24ba5043e397797e292be30a882d6496  data/levels/space/upgrades/Upgrades.sam
771d5813c2140e43e428271b4fddd09e59110c1982ea13e337842d2751ddba53  data/levels/space/upgrades/meteor.wad
578d65157183b32ed1fec8c3a6c5e8638ab79f86b5d443397e5896047f83569e  data/levels/space/upgrades/teleport.wad
b9cbd1acca710b78698939779901709431a10f13f2e99b23e58582095aa81092  data/levels/space/upgrades/zob2.wad
b9afda6264961021aecaca882adf9ce25aa6973f48111415176ed45afd2c9ee7  data/lobby.wad
9506293a836d23481665080d1da00d7a449b200a6f69b92e5cccaf167c30b4bd  data/low.sam
b052aaf32721382b646fa4dcbbb6f506b0439231eb9cab836a33ead6e10bf084  data/med.sam
868a783587a13610f00084933a98be7383e7b9c9fa97d5e9e619bed1e41b4bdc  data/med.sam.finfo
81adad48917cb0025d5bba9d7aa5b236e744fd9e926c2706a0079ced4b4128e8  data/med.sam.rsrc
0e1c787737a0173f764e36738c5bdbca26ce16387cfa631d166a751861c7c559  data/onlineoverride.sam
d04a3b8d2f22369981d7a9373be2ebcb4657f4352f63cc9400ba16e4faa7c763  data/postcard/bullfrog.tga
c2a481b14bbaafd30ee4eacc050ceb13033b40c4298eb45cdbdc75372669b018  data/postcard/frank.tga
b225505df268a84604f9786ec32b1883ab0f1be7dbb760c6d8fe4b817fe17f67  data/postcard/legal.tga
2e1cd000dafdd767c1d6a8ad2ede666449cfc023c45d8854f1463360385d5b14  data/postcard/stamp.tga
3cc76347df7970d7fda5c137b045146cede7cfe3ced4b985f23af25eab4c9e23  data/postcard/tpwlogo.tga
112e6b132ca7f0590aca02413d8f9bc2ae09bd4ba5eb565cc6051b27c3eeca5b  data/sound.sam
868a783587a13610f00084933a98be7383e7b9c9fa97d5e9e619bed1e41b4bdc  data/sound.sam.finfo
32905f065abd6f127bc7fbc03a220919caaed09fb3ac5952310b359e9d7cc7f0  data/sound.sam.rsrc
dd9cbf829d14c798be908b8910305d7ec04e9b449794806c0581293458bc9362  data/ui.wad
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  data/ui/Here2000.txt
0aa332152fafd6dd97e48e4befab23baafda1d535566584374f15937d20f90e4  data/ui/cursors/Cash.tga
40cf8dad69971b7fcc1b507f47ac07cd4a7d0d6dcbd0e4d47a740ff6b2aacf4e  data/ui/cursors/Cbus.tga
3d22001d36aace08bfd8877c96498f40a1a65715df9434add84c3b70f06c0976  data/ui/cursors/Ccam.tga
ca64c922f1ea86f58be886fcd45f1569ff3638f672343207a7e76cc555aff4f9  data/ui/cursors/Ccar.tga
81de379dc42e4b76e1b63f0f8b88319f5d8216166e958af61230322e90519acc  data/ui/cursors/Ccro.tga
b2cc119264c5d57b61c15e039ef751d8e2ee1fc6ef32ec85986c103e3d482339  data/ui/cursors/Cend.tga
7195d4eedeba61cd719535e2c2cb2eeb5694714fb3a12b7ff7418195a033474f  data/ui/cursors/Cera.tga
30a36dde49fe277380dcd3449a8255c9ee5aab28f44bb3e731a9f82d2fe2185b  data/ui/cursors/Chei.tga
2decf58d5fd2dcf208b20fcb2154d3c8077a76eb894331cbe5f64d6b5dd5b311  data/ui/cursors/Clin.tga
029446e318d64b083d47ae53edd5e9376f3087fb11749ee0655cf18a8244e72c  data/ui/cursors/Cmov.tga
fb3c7dc5507951552fb45ac35c3a54f785e2d6366dc60cde8e683607c11c9b19  data/ui/cursors/Cnog.tga
2a7dd336ff0793d782ed325ae4cda426feaee246e4990d0934acd68bee330fb7  data/ui/cursors/Cnor.tga
8160275504151d7c9f675192a792f1815c8de1b5226cfe1b07b234e8236d3f0c  data/ui/cursors/Cpat.tga
8671c316e9b5f80e77936a5eafd8ac020020bbc54951275f27c0804eb9704261  data/ui/cursors/Cpic.tga
90ac2ac1885b861a45f038d43ce8f3b31c0cf98c70d634ad1e8bfcff04f8aef9  data/ui/cursors/Cpiv.tga
3debfcd6732b4932698ccbbc92c75097014c1f5f554a26a342e7ed5c5f0a0a5d  data/ui/cursors/Cpla.tga
7540e077a11ad9afc3a626214c5c2123844110bd0061bd59ce8675c889119317  data/ui/cursors/Cpyl.tga
340c80a7cb17db2f37abb561404531443498f2877f274451cd892b8e99a32e8e  data/ui/cursors/Cque.tga
e36c66e6c16cb3b0e72abfc197ed06ec51284626d6eee21a24a455c093b8b7ea  data/ui/cursors/Crot.tga
22d99b9c70f71b8036e94234182a58a6969c1cbd3037093ee57a33d83f62a187  data/ui/cursors/Csdn.tga
01b33e79403ca483e7cf6607c58edbb0efc7396460aff194868858b69fae0773  data/ui/cursors/Csup.tga
1c5d2847f9927fdd770ce3c3d9b9f19932d50bd2f2e6298299850d7af52b4dd4  data/ui/cursors/Ctra.tga
0bd0129aa928786f6e144afb9dd599eb1b7dfbede5d170598721271bcc4971a5  data/ui/cursors/c_busy.ani
7f729c0c536c3f17534b1f8641ad68e3c2bd7031f701fdea800f48113720e201  data/ui/cursors/c_cam.ani
d078cc47bf2861f84e328540c63842950f06e40f9bdb0630cd7850cb900695cf  data/ui/cursors/c_carry.ani
efa535bad66a67e0ca842bd57482d5cb5f8acb5317a03e0a4efdabdd02904e13  data/ui/cursors/c_cash.ani
2d22656235fe43be3f26f9819be528d70655f7b85aab2ed7b2a8feaea8776d14  data/ui/cursors/c_crosshair.ani
bfa2d43da2460f87d6ab0168c0a377211743964eab00f6e244d1355e83753422  data/ui/cursors/c_end.ani
c534cb140d9a0fca73af3fabbb5fee2c6db6ac1a8f6094f7cf484fbb7e627517  data/ui/cursors/c_erase.ani
266c49c40281ff199fa9ffffea45f02cb7721b81672775a7fe3ecb86da9453f9  data/ui/cursors/c_height.ani
357203757e19fee4c03f32bd6208b6d843f68827f1899e212725ce89e0aace14  data/ui/cursors/c_link.ani
0dcbbac4223dbd1ac4d9135223047ab1472f22d3e0e55f19d4b2b70bdf78329a  data/ui/cursors/c_move.ani
7a7089560d315c18b36a72b1c85e7126e5c487f97abe8b872b7d48137f3047bb  data/ui/cursors/c_noplace.cur
e4e3d30d73547a546ef3766dd005495e3915466df7b80872a2b28901ff5085b3  data/ui/cursors/c_norm.ani
6c7908a2a8a7ac4889b2e1bb66d09b775da3905750f6a06a631544f3675fb252  data/ui/cursors/c_path.ani
b66c4f72a012dd1505a1578f48ee6eab1acdf50e7e4bc5117da920c73a9ba260  data/ui/cursors/c_pick.ani
54d4703230cc52b3ed4f0a86479fb893b22af974eb308915430d9c9a397e2553  data/ui/cursors/c_pivot.ani
31f3316d722cdd78baee44db2274ff2332faa6ce578629dd4103a6c03a950fef  data/ui/cursors/c_place.ani
1123cdac8eb53613387caa60f6e2ffb71c6d5532d3439098e1df3fb3941fde37  data/ui/cursors/c_pylon.ani
cc71fdddc085d4d4e710c1d3675906fdf873ccb5d2e8468471ab4d5ea9f37702  data/ui/cursors/c_queue.ani
aa28281e29358ce92f6dec96f0e34496849bdfa9efbe91b1c6ebe49b5c3d6687  data/ui/cursors/c_rotate.ani
6f183263c59a41f293c88aa5539ef36781ac7bef05421e0d48b9b16755ac06e0  data/ui/cursors/c_swapdown.ani
545c52736b017dd497b0c9bd9eb1af1f92121387ae687326e688281ba0492e9b  data/ui/cursors/c_swapup.ani
6396708e88e5f20be8c6f220d28280d3011fb050a2cff7496300e5a20617a3b7  data/ui/cursors/c_track.ani
"""

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
