// net.h: the launcher's only network code. The port is played offline (port_CONTRIBUTING.md, "Playable offline"), and
// everything that talks to the internet is in this module, so that devtools/check_offline.py can hold the rest of the
// tree to that. Its one user is the update check (launcher/ui/updater.cpp).
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

// The pages the launcher opens in the browser (SDL_OpenURL), kept here so that no other file names a web address.
std::string ReleasePageUrl(const std::string& version);   // "0.0.3": its release page

}  // namespace tpw::net
