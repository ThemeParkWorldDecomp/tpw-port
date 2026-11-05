// net.cpp: net.h over the system's curl.
#include "net.h"

#include <SDL3/SDL.h>

#include "process.h"

namespace tpw::net {

namespace {
const char* const kRepoPage = "https://github.com/ThemeParkWorldDecomp/tpw-port";
const char* const kOnlinePointerUrl = "https://raw.githubusercontent.com/ThemeParkWorldDecomp/tpw-port/main/online.txt";
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

bool Digit(char c) { return c >= '0' && c <= '9'; }
bool HexDigit(char c) { return Digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool Letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::vector<std::string> Split(const std::string& s, char at) {
    std::vector<std::string> parts;
    size_t from = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == at) {
            parts.push_back(s.substr(from, i - from));
            from = i + 1;
        }
    return parts;
}

// "192.0.2.1": four numbers of 0 to 255, without leading zeros.
bool Ipv4(const std::string& s) {
    const std::vector<std::string> parts = Split(s, '.');
    if (parts.size() != 4) return false;
    for (const std::string& p : parts) {
        if (p.empty() || p.size() > 3 || (p.size() > 1 && p[0] == '0')) return false;
        for (char c : p)
            if (!Digit(c)) return false;
        if (std::stoi(p) > 255) return false;
    }
    return true;
}

// "2001:db8::1", "::ffff:192.0.2.1": eight groups of 1 to 4 hex digits, one "::" standing for one or more of them, the
// last two may be an IPv4 address.
bool Ipv6(const std::string& s) {
    if (s.size() < 2 || s.size() > 45) return false;
    const size_t gap = s.find("::");
    if (gap != std::string::npos && s.find("::", gap + 1) != std::string::npos) return false;
    auto groups = [](const std::string& part, bool last, int& count) {
        if (part.empty()) return true;
        const std::vector<std::string> fields = Split(part, ':');
        for (size_t i = 0; i < fields.size(); ++i) {
            const std::string& f = fields[i];
            if (last && i + 1 == fields.size() && f.find('.') != std::string::npos) {
                if (!Ipv4(f)) return false;
                count += 2;
                continue;
            }
            if (f.empty() || f.size() > 4) return false;
            for (char c : f)
                if (!HexDigit(c)) return false;
            ++count;
        }
        return true;
    };
    int count = 0;
    if (gap == std::string::npos) return groups(s, true, count) && count == 8;
    const std::string head = s.substr(0, gap), tail = s.substr(gap + 2);
    return groups(head, tail.empty(), count) && groups(tail, true, count) && count <= 7;
}

// "server.example.org", "localhost": labels of 1 to 63 letters, digits and hyphens, not starting or ending with one,
// the last not all digits (that is an IPv4 address or a mistake).
bool DnsName(const std::string& s) {
    const std::vector<std::string> labels = Split(s, '.');
    for (const std::string& l : labels) {
        if (l.empty() || l.size() > 63 || l.front() == '-' || l.back() == '-') return false;
        for (char c : l)
            if (!Letter(c) && !Digit(c) && c != '-') return false;
    }
    bool digits = true;
    for (char c : labels.back()) digits = digits && Digit(c);
    return !digits;
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

bool ValidHost(const std::string& host, std::string* why) {
    auto refuse = [why](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (host.empty()) return refuse("no host");
    if (host.size() > 253) return refuse("longer than 253 characters");
    if (host.find("://") != std::string::npos) return refuse("a scheme (only the host is written)");
    if (host.find('/') != std::string::npos) return refuse("a path (only the host is written)");
    if (host.front() == '[') return refuse("brackets (an IPv6 address is written without them, and without a port)");
    if (Ipv4(host) || Ipv6(host)) return true;
    const size_t colon = host.find(':');
    if (colon != std::string::npos && host.find(':', colon + 1) == std::string::npos)
        return refuse("a port (the ports are the game's own)");
    if (colon != std::string::npos) return refuse("not an IPv6 address");
    if (DnsName(host)) return true;
    return refuse("not a host name or an IP address");
}

OnlinePointer ParseOnlinePointer(const std::string& body) {
    OnlinePointer pointer;
    if (body.size() > kOnlinePointerMax) {
        pointer.why = "longer than " + std::to_string(kOnlinePointerMax) + " bytes";
        return pointer;
    }
    for (char c : body) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u != '\n' && u != '\r' && u != '\t' && (u < 0x20 || u > 0x7e)) {
            pointer.why = "a byte that is not printable ASCII (" + std::to_string(u) + ")";
            return pointer;
        }
    }
    std::vector<std::string> lines = Split(body, '\n');
    int lineNumber = 0, servers = 0;
    std::string server;
    for (std::string& line : lines) {
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::string key = "server=";
        if (line.rfind(key, 0) != 0) {
            pointer.why = "line " + std::to_string(lineNumber) + " is not a comment or server=HOST: \"" +
                          line.substr(0, 40) + "\"";
            return pointer;
        }
        if (++servers > 1) {
            pointer.why = "more than one server line (line " + std::to_string(lineNumber) + ")";
            return pointer;
        }
        server = line.substr(key.size());
        std::string why;
        if (!ValidHost(server, &why)) {
            pointer.why = "line " + std::to_string(lineNumber) + "'s server: " + why;
            return pointer;
        }
    }
    if (servers == 0) {
        pointer.why = "no server line";
        return pointer;
    }
    pointer.ok = true;
    pointer.server = server;
    return pointer;
}

OnlinePointer OnlineServerPointer(const Limits& limits) {
    // -f: a 404 (the file not published yet) or any other error page is a failure, not a body to read.
    const Fetched fetched = Curl({"-f", "--max-redirs", "0", "--max-filesize", std::to_string(kOnlinePointerMax),
                                  kOnlinePointerUrl},
                                 limits);
    if (!fetched.ok) {
        OnlinePointer pointer;
        pointer.why = fetched.why;
        return pointer;
    }
    return ParseOnlinePointer(fetched.output);
}

std::string ReleasePageUrl(const std::string& version) {
    return std::string(kRepoPage) + "/releases/tag/v" + version;
}

}  // namespace tpw::net
