# Meridian 59 on the Steam Deck

The client runs on the Deck as a Windows build under Proton, with native
gamepad support and a full screen toggle built in. There is no separate Deck
binary: `meridian.exe` detects the controller at startup and the same
executable behaves identically on a desktop with a pad plugged in.

Why Proton rather than a native build: the client is Win32 throughout — its
dialogs, menus, and interface widgets are Windows resources drawn with GDI —
and it renders with Direct3D 9. Proton runs both well, DXVK translates D3D9 to
Vulkan, and gamescope composites the result to the Deck's 1280x800 panel. A
native port would mean replacing the entire UI layer and the renderer.

## Building the package

From a Visual Studio command prompt at the top of the tree:

    nmake RELEASE=1
    nmake steamdeck

The first command builds the client and modules into `run\localclient`. The
second stages a Deck-ready folder in `run\steamdeck`: the client, its
resources, the Deck-tuned `config.ini` from this directory, and the installer
script below.

You still need to supply the artwork, rooms and audio, which are not part of
this repository — drop them into `run\steamdeck\resource` before copying the
folder to the Deck, or into `~/Games/Meridian59/resource` afterwards.

## Installing on the Deck

Copy `run\steamdeck` to the Deck, then in Desktop Mode:

    ./install-steamdeck.sh

It copies the payload to `~/Games/Meridian59` and prints the remaining steps,
which have to be done by hand because they're Steam UI actions:

1. Steam → Games → **Add a Non-Steam Game to My Library** → **Browse** →
   `~/Games/Meridian59/meridian.exe`.
2. Right-click the new entry → **Properties** → **Compatibility** → tick
   **Force the use of a specific Steam Play compatibility tool** and pick
   **Proton 9.0** or newer (Experimental also works).
3. Launch it once from Steam in Desktop Mode. That creates the Wine prefix and
   confirms the renderer comes up; check `d3dlog.txt` in the game folder if it
   doesn't.
4. To point at a server other than the default, put the flags in **Launch
   Options**: `%command% /H:your.server.example /P:5959`.

Then switch to Game Mode and launch it there.

## Controls

Steam Input presents the Deck's built-in controls to the game as an Xbox 360
pad, so the stock **Gamepad** controller template is the right one — you do not
need a custom layout. The client reads the pad directly through XInput.

| Steam Deck | Xbox | Action |
| --- | --- | --- |
| Left stick | Left stick | Move and strafe. Push past about three quarters of the travel to run |
| Right stick | Right stick | Turn, and tilt the view up and down |
| L3 (click left stick) | LS | Turn around 180° |
| R3 (click right stick) | RS | Level the view |
| A | A | Open / activate |
| B | B | Clear target |
| X | X | Pick up |
| Y | Y | Look at target |
| R2 | RT | Attack |
| L2 | LT | Target self |
| R1 | RB | Next target |
| L1 | LB | Previous target |
| D-pad up / down | D-pad up / down | Look up / down |
| D-pad left / right | D-pad left / right | Zoom map out / in |
| ⧉ (above the left stick) | View | Toggle the map |
| ☰ (above the right stick) | Menu | **Toggle full screen** |

Every button in that table can be reassigned in the `[gamepad]` section of
`config.ini`; the file lists the button names and the actions they accept. The
sticks are fixed — the left one moves, the right one looks — with sensitivity,
dead zones and Y inversion set in `[config]`.

### About the "..." button

The **"..." button is the Quick Access Menu button, and SteamOS keeps it for
itself.** Pressing it opens the Quick Access panel; the press is consumed by
the compositor and never reaches the running game, so no game can bind it.
The same is true of the STEAM button.

Full screen is therefore on the **☰ Menu button**, immediately to its left
above the right stick — the closest button that games are actually allowed to
see. If you specifically want full screen on a back grip button instead, the
client also toggles on **F11**, so a Steam Input layout can send that:

1. Game Mode → the controller icon → **Edit Layout**.
2. **Back Grip Buttons** → pick L4, L5, R4 or R5.
3. Assign the keyboard key **F11**.

**Alt+Enter** works too, and there is a **Options → Full screen** menu item for
mouse and keyboard.

Full screen is borderless rather than an exclusive mode switch, which is what
you want here: the Steam overlay keeps working, the Quick Access Menu still
comes up over the game, and gamescope scales the window without a mode change.
The setting is remembered, so the client comes back full screen next launch.

## Notes

- `gpuefficiency=true` in the shipped config caps the frame rate at 60 and
  presents on vblank. The Deck's panel is 60Hz, so frames above that are drawn
  and thrown away at the cost of battery. Turn it off in
  **Options → Preferences** if you have the Deck docked to a faster display.
- Typing needs the on-screen keyboard: **STEAM + X**. The chat box takes focus
  with the keyboard's Enter, or bind `chat` to a pad button in `[gamepad]`.
- The client writes `config.ini`, `meridian.ini` and its logs into its own
  folder, so keep it somewhere writable — `~/Games`, not a read-only mount.
- A microSD card works, but the Deck mounts it without Windows file
  permissions; if the client can't save settings, move the folder to internal
  storage.
