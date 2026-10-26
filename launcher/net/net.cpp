// net.cpp: net.h over the system's curl.
#include "net.h"

#include <SDL3/SDL.h>

#include "process.h"

namespace tpw::net {

namespace {
const char* const kRepoPage = "https://github.com/ThemeParkWorldDecomp/tpw-port";
constexpr size_t kMaxOutput = 64 * 1024;

std::string CurlProgram() {
    if (const char* hook = SDL_getenv("TPW_NET_CURL"); hook != nullptr && *hook != '\0') return hook;
#ifdef _WIN32
    // Windows 10 (1803) and later ship curl in System32. The full path, so that no other curl.exe on PATH is taken.
    const char* root = SDL_getenv("SystemRoot");
    return std::string(root != nullptr && *root != '\0' ? root : "C:\\Windows") + "\\System32\\curl.exe";
#else
    return "/usr/bin/curl";
#endif
}

const char* NullFile() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}

bool AllDigits(const std::string& s, size_t from, size_t to) {
    if (to <= from || to - from > 6) return false;
    for (size_t i = from; i < to; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// "1.2.3": three groups of 1 to 6 digits, nothing else.
bool PlainVersion(const std::string& s) {
    const size_t a = s.find('.');
    if (a == std::string::npos) return false;
    const size_t b = s.find('.', a + 1);
    if (b == std::string::npos) return false;
    return AllDigits(s, 0, a) && AllDigits(s, a + 1, b) && AllDigits(s, b + 1, s.size());
}
}  // namespace

Fetched Curl(const std::vector<std::string>& args, const Limits& limits) {
    proc::Options options;
    // -q first: no ~/.curlrc. https only, also where a redirect would go (none is followed here anyway).
    options.argv = {CurlProgram(), "-q", "-s", "--proto", "=https", "--proto-redir", "=https",
                    "--connect-timeout", std::to_string(limits.seconds < 5 ? limits.seconds : 5),
                    "--max-time", std::to_string(limits.seconds), "-A", "tpw-port-launcher"};
    options.argv.insert(options.argv.end(), args.begin(), args.end());
    options.timeoutSeconds = limits.seconds + 2;
    options.maxOutputBytes = kMaxOutput;
    options.cancel = limits.cancel;
    const proc::Result run = proc::Run(options);
    Fetched fetched;
    if (!run.started)
        fetched.why = "curl could not be started (" + options.argv[0] + ")";
    else if (run.cancelled)
        fetched.why = "stopped";
    else if (run.timedOut)
        fetched.why = "curl did not finish in time";
    else if (run.tooMuch)
        fetched.why = "the answer was too long";
    else if (run.exitCode != 0)
        fetched.why = "curl failed (exit code " + std::to_string(run.exitCode) + ")";
    else
        fetched.ok = true;
    if (fetched.ok) fetched.output = run.output;
    return fetched;
}

Release ParseLatestRelease(const std::string& curlLine) {
    Release release;
    std::string line = curlLine;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    const std::string releases = std::string(kRepoPage) + "/releases";
    const std::string tagPrefix = releases + "/tag/v";
    // GitHub answers 302. A captive portal, a proxy's page or a changed site answer something else: no answer.
    if (line.rfind("302 ", 0) != 0) {
        release.why = "not a redirect: \"" + line.substr(0, 80) + "\"";
        return release;
    }
    const std::string url = line.substr(4);
    if (url == releases) {
        release.answered = true;   // no release yet
    } else if (url.rfind(tagPrefix, 0) == 0 && PlainVersion(url.substr(tagPrefix.size()))) {
        release.answered = true;
        release.version = url.substr(tagPrefix.size());
    } else {
        release.why = "an unexpected redirect: \"" + url.substr(0, 80) + "\"";
    }
    return release;
}

Release LatestRelease(const Limits& limits) {
    const Fetched fetched = Curl({"--max-redirs", "0", "-o", NullFile(), "-w", "%{http_code} %{redirect_url}",
                                  std::string(kRepoPage) + "/releases/latest"},
                                 limits);
    if (!fetched.ok) {
        Release release;
        release.why = fetched.why;
        return release;
    }
    return ParseLatestRelease(fetched.output);
}

std::string ReleasePageUrl(const std::string& version) {
    return std::string(kRepoPage) + "/releases/tag/v" + version;
}

}  // namespace tpw::net
