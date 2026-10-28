# For AI coding agents working in tpw-port

RULES.md and AGENTS.md hold the same text. Read CONTRIBUTING.md first: what belongs here, how a change is made, the
changelog line, and the rules below in full.

## The game's files

- The game's files are the retail discs' (the US Mac CD, the European PC CD and the Gold bonus CD). Call them that, and
  nothing else: never name a download site, an archive item, a link, a file name from a particular download or a hash of
  one, in code, docs, tests, commit messages, pull request texts, issues or comments.
- Never add the game's files to the repository, and never fetch them yourself. Where the person you work for keeps theirs
  is a local setting: the environment variable `TPW_PLAYTEST_DATA`, or the first line of the git-ignored `.playtest-data`
  in the repository's root. Never commit that location, quote it in a pull request or write it into a file that is
  tracked. If it is not set, ask the person; do not look for the files or download them.
- CI never sees the game's files, and a check fails a pull request that adds a link to them
  (`devtools/check_no_game_data.py`).

## Before a pull request

A change to the game or the launcher (anything they run: overrides/, platform/, launcher/, the build, the installers) is
proposed only when, on the person's own computer:

1. the verifier shows 100% for US, European and Gold (US 770/770, European 862/862, Gold 36/36 required files), and
2. the playtest passes on all three editions: `sh devtools/playtest.sh` (macOS) or `devtools\playtest.ps1` (Windows). It
   runs the verifier first and refuses to play with less than 100%, then prints one result line per edition.

The pull request shows the verifier's line and the playtest's result lines, and never the data location. A pull request
that changes only documents, CI or other files the game and the launcher never run says "docs or CI only" instead. The
launcher is not exempt. A skipped playtest (no data location set) tested nothing and is not a pass.

The playtest opens the game's windows. Run it only when the person has said it may, and leave the windows alone while it
runs. It writes its logs into the data folder, so point it at a copy if the folder holds saves that matter.
