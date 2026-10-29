// log_scrub_tests.cpp: platform/log_scrub.h. The rules for the Mac's and Windows' paths (checked on every system, since the
// identity is described, not read), names with accents and spaces, a name that is also an ordinary word, the other
// identifying things (SIDs, MAC and IP addresses), that the useful parts of a log stay, and that the log file and Export
// logs scrub what they write. No window and no game.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <process.h>
#endif

#include "log_export.h"
#include "log_file.h"
#include "log_scrub.h"
#include "platform.h"

namespace fs = std::filesystem;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

// `input` scrubbed with `id` must be `expected`, and scrubbing that again must change nothing.
void Expect(const tpw::ScrubIdentity* id, const std::string& input, const std::string& expected) {
    const std::string got = tpw::Scrub(id, input);
    Check(got == expected, "scrub of [" + input + "] gave [" + got + "], wanted [" + expected + "]");
    const std::string again = tpw::Scrub(id, got);
    Check(again == got, "scrubbing [" + got + "] again changed it to [" + again + "]");
}

void Same(const tpw::ScrubIdentity* id, const std::string& input) { Expect(id, input, input); }

tpw::ScrubIdentity MacPlayer() {
    tpw::ScrubInputs in;
    in.homes = {"/Users/alex"};
    in.users = {"alex", "alex"};
    in.hosts = {"Alexs-MacBook-Pro.local"};
    in.roots = {{"/Volumes/My Backup", "/Volumes/<volume>"}};
    return tpw::MakeScrubIdentity(in);
}

tpw::ScrubIdentity WindowsPlayer() {
    tpw::ScrubInputs in;
    in.windows = true;
    in.homes = {"C:\\Users\\Zo\xC3\xAB M\xC3\xBCller", "C:\\Users\\ZOEMUL~1"};
    in.users = {"Zo\xC3\xAB M\xC3\xBCller"};
    in.hosts = {"DESKTOP-4F7Q2LM"};
    in.roots = {{"D:\\Roaming\\zoe", "%APPDATA%"}};
    return tpw::MakeScrubIdentity(in);
}

std::string Slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    const tpw::ScrubIdentity mac = MacPlayer();
    const tpw::ScrubIdentity win = WindowsPlayer();

    // The Mac: the home folder is ~.
    Expect(&mac, "data folder: /Users/alex/Library/Application Support/STP-TPW Launcher",
           "data folder: ~/Library/Application Support/STP-TPW Launcher");
    Expect(&mac, "saves: copied from \"/Users/alex/Documents/a b/saves\" to \"/Users/alex/x\"",
           "saves: copied from \"~/Documents/a b/saves\" to \"~/x\"");
    Expect(&mac, "[main app.cpp:12] opened /Users/alex.", "[main app.cpp:12] opened ~.");
    Expect(&mac, "log: /Users/alex", "log: ~");
    Expect(&mac, "args: tpw_launcher --data /Users/alex/tpw", "args: tpw_launcher --data ~/tpw");
    // A longer name that starts with the player's is another account, not the player followed by letters.
    Expect(&mac, "/Users/alexander/x", "~/x");
    Expect(&mac, "/Users/jordan/Desktop/a.txt", "~/Desktop/a.txt");
    Same(&mac, "/Users/Shared/TPW");
    // Inside a longer path the "/Users/" stays and the name goes.
    Expect(&mac, "/Volumes/Backup/Users/jordan/f", "/Volumes/<volume>/Users/<user>/f");
    Expect(&mac, "/System/Volumes/Data/Users/jordan/f", "/System/Volumes/Data/Users/<user>/f");
    // Volume names, the player's own with a space and any other.
    Expect(&mac, "import: /Volumes/My Backup/TPW/data", "import: /Volumes/<volume>/TPW/data");
    Expect(&mac, "import: \"/Volumes/Other Disk/x\"", "import: \"/Volumes/<volume>/x\"");
    Expect(&mac, "import: /Volumes/Disc1/x", "import: /Volumes/<volume>/x");
    Expect(&mac, "/var/folders/zz/abc123_Def/T/tpw/logs", "/var/folders/<id>/T/tpw/logs");
    Expect(&mac, "/private/var/folders/zz/abc123_Def/T/", "/private/var/folders/<id>/T/");
    // The account name where it is plainly the name.
    Expect(&mac, "user: alex", "user: <user>");
    Expect(&mac, "USER=alex", "USER=<user>");
    Expect(&mac, "{\"username\": \"alex\"}", "{\"username\": \"<user>\"}");
    Expect(&mac, "login = alex.", "login = <user>.");
    Same(&mac, "user: alexander");
    Same(&mac, "alex played for an hour");
    Same(&mac, "user alex");   // no = or : after the key: not clearly a name
    // The computer name, as a word of its own.
    Expect(&mac, "host Alexs-MacBook-Pro.local is up", "host <computer> is up");
    Expect(&mac, "host Alexs-MacBook-Pro is up", "host <computer> is up");
    Expect(&mac, "ssh://Alexs-MacBook-Pro:22", "ssh://<computer>:22");
    Same(&mac, "xAlexs-MacBook-Pro and Alexs-MacBook-Pros");
    Expect(&mac, "alexs-macbook-pro", "<computer>");   // long enough that the case does not matter
    // The useful lines stay.
    Same(&mac, "os: macOS 15.1 (24B83)");
    Same(&mac, "hardware: Mac14,5, arm64, Apple M2 Max, 12 cores, 32 GB RAM");
    Same(&mac, "display: 1 screen: \"Built-in Retina Display\" 1512x982 points, scale 2.00, 120 Hz (primary)");
    Same(&mac, "STP/TPW Launcher v0.0.3-dev (build: 92d34b53)");
    Same(&mac, "date: 2026-10-25T10:12:33-03:00 (ADT)");
    Same(&mac, "game folder data/Resources/levels/foo.sam; libavutil 61.1.102; /usr/lib/libSystem.B.dylib; /opt/homebrew/lib");
    Same(&mac, "exit: code 0");

    // A name that is also an ordinary word.
    {
        tpw::ScrubInputs in;
        in.homes = {"/Users/Max"};
        in.users = {"Max"};
        const tpw::ScrubIdentity max = tpw::MakeScrubIdentity(in);
        Same(&max, "Max has a max of 3; maximum, Maxine and the Max Payne speed");
        Expect(&max, "/Users/Max/Max.txt and Max", "~/Max.txt and Max");
        Expect(&max, "user=Max, max=3", "user=<user>, max=3");
        Same(&max, "username=max");   // the Mac keeps case: another word
        tpw::ScrubInputs data;
        data.homes = {"/Users/data"};
        data.users = {"data"};
        const tpw::ScrubIdentity word = tpw::MakeScrubIdentity(data);
        Same(&word, "data folder: /opt/data/foo and data/Resources");
        Expect(&word, "data folder: /Users/data/data", "data folder: ~/data");
    }

    // A name with accents and spaces, in UTF-8.
    {
        tpw::ScrubInputs in;
        in.homes = {"/Users/Jos\xC3\xA9 Garc\xC3\xAD" "a"};
        in.users = {"Jos\xC3\xA9 Garc\xC3\xAD" "a"};
        in.hosts = {"Jos\xC3\xA9s-iMac"};
        const tpw::ScrubIdentity jose = tpw::MakeScrubIdentity(in);
        Expect(&jose, "data folder: /Users/Jos\xC3\xA9 Garc\xC3\xAD" "a/Library/x", "data folder: ~/Library/x");
        Expect(&jose, "user: Jos\xC3\xA9 Garc\xC3\xAD" "a", "user: <user>");
        Expect(&jose, "on Jos\xC3\xA9s-iMac today", "on <computer> today");
        Same(&jose, "Jos\xC3\xA9 Garc\xC3\xAD" "a is a name in a story");
        Expect(&jose, "/Users/Jos\xC3\xA9x/y", "~/y");   // another account (generic rule), not the player and a letter
    }

    // Windows: the home is %USERPROFILE%, whatever the case, the slashes or the spelling.
    Expect(&win, "data folder: C:\\Users\\Zo\xC3\xAB M\xC3\xBCller\\AppData\\Roaming\\STP-TPW Launcher\\logs",
           "data folder: %USERPROFILE%\\AppData\\Roaming\\STP-TPW Launcher\\logs");
    Expect(&win, "c:\\users\\zo\xC3\xAB m\xC3\xBCller\\AppData\\Local", "%USERPROFILE%\\AppData\\Local");
    Expect(&win, "C:/Users/Zo\xC3\xAB M\xC3\xBCller/tpw", "%USERPROFILE%/tpw");
    Expect(&win, "C:\\Users\\ZOEMUL~1\\AppData\\Local\\Temp", "%USERPROFILE%\\AppData\\Local\\Temp");
    Expect(&win, "\\\\?\\C:\\Users\\Zo\xC3\xAB M\xC3\xBCller\\x", "\\\\?\\%USERPROFILE%\\x");
    Expect(&win, "log: C:\\Users\\Zo\xC3\xAB M\xC3\xBCller", "log: %USERPROFILE%");
    Expect(&win, "C:\\Users\\Zo\xC3\xAB M\xC3\xBCller\\OneDrive - Contoso Ltd\\Documents\\TPW",
           "%USERPROFILE%\\OneDrive\\Documents\\TPW");
    Expect(&win, "D:\\OneDrive - Contoso Ltd\\Documents", "D:\\OneDrive\\Documents");
    Expect(&win, "D:\\Roaming\\zoe\\STP", "%APPDATA%\\STP");
    Expect(&win, "E:\\Users\\sam\\Desktop", "%USERPROFILE%\\Desktop");   // another account on another drive
    Expect(&win, "C:\\Users\\sam smith\\Desktop", "%USERPROFILE% smith\\Desktop");   // an unknown name with a space: the limit
    Expect(&win, "\"C:\\Users\\sam smith\\Desktop\"", "\"%USERPROFILE%\\Desktop\"");   // quoted: whole
    Same(&win, "C:\\Users\\Public\\Documents");
    Same(&win, "C:\\Users\\Default\\x");
    Same(&win, "C:\\Program Files\\STP-TPW Launcher\\tpw_launcher.exe");
    Same(&win, "C:\\Windows\\System32\\ntdll.dll");
    Same(&win, "os: Windows 10.0 (build 22631)");
    Same(&win, "hardware: unknown model, x64, unknown cpu, 16 cores, 15.9 GB RAM");
    Expect(&win, "USERNAME=zo\xC3\xAB m\xC3\xBCller", "USERNAME=<user>");   // Windows names are case-blind
    Expect(&win, "computer DESKTOP-4F7Q2LM and desktop-4f7q2lm.local", "computer <computer> and <computer>.local");
    Expect(&win, "S-1-5-21-1004336348-1177238915-682003330-1001 and S-1-5-18", "<sid> and S-1-5-18");
    Same(&win, "tpw_launcher.exe+0x1daed");

    // MAC and IP addresses; not the look-alikes.
    Expect(&mac, "mac 3c:22:fb:12:34:56 ip 192.168.1.20", "mac <mac> ip <ip>");
    Expect(&mac, "mac 3C-22-FB-12-34-56.", "mac <mac>.");
    Expect(&mac, "peer 10.1.2.3:8080", "peer <ip>:8080");
    Same(&mac, "version 10.0.19045.3693, v1.2.3.4, 1.2.3.4.5, 127.0.0.1, 999.1.1.1, 10:12:33");
    Same(&mac, "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    Same(&mac, "launcher-20261022-194629-2.log");
    Same(&mac, "3c:22:fb:12:34");

    // Without an identity only the rules that need none apply.
    Expect(nullptr, "/Users/jordan/x C:\\Users\\sam\\y /var/folders/aa/bb/T", "~/x %USERPROFILE%\\y /var/folders/<id>/T");
    Same(nullptr, "nothing to hide here");

    // A line that ends in the middle of a match, and the empty text.
    Same(&mac, "");
    Expect(&mac, "/Users/", "/Users/");
    Expect(&mac, "/Users/alex", "~");
    Expect(&mac, "user:", "user:");
    Expect(&mac, "user: ale", "user: ale");

    // Chunks: a text far longer than the output buffer, with matches everywhere, scrubs as it would in one piece, also when
    // the buffer is the smallest allowed.
    {
        std::string text, want;
        for (int k = 0; k < 400; ++k) {
            text += "line " + std::to_string(k) + " /Users/alex/Library/Application Support/x user: alex 192.168.0.1 filler filler\n";
            want += "line " + std::to_string(k) + " ~/Library/Application Support/x user: <user> <ip> filler filler\n";
        }
        Check(tpw::Scrub(&mac, text) == want, "chunks: a long text through Scrub");
        std::string got;
        char out[tpw::kScrubRoom + 2];
        for (size_t at = 0; at < text.size();) {
            size_t written = 0;
            const size_t next = tpw::ScrubChunk(&mac, text.data(), text.size(), at, out, sizeof out, &written);
            Check(next > at, "chunks: every step goes forward");
            if (next <= at) break;
            got.append(out, written);
            at = next;
        }
        Check(got == want, "chunks: the smallest buffer gives the same text");
    }

    // The log file: every line is scrubbed on its way in, the header too, and the old log files folded in at the first
    // launch; and Export logs scrubs once more.
    {
        const fs::path root = fs::temp_directory_path() /
#ifdef _WIN32
                              ("tpw-scrub-" + std::to_string(_getpid()));
#else
                              ("tpw-scrub-" + std::to_string(getpid()));
#endif
        fs::create_directories(root);
        tpw::ScrubInputs in;
        in.homes = {(root / "home" / "sam").string()};
        in.users = {"sam"};
        in.hosts = {"Sams-Test-Computer"};
        in.roots = {{root.string(), "<TESTROOT>"}};
        const tpw::ScrubIdentity test = tpw::MakeScrubIdentity(in);
        const tpw::ScrubIdentity* before = tpw::gScrubIdentity.exchange(&test);
        const std::string home = (root / "home" / "sam").string();

        // The old files (written before the scrub): one launcher-*.log run and Windows' game.log beside the logs folder.
        fs::create_directories(root / "logs");
        {
            std::ofstream old(root / "logs" / "launcher-20261025-101100.log", std::ios::binary);
            old << "STP/TPW Launcher v0.1.0\nold run: " << home << "/old\nuser: sam on Sams-Test-Computer\n";
        }
        {
            std::ofstream game(root / "game.log", std::ios::binary);
            game << "game folder: " << home << "/games\n";
        }

        std::string why;
        const char* argv[] = {"tpw_launcher", "--data", "HOMEARG"};
        std::string homeArg = home + "/data";
        argv[2] = homeArg.c_str();
        const bool opened = tpw::OpenLogFile(root / "logs", "STP/TPW Launcher v0.0.0 test", 3, argv, why);
        Check(opened, "log file: opened: " + why);
        tpw::LogFileF("play: from %s/tpw, host Sams-Test-Computer, user: sam", home.c_str());
        tpw::Log("data folder: %s", (root / "data").string().c_str());
        tpw::LogRawF("launcher: %s cannot be used\n", home.c_str());
        tpw::LogToFile("[mod file.cpp:1] ", ("see " + home + "/x").c_str());
        std::string longLine = "long ";
        for (int k = 0; k < 300; ++k) longLine += home + "/segment" + std::to_string(k) + " ";   // far past one buffer
        tpw::LogToFile("", longLine.c_str());
        const fs::path file = fs::path(tpw::LogFilePath());
        tpw::CloseLogFile();
        const std::string content = Slurp(file);
        Check(!content.empty(), "log file: written");
        Check(content.find(root.string()) == std::string::npos, "log file: no part of the test folder's path is left");
        Check(content.find("user: sam") == std::string::npos && content.find("Sams-Test-Computer") == std::string::npos,
              "log file: the account and computer names are gone");
        Check(content.find("args: tpw_launcher --data <TESTROOT>") != std::string::npos ||
                  content.find("args: tpw_launcher --data ~/data") != std::string::npos,
              "log file: the command line is scrubbed");
        Check(content.find("play: from ~/tpw, host <computer>, user: <user>") != std::string::npos, "log file: LogFileF's line");
        Check(content.find("platform: data folder: " + (fs::path("<TESTROOT>") / "data").string()) != std::string::npos,
              "log file: the platform log's line");   // the system's own separator, as (root / "data") spells it
        Check(content.find("launcher: ~ cannot be used") != std::string::npos, "log file: LogRawF's line");
        Check(content.find("[mod file.cpp:1] see ~/x") != std::string::npos, "log file: a prefix and a text");
        Check(content.find("~/segment299 ") != std::string::npos && content.find("long ~/segment0 ~/segment1 ") != std::string::npos,
              "log file: a line longer than one buffer");
        Check(content.find("os: ") != std::string::npos && content.find("hardware: ") != std::string::npos &&
                  content.find("STP/TPW Launcher v0.0.0 test") != std::string::npos,
              "log file: the useful header lines stay");
        Check(content.find("\nold run: ~/old\nuser: <user> on <computer>\n") != std::string::npos &&
                  content.find("\ngame folder: ~/games\n") != std::string::npos && !fs::exists(root / "game.log") &&
                  !fs::exists(root / "logs" / "launcher-20261025-101100.log"),
              "log file: the old files are folded in scrubbed, and removed");

        // Export logs: lines an older launcher wrote (before the scrub) come out clean; a clean file comes out as it was.
        const fs::path logs = root / "old logs", downloads = root / "Downloads";
        fs::create_directories(logs);
        fs::create_directories(downloads);
        {
            std::ofstream old(logs / "launcher.log", std::ios::binary);
            old << "data folder: " << home << "/x\nuser: sam on Sams-Test-Computer\nos: macOS 15.1\n";
            old << "==== run 2 | a game's run ====\ngame folder: " << home << "/games\n";
        }
        const tpw::LogExportResult first = tpw::ExportLogs(logs / "launcher.log", downloads, 1793000000, tpw::ExportProgram{});
        Check(first.ok, "export: saved: " + first.message);
        const std::string exported = Slurp(first.file);
        Check(exported.find("\ndata folder: ~/x\nuser: <user> on <computer>\nos: macOS 15.1\n") != std::string::npos &&
                  exported.find("\ngame folder: ~/games\n") != std::string::npos && exported.find("sam") == std::string::npos &&
                  exported.find("Sams") == std::string::npos && exported.find(home) == std::string::npos,
              "export: every run scrubbed again: [" + exported + "]");
        Check(Slurp(logs / "launcher.log").find("sam") != std::string::npos, "export: the log file is not changed");
        const tpw::LogExportResult second = tpw::ExportLogs(logs / "launcher.log", downloads, 1793000000, tpw::ExportProgram{}, &mac);
        Check(second.ok && Slurp(second.file).find("user: sam on Sams-Test-Computer") != std::string::npos,
              "export: an identity can be given (this one does not know the test's names)");

        tpw::gScrubIdentity.store(before);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    std::printf("%d of %d log scrub checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
