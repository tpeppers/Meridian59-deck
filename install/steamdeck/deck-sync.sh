#!/usr/bin/env bash
#
# Pull the client binaries built by .github/workflows/deck-build.yml into the
# installed game folder on the Steam Deck.
#
# The Deck cannot build the client -- it needs MSVC -- so CI builds it on a
# Windows runner and this fetches the result.  Run it from inside the repo
# clone, after pushing whatever you changed.
#
#   ./install/steamdeck/deck-sync.sh            # latest successful build of this branch
#   ./install/steamdeck/deck-sync.sh --wait     # wait for the run in flight first
#   MERIDIAN_DIR=/some/path ./deck-sync.sh      # somewhere other than ~/Games/Meridian59
#
# Requires the GitHub CLI.  SteamOS has an immutable root, so install it into
# your home directory rather than with pacman -- see DEVELOPMENT.md.

set -euo pipefail

GAME_DIR="${MERIDIAN_DIR:-$HOME/Games/Meridian59}"
ARTIFACT="meridian-client-win32"
WORKFLOW="deck-build.yml"
WAIT=0

for arg in "$@"; do
    case "$arg" in
        --wait) WAIT=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v gh >/dev/null 2>&1 || {
    echo "error: the GitHub CLI (gh) is not on your PATH." >&2
    echo "See install/steamdeck/DEVELOPMENT.md for installing it on SteamOS." >&2
    exit 1
}

git rev-parse --git-dir >/dev/null 2>&1 || {
    echo "error: run this from inside the repository clone." >&2
    exit 1
}

if [ ! -x "$GAME_DIR/meridian.exe" ] && [ ! -f "$GAME_DIR/meridian.exe" ]; then
    echo "error: no meridian.exe in $GAME_DIR." >&2
    echo "Install the game there first (install-steamdeck.sh), or set MERIDIAN_DIR." >&2
    exit 1
fi

BRANCH="$(git rev-parse --abbrev-ref HEAD)"

if [ "$WAIT" -eq 1 ]; then
    IN_FLIGHT="$(gh run list --workflow "$WORKFLOW" --branch "$BRANCH" \
        --status in_progress --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
    if [ -n "$IN_FLIGHT" ]; then
        echo "Waiting for run $IN_FLIGHT on $BRANCH..."
        gh run watch "$IN_FLIGHT" --exit-status
    fi
fi

RUN_ID="$(gh run list --workflow "$WORKFLOW" --branch "$BRANCH" \
    --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')"

if [ -z "$RUN_ID" ]; then
    echo "error: no successful $WORKFLOW run found for branch $BRANCH." >&2
    echo "Push your changes and wait for the build, or pass --wait." >&2
    exit 1
fi

# Report what we're about to install, so a stale artifact is obvious.
gh run view "$RUN_ID" --json headSha,displayTitle,createdAt \
    --jq '"Run \(.createdAt)  \(.headSha[0:8])  \(.displayTitle)"'

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

gh run download "$RUN_ID" --name "$ARTIFACT" --dir "$TMP"

# Only the binaries move.  resource/ holds the artwork as well as the module
# DLLs, so copy the DLLs in rather than replacing the directory, and never
# touch config.ini or meridian.ini -- those are the player's settings.
cp "$TMP/meridian.exe" "$GAME_DIR/"
cp "$TMP/OpenAL32.dll" "$GAME_DIR/"
mkdir -p "$GAME_DIR/resource"
cp "$TMP"/resource/*.dll "$GAME_DIR/resource/"

if [ ! -f "$GAME_DIR/config.ini" ]; then
    echo "No config.ini in $GAME_DIR; installing the Deck defaults."
    cp "$TMP/config.ini" "$GAME_DIR/"
fi

chmod u+rw "$GAME_DIR/meridian.exe" "$GAME_DIR/OpenAL32.dll" "$GAME_DIR"/resource/*.dll

echo
echo "Updated $GAME_DIR:"
echo "  meridian.exe   $(stat -c %s "$GAME_DIR/meridian.exe") bytes"
echo "  $(ls -1 "$GAME_DIR"/resource/*.dll | wc -l) module DLLs"
echo
echo "Relaunch from Steam to pick it up."
