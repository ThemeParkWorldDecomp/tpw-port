#!/usr/bin/env python3
"""check_no_game_data.py: keeps a link to the game's files out of the repository.

usage: python3 -I devtools/check_no_game_data.py [--base REV] [--tree] [--self-test]

The repository never holds the game's files, a link to them, or the place a particular copy came from (CONTRIBUTING.md,
"Before a pull request"). This fails (exit 1) on what a download link looks like:

  * an archive.org URL that is a download, a details page, a stream, an embed or a metadata record
    (archive.org/download/..., /details/..., /compress/..., /stream/..., /embed/..., /metadata/..., /services/...),
    or the ia command line tool's "ia download";
  * a web address ending in a disc image or a Mac disc format (.iso .img .bin .cue .ccd .mdf .mds .nrg .toast .dmg .sit
    .sea .hqx), except on github.com and githubusercontent.com (a release's own files);
  * a magnet link or a .torrent file;
  * a share link of a file host (mega.nz/file, drive.google.com/file, mediafire.com/file, dropbox.com/s/, 1drv.ms,
    wetransfer.com/downloads);
  * a file added with a disc image's name (the same extensions), or one named .playtest-data (the playtest's local file
    for the data location).

It also fails on what identifies a particular download without linking to it:

  * a 32-hex string (an MD5) anywhere outside third_party/;
  * a 40- or 64-hex string (SHA-1, SHA-256) on a line that talks about a disc, image, zip, download, item, upload,
    pressing or Redump, except in the files that list the game's own files (HASH_FILES);
  * a Redump id ("redump 12345"), a game-download site's name (myabandonware, abandonia, emuparadise, vimm, cdromance,
    redump.org) and a title-shaped item name (words joined by underscores with a year and a month in them);
  * the word archive.org itself, outside third_party/ and outside the files the export does not publish (ARCHIVE_OK:
    HANDOFF.md, notes/ and devtools/split/ except its port_ files).

It allows, on purpose: the words "Internet Archive" in prose and any link that is not one of the above. It does not find a
download link spelled in a way not listed here: review still reads the diff.

Modes: --base REV checks what a pull request adds relative to REV (the added lines of the diff REV..HEAD, the names of
the added files and the commits' messages); --tree checks every tracked file (the default when --base is not given),
leaving vendored third_party/ alone. --self-test runs the patterns over samples.
"""
import re
import subprocess
import sys

IMAGE_EXT = r"(?:iso|img|bin|cue|ccd|mdf|mds|nrg|toast|dmg|sit|sea|hqx)"
HOST_OK = r"(?:[a-z0-9-]+\.)*(?:github\.com|githubusercontent\.com)"

PATTERNS = [
    ("an archive.org download or item link",
     re.compile(r"(?<![a-z0-9.-])(?!web\.)(?:[a-z0-9-]+\.)*archive\.org/"
                r"(?:download|details|compress|stream|embed|metadata|services|serve)/", re.I)),
    ("the ia tool downloading", re.compile(r"\bia\s+download\b", re.I)),
    ("a web address ending in a disc image",
     re.compile(r"https?://(?!" + HOST_OK + r"[/:])[^\s\"'<>)]*\." + IMAGE_EXT + r"(?![a-z0-9])", re.I)),
    ("a magnet link or a torrent file", re.compile(r"magnet:\?xt=|\.torrent(?![a-z0-9])", re.I)),
    ("a file host's share link",
     re.compile(r"(?<![a-z0-9.-])(?:mega\.nz/(?:file|folder|#)|drive\.google\.com/(?:file|uc|open)|mediafire\.com/(?:file|\?)|"
                r"dropbox\.com/(?:s|scl)/|1drv\.ms/|wetransfer\.com/downloads)", re.I)),
]
# What names a download without linking to it (the maintainer, 2026-10-27: no hashes of downloads, item titles or ids in a repository).
HEX32 = re.compile(r"(?<![0-9a-f])[0-9a-f]{32}(?![0-9a-f])", re.I)
GUID_BYTES = re.compile(r"""fromhex\(\s*(?:'[0-9a-f]{32}'|"[0-9a-f]{32}")\s*\)""", re.I)   # a GUID in a test, not a hash
HEX40_64 = re.compile(r"(?<![0-9a-f])(?:[0-9a-f]{40}|[0-9a-f]{64})(?![0-9a-f])", re.I)
HASH_CONTEXT = re.compile(r"\b(?:iso|img|cd|discs?|zip|toast|download(?:s|ed)?|items?|upload(?:s|ed)?|image|pressing|redump)\b", re.I)
REDUMP_ID = re.compile(r"redump[ _-]?\d+", re.I)
SITE_NAME = re.compile(r"\b(?:myabandonware|abandonia|emuparadise|vimm|cdromance)\b|redump\.org", re.I)
TITLE_SHAPED = re.compile(r"\w+_\w+_.*_(?:19|20)\d\d-\d\d_|Image_2000")
BARE_ARCHIVE = re.compile(r"archive\.org", re.I)
# The files that list the game's own files by hash (per-file SHA-1/SHA-256, program builds, tool downloads): a long hash
# beside the word "image" or "download" there names a file of the game or a tool, not a particular download.
HASH_FILES = {"devtools/import_eur.py", "devtools/import_originals.py", "assets/manifest_data.inc",
              "include/platform/game_table_lists.inc", "tests/host_manifest.cpp", "Makefile", "install.ps1"}
# The monorepo's own files that the export does not publish (export_split.py MONO_ONLY): archive.org may be written there.
# Not README.md: the port's README.md is published, so it stays under the rule (as does every file in a published tree).
ARCHIVE_OK = {"HANDOFF.md", "NOTES.md", "STATUS.md", "PROGRESS.md"}


def archive_ok(path):
    return (path in ARCHIVE_OK or path.startswith("notes/")
            or (path.startswith("devtools/split/") and not path.startswith("devtools/split/port_")))


NAME_BAD = re.compile(r"(?:\." + IMAGE_EXT + r"$|(?:^|/)\.playtest-data$)", re.I)
SELF = {"devtools/check_no_game_data.py", ".github/workflows/no-game-data.yml"}


def check_line(text, path=""):
    """Why `text`, a line of the file `path` (empty for a commit message), is refused."""
    found = [why for why, rx in PATTERNS if rx.search(text)]
    if HEX32.search(GUID_BYTES.sub("", text)):
        found.append("a 32-hex string (an MD5)")
    if HEX40_64.search(text) and HASH_CONTEXT.search(text) and path not in HASH_FILES:
        found.append("a SHA-1 or SHA-256 hash beside a disc, image, download or item")
    if REDUMP_ID.search(text):
        found.append("a Redump id")
    if SITE_NAME.search(text):
        found.append("the name of a site the game is downloaded from")
    if TITLE_SHAPED.search(text):
        found.append("an item title")
    if BARE_ARCHIVE.search(text) and not archive_ok(path):
        found.append("archive.org named (only HANDOFF.md and the other unpublished files may)")
    return found


def git(*args):
    r = subprocess.run(("git",) + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        sys.exit("check_no_game_data: git %s failed: %s" % (" ".join(args), r.stderr.decode(errors="replace").strip()))
    return r.stdout.decode("utf-8", errors="replace")


def scan_diff(base):
    found = []
    path = None
    lineno = 0
    for raw in git("diff", "--unified=0", "--no-color", "--no-ext-diff", "--diff-filter=AMR", base, "HEAD").splitlines():
        if raw.startswith("+++ "):
            path = raw[6:] if raw.startswith("+++ b/") else None
        elif raw.startswith("@@"):
            m = re.search(r"\+(\d+)", raw)
            lineno = int(m.group(1)) if m else 0
        elif raw.startswith("+") and path and path not in SELF and not path.startswith("third_party/"):
            for why in check_line(raw[1:], path):
                found.append("%s:%d: %s" % (path, lineno, why))
            lineno += 1
    for name in git("diff", "--name-only", "--no-renames", "--diff-filter=AR", base, "HEAD").splitlines():
        if NAME_BAD.search(name):
            found.append("%s: a file with a disc image's name, or the playtest's local file" % name)
    for sha, body in (c.split("\0", 1) for c in git("log", "--format=%H%x00%B%x01", base + "..HEAD").split("\x01") if "\0" in c):
        for line in body.splitlines():
            for why in check_line(line):
                found.append("commit %s: %s" % (sha.strip()[:10], why))
    return found


def scan_files(items):
    """The problems in (name, bytes) pairs: the export checks the trees it is about to write with this."""
    found = []
    for name, data in items:
        if name in SELF or name.startswith("third_party/"):
            continue
        if NAME_BAD.search(name):
            found.append("%s: a file with a disc image's name, or the playtest's local file" % name)
            continue
        if b"\0" in data[:8000]:
            continue
        for n, line in enumerate(data.decode("utf-8", errors="replace").splitlines(), 1):
            for why in check_line(line, name):
                found.append("%s:%d: %s" % (name, n, why))
    return found


def scan_tree():
    def items():
        for name in git("ls-files", "-z").split("\0"):
            try:
                with open(name, "rb") as f:
                    yield name, f.read()
            except OSError:
                continue
    return scan_files(items())


def self_test():
    # The samples are joined from pieces so that this file does not hold a link itself.
    dl = "https://" + "archive" + ".org/" + "download/some-item/file.zip"
    cases = [
        (dl, "", True), ("see archive" + ".org/details/some-item", "", True), ("ia" + " download some-item", "", True),
        ("http://example.com/files/disc." + "iso", "", True), ("curl -O https://example.com/x/Disc." + "IMG", "", True),
        ("mag" + "net:?xt=urn:btih:abc", "", True), ("get it from mega" + ".nz/file/abc", "", True),
        ("https://drive.google" + ".com/file/d/abc/view", "", True),
        ("the files come from an Internet Archive item", "", False),
        ("https://github.com/o/r/releases/download/v1/tool." + "dmg", "", False),
        ("a binary, not a disc: x.bin" + "ary", "", False), ("https://example.com/page.html", "", False),
        ("see the bin" + "/ folder at https://example.com/a", "", False),
        # the word itself, a Wayback link, a home page: only the unpublished files may
        ("archive" + ".org is not used here", "README.md", True), ("https://archive" + ".org/", "x.md", True),
        ("https://web.archive" + ".org/web/2013/http://example.com/", "launcher/x.cpp", True),
        ("from archive" + ".org", "commit message", True),
        ("from archive" + ".org", "HANDOFF.md", False), ("from archive" + ".org", "notes/a.md", False),
        ("from archive" + ".org", "devtools/split/export_split.py", False),
        ("from archive" + ".org", "devtools/split/port_README.md", True),
        # an MD5 anywhere, a GUID in a test is not one
        ("md5 " + "d41d8cd98f00b204e9800998ecf8427e", "a.py", True), ("0x" + "D41D8CD98F00B204E9800998ECF8427E", "a.md", True),
        ("SFX_GUID = bytes.fromhex('" + "002c61e9d031d211b40900b0c993f203" + "')", "tests/t.py", False),
        ("a 40-hex that is no hash: " + "0" * 40, "a.md", False),
        # a long hash beside a disc or a download, except where the game's own files are listed
        ("the disc image's sha256 " + "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "a.md", True),
        ("sha1 of the downloaded zip " + "da39a3ee5e6b4b0d3255bfef95601890afd80709", "launcher/a.cpp", True),
        ("the zip item " + "da39a3ee5e6b4b0d3255bfef95601890afd80709", "include/platform/game_table_lists.inc", False),
        ('{"Data/a.wad", "' + "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" + '"},', "assets/manifest_data.inc", False),
        ("cmake sha256 " + "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "a.cmake", False),
        # Redump ids, site names, item titles
        ("Red" + "ump 12345", "a.md", True), ("red" + "ump-678", "a.cpp", True), ("the red" + "ump dat", "a.md", False),
        ("my" + "abandonware", "a.md", True), ("from Emu" + "paradise", "a.md", True), ("red" + "ump.org", "a.md", True),
        ("vi" + "mm's lair", "a.md", True), ("the Vimmer", "a.md", False),
        ("Sim_Theme_" + "Park_Mac_2000-05_" + "x", "a.md", True), ("Image_" + "2000", "a.md", True),
        ("a_b_c: a snake_case_name 2000", "a.cpp", False),
    ]
    bad = 0
    for text, path, want in cases:
        got = bool(check_line(text, path))
        if got != want:
            print("self-test: %r in %r: expected %s, got %s" % (text, path, want, got))
            bad += 1
    for name, want in ((("a/b/Disc." + "iso"), True), ("dir/.playtest-data", True), ("a/b.binary", False), ("c/scan.png", False)):
        if bool(NAME_BAD.search(name)) != want:
            print("self-test: file name %r: expected %s" % (name, want))
            bad += 1
    print("self-test: %d case(s), %d wrong" % (len(cases) + 4, bad))
    return 1 if bad else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    base = None
    if "--base" in argv:
        i = argv.index("--base")
        if i + 1 >= len(argv):
            sys.exit("check_no_game_data: --base wants a revision")
        base = argv[i + 1]
    found = scan_diff(base) if base and "--tree" not in argv else scan_tree()
    for line in found:
        print("check_no_game_data: " + line)
    if found:
        print("check_no_game_data: %d problem(s). The repository never holds the game's files or a link to them "
              "(CONTRIBUTING.md, \"Before a pull request\"); take the link out." % len(found))
        return 1
    print("check_no_game_data: ok (%s)" % ("what the pull request adds" if base and "--tree" not in argv else "every tracked file"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
