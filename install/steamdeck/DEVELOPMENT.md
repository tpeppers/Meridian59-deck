# Iterating on the Deck

Working on this fork from the Steam Deck itself, in Desktop Mode, with Claude
Code.

## What can be built where

The one hard constraint: **the client cannot be compiled on the Deck.** It is
MSVC-only — `nmake`, `cl -TP` (C compiled as C++), `rc.exe` for the
three-language resource script, D3D9 headers, `#pragma warning`, `-WX`. None of
that exists on Linux, and cross-compiling with mingw-w64 would be a porting
project rather than a configuration change.

Everything else is native.

| What you change | On the Deck |
| --- | --- |
| `kod/` — game logic, items, NPCs, spells | Builds natively with `blakcomp` |
| `blakserv/` — the server | Builds natively (`makefile.linux`) |
| `resource/rooms/` — room geometry | Data; no build needed |
| `clientd3d/`, `module/` — client, renderer, gamepad, UI | CI builds it; you download the result |

So game-side iteration is a local loop measured in seconds. Client changes go
out through GitHub Actions and come back as an artifact, a few minutes later.

## One-time setup

SteamOS has an immutable root filesystem. `sudo pacman -S` works only after
`sudo steamos-readonly disable`, and an OS update wipes it. So everything below
installs into `$HOME`, which survives updates.

Switch to Desktop Mode and open Konsole. If you have never set a password,
`passwd` first — sudo needs one.

### Node and Claude Code

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh | bash
exec $SHELL -l
nvm install --lts

npm install -g @anthropic-ai/claude-code
claude
```

`~/.nvm` and the global npm prefix live in your home directory, so this
survives SteamOS updates.

### GitHub CLI

Needed by `deck-sync.sh` to fetch client builds. Install the static binary
rather than using pacman:

```bash
mkdir -p ~/.local/bin ~/.local/share
GH_VER=$(curl -s https://api.github.com/repos/cli/cli/releases/latest | grep -Po '"tag_name": "v\K[^"]*')
curl -sL "https://github.com/cli/cli/releases/download/v${GH_VER}/gh_${GH_VER}_linux_amd64.tar.gz" | tar -xz -C ~/.local/share
ln -sf ~/.local/share/gh_${GH_VER}_linux_amd64/bin/gh ~/.local/bin/gh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
exec $SHELL -l

gh auth login
```

### The repo and the game data

```bash
git clone https://github.com/<you>/<your-fork>.git ~/Meridian59
```

The clone is about 88 MB. **It is not playable on its own** — the artwork,
rooms and audio are roughly 300 MB and are deliberately not in git (`run/*` is
gitignored). Copy them across once, from the packaged folder that
`nmake steamdeck` produces on a Windows machine:

```bash
# on the Windows box, having run: nmake RELEASE=1 && nmake steamdeck
# then from the Deck:
scp -r deck@windows-box:/c/code/Meridian59/run/steamdeck ~/Games/Meridian59
cd ~/Games/Meridian59 && ./install-steamdeck.sh
```

Anything works for the transfer — scp, a USB stick, Syncthing. It only has to
happen once; after that only binaries move.

Note that `~/Games` and `~/Meridian59` are two different things on purpose: the
first is the installed game Steam launches, the second is the source you edit.

### A C++ toolchain, if you want to build the server locally

SteamOS does not ship gcc, make, flex or bison persistently. Two ways round it,
neither touching the immutable root:

- **The container already in this tree.** `docker/Dockerfile` builds
  `blakcomp`, the kod and `blakserv` from source on `debian:bookworm-slim`, and
  was written with the Deck in mind. SteamOS ships podman; you will need
  `podman-compose`, or run the Dockerfile directly with `podman build`.
- **distrobox.** A full Arch or Debian userspace in a container, with your home
  directory mounted, so `make` in `~/Meridian59` just works. Installs into
  `~/.local`.

If you would rather not deal with a toolchain at all, skip this entirely:
`deck-build.yml` has a `server` job that builds `blakserv` and compiles the
kod on Linux and uploads both as the `meridian-server-linux` artifact. It is
slower than building locally, but it needs nothing installed on the Deck.

```bash
gh run download --repo <you>/<fork> --name meridian-server-linux --dir server
chmod +x server/blakserv   # artifact downloads do not keep the exec bit
```

`server/loadkod` holds the compiled `.bof` bytecode the server loads.

## The loops

### Game code (kod) — local, seconds

```bash
make -C blakcomp -f makefile.linux -j"$(nproc)"
make -C kod -f makefile.linux          # copies .bof files into run/server/loadkod
```

Then `reload system` over the maintenance socket, or restart `blakserv`. No
client rebuild is involved, so most content and behaviour work never leaves the
Deck.

If the build trips over `-Werror` on a newer g++, do what `docker/Dockerfile`
already does and strip it from `common.mak.linux`.

### Server — local, seconds

```bash
make -C blakserv -f makefile.linux -j"$(nproc)"
```

`blakserv.cfg` needs its paths adjusted for Linux; the compose file in
`docker/` already handles that.

### Client — through CI, minutes

```bash
git commit -am "..." && git push
./install/steamdeck/deck-sync.sh --wait
```

`deck-sync.sh` finds the most recent successful `deck-build.yml` run for the
branch you are on, downloads `meridian.exe`, `OpenAL32.dll` and the module
DLLs, and drops them into `~/Games/Meridian59`. It prints the commit the
artifact was built from, so a stale download is obvious. It will not overwrite
`config.ini` or `meridian.ini` — those hold your key and pad bindings.

The artifact carries six module DLLs; the installed game has seven.
`intro.dll` has no source in this repository, so nothing builds it: it ships
with the game data, like the artwork. `deck-sync.sh` copies the built DLLs in
alongside whatever is already there rather than replacing the directory, so
it survives.

Then relaunch from Steam. `--wait` blocks until an in-flight run finishes;
without it, the script uses the newest build that has already succeeded.

## CI minutes

Actions is free on public repositories and metered on private ones: 2,000
minutes a month on a free plan, with Windows billed at 2x and macOS at 10x.

Upstream's `c-cpp.yml` builds Linux, macOS, and both a 32- and 64-bit Windows
client. Measured on this fork that is about 50 minutes of allowance per push,
against 13 for `deck-build.yml` and Spellcheck together — roughly 32 pushes a
month versus 150. It is also almost entirely redundant here: the 32-bit client
duplicates the Deck build, the 64-bit one never runs on a Deck, and macOS is
not a target.

So `c-cpp.yml` is disabled on this fork, and `deck-build.yml` carries a Linux
job to keep the server and kod build check. To undo:

```bash
gh workflow enable "Build code"
```

Watch the meter with `gh api /users/<you>/settings/billing/actions`.

## Notes

- You cannot run Claude Code in Game Mode. The practical rhythm is to play in
  Game Mode, then switch to Desktop Mode to work. Switching does not require a
  reboot, but it does close the running game.
- `origin` is your private fork and `upstream` is `Meridian59/Meridian59`, so
  plain `git push` and `git pull` stay on your fork and `git pull upstream
  master` brings in the real project.
- With two remotes, bare `gh run list` is ambiguous and will happily show
  upstream's builds instead of yours. `deck-sync.sh` resolves the repo from
  `origin` itself; for interactive `gh` use, run `gh repo set-default` once.
- `deck-build.yml` is a separate file from the upstream `c-cpp.yml` on purpose:
  merging upstream will never conflict with it.
- Verified: both CI jobs pass on the fork. The client artifact carries
  `meridian.exe`, `OpenAL32.dll` and six module DLLs; the server artifact
  carries an x86-64 Linux `blakserv` and 1,231 compiled `.bof` files. The
  Deck-side steps above have *not* been run on hardware — treat the toolchain
  section in particular as a starting point rather than a transcript.
