#!/usr/bin/env bash
#
# Install the Meridian 59 client on a Steam Deck.
#
# Run this from the folder it shipped in, in Desktop Mode.  It copies the
# client somewhere writable and then prints the Steam steps, which have to be
# done through the Steam UI.

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${1:-$HOME/Games/Meridian59}"

if [ ! -f "$SOURCE_DIR/meridian.exe" ]; then
    echo "error: meridian.exe is not next to this script." >&2
    echo "Run this from the packaged client folder (run/steamdeck)." >&2
    exit 1
fi

# The client writes its INI files and logs next to the executable, so it has
# to live on a filesystem that carries Unix permissions.  A microSD card
# formatted by the Deck qualifies; an exFAT card does not.
mkdir -p "$INSTALL_DIR"

echo "Installing to $INSTALL_DIR"

# An existing config.ini holds the player's key and pad bindings, so hold on
# to it across the copy.  Copying with --no-clobber instead would also skip
# game files that this package updates.
SAVED_CONFIG=""
if [ -f "$INSTALL_DIR/config.ini" ]; then
    echo "Keeping the config.ini already in $INSTALL_DIR"
    SAVED_CONFIG="$(mktemp)"
    cp "$INSTALL_DIR/config.ini" "$SAVED_CONFIG"
fi

cp -R "$SOURCE_DIR/." "$INSTALL_DIR/"

if [ -n "$SAVED_CONFIG" ]; then
    cp "$SAVED_CONFIG" "$INSTALL_DIR/config.ini"
    rm -f "$SAVED_CONFIG"
fi

rm -f "$INSTALL_DIR/install-steamdeck.sh"
chmod -R u+rw "$INSTALL_DIR"

if [ ! -d "$INSTALL_DIR/resource" ] || [ -z "$(ls -A "$INSTALL_DIR/resource" 2>/dev/null)" ]; then
    echo
    echo "warning: $INSTALL_DIR/resource is empty."
    echo "The artwork, rooms and audio are not part of the source distribution;"
    echo "copy them in before playing."
fi

cat <<EOF

Installed.  The rest has to be done in the Steam UI:

  1. Steam -> Games -> Add a Non-Steam Game to My Library -> Browse
       $INSTALL_DIR/meridian.exe
  2. Right-click it -> Properties -> Compatibility
       Tick "Force the use of a specific Steam Play compatibility tool"
       Choose Proton 9.0 or newer.
  3. Launch it once here in Desktop Mode to build the Wine prefix.
  4. For a server other than the default, set Launch Options to
       %command% /H:your.server.example /P:5959

Then switch to Game Mode.  Leave the controller layout on the stock Gamepad
template; the client reads the pad itself.

The Menu button (the one above the right stick, not the "..." button, which
SteamOS keeps for the Quick Access Menu) toggles full screen.  See README.md
for the rest of the controls.
EOF
