#!/bin/sh
# Tests the log scrubber of install.sh (what goes into last-build.log): made-up names only. Run: sh devtools/test_install_log_scrub.sh
# Needs a Mac, a Linux or another system with a POSIX awk. Exit 0 when every check passes.
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/tpw-scrub-test.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
fails=0
checks=0

# scrub HOME USER HOST TEXT: what the log gets for TEXT on a computer with that player and name.
scrub() {
    : >"$work/log"
    printf '%s\n' "$4" | env HOME="$1" USER="$2" TPW_INSTALL_HOSTNAME="$3" TPW_INSTALL_SCRUB="$work/log" sh "$here/install.sh" >"$work/screen"
    # The screen shows the text as it is.
    if [ "$(cat "$work/screen")" != "$4" ]; then
        echo "FAIL screen changed: $4"
        fails=$((fails + 1))
    fi
    cat "$work/log"
}

expect() {
    checks=$((checks + 1))
    if [ "$2" != "$3" ]; then
        echo "FAIL $1"
        echo "  want: $3"
        echo "  got:  $2"
        fails=$((fails + 1))
    fi
}

H=/Users/alex
got=$(scrub $H alex studio-mac-4f7q "cc -I$H/Applications/STP-TPW-Launcher/source/platform -c a.cpp")
expect "home folder" "$got" "cc -I~/Applications/STP-TPW-Launcher/source/platform -c a.cpp"

got=$(scrub $H alex studio-mac-4f7q "cd $H && make -C \"$H/Applications/x y/source\"")
expect "home at the end and quoted" "$got" 'cd ~ && make -C "~/Applications/x y/source"'

got=$(scrub $H alex studio-mac-4f7q "/Users/alexander/a and /Users/alex.old/b")
# shellcheck disable=SC2088  # the ~ is the text
expect "another account is not this home but is still a name" "$got" "~/a and ~/b"

got=$(scrub "/Users/Zoë Müller" "Zoë Müller" studio-mac-4f7q "cc /Users/Zoë Müller/Applications/x.c")
expect "UTF-8 name with a space" "$got" "cc ~/Applications/x.c"

got=$(scrub $H alex studio-mac-4f7q "cc /Users/Shared/a.c /Users/Public/b.c")
expect "shared folders stay" "$got" "cc /Users/Shared/a.c /Users/Public/b.c"

got=$(scrub $H alex studio-mac-4f7q "x -o /Volumes/Samsung T7/tpw/a.o -o /Volumes/6TB/b.o")
expect "volume names" "$got" "x -o /Volumes/<volume> T7/tpw/a.o -o /Volumes/<volume>/b.o"

got=$(scrub $H alex studio-mac-4f7q "TMPDIR=/var/folders/zz/abc123_x/T/ and /private/var/folders/q9/k2l/C/")
expect "temporary folders" "$got" "TMPDIR=/var/folders/<id>/T/ and /private/var/folders/<id>/C/"

got=$(scrub $H alex studio-mac-4f7q "host studio-mac-4f7q.local, STUDIO-MAC-4F7Q, studio-mac-4f7qx")
expect "computer name" "$got" "host <computer>.local, <computer>, studio-mac-4f7qx"

got=$(scrub $H alex Mac "a Mac with a Mac Pro")
expect "a short computer name is left alone" "$got" "a Mac with a Mac Pro"

got=$(scrub $H max studio-mac-4f7q "max 4 jobs, Max speed, user=max, owner: Max, maximum")
expect "a user name that is a common word changes only where it is the user" "$got" "max 4 jobs, Max speed, user=<user>, owner: <user>, maximum"

got=$(scrub $H alex studio-mac-4f7q "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 clang 17.0.0 arm64 /usr/bin/cc ./build/a.o")
expect "debugging info stays" "$got" "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 clang 17.0.0 arm64 /usr/bin/cc ./build/a.o"

# Scrubbing the scrubbed text again changes nothing.
once=$(scrub $H alex studio-mac-4f7q "$H/a /Volumes/X/b /var/folders/aa/bb/c studio-mac-4f7q user=alex /Users/sam/z")
twice=$(scrub $H alex studio-mac-4f7q "$once")
expect "scrubbing again" "$twice" "$once"

# Linux: the home is /home/NAME (or /root), removable drives are under /run/media/NAME or /media/NAME, /tmp names no one.
L=/home/alex
got=$(scrub $L alex thinkpad-x1 "c++ -I$L/.local/share/STP-TPW-Launcher/source/platform -c a.cpp")
expect "Linux home folder" "$got" "c++ -I~/.local/share/STP-TPW-Launcher/source/platform -c a.cpp"

got=$(scrub $L alex thinkpad-x1 "-- Build files have been written to: \"$L/.local/share/x y/build-next\"")
expect "Linux home quoted, with a space after it" "$got" '-- Build files have been written to: "~/.local/share/x y/build-next"'

got=$(scrub $L alex thinkpad-x1 "/home/sam/a, (/home/Sam/b) and -I/home/sam/c /srv/home/sam/d /home/shared/e")
# shellcheck disable=SC2088  # the ~ is the text
expect "another account's home where a path begins, as log_scrub.h has it" "$got" "~/a, (~/b) and -I/home/sam/c /srv/home/sam/d /home/shared/e"

got=$(scrub /root root build-box "cmake -S /root/.local/share/STP-TPW-Launcher/source -B /root/x /rootfs/y")
expect "root's home" "$got" "cmake -S ~/.local/share/STP-TPW-Launcher/source -B ~/x /rootfs/y"

got=$(scrub $L alex thinkpad-x1 "x -o /run/media/alex/USBSTICK/tpw/a.o -o /media/alex/Data/b.o /mnt/media/a/b")
expect "removable drives" "$got" "x -o /run/media/<user>/<volume>/tpw/a.o -o /media/<user>/<volume>/b.o /mnt/media/a/b"

got=$(scrub $L alex thinkpad-x1 "TMPDIR=/tmp/tpw-icon.ab12CD and /tmp/user/1000 /var/tmp/x")
expect "the temporary folder stays" "$got" "TMPDIR=/tmp/tpw-icon.ab12CD and /tmp/user/1000 /var/tmp/x"

got=$(scrub $L alex thinkpad-x1 "Linux thinkpad-x1 6.8.0 x86_64, THINKPAD-X1.lan, user: alex, /usr/lib/x86_64-linux-gnu")
expect "Linux computer and account names" "$got" "Linux <computer> 6.8.0 x86_64, <computer>.lan, user: <user>, /usr/lib/x86_64-linux-gnu"

once=$(scrub $L alex thinkpad-x1 "$L/a /home/sam/b /run/media/alex/D/c /media/alex/E/f /root/g thinkpad-x1")
twice=$(scrub $L alex thinkpad-x1 "$once")
expect "scrubbing Linux paths again" "$twice" "$once"

# A second line is appended, not written over the first.
: >"$work/log"
printf '%s\n' "one $H" | env HOME=$H USER=alex TPW_INSTALL_HOSTNAME=x TPW_INSTALL_SCRUB="$work/log" sh "$here/install.sh" >/dev/null
printf '%s\n' "two $H" | env HOME=$H USER=alex TPW_INSTALL_HOSTNAME=x TPW_INSTALL_SCRUB="$work/log" sh "$here/install.sh" >/dev/null
expect "appends" "$(cat "$work/log")" "one ~
two ~"

if [ "$fails" -ne 0 ]; then
    echo "$fails of $checks install log scrub checks failed"
    exit 1
fi
echo "$checks of $checks install log scrub checks passed"
