#!/usr/bin/env python3
"""The "playable offline" rule's static check (CONTRIBUTING.md, "Playable offline"): nothing outside the launcher's
network module, launcher/net/, may talk to the internet. Run it from anywhere; CI runs it on every push.

usage: python3 -I devtools/check_offline.py [--root DIR]

  --root DIR   the tree to check (default: the one this script is in). A tpw-port clone with its submodules, or the
               monorepo.

Every C, C++ and Objective-C file of the tree (build folders and .git left out) is read with its comments taken out,
its strings kept, and these are findings:

  header    a network header: sys/socket.h, netinet/, arpa/inet.h, netdb.h, winsock.h, winsock2.h, ws2tcpip.h,
            wininet.h, winhttp.h, urlmon.h, curl/curl.h, CFNetwork, Network/Network.h
  call      a network call: socket(), getaddrinfo(), gethostbyname(), WSAStartup(), InternetOpen(), WinHttpOpen(),
            URLDownloadToFile(), NSURLSession and its kin, CFSocketCreate(), nw_connection_create(), SDLNet_, libcurl,
            inet_addr(), inet_pton(), and the socket calls of platform/winsock.cpp (the original's Winsock imports)
  address   a web address in a string ("http://", "https://", "ftp://"), outside third_party/
  program   curl or wget as a program to run: a string that is just "curl" (or a path to it, or curl.exe), or a command
            line that starts with it ("curl -fL ...")

launcher/net/ is the one place they belong and is not read. Everything else that has one must be on one of the two
lists below: ORIGINAL_ONLINE (the original game's online layer, kept as the original had it) or VENDORED (a library's
file, with the reason). An entry that no longer has a finding fails the check too, so the lists cannot go stale. In a
tpw-port clone, tpw-decomp/src/... and overrides/src/... are listed as src/....

What the check does not see: build tooling (shell scripts, the Makefile, CMake, devtools' Python), which runs while
building and is not part of the game or the launcher; and the update script the launcher's Update button runs
(install.sh, install.ps1), which downloads only when the player presses Update. The runtime half of the rule is in CI:
the tests and the launcher run there with the network blocked.

Exit status 0 with no findings and no stale entries, 1 otherwise.
"""
import argparse
import os
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm", ".inc"}
EXEMPT_PREFIXES = ("launcher/net/",)

# The original game's online layer: the TPW Online world, its e-mail and its FTP client, translated as the original had
# them, the port's stand-in for the Winsock calls they make (platform/winsock.cpp, over the host's sockets) and the unit
# tests of that code. They stay as they are: the translation keeps the original's code whole, and none of it runs
# unless the player picks the online world in the game's lobby, which the port does not offer (gPortOnlineWorldGone
# keeps the lobby's globe on the islands; the servers are long gone). Should it run anyway, a failure shows the game's
# own message and nothing else changes. The check still reads these files, so that the list holds them and no more: a
# new file that reaches the sockets is a finding. Paths as listed (see normalized()).
ORIGINAL_ONLINE = {
    "platform/winsock.cpp", "include/FS_Online.h", "include/email.h",
    "src/game/online/Connection.cpp", "src/game/online/EmailMessage.cpp", "src/game/online/FS_Online.cpp",
    "src/game/online/FTPClient.cpp", "src/game/online/IPBase.cpp",
    # Unit tests of it: the socket calls there are recording fakes the test defines; nothing is opened.
    "tests/unit_Email_Callback_2.cpp", "tests/unit_Email_Send.cpp", "tests/unit_FTPClient_ParseFileSize.cpp",
    "tests/unit_FTPClient_RecvData.cpp", "tests/unit_std___tree_insert_node_at_3.cpp",
}
# Vendored libraries (third_party/, as their projects published them), each with the reason its findings are harmless.
# Web addresses in their text (their log messages name their projects' pages) are not findings.
VENDORED = {
    "third_party/ffmpeg/libavformat/os_support.h": "FFmpeg's socket headers and calls, under #if CONFIG_NETWORK; our "
                                                   "config/config.h sets CONFIG_NETWORK 0 (only the file protocol is "
                                                   "built).",
    "third_party/ffmpeg/libavformat/url.c": "FFmpeg's address parsing, under #if CONFIG_NETWORK, which our "
                                            "config/config.h sets to 0.",
}
ALLOWED = ORIGINAL_ONLINE | set(VENDORED)

HEADERS = re.compile(r"^\s*#\s*(?:include|import)\s*[<\"]\s*("
                     r"sys/socket\.h|netinet/[^>\"]*|arpa/inet\.h|netdb\.h|winsock2?\.h|ws2tcpip\.h|mswsock\.h|"
                     r"wininet\.h|winhttp\.h|urlmon\.h|curl/[^>\"]*|CFNetwork/[^>\"]*|Network/Network\.h"
                     r")\s*[>\"]", re.I | re.M)
# The calls, also qualified (ns::socket, not obj.socket or obj->socket). WSAStartup, WSACleanup and WinsockStart are
# also the names of platform/winsock.cpp's own; the rest of its calls are reached through the *_callee namespaces below.
CALLS = re.compile(r"(?<![\w.>])(?:"
                   r"(?:socket|getaddrinfo|gethostbyname|gethostbyaddr|inet_addr|inet_pton|"
                   r"WSAStartup|WSACleanup|WinsockStart|WSASocket[AW]?|"
                   r"InternetOpen\w*|WinHttpOpen\w*|URLDownloadToFile[AW]?|CFSocketCreate\w*|"
                   r"CFStreamCreatePairWithSocket\w*|nw_connection_create|SDLNet_\w+|curl_easy_\w+|curl_global_init)"
                   r"\s*\(|(?:NSURLSession|NSURLConnection|NSURLRequest)\b|"
                   r"\b(?:conn|csock|email|emailcb|ftp|v5917)_callee::(?:Accept|Bind|CloseSocket|GetHostByName|"
                   r"GetSockName|Gethostname|InetAddr|Listen|Recv|Send|Shutdown|SockConnect|Socket)\s*\()")
ADDRESSES = re.compile(r"\"[^\"\n]*?\b(?:https?|ftp)://", re.I)
PROGRAMS = re.compile(r"\"(?:[^\"\s]*[/\\])?(?:curl|wget)(?:\.exe)?(?:\"| -)", re.I)
KINDS = (("header", HEADERS), ("call", CALLS), ("address", ADDRESSES), ("program", PROGRAMS))


def strip_comments(text):
    """text with its comments blanked (newlines kept, so line numbers hold) and its strings and characters kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == '"' or (c == "'" and not (i and (text[i - 1].isalnum() or text[i - 1] == "_"))):
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def normalized(rel):
    """A tpw-port path as the monorepo has it: tpw-decomp/src/x and overrides/src/x are src/x."""
    for prefix in ("tpw-decomp/", "overrides/"):
        if rel.startswith(prefix):
            return rel[len(prefix):]
    return rel


def sources(root):
    for top, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d != ".git" and not d.startswith("build")
                         and not os.path.exists(os.path.join(top, d, "CMakeCache.txt")))
        for name in sorted(files):
            if os.path.splitext(name)[1].lower() in SOURCE_SUFFIXES:
                yield pathlib.Path(top, name)


def findings(text):
    """(line, kind, the line's text) for each finding, in line order."""
    code, lines, found = strip_comments(text), text.split("\n"), []
    for kind, pattern in KINDS:
        for m in pattern.finditer(code):
            line = code.count("\n", 0, m.start()) + 1
            found.append((line, kind, lines[line - 1].strip()[:120]))
    return sorted(found)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(HERE.parent))
    root = pathlib.Path(ap.parse_args().root).resolve()

    scanned, bad, allowed_hits = 0, [], {}
    for path in sources(root):
        rel = path.relative_to(root).as_posix()
        if rel.startswith(EXEMPT_PREFIXES):
            continue
        scanned += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        key = normalized(rel)
        for line, kind, shown in findings(text):
            if kind == "address" and key.startswith("third_party/"):
                continue
            if key in ALLOWED:
                allowed_hits[key] = allowed_hits.get(key, 0) + 1
            else:
                bad.append("%s:%d: %s: %s" % (rel, line, kind, shown))

    stale = sorted(set(ALLOWED) - set(allowed_hits))
    for line in bad:
        print(line)
    for key in stale:
        print("%s: listed in devtools/check_offline.py but has no finding any more: take it off the list" % key)
    if bad:
        print("\nNetwork code belongs in launcher/net/ (CONTRIBUTING.md, \"Playable offline\"). Move it there, or, for "
              "a file of the original game's online layer or of a vendored library, add it to ORIGINAL_ONLINE or "
              "VENDORED in devtools/check_offline.py with the reason.")
    print("check_offline: %d files read, %d findings, %d allowed in %d files, %d stale entries"
          % (scanned, len(bad), sum(allowed_hits.values()), len(allowed_hits), len(stale)))
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())
