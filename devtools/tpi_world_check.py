#!/usr/bin/env python3
"""Check a TPI Worlds and Rides content folder against the world recipe.

usage: python -I tpi_world_check.py <data>/assets/tpi [--world NAME ...] [--quiet]
       python -I tpi_world_check.py --self-test

The content folder is what the launcher's converter writes (platform/tpi_content.h): converter.txt (the stamp) and
data/levels/<world>/ for arabian, water and science, each a level folder in Theme Park World's form. The game fills what
a world lacks from its donor world, except the item folders (rides, shops, sideshow, features, upgrades, facilities,
obstacles), which never fall back; so this checker reports what the engine cannot get from anywhere:

  stamp       converter.txt missing, or its first line not a version number
  world       a world folder missing; a file that must be the world's own missing (global.sam, Standard.sam,
              terrain.wad, sharetex.wad, the sky textures, 2dmap.tga); a terrain.wad without <world>.tct
  global      global.sam without Tickets.CanEarnTicketsInTheme, Tickets.CanSpendTicketsInTheme, Keys.CostToEnter or
              ParkName.GateObjectId; a GateObjectId that is no item's Info.Id; a Theme Park Inc-only key left in
  standard    Standard.sam without FixedItemInfo.TicketBoothAPosX/APosY/BPosX/BPosY; a Theme Park Inc-only key left in
  item        an item (a <name>.wad or a <name>/ folder in an item folder) without its .sam, a .sam without Info.Id,
              an Info.Id out of 1 to 65535, used twice in the world, or one of the base game's (1100 to 4605, the
              ids the game's built-in ride name table names; such a ride would get the base game's name);
              Info.DownloadedRideNumber present; Info.Name over 128 characters; a Theme Park Inc-only key left in;
              no .MD2 mesh; a qickload.txt naming a member that is not there; a texture that is not .tga or .wct;
              a TGA that is not uncompressed true colour (type 2, 24 or 32 bits); both <name>.wad and <name>/
  folder      an item folder with items but without its defaults file (rides/Rides.sam, shops/Shops.sam,
              sideshow/SideShow.sam, features/Features.sam, upgrades/Upgrades.sam); a Theme Park Inc-only key in it
  limit       more than 330 items in a world
  wad         a .wad (an item's, terrain.wad, sharetex.wad or another level wad) the game cannot read: a header
              version over 2, a version-2 name block outside the file, an entry's name outside the file or its name
              hash not the game's hash of the member's path (the game then finds none of the members, without a
              word in its log), a member name that is not plain ASCII

Warnings (reported, do not fail): no data/lobby/<world>.txt (the island screen's), an Easymode park in the world (the
recipe says write none), a texture over 512 pixels on a side, no Info.Name, an empty item folder, a sky texture or
other donor-fallback file left to the donor.

Exit code 0 when no error was found, 1 otherwise, 2 for a usage error. No game data is in this file: the names of the
required files, the keys and the id range come from the recipe and
the game's code, not from any disc. --self-test builds a small synthetic content folder in a temporary directory and
checks the findings on it.
"""
import os
import re
import struct
import sys
import tempfile

WORLDS = ("arabian", "water", "science")
ITEM_FOLDERS = ("rides", "shops", "sideshow", "features", "upgrades", "facilities", "obstacles")
FOLDER_DEFAULTS = {"rides": "Rides.sam", "shops": "Shops.sam", "sideshow": "SideShow.sam",
                   "features": "Features.sam", "upgrades": "Upgrades.sam"}
MAX_ITEMS = 330
BASE_ID_RANGE = (1100, 4605)
MAX_NAME = 0x80
OWN_FILES = ("global.sam", "Standard.sam", "terrain.wad", "sharetex.wad", "2dmap.tga")
SKY_FILES = ("sky/sky.tga", "sky/sky_rgb.tga", "sky/sky_cyl.tga")
GLOBAL_KEYS = ("Tickets.CanEarnTicketsInTheme", "Tickets.CanSpendTicketsInTheme", "Keys.CostToEnter",
               "ParkName.GateObjectId")
STANDARD_KEYS = ("FixedItemInfo.TicketBoothAPosX", "FixedItemInfo.TicketBoothAPosY",
                 "FixedItemInfo.TicketBoothBPosX", "FixedItemInfo.TicketBoothBPosY")
# Keys Theme Park Inc's files have and Theme Park World's parser does not know (tpiassets' research). A regular
# expression each, matched against the whole key.
TPI_ONLY_GLOBAL = (r"ParkInfo\.AvailableFromStart", r"StaffPoolInfo\..*")
TPI_ONLY_STANDARD = (r"Seasons\[\d+\]\.PercentageChanceFor.*", r"Weather\..*", r"WeatherEffects\..*",
                     r"FixedItemInfo\.NumTicketBooths", r"TicketBooth\[\d+\]\..*")
TPI_ONLY_ITEM = (r"Info\.ExpectedPrice", r"Info\.WorkingLife", r"RideBreakDown\..*", r"Upgrades\[\d+\]\.RunningCost",
                 r"UsageInfo\.InitPricePerUse", r"WeatherModifier\..*", r"Info\.GotoExitMode", r"UsageInfo\.ToiletEffect")
TEXTURE_EXT = (".tga", ".wct")
TEXTURE_WARN_SIZE = 512


# ---- the wad container (DWFB, as tools/wad.py reads it: 88-byte header, count at 0x48, 40-byte entries from 0x58)

def refpack_decompress(src):
    flags, magic = src[0], src[1]
    if magic != 0xFB:
        raise ValueError("not RefPack")
    n = 4 if flags & 0x80 else 3
    p = 2 + (n if flags & 0x01 else 0)
    size = int.from_bytes(src[p:p + n], "big")
    p += n
    out = bytearray()
    while p < len(src):
        b0 = src[p]
        if b0 < 0x80:
            b1 = src[p + 1]
            plain, copy, off = b0 & 3, ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1
            p += 2
        elif b0 < 0xC0:
            b1, b2 = src[p + 1], src[p + 2]
            plain, copy, off = b1 >> 6, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
            p += 3
        elif b0 < 0xE0:
            b1, b2, b3 = src[p + 1], src[p + 2], src[p + 3]
            plain, copy, off = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
            p += 4
        elif b0 < 0xFC:
            plain, copy, off = ((b0 & 0x1F) << 2) + 4, 0, 0
            p += 1
        else:
            out += src[p + 1:p + 1 + (b0 & 3)]
            break
        out += src[p:p + plain]
        p += plain
        for _ in range(copy):
            out.append(out[-off])
    if len(out) != size:
        raise ValueError("RefPack size %d != %d" % (len(out), size))
    return bytes(out)


def wad_hash(path):
    """The game's hash of a member path (bytes, '\\' between folders): WadNameTree's, letters upper-cased, the first
    four bytes of each four shifted by their index, the rest added plain, 32 bits. Stored in every entry; the game
    compares it before the name, so a wrong one hides the member."""
    value = 0
    upper = bytes(c - 32 if 97 <= c <= 122 else (58 if c == 47 else c) for c in path)
    i = 0
    while len(upper) - i >= 4:
        for k in range(4):
            value += (0xffffffff if upper[i + k] == 0xff else upper[i + k]) << k
        i += 4
    for k in range(i, len(upper)):
        value += 255 if upper[k] == 0xff else upper[k]
    return value & 0xffffffff


def wad_problems(data):
    """What keeps the game from reading a DWFB wad: a list of texts, empty when it is readable (header version 0 to
    2, a version-2 name block inside the file, every entry's name inside the file and its hash the game's). A name
    with a '\\' moves the following entries into that folder, as WadNameTree::Initialise does."""
    problems = []
    if data[:4] != b"DWFB" or len(data) < 0x58:
        return ["not a DWFB wad"]
    version, = struct.unpack_from("<I", data, 4)
    count, name_offset, name_size = struct.unpack_from("<I", data, 0x48)[0], *struct.unpack_from("<II", data, 0x4c)
    if version > 2:
        problems.append("header version %d (the game reads 0 to 2)" % version)
    if 0x58 + 40 * count > len(data):
        problems.append("entry table past the end")
        return problems
    if version == 2 and (name_offset < 0x58 + 40 * count or name_offset + name_size > len(data)):
        problems.append("version-2 name block %d+%d outside the file" % (name_offset, name_size))
    cwd = b""
    for i in range(count):
        stored, name_off, name_len, data_off, length = struct.unpack_from("<5I", data, 0x58 + 40 * i)
        end = data.find(b"\0", name_off)
        if end == -1 or (version == 2 and not (name_offset <= name_off and end < name_offset + name_size)):
            problems.append("entry %d: name outside the file or the name block" % i)
            continue
        name = data[name_off:name_off + name_len].split(b"\0")[0]
        if b"\\" in name:
            cwd, name = name.rsplit(b"\\", 1)
        full = cwd + b"\\" + name if cwd else name
        if any(c < 32 or c > 126 for c in full):
            problems.append("entry %d: name %r is not plain ASCII" % (i, full))
        if data_off + length > len(data):
            problems.append("entry %d (%s): data past the end" % (i, full.decode("latin-1")))
        wanted = wad_hash(full)
        if stored != wanted:
            problems.append("entry %d (%s): name hash 0x%x, the game computes 0x%x" % (i, full.decode("latin-1"), stored, wanted))
    return problems


def wad_members(data):
    """{member path with '/' separators (as stored, case kept): bytes} of a DWFB wad, or None if it is no wad."""
    if data[:4] != b"DWFB" or len(data) < 0x58:
        return None
    count = struct.unpack_from("<I", data, 0x48)[0]
    if 0x58 + 40 * count > len(data):
        raise ValueError("entry table past the end")
    members = {}
    cwd = ""
    for i in range(count):
        _, name_off, name_len, data_off, length, comp, usize = struct.unpack_from("<7I", data, 0x58 + 40 * i)
        name = data[name_off:name_off + name_len].split(b"\0")[0].decode("latin-1")
        if "\\" in name:
            cwd, name = name.rsplit("\\", 1)
        path = (cwd + "/" + name) if cwd else name
        raw = data[data_off:data_off + length]
        if comp == 0:
            members[path] = raw
        elif comp == 4:
            members[path] = refpack_decompress(raw)
        else:
            raise ValueError("member %s: unknown compression %d" % (path, comp))
    return members


def folder_members(root):
    """The same for an unpacked item folder: every file under it, path relative with '/'."""
    members = {}
    for dirpath, _, files in os.walk(root):
        for f in files:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            with open(full, "rb") as fh:
                members[rel] = fh.read()
    return members


# ---- the text .sam files

SAM_KEY = re.compile(r"^([A-Za-z_][A-Za-z0-9_.\[\]]*)\s*(.*)$")


def parse_sam(data):
    """[(key, value text)] of a .sam: '#' starts a comment, a key is followed by its value on the line, and a key with
    no value followed by a '---' line opens a block that the next '---' line closes (Info.Shape, Info.Hoarding)."""
    text = data.decode("latin-1")
    pairs = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].split("#", 1)[0].rstrip()
        i += 1
        if not line.strip():
            continue
        m = SAM_KEY.match(line.strip())
        if not m:
            continue
        key, value = m.group(1), m.group(2).strip()
        if not value and i < len(lines) and lines[i].strip() == "---":
            i += 1
            while i < len(lines) and lines[i].strip() != "---":
                i += 1
            i += 1
            value = "<block>"
        pairs.append((key, value))
    return pairs


def sam_value(pairs, key):
    for k, v in pairs:
        if k.lower() == key.lower():
            return v
    return None


def sam_int(value):
    if value is None:
        return None
    m = re.match(r"^-?\d+", value)
    return int(m.group(0)) if m else None


def tpi_only_keys(pairs, patterns):
    found = []
    for k, _ in pairs:
        for pat in patterns:
            if re.fullmatch(pat, k):
                found.append(k)
                break
    return found


# ---- the findings

class Report:
    def __init__(self, quiet=False):
        self.errors = []
        self.warnings = []
        self.quiet = quiet

    def error(self, where, text):
        self.errors.append((where, text))

    def warn(self, where, text):
        self.warnings.append((where, text))

    def print(self, out=sys.stdout):
        for where, text in self.errors:
            out.write("ERROR   %s: %s\n" % (where, text))
        if not self.quiet:
            for where, text in self.warnings:
                out.write("warning %s: %s\n" % (where, text))
        out.write("%d error(s), %d warning(s)\n" % (len(self.errors), len(self.warnings)))


def find_ci(folder, name):
    """The path of `name` (a relative path, any case) under `folder`, or None."""
    parts = name.replace("\\", "/").split("/")
    cur = folder
    for part in parts:
        if not os.path.isdir(cur):
            return None
        match = None
        for entry in os.listdir(cur):
            if entry.lower() == part.lower():
                match = entry
                break
        if match is None:
            return None
        cur = os.path.join(cur, match)
    return cur


def member_ci(members, name):
    name = name.replace("\\", "/").lower()
    for path in members:
        if path.lower() == name:
            return path
    return None


def check_wad(where, path, report):
    """Members of the wad at path (as wad_members), after reporting what the game could not read; None when it is
    no wad or unreadable."""
    with open(path, "rb") as fh:
        data = fh.read()
    problems = wad_problems(data)
    for text in problems:
        report.error(where, text)
    if problems:
        return None
    try:
        return wad_members(data)
    except ValueError as e:
        report.error(where, "unreadable: %s" % e)
        return None


def check_tga(where, name, data, report):
    if len(data) < 18:
        report.error(where, "%s: not a TGA (too short)" % name)
        return
    image_type, width, height, depth = data[2], struct.unpack_from("<H", data, 12)[0], \
        struct.unpack_from("<H", data, 14)[0], data[16]
    if image_type != 2 or depth not in (24, 32):
        report.error(where, "%s: TGA type %d, %d bits; the texture loader takes uncompressed true colour (type 2, 24 or 32 bits)"
                     % (name, image_type, depth))
    if width > TEXTURE_WARN_SIZE or height > TEXTURE_WARN_SIZE:
        report.warn(where, "%s: %d x %d, over %d on a side" % (name, width, height, TEXTURE_WARN_SIZE))


def check_item(where, name, members, report, ids, world_ids):
    """One item (a wad's or a folder's members). Adds its Info.Id to `ids` (the world's) and returns it (or None)."""
    sam_name = member_ci(members, name + ".sam")
    if sam_name is None:
        candidates = [m for m in members if m.lower().endswith(".sam") and "/" not in m
                      and not m.lower().startswith(("easy_", "online_"))]
        sam_name = candidates[0] if len(candidates) == 1 else None
    if sam_name is None:
        report.error(where, "no %s.sam among its members" % name)
        return None
    pairs = parse_sam(members[sam_name])
    item_id = sam_int(sam_value(pairs, "Info.Id"))
    if item_id is None:
        report.error(where, "%s has no Info.Id" % sam_name)
    else:
        if not 1 <= item_id <= 65535:
            report.error(where, "Info.Id %d is out of 1 to 65535" % item_id)
        elif BASE_ID_RANGE[0] <= item_id <= BASE_ID_RANGE[1]:
            report.error(where, "Info.Id %d is in the base game's range %d to %d (its name table would name this item)"
                         % (item_id, BASE_ID_RANGE[0], BASE_ID_RANGE[1]))
        if item_id in ids:
            report.error(where, "Info.Id %d is also %s's" % (item_id, ids[item_id]))
        else:
            ids[item_id] = where
        world_ids.add(item_id)
    if sam_value(pairs, "Info.DownloadedRideNumber") is not None:
        report.error(where, "Info.DownloadedRideNumber present (a mod world's item carries none)")
    name_value = sam_value(pairs, "Info.Name")
    if name_value is None:
        report.warn(where, "no Info.Name")
    else:
        bare = name_value.strip().strip('"')
        if len(bare) > MAX_NAME:
            report.error(where, "Info.Name is %d characters, over %d" % (len(bare), MAX_NAME))
    for key in tpi_only_keys(pairs, TPI_ONLY_ITEM):
        report.error(where, "%s: Theme Park Inc-only key %s" % (sam_name, key))
    if not any(m.lower().endswith(".md2") for m in members):
        report.error(where, "no .MD2 mesh among its members")
    for qick in [m for m in members if m.lower().rsplit("/", 1)[-1] == "qickload.txt"]:
        folder = qick.rsplit("/", 1)[0] + "/" if "/" in qick else ""
        for line in members[qick].decode("latin-1").splitlines():
            entry = line.strip()
            if not entry or entry.startswith("#"):
                continue
            if member_ci(members, folder + entry) is None and member_ci(members, entry) is None:
                report.error(where, "%s names %s, which is not among the members" % (qick, entry))
    for m, data in members.items():
        low = m.lower()
        if low.startswith(("textures/", "stexture/")) and not low.endswith("qickload.txt"):
            if not low.endswith(TEXTURE_EXT):
                report.error(where, "%s: a texture must be .tga or .wct" % m)
            elif low.endswith(".tga"):
                check_tga(where, m, data, report)
    return item_id


def check_world(root, world, report, lobby_root):
    level = find_ci(root, "data/levels/" + world)
    if level is None:
        report.error(world, "data/levels/%s missing" % world)
        return
    where = "data/levels/" + world
    for name in OWN_FILES:
        if find_ci(level, name) is None:
            report.error(where, "%s missing (must be the world's own)" % name)
    for name in SKY_FILES:
        if find_ci(level, name) is None:
            report.warn(where, "%s missing (the donor's sky is used)" % name)
    for name in ("ssharete.wad", "scape.omp", "dynamic.wad", "queue.wad", "miscmesh.wad", "hoarding.wad",
                 "Hoardings.sam", "EndHoardings.sam", "Music", "Sound"):
        if find_ci(level, name) is None:
            report.warn(where, "%s missing (the donor's is used)" % name)
    for entry in os.listdir(level):
        if entry.lower().startswith("easymode"):
            report.warn(where, "%s present: an easy-mode park must be a park of this world (the recipe says write none)" % entry)
    for name in ("sharetex.wad", "ssharete.wad", "dynamic.wad", "queue.wad", "miscmesh.wad", "hoarding.wad"):
        wad = find_ci(level, name)
        if wad is not None and os.path.isfile(wad):
            check_wad(where + "/" + name, wad, report)
    terrain = find_ci(level, "terrain.wad")
    if terrain is not None and os.path.isfile(terrain):
        members = check_wad(where + "/terrain.wad", terrain, report)
        if members is not None:
            tcts = [m for m in members if m.lower().endswith(".tct")]
            if not any(m.lower().rsplit("/", 1)[-1] == world.lower() + ".tct" for m in tcts):
                report.error(where, "terrain.wad holds no %s.tct (it holds %s)" % (world, ", ".join(tcts) or "no .tct"))
    # global.sam and Standard.sam
    ids = {}
    world_ids = set()
    total = 0
    for folder in ITEM_FOLDERS:
        path = find_ci(level, folder)
        if path is None:
            continue
        entries = sorted(os.listdir(path), key=str.lower)
        names = {}
        for entry in entries:
            full = os.path.join(path, entry)
            low = entry.lower()
            if os.path.isdir(full):
                stem = entry
            elif low.endswith(".wad"):
                stem = entry[:-4]
            else:
                continue
            if stem.lower() in names:
                report.error("%s/%s" % (where, folder), "both %s and %s (one item twice)" % (names[stem.lower()], entry))
                continue
            names[stem.lower()] = entry
            item_where = "%s/%s/%s" % (where, folder, entry)
            if os.path.isdir(full):
                members = folder_members(full)
            else:
                members = check_wad(item_where, full, report)
                if members is None:
                    continue
            total += 1
            check_item(item_where, stem, members, report, ids, world_ids)
        if not names:
            report.warn("%s/%s" % (where, folder), "no items")
        defaults = FOLDER_DEFAULTS.get(folder)
        if defaults is not None and names:
            dpath = find_ci(path, defaults)
            if dpath is None:
                report.error("%s/%s" % (where, folder), "%s missing (nothing under %s comes from the donor)" % (defaults, folder))
            else:
                with open(dpath, "rb") as fh:
                    for key in tpi_only_keys(parse_sam(fh.read()), TPI_ONLY_ITEM):
                        report.error("%s/%s/%s" % (where, folder, defaults), "Theme Park Inc-only key %s" % key)
    if total > MAX_ITEMS:
        report.error(where, "%d items, over the %d the ride database holds" % (total, MAX_ITEMS))
    gpath = find_ci(level, "global.sam")
    if gpath is not None:
        with open(gpath, "rb") as fh:
            pairs = parse_sam(fh.read())
        for key in GLOBAL_KEYS:
            if sam_value(pairs, key) is None:
                report.error(where + "/global.sam", "%s missing" % key)
        gate = sam_int(sam_value(pairs, "ParkName.GateObjectId"))
        if gate is not None and gate not in world_ids:
            report.error(where + "/global.sam", "ParkName.GateObjectId %d is no item's Info.Id in this world" % gate)
        for key in tpi_only_keys(pairs, TPI_ONLY_GLOBAL):
            report.error(where + "/global.sam", "Theme Park Inc-only key %s" % key)
    spath = find_ci(level, "Standard.sam")
    if spath is not None:
        with open(spath, "rb") as fh:
            pairs = parse_sam(fh.read())
        for key in STANDARD_KEYS:
            if sam_value(pairs, key) is None:
                report.error(where + "/Standard.sam", "%s missing" % key)
        for key in tpi_only_keys(pairs, TPI_ONLY_STANDARD):
            report.error(where + "/Standard.sam", "Theme Park Inc-only key %s" % key)
    if lobby_root is None or find_ci(lobby_root, world + ".txt") is None:
        report.warn(where, "no data/lobby/%s.txt (the island screen's descriptor)" % world)


def check_content(root, worlds=WORLDS, quiet=False):
    report = Report(quiet)
    if not os.path.isdir(root):
        report.error(root, "not a folder")
        return report
    stamp = find_ci(root, "converter.txt")
    if stamp is None:
        report.error("converter.txt", "missing (the converter writes it last; without it the content is not used)")
    else:
        with open(stamp, "rb") as fh:
            first = fh.read(64).decode("latin-1").splitlines()[:1]
        if not first or not re.fullmatch(r"\s*[1-9]\d*\s*", first[0]):
            report.error("converter.txt", "first line is not a version number")
    lobby = find_ci(root, "data/lobby")
    for world in worlds:
        check_world(root, world, report, lobby)
    return report


# ---- self test: a synthetic content folder, no game data

def _wad(members, version=2, hash_error=0):
    """A DWFB wad of {path: bytes}, members stored, in the game's version-2 layout (names in one block named by the
    header, every entry's hash the game's). version and hash_error make a wad the game cannot read, for the test."""
    datas = b""
    count = len(members)
    header = bytearray(0x58)
    header[:4] = b"DWFB"
    struct.pack_into("<II", header, 4, version, 0)
    struct.pack_into("<I", header, 0x48, count)
    table_size = 40 * count
    name_base = 0x58 + table_size
    name_blobs = []
    for path in members:
        name_blobs.append(path.replace("/", "\\").encode("latin-1") + b"\0")
    names = b"".join(name_blobs)
    struct.pack_into("<II", header, 0x4c, name_base, len(names))
    data_base = name_base + len(names)
    table = bytearray()
    name_off = name_base
    data_off = data_base
    for path, blob in zip(members, name_blobs):
        data = members[path]
        stored = (wad_hash(blob[:-1]) + hash_error) & 0xffffffff
        table += struct.pack("<7I", stored, name_off, len(blob), data_off, len(data), 0, len(data)) + b"\0" * 12
        name_off += len(blob)
        data_off += len(data)
        datas += data
    return bytes(header) + bytes(table) + names + datas


def _tga(width, height, depth=32, image_type=2):
    head = bytearray(18)
    head[2] = image_type
    struct.pack_into("<H", head, 12, width)
    struct.pack_into("<H", head, 14, height)
    head[16] = depth
    return bytes(head) + b"\0" * (width * height * depth // 8)


def _item(name, item_id, extra=""):
    sam = ("# synthetic\nInfo.Id\t\t%d\nInfo.Name\t\t\"Item %s\"\nInfo.Shape\n---\n*S*\n---\n%s" % (item_id, name, extra)).encode()
    return {
        name + ".sam": sam, name + ".MD2": b"IDP2" + b"\0" * 60, "qickload.txt": (name + ".MD2\n").encode(),
        "textures/t1.tga": _tga(64, 64), "textures/qickload.txt": b"t1.tga\n",
    }


def _write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)


def _world(root, world, gate_id, base):
    level = os.path.join(root, "data", "levels", world)
    _write(os.path.join(level, "global.sam"),
           ("Tickets.CanEarnTicketsInTheme 1\nTickets.CanSpendTicketsInTheme 1\nKeys.CostToEnter 0\n"
            "ParkName.GateObjectId %d\n" % gate_id).encode())
    _write(os.path.join(level, "Standard.sam"),
           b"FixedItemInfo.TicketBoothAPosX 1\nFixedItemInfo.TicketBoothAPosY 2\nFixedItemInfo.TicketBoothBPosX 3\n"
           b"FixedItemInfo.TicketBoothBPosY 4\n")
    _write(os.path.join(level, "terrain.wad"), _wad({"Terrain/base.lnd": b"x", "Terrain/%s.tct" % world: b"y"}))
    _write(os.path.join(level, "sharetex.wad"), _wad({"a.tga": _tga(8, 8)}))
    _write(os.path.join(level, "2dmap.tga"), _tga(8, 8))
    for name in ("sky.tga", "sky_rgb.tga", "sky_cyl.tga"):
        _write(os.path.join(level, "sky", name), _tga(8, 8))
    _write(os.path.join(level, "rides", "Rides.sam"), b"Info.HasQueue 1\n")
    _write(os.path.join(level, "rides", "ride1.wad"), _wad(_item("ride1", base + 1)))
    _write(os.path.join(level, "features", "Features.sam"), b"Info.HasQueue 0\n")
    gate = _item("gate", gate_id)
    for path, data in gate.items():
        _write(os.path.join(level, "features", "gate", path), data)
    _write(os.path.join(root, "data", "lobby", world + ".txt"), b"synthetic\n")
    return level


def self_test():
    failures = []

    def expect(cond, text):
        if not cond:
            failures.append(text)

    with tempfile.TemporaryDirectory(prefix="tpi_world_check_") as tmp:
        root = os.path.join(tmp, "tpi")
        _write(os.path.join(root, "converter.txt"), b"3\n")
        for world, base in zip(WORLDS, (5000, 6000, 7000)):
            _world(root, world, base + 500, base)
        clean = check_content(root)
        expect(not clean.errors, "a clean folder has errors: %r" % clean.errors)
        expect(not clean.warnings or all("missing (the donor's is used)" in w[1] for w in clean.warnings),
               "unexpected warnings on a clean folder: %r" % clean.warnings)

        # Break it in every way the checker names, one at a time, and look for each finding.
        level = os.path.join(root, "data", "levels", "arabian")
        _write(os.path.join(level, "rides", "ride2.wad"), _wad(_item("ride2", 5001)))               # id twice
        _write(os.path.join(level, "rides", "ride3.wad"), _wad(_item("ride3", 1200)))               # base range
        _write(os.path.join(level, "rides", "ride4.wad"), _wad(_item("ride4", 5004, "Info.DownloadedRideNumber 201\nInfo.WorkingLife 5\n")))
        bad = _item("ride5", 5005)
        bad["qickload.txt"] = b"ride5.MD2\nmissing.MD2\n"
        bad["textures/t2.bmp"] = b"BM"
        bad["textures/t3.tga"] = _tga(1024, 16, 8, 1)
        _write(os.path.join(level, "rides", "ride5.wad"), _wad(bad))
        _write(os.path.join(level, "rides", "ride6.wad"), _wad({"ride6.MD2": b"IDP2"}))            # no .sam
        _write(os.path.join(level, "rides", "ride7.wad"), _wad({"ride7.sam": b"Info.Name \"x\"\n"}))   # no id, no mesh
        _write(os.path.join(level, "rides", "dup.wad"), _wad(_item("dup", 5020)))                     # wad and folder
        _write(os.path.join(level, "rides", "dup", "dup.sam"), b"Info.Id 5021\n")
        _write(os.path.join(level, "shops", "shop1.wad"), _wad(_item("shop1", 5010)))                 # no Shops.sam
        _write(os.path.join(level, "rides", "ride8.wad"), _wad(_item("ride8", 5008), hash_error=1))   # wrong hashes
        _write(os.path.join(level, "rides", "ride9.wad"), _wad(_item("ride9", 5009), version=3))      # bad version
        _write(os.path.join(level, "sharetex.wad"), _wad({"a.tga": _tga(8, 8)}, hash_error=7))
        _write(os.path.join(level, "Easymode.TPWI"), b"park")
        _write(os.path.join(level, "global.sam"), b"Keys.CostToEnter 0\nParkName.GateObjectId 9999\nStaffPoolInfo.AvgGradeOfGuards 3\n")
        _write(os.path.join(level, "Standard.sam"), b"Weather.YearsBeforeFirstFreakWeather 2\n")
        _write(os.path.join(level, "terrain.wad"), _wad({"Terrain/jungle.tct": b"y"}))
        os.remove(os.path.join(level, "2dmap.tga"))
        os.remove(os.path.join(root, "data", "lobby", "arabian.txt"))
        _write(os.path.join(root, "converter.txt"), b"draft\n")
        import shutil
        shutil.rmtree(os.path.join(root, "data", "levels", "water"))
        broken = check_content(root)
        texts = [w + ": " + t for w, t in broken.errors]
        warns = [w + ": " + t for w, t in broken.warnings]

        def has(fragment, where=texts):
            expect(any(fragment in t for t in where), "no finding with %r in %r" % (fragment, where))

        has("first line is not a version number")
        has("data/levels/water missing")
        has("Info.Id 5001 is also")
        has("Info.Id 1200 is in the base game's range")
        has("Info.DownloadedRideNumber present")
        has("Theme Park Inc-only key Info.WorkingLife")
        has("qickload.txt names missing.MD2")
        has("t2.bmp: a texture must be .tga or .wct")
        has("t3.tga: TGA type 1, 8 bits")
        has("t3.tga: 1024 x 16", warns)
        has("no ride6.sam among its members")
        has("ride7.sam has no Info.Id")
        has("no .MD2 mesh")
        has("both dup and dup.wad")
        has("Shops.sam missing")
        has("ride8.wad: entry 0 (ride8.sam): name hash")
        has("ride9.wad: header version 3")
        has("arabian/sharetex.wad: entry 0 (a.tga): name hash")
        has("Easymode.TPWI present", warns)
        has("Tickets.CanEarnTicketsInTheme missing")
        has("ParkName.GateObjectId 9999 is no item's")
        has("Theme Park Inc-only key StaffPoolInfo.AvgGradeOfGuards")
        has("FixedItemInfo.TicketBoothAPosX missing")
        has("Theme Park Inc-only key Weather.YearsBeforeFirstFreakWeather")
        has("terrain.wad holds no arabian.tct")
        has("2dmap.tga missing")
        has("no data/lobby/arabian.txt", warns)
        expect(not any("science" in t for t in texts), "science (untouched) has errors: %r" % [t for t in texts if "science" in t])

        # Over the limit.
        many = os.path.join(root, "data", "levels", "science", "sideshow")
        _write(os.path.join(many, "SideShow.sam"), b"Info.HasQueue 0\n")
        for i in range(MAX_ITEMS):
            _write(os.path.join(many, "s%03d.wad" % i), _wad(_item("s%03d" % i, 7100 + i)))
        over = check_content(root, worlds=("science",))
        has("items, over the %d" % MAX_ITEMS, [w + ": " + t for w, t in over.errors])

    # The hash against values the game's own wads carry (WadNameTree's rule; no game data: three names and hashes).
    for path, value in ((b"Gates.sam", 0x891), (b"stexture\\jgt_vin2.wct", 0x16de), (b"textures\\md_pill03.wct", 0x1790)):
        expect(wad_hash(path) == value, "wad_hash(%r) = 0x%x, not 0x%x" % (path, wad_hash(path), value))
    for f in failures:
        sys.stdout.write("SELF-TEST FAIL: %s\n" % f)
    sys.stdout.write("self-test: %s\n" % ("ok" if not failures else "%d failure(s)" % len(failures)))
    return 0 if not failures else 1


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    if argv[0] == "--self-test":
        return self_test()
    root = None
    worlds = []
    quiet = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--world" and i + 1 < len(argv):
            worlds.append(argv[i + 1])
            i += 2
            continue
        if a == "--quiet":
            quiet = True
        elif a.startswith("-"):
            sys.stderr.write("unknown option %s\n" % a)
            return 2
        elif root is None:
            root = a
        else:
            sys.stderr.write("one content folder only\n")
            return 2
        i += 1
    if root is None:
        sys.stderr.write(__doc__)
        return 2
    report = check_content(os.path.abspath(root), tuple(worlds) or WORLDS, quiet)
    report.print()
    return 0 if not report.errors else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
