// net.h: the launcher's only network code. The port is played offline (port_CONTRIBUTING.md, "Playable offline"), and
// everything that talks to the internet is in this module, so that devtools/check_offline.py can hold the rest of the
// tree to that. Its one user is the update check (launcher/ui/updater.cpp), which also reads the online server's
// address from the tpw-port repository (OnlinePointer below).
//
// There is no TLS or HTTP library in the port: the questions are asked by the system's own curl (macOS /usr/bin/curl,
// Windows %SystemRoot%\System32\curl.exe, both part of the system), run through launcher/ui/process.h with a short time
// limit and a cap on what it may print. Every call blocks, so the caller runs it off the UI thread. Any failure (no
// internet, GitHub down, a rate limit, a captive portal's page, a time-out, curl missing) is "no answer", and an answer
// is accepted only in the exact shape GitHub gives it.
//
// Test hook (for net_tests, not for players): TPW_NET_CURL names the program to run instead of the system's curl.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace tpw::net {

struct Limits {
    int seconds = 10;   // curl's --max-time; the process is stopped 2 seconds after it, should curl not stop itself
    const std::atomic<bool>* cancel = nullptr;   // set by another thread: curl is stopped (the launcher is closing)
};

// What curl printed (its -w line or the body), when it ran and exited 0. `why` says what went wrong otherwise.
struct Fetched {
    bool ok = false;
    std::string output;
    std::string why;
};

// Runs curl with the fixed safe options (no .curlrc, silent, https only, also after a redirect, a connect time-out,
// --max-time, a user agent) and then `args`. 64 KiB of output at most. Public for the tests; the updater uses the two
// questions below.
Fetched Curl(const std::vector<std::string>& args, const Limits& limits = {});

// The newest tpw-port release: GitHub's /releases/latest answers with a redirect to /releases/tag/vX.Y.Z, or to
// /releases when there is no release yet. Nothing else is an answer.
struct Release {
    bool answered = false;
    std::string version;   // "0.0.3" (without the v); empty when answered and there is no release yet
    std::string why;       // when not answered
};
Release LatestRelease(const Limits& limits = {});
// The parse alone: curl's "-w '%{http_code} %{redirect_url}'" line.
Release ParseLatestRelease(const std::string& curlLine);

// The online server's address, which the port's online screens connect to (platform/online_boss.cpp) when the player
// names none: online.txt on tpw-port's main branch, read with the update check, so that a launcher already installed
// can be pointed at another server. Its whole form (devtools/split/online.txt):
//
//   # a comment line, starting with #
//   server=HOST
//
// blank lines, comment lines and exactly one server line, 4 KiB at most, printable ASCII. HOST is a DNS name or an
// IPv4 or IPv6 literal (ValidHost): no scheme, no port, no path. Anything else is refused whole, with the reason.
inline constexpr size_t kOnlinePointerMax = 4096;
// The address used when no answer has ever come (a launcher offline since it was installed, or one with Check for
// updates off): the server's own address, the only place it is written.
inline constexpr const char* kOnlineServerBuiltIn = "themedecompworld.net";
struct OnlinePointer {
    bool ok = false;
    std::string server;   // when ok
    std::string why;      // when not
};
OnlinePointer OnlineServerPointer(const Limits& limits = {});
// The parse alone: the file as it came.
OnlinePointer ParseOnlinePointer(const std::string& body);
// A DNS name (labels of letters, digits and hyphens, 253 characters at most, not all digits) or an IPv4 or IPv6
// literal (no brackets, no zone). False with the reason otherwise ("a scheme", "a port", ...).
bool ValidHost(const std::string& host, std::string* why = nullptr);

// The pages the launcher opens in the browser (SDL_OpenURL), kept here so that no other file names a web address.
std::string ReleasePageUrl(const std::string& version);   // "0.0.3": its release page

}  // namespace tpw::net
