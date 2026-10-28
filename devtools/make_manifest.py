#!/usr/bin/env python3
"""Regenerate assets/manifest_data.inc, the asset manifest the launcher compiles in (hashes, sizes and names only).

usage: python3 -I devtools/make_manifest.py [--scratch DIR | --base FILE] [--languages DIR | --keep-languages]
                                           [--out FILE] [--check] [--verify-program EXTRACTED]

What a game folder must hold, one record per file, found by CONTENT (size and SHA-256), never by the disc image's MD5:

  US  (editions us)    import_originals.py CHECKSUMS, the "Theme Park Data" folder of the Mac CD: data/ without the
                       four Finder "Icon" files, the 17 font resource forks (data/fonts/*.rsrc: the game reads the
                       TrueType fonts out of them, the data forks are empty) and the program's data fork, with BOTH
                       accepted builds (CD 2.0.1 and patch 2.04b3) as SHA-256 values of one record.
  EUR (editions eur)   import_eur.py EXPECTED, all of the European folder (PC disc files, Mac files, tuning files,
                       resource forks and Finder info files); the sizes come from tree-ee54d817.json and the content
                       source (pc or mac) from draft-manifests.json, both written by the launcher-design study.
  Gold (editions gold) gold-wads.sha256 (the 35 ride wads) and the ride configuration text, sizes from gold-cab/.
  Languages (editions lang)  one pack per language the original knows, under lang/<Name>/<game path>: the text folder
                       data/Language/<Name>/ (tables, fonts, residx.dat, the code page files, swears.txt and alloweds.txt,
                       the 3 meshes), the 20 speech files of data/global/Speech and data/levels/<world>/Speech, and the
                       Splash/Welcome pictures of data/Init/<res>. English and American are copies of the eur and us
                       records; the other 13 come from the disc lists in --languages (one "SHA-256<TAB>size<TAB>path"
                       file per disc or installed copy, *.tsv), read by layout: a language overlay folder (<Lang>/data/...,
                       <Lang>/Filter, <Lang>/Meshes) or a disc or installed game folder whose base data/Language holds
                       one language besides English and American (the base speech is then that language's). The
                       pictures and alloweds.txt are optional. The table kManifestLanguages gives each language its
                       flags: older text tables (a fixed list, checked against residx.dat) and date fonts from the base
                       edition (no DATE*.bf4 in its folder).

A file whose destination, hashes, group, flags and source are the same in two editions is ONE record with both
edition bits. The first pressing's differing files have no alternate hashes (decided 2026-10-21).

The scratch folder is the launcher-design study's (draft-manifests.json, tree-*.json, gold-wads.sha256, gold-cab/); no
disc is needed. --base FILE reads the us, eur and gold records back from an earlier output instead (the same records);
--keep-languages takes the language records from it too, so `--base assets/manifest_data.inc --keep-languages --check`
needs nothing outside the repository and tests that reading and writing agree (ctest "manifest_generator").
--check compares instead of writing (exit 1 when the file would change). --verify-program EXTRACTED
(ROOT/extracted) recomputes the two program builds' SHA-1 and SHA-256 from the files and fails if they differ from the
constants below.

Python 3.8 or later and nothing else. The lists are read as text (ast), never imported.
"""
import argparse
import ast
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DECOMP = os.path.dirname(HERE)
DEFAULT_SCRATCH = "/Volumes/6TB-2024/_scratch/wiring/launcher-design"
DEFAULT_OUT = os.path.join(DECOMP, "assets", "manifest_data.inc")
DEFAULT_LANGUAGES = os.path.expanduser("~/tpw-local/languages/hash")

# The two builds of the program the launcher accepts (SHA-1 as the study and gen_game_tables.py name them), and the
# SHA-256 the lists carry for each; --verify-program recomputes both from the files.
PROGRAM_CD = ("45d731f0d059a7dde6caa1142545fcd3a7fadd43",
              "04809cd4ccee5433c7fb0b7c93d32f6a7aa629c1849181c0b7906415e5e295f5", 2274758)
PROGRAM_PATCH = ("ce184baca37824def7faa1fb033840a2ce273ef9",
                 "29da2392beefc1ee00b097536b7f1b202044cbf0743c0eb3a6235254fd318000", 2746951)
PROGRAM_DEST = "SimTheme Park"

GOLD_DEST_FOLDER = "gold"
GOLD_CONFIG = "RideConfig.txt"
EXPECTED_WADS = 35

# the codes the header (assets/manifest.h) names
GROUPS = ["core", "fonts-rsrc", "movies", "language:american", "language:english", "program", "gold", "language-pack"]
GROUP_NAME = {"core": "kGroupCore", "fonts-rsrc": "kGroupFontsRsrc", "movies": "kGroupMovies",
              "language:american": "kGroupLanguageAmerican", "language:english": "kGroupLanguageEnglish",
              "program": "kGroupProgram", "gold": "kGroupGold", "language-pack": "kGroupLanguagePack"}
EDITION_BIT = {"us": 1, "eur": 2, "gold": 4, "lang": 8}
EDITION_NAME = {"us": "kEditionUs", "eur": "kEditionEur", "gold": "kEditionGold", "lang": "kEditionLanguages"}
SOURCE_NAME = {"mac": "kSourceMacCd", "pc": "kSourcePcCd", "gold": "kSourceGoldCd", "lang": "kSourceLanguageDisc"}
FLAG_REQUIRED, FLAG_RSRC, FLAG_FINFO, FLAG_TUNING = 1, 2, 4, 8
MAC_TUNING = ("data/med.sam", "data/sound.sam")

LANG_DEST_FOLDER = "lang"
# The original's spellings of the languages (FS_Registry.cpp), in the order of the table.
LANGUAGES = ["English", "American", "French", "German", "Danish", "Swedish", "Spanish", "Italian", "Dutch", "Polish",
             "Japanese", "Korean", "TraditionalChinese", "SimplifiedChinese", "Hebrew"]
FROM_EDITION = {"English": "eur", "American": "us"}      # copies of an edition's records, no disc list read
# Text tables of the older build (the run-time layer adapts them); every language with the older residx.dat must be here.
OLD_TABLES = {"Spanish", "Italian", "Dutch", "Polish", "TraditionalChinese", "SimplifiedChinese"}
OLD_RESIDX_SIZE = 1343
DATE_FONTS = ("DATEBIG.bf4", "DATEMED.bf4", "DATESMALL.bf4", "DATETINY.bf4")
LANGUAGE_OLD_TABLES, LANGUAGE_BASE_DATE_FONTS = 1, 2
# the folder names a disc gives a language (lower case)
FOLDER_LANGUAGE = dict((n.lower(), n) for n in LANGUAGES)
FOLDER_LANGUAGE.update({"tchinese": "TraditionalChinese", "schinese": "SimplifiedChinese", "usenglish": "American"})
WORLDS = ("fantasy", "hallow", "jungle", "space")
SPEECH_FILES = 20
MESHES = ("bankrupt.MD2", "congrats.MD2", "paused.MD2")
FILTER_FILES = ("alloweds.txt", "swears.txt")


class Fail(Exception):
    pass


def fail(msg):
    raise Fail(msg)


# ---- the lists, read as text

def module_constants(path):
    """The string and number constants assigned at the top level of the Python file PATH (no code runs)."""
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), path)
    out = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            try:
                out[node.targets[0].id] = ast.literal_eval(node.value)
            except ValueError:
                pass
    return out


def checksums():
    """{relative path under the disc/patch folders: (size, sha256)} from import_originals.py CHECKSUMS."""
    c = module_constants(os.path.join(HERE, "import_originals.py"))
    out = {}
    folder = ""
    for line in c["CHECKSUMS"].split("\n"):
        if not line:
            continue
        if line.startswith("= "):
            folder = line[2:]
            continue
        sha, size, name = line.split(" ", 2)
        out[folder + name] = (int(size), sha)
    return out, c


def expected_eur():
    """{relative path: sha256} from import_eur.py EXPECTED (all 913 entries)."""
    c = module_constants(os.path.join(HERE, "import_eur.py"))
    out = {}
    for line in c["EXPECTED"].strip().splitlines():
        digest, rel = line.split("  ", 1)
        if rel in out:
            fail("EXPECTED lists %s twice" % rel)
        out[rel] = digest
    return out


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---- classification

def group_of(path):
    low = path.lower()
    if low.startswith("data/fonts/"):
        return "fonts-rsrc"
    if low.startswith("data/movies/"):
        return "movies"
    if low.startswith("data/language/american/"):
        return "language:american"
    if low.startswith("data/language/english/"):
        return "language:english"
    return "core"


def is_font_resource_fork(path):
    return path.startswith("data/fonts/") and path.endswith(".rsrc")


# ---- the sets. A candidate is (dest, [(size, sha256)], group, edition, source, flags)

def us_set(cs, c):
    base = c["DISC"] + "/" + c["GAME_FOLDER"] + "/"
    patch = c["PATCH"] + "/" + c["PATCH_FOLDER"] + "/patch 2.04b3/SimTheme Park"
    out = []
    for path, (size, sha) in sorted(cs.items()):
        if not path.startswith(base):
            continue
        rel = path[len(base):]
        if rel.endswith(".finfo") or (rel.endswith(".rsrc") and not is_font_resource_fork(rel)):
            continue
        base_name = rel.rsplit("/", 1)[-1]
        if base_name == "Icon\r":
            continue
        if rel == PROGRAM_DEST:
            continue                                   # below, with both builds
        if not rel.startswith("data/"):
            continue
        flags = FLAG_REQUIRED | (FLAG_RSRC if rel.endswith(".rsrc") else 0)
        out.append((rel, [(size, sha)], group_of(rel), "us", "mac", flags))
    cd = cs.get(base + PROGRAM_DEST)
    pt = cs.get(patch)
    if cd != (PROGRAM_CD[2], PROGRAM_CD[1]):
        fail("CHECKSUMS has no CD program with the expected size and SHA-256 (%r)" % (cd,))
    if pt != (PROGRAM_PATCH[2], PROGRAM_PATCH[1]):
        fail("CHECKSUMS has no patch program with the expected size and SHA-256 (%r)" % (pt,))
    out.append((PROGRAM_DEST, [cd, pt], "program", "us", "mac", FLAG_REQUIRED))
    return out


def eur_set(exp, tree, draft):
    src = {x["path"]: x["source"] for x in draft["eur"]}
    for x in draft["eur"]:
        if tree.get(x["path"]) != [x["size"], x["sha256"]]:
            fail("draft-manifests.json and tree-ee54d817.json disagree on %s" % x["path"])
    if set(exp) != set(tree):
        fail("EXPECTED and tree-ee54d817.json list different paths (%d, %d)" % (len(exp), len(tree)))
    out = []
    for path in sorted(exp):
        size, sha = tree[path]
        if sha != exp[path]:
            fail("EXPECTED and tree-ee54d817.json disagree on the hash of %s" % path)
        side = path.rsplit(".", 1)[-1] if path.endswith((".rsrc", ".finfo")) else ""
        owner = path[:-len("." + side)] if side else path
        if owner not in src:
            fail("%s has no content source in draft-manifests.json" % owner)
        source = "mac" if src[owner] in ("mac", "mac-tuning") else "pc"
        flags = 0
        if side == "rsrc":
            flags |= FLAG_RSRC
        if side == "finfo":
            flags |= FLAG_FINFO
        if owner in MAC_TUNING:
            flags |= FLAG_TUNING
        if side == "" or is_font_resource_fork(path):
            flags |= FLAG_REQUIRED
        out.append((path, [(size, sha)], group_of(path), "eur", source, flags))
    return out


def gold_set(scratch, shalist):
    folder = os.path.join(scratch, "gold-cab")
    out = []
    with open(shalist, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            sha, name = line.split("  ", 1)
            name = name.split("/", 1)[1]
            p = os.path.join(folder, name)
            if sha256_file(p) != sha:
                fail("gold-cab/%s does not match gold-wads.sha256" % name)
            out.append((GOLD_DEST_FOLDER + "/" + name, [(os.path.getsize(p), sha)], "gold", "gold", "gold",
                        FLAG_REQUIRED))
    if len(out) != EXPECTED_WADS:
        fail("gold-wads.sha256 lists %d wads, expected %d" % (len(out), EXPECTED_WADS))
    p = os.path.join(folder, GOLD_CONFIG)
    out.append((GOLD_DEST_FOLDER + "/" + GOLD_CONFIG, [(os.path.getsize(p), sha256_file(p))], "gold", "gold", "gold",
                FLAG_REQUIRED))
    return out


def lang_dest(name, rel):
    return "%s/%s/%s" % (LANG_DEST_FOLDER, name, rel)


def canonical_names(base_records):
    """Spellings for the language files, from the European records: {lower-case name: name} for the text folder and
    {lower-case game path: game path} for the speech."""
    text, speech = {}, {}
    for dest, _, _, bits, _, _ in base_records:
        if not bits & EDITION_BIT["eur"]:
            continue
        low = dest.lower()
        if low.startswith("data/language/english/"):
            text[low.rsplit("/", 1)[1]] = dest.rsplit("/", 1)[1]
        elif speech_path(low):
            speech[low] = dest
    if len(speech) != SPEECH_FILES:
        fail("the eur records hold %d speech files, expected %d" % (len(speech), SPEECH_FILES))
    return text, speech


def speech_path(low):
    """True for a lower-case game path of one of the 20 speech files."""
    p = low.split("/")
    if p[:3] == ["data", "global", "speech"] and len(p) == 4:
        return p[3] in ("cat_speechbank.map", "cat_speechsfx.map", "speechhd.sdt", "lips.wad")
    if p[:2] == ["data", "levels"] and len(p) >= 5 and p[2] in WORLDS and p[3] == "speech":
        return (len(p) == 5 and p[4] in ("cat_speechbank.map", "cat_speechsfx.map", "speechhd.sdt")) or \
            (len(p) == 6 and p[4:] == ["lips", "sp_001.lip"])
    return False


def picture(low_parts):
    """(resolution, "Splash" or "Welcome", language) for data/init/<res>/<splash|welcome>_<lang>.tga, else None."""
    if len(low_parts) != 4 or low_parts[:2] != ["data", "init"] or not low_parts[3].endswith(".tga"):
        return None
    kind, _, lang = low_parts[3][:-4].partition("_")
    if kind not in ("splash", "welcome") or lang not in FOLDER_LANGUAGE or lang != FOLDER_LANGUAGE[lang].lower():
        return None
    return low_parts[2], kind.capitalize(), FOLDER_LANGUAGE[lang]


def edition_languages(base_records, canon_speech):
    """English and American: the eur and us records of the text folder, the speech and the pictures, under lang/."""
    out = []
    for name, edition in sorted(FROM_EDITION.items()):
        for dest, hs, _, bits, _, flags in base_records:
            if not bits & EDITION_BIT[edition] or flags & (FLAG_RSRC | FLAG_FINFO):
                continue
            low = dest.lower()
            parts = low.split("/")
            pic = picture(parts)
            if low.startswith("data/language/%s/" % name.lower()) and len(parts) == 4:
                rel = "data/Language/%s/%s" % (name, dest.rsplit("/", 1)[1])
                required = parts[3] != "alloweds.txt"
            elif speech_path(low):
                rel, required = canon_speech[low], True
            elif pic and pic[2] == name:
                rel, required = "data/Init/%s/%s_%s.tga" % (pic[0], pic[1], name), False
            else:
                continue
            out.append((lang_dest(name, rel), hs, "language-pack", "lang", "lang", FLAG_REQUIRED if required else 0))
    return out


def read_disc_list(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line:
                sha, size, rel = line.split("\t", 2)
                rows.append((rel, int(size), sha.lower()))
    return rows


def disc_languages(rows, canon_text, canon_speech):
    """{(language, game path): ((size, sha256), required)} for the language files of one disc list."""
    # the base speech is the language of the base data/Language folder that is not English or American, if only one
    base_text = set()
    for rel, _, _ in rows:
        p = rel.lower().split("/")
        if len(p) == 4 and p[:2] == ["data", "language"] and p[2] in FOLDER_LANGUAGE:
            base_text.add(FOLDER_LANGUAGE[p[2]])
    base_text -= set(FROM_EDITION)
    base_owner = base_text.pop() if len(base_text) == 1 else None

    out = {}
    for rel, size, sha in rows:
        parts = rel.split("/")
        low = [x.lower() for x in parts]
        if low[0] in FOLDER_LANGUAGE and len(low) > 1 and low[1] in ("data", "filter", "meshes"):
            owner, game, low_game = FOLDER_LANGUAGE[low[0]], parts[1:], low[1:]
        elif low[0] == "data":
            owner, game, low_game = None, parts, low
        else:
            continue
        name = game[-1]
        if low_game[0] in ("filter", "meshes"):
            if len(low_game) != 3:
                continue
            if low_game[0] == "filter" and low_game[2] in FILTER_FILES:
                key = (owner, "data/Language/%s/%s" % (owner, low_game[2]), low_game[2] != "alloweds.txt")
            elif low_game[0] == "meshes" and low_game[2].endswith(".md2"):
                key = (owner, "data/Language/%s/%s" % (owner, canon_text.get(low_game[2], name)), True)
            else:
                continue                                      # the .mtr files: the game never reads them
        elif len(low_game) == 4 and low_game[1] == "language" and low_game[2] in FOLDER_LANGUAGE:
            lang = owner or FOLDER_LANGUAGE[low_game[2]]
            key = (lang, "data/Language/%s/%s" % (lang, canon_text.get(low_game[3], name)), low_game[3] != "alloweds.txt")
        elif speech_path("/".join(low_game)):
            lang = owner or base_owner
            key = (lang, canon_speech["/".join(low_game)], True)
        elif picture(low_game):
            res, kind, lang = picture(low_game)
            key = (lang, "data/Init/%s/%s_%s.tga" % (res, kind, lang), False)
        else:
            continue
        if key[0] is None or key[0] in FROM_EDITION:
            continue
        out.setdefault(key[:2], set()).add(((size, sha), key[2]))
    return out


def language_set(folder, base_records):
    """The language records from the disc lists in FOLDER, and the language table [(name, flags)]."""
    canon_text, canon_speech = canonical_names(base_records)
    lists = sorted(f for f in os.listdir(folder) if f.endswith(".tsv"))
    if not lists:
        fail("no disc lists (*.tsv) in %s" % folder)
    found = {}
    for f in lists:
        for key, contents in disc_languages(read_disc_list(os.path.join(folder, f)), canon_text, canon_speech).items():
            found.setdefault(key, set()).update(contents)
    # A disc read without long names gives 8.3 names (CASHSMAL.BF4): such a file is dropped when the same language has
    # its content under a known name too, and fails the run otherwise.
    known = {v.lower() for v in canon_text.values()}
    for (lang, rel), contents in sorted(found.items()):
        if rel.startswith("data/Language/") and rel.rsplit("/", 1)[1].lower() not in known:
            elsewhere = set()
            for (l2, r2), c2 in found.items():
                if l2 == lang and r2 != rel and r2.rsplit("/", 1)[1].lower() in known:
                    elsewhere.update(h for h, _ in c2)
            if not {h for h, _ in contents} <= elsewhere:
                fail("%s %s: a file the English folder does not have, and no known name has its content" % (lang, rel))
            del found[(lang, rel)]
    out = edition_languages(base_records, canon_speech)
    for (lang, rel), contents in sorted(found.items()):
        required = {req for _, req in contents}
        hashes = sorted({h for h, _ in contents})
        if len(required) != 1:
            fail("%s %s is both required and optional" % (lang, rel))
        required = required.pop()
        if required and len(hashes) != 1:
            fail("%s %s has %d different contents on the discs" % (lang, rel, len(hashes)))
        out.append((lang_dest(lang, rel), hashes, "language-pack", "lang", "lang", FLAG_REQUIRED if required else 0))
    return out, language_table(out)


def language_table(candidates):
    """[(name, flags)] for the languages of LANGUAGES, checked: each has its tables, filter, meshes and speech."""
    table = []
    for name in LANGUAGES:
        prefix = lang_dest(name, "")
        mine = {c[0][len(prefix):]: c for c in candidates if c[0].startswith(prefix)}
        text = "data/Language/%s/" % name
        speech = [r for r in mine if speech_path(r.lower())]
        missing = [f for f in ("UITEXT.str", "residx.dat", "swears.txt") + MESHES if text + f not in mine]
        if missing or len(speech) != SPEECH_FILES:
            fail("%s: no %s; %d speech files" % (name, ", ".join(missing) or "file missing", len(speech)))
        residx = mine[text + "residx.dat"][1]
        old_residx = any(size == OLD_RESIDX_SIZE for size, _ in residx)
        if old_residx and name not in OLD_TABLES:
            fail("%s has the older residx.dat but is not in OLD_TABLES" % name)
        flags = LANGUAGE_OLD_TABLES if name in OLD_TABLES else 0
        dates = [text + f in mine for f in DATE_FONTS]
        if any(dates) and not all(dates):
            fail("%s has some of the date fonts but not all" % name)
        if not any(dates):
            flags |= LANGUAGE_BASE_DATE_FONTS
        table.append((name, flags))
    stray = [c[0] for c in candidates if c[0].split("/")[1] not in LANGUAGES]
    if stray:
        fail("language records outside the %d names: %s" % (len(LANGUAGES), stray[0]))
    return table


def merge(candidates):
    """One record per (dest, hashes, group, flags, source); the editions of equal candidates are or-ed together."""
    recs = {}
    for dest, hashes, group, edition, source, flags in candidates:
        key = (dest, tuple(hashes), group, flags, source)
        recs[key] = recs.get(key, 0) | EDITION_BIT[edition]
    out = []
    for (dest, hashes, group, flags, source), editions in recs.items():
        out.append((dest, list(hashes), group, editions, source, flags))
    out.sort(key=lambda r: (r[0].encode("utf-8"), r[3], r[4], r[5]))
    seen = set()
    for r in out:
        for e, bit in EDITION_BIT.items():
            if r[3] & bit:
                if (r[0], e) in seen:
                    fail("%s is listed twice for %s with different content" % (r[0], e))
                seen.add((r[0], e))
    return out


# ---- the C++ text

def c_string(s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"' or ch == "\\":
            out.append("\\" + ch)
        elif 0x20 <= o < 0x7F and ch != "?":
            out.append(ch)
        elif o < 0x80:
            out.append("\\%03o" % o)
        else:
            fail("a destination with a non-ASCII character: %r" % s)
    out.append('"')
    return "".join(out)


def flag_text(flags):
    names = []
    for bit, name in ((FLAG_REQUIRED, "kFlagRequired"), (FLAG_RSRC, "kFlagRsrc"), (FLAG_FINFO, "kFlagFinfo"),
                      (FLAG_TUNING, "kFlagTuning")):
        if flags & bit:
            names.append(name)
    return "|".join(names) if names else "0"


def edition_text(bits):
    return "|".join(EDITION_NAME[e] for e in EDITION_BIT if bits & EDITION_BIT[e])


def language_flag_text(flags):
    names = []
    for bit, name in ((LANGUAGE_OLD_TABLES, "kLanguageOldTables"), (LANGUAGE_BASE_DATE_FONTS, "kLanguageBaseDateFonts")):
        if flags & bit:
            names.append(name)
    return "|".join(names) if names else "0"


def render(records, languages):
    hashes = []                       # (sha bytes, size, record index)
    for ri, (_, hs, _, _, _, _) in enumerate(records):
        for size, sha in hs:
            hashes.append((bytes.fromhex(sha), size, ri))
    first = []
    n = 0
    for _, hs, _, _, _, _ in records:
        first.append(n)
        n += len(hs)
    by_hash = sorted(range(len(hashes)), key=lambda i: (hashes[i][0], hashes[i][1], hashes[i][2]))

    counts = {}
    for e in EDITION_BIT:
        counts[e] = sum(1 for r in records if r[3] & EDITION_BIT[e])

    o = []
    o.append("// manifest_data.inc: generated by devtools/make_manifest.py; do not edit. Hashes, sizes and names only.\n")
    o.append("// Included once, by assets/manifest.cpp, inside namespace tpw::assets.\n")
    o.append("// %d records (us %d, eur %d, gold %d, lang %d; a record shared by two editions is counted in both), %d hashes.\n"
             % (len(records), counts["us"], counts["eur"], counts["gold"], counts["lang"], len(hashes)))
    o.append("\n")
    o.append("// One accepted content: SHA-256, size in bytes and the record it belongs to. A record's hashes are\n")
    o.append("// consecutive, from its first_hash.\n")
    o.append("const ManifestHash kManifestHashes[] = {\n")
    for sha, size, ri in hashes:
        o.append("    {{%s}, %du, %du},\n" % (",".join("0x%02x" % b for b in sha), size, ri)
                 if size < (1 << 32) else
                 "    {{%s}, %dull, %du},\n" % (",".join("0x%02x" % b for b in sha), size, ri))
    o.append("};\n\n")
    o.append("// dest, first_hash, hash_count, group, editions, source, flags; sorted by dest (bytes), then editions.\n")
    o.append("const ManifestRecord kManifestRecords[] = {\n")
    for ri, (dest, hs, group, editions, source, flags) in enumerate(records):
        o.append("    {%s, %du, %d, %s, %s, %s, %s},\n"
                 % (c_string(dest), first[ri], len(hs), GROUP_NAME[group], edition_text(editions), SOURCE_NAME[source],
                    flag_text(flags)))
    o.append("};\n\n")
    o.append("// Indexes into kManifestHashes, sorted by SHA-256, then size, then record: the lookup by content.\n")
    o.append("const uint32_t kManifestByHash[] = {\n")
    line = "   "
    for i in by_hash:
        item = " %du," % i
        if len(line) + len(item) > 118:
            o.append(line + "\n")
            line = "   "
        line += item
    o.append(line + "\n};\n\n")
    o.append("// The language packs (records of kEditionLanguages under lang/<name>/): name and flags, in the original's order.\n")
    o.append("const ManifestLanguage kManifestLanguages[] = {\n")
    for name, flags in languages:
        o.append("    {%s, %s},\n" % (c_string(name), language_flag_text(flags)))
    o.append("};\n")
    return "".join(o)


def verify_program(extracted):
    pairs = (("disc-hfs/Theme Park Data/SimTheme Park", PROGRAM_CD),
             ("SimTPPatch_204b3/Sim Theme Park Patch/patch 2.04b3/SimTheme Park", PROGRAM_PATCH))
    for rel, (sha1, sha256, size) in pairs:
        with open(os.path.join(extracted, rel), "rb") as f:
            data = f.read()
        if (hashlib.sha1(data).hexdigest(), hashlib.sha256(data).hexdigest(), len(data)) != (sha1, sha256, size):
            fail("%s does not match the program constants" % rel)
        print("program ok: %s %d bytes" % (sha1, size))


# ---- an earlier output, read back (no study folder needed)

def c_unescape(text):
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != "\\":
            out.append(ch)
            i += 1
        elif text[i + 1] in "01234567":
            out.append(chr(int(text[i + 1:i + 4], 8)))
            i += 4
        else:
            out.append(text[i + 1])
            i += 2
    return "".join(out)


def parse_inc(path):
    """The records of an earlier manifest_data.inc, as (dest, [(size, sha256)], group, editions, source, flags)."""
    group_code = {v: k for k, v in GROUP_NAME.items()}
    edition_code = {v: k for k, v in EDITION_NAME.items()}
    source_code = {v: k for k, v in SOURCE_NAME.items()}
    flag_code = {"kFlagRequired": FLAG_REQUIRED, "kFlagRsrc": FLAG_RSRC, "kFlagFinfo": FLAG_FINFO,
                 "kFlagTuning": FLAG_TUNING, "0": 0}
    hashes, records, languages = [], [], []
    section = None
    with open(path, "r", encoding="utf-8", newline="") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("const "):
                section = line.split()[2].split("[")[0]
                continue
            if not line.startswith("    {"):
                continue
            if section == "kManifestHashes":
                digest, rest = line[6:].split("}, ", 1)
                size, record = rest.rstrip("},").split(", ")
                hashes.append(("".join("%02x" % int(b, 16) for b in digest.split(",")), int(size.rstrip("ul")),
                               int(record.rstrip("u"))))
            elif section == "kManifestRecords":
                end = line.rindex('", ')
                dest = c_unescape(line[6:end])
                first, count, group, editions, source, flags = line[end + 3:].rstrip("},").split(", ")
                bits = 0
                for e in editions.split("|"):
                    bits |= EDITION_BIT[edition_code[e]]
                fl = 0
                for x in flags.split("|"):
                    fl |= flag_code[x]
                first, count = int(first.rstrip("u")), int(count)
                hs = [(size, sha) for sha, size, _ in hashes[first:first + count]]
                if len(hs) != count or any(r != len(records) for _, _, r in hashes[first:first + count]):
                    fail("%s: the hashes of %s do not point back to it" % (path, dest))
                records.append((dest, hs, group_code[group], bits, source_code[source], fl))
            elif section == "kManifestLanguages":
                name, flags = line[6:].rstrip("},").split('", ')
                fl = 0
                for x in flags.split("|"):
                    fl |= {"kLanguageOldTables": LANGUAGE_OLD_TABLES, "kLanguageBaseDateFonts": LANGUAGE_BASE_DATE_FONTS,
                           "0": 0}[x]
                languages.append((c_unescape(name), fl))
    if not records or len(hashes) != sum(len(r[1]) for r in records):
        fail("%s holds no records, or hashes that belong to none" % path)
    return records, languages


def build(scratch):
    cs, c = checksums()
    exp = expected_eur()
    tree_us = load_json(os.path.join(scratch, "tree-e921f604.json"))
    tree_eur = load_json(os.path.join(scratch, "tree-ee54d817.json"))
    draft = load_json(os.path.join(scratch, "draft-manifests.json"))
    # CHECKSUMS against the study's own hashes of the same Mac folder (nothing is taken from the study for the US set)
    base = c["DISC"] + "/" + c["GAME_FOLDER"] + "/"
    mine = {p[len(base):]: list(v) for p, v in cs.items() if p.startswith(base)}
    for p, v in mine.items():
        if tree_us.get(p) != v:
            fail("CHECKSUMS and tree-e921f604.json disagree on %s" % p)
    cand = us_set(cs, c) + eur_set(exp, tree_eur, draft) + gold_set(scratch, os.path.join(scratch, "gold-wads.sha256"))
    return merge(cand)


def summary(records, languages):
    lines = []
    for e, bit in EDITION_BIT.items():
        rs = [r for r in records if r[3] & bit]
        by_group = {}
        for r in rs:
            by_group[r[2]] = by_group.get(r[2], 0) + 1
        lines.append("%-5s %4d records; groups %s" % (e, len(rs), ", ".join("%s %d" % (g, by_group[g]) for g in GROUPS
                                                                        if g in by_group)))
        req = sum(1 for r in rs if r[5] & FLAG_REQUIRED)
        lines.append("      required %d, optional %d, rsrc %d, finfo %d, tuning %d; sources %s"
                     % (req, len(rs) - req, sum(1 for r in rs if r[5] & FLAG_RSRC), sum(1 for r in rs if r[5] & FLAG_FINFO),
                        sum(1 for r in rs if r[5] & FLAG_TUNING),
                        ", ".join("%s %d" % (s, sum(1 for r in rs if r[4] == s)) for s in SOURCE_NAME
                                  if any(r[4] == s for r in rs))))
    for name, flags in languages:
        rs = [r for r in records if r[0].startswith(lang_dest(name, ""))]
        req = sum(1 for r in rs if r[5] & FLAG_REQUIRED)
        lines.append("      %-18s %3d records, required %d, optional %d%s" % (
            name, len(rs), req, len(rs) - req, "".join(t for b, t in ((LANGUAGE_OLD_TABLES, "; older text tables"),
                                                                      (LANGUAGE_BASE_DATE_FONTS, "; base date fonts"))
                                                     if flags & b)))
    return "\n".join(lines)


def records_as_candidates(records):
    out = []
    for dest, hs, group, bits, source, flags in records:
        for e in EDITION_BIT:
            if bits & EDITION_BIT[e]:
                out.append((dest, hs, group, e, source, flags))
    return out


def from_base(path, keep_languages):
    """The records of an earlier output (the language ones only with KEEP_LANGUAGES), and its language table."""
    out = []
    records, languages = parse_inc(path)
    for dest, hs, group, bits, source, flags in records:
        for e in EDITION_BIT:
            if bits & EDITION_BIT[e] and (keep_languages or e != "lang"):
                out.append((dest, hs, group, e, source, flags))
    return merge(out), languages


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--scratch", default=DEFAULT_SCRATCH, help="the launcher-design study's folder")
    ap.add_argument("--base", metavar="FILE", help="take the us, eur and gold records from an earlier output instead of "
                    "the study's lists")
    ap.add_argument("--languages", metavar="DIR", default=DEFAULT_LANGUAGES,
                    help="the disc lists (*.tsv: SHA-256, size and path per file) the language packs come from")
    ap.add_argument("--keep-languages", action="store_true",
                    help="with --base: take the language records and table from FILE too (no disc lists read)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="compare with --out instead of writing it")
    ap.add_argument("--verify-program", metavar="EXTRACTED", help="recompute the program builds' hashes from ROOT/extracted")
    a = ap.parse_args(argv)
    try:
        if a.verify_program:
            verify_program(a.verify_program)
        if a.keep_languages and not a.base:
            fail("--keep-languages needs --base")
        if a.base:
            records, languages = from_base(a.base, a.keep_languages)
        else:
            records, languages = build(a.scratch), []
        if not a.keep_languages:
            extra, languages = language_set(a.languages, records)
            records = merge(records_as_candidates(records) + extra)
        text = render(records, languages)
    except Fail as e:
        sys.exit("make_manifest: %s" % e)
    print(summary(records, languages))
    if a.check:
        with open(a.out, "r", encoding="utf-8", newline="") as f:
            same = f.read() == text
        print("up to date" if same else "DIFFERENT from %s" % a.out)
        return 0 if same else 1
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
