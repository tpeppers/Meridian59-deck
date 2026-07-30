# Meridian 59 server (blakserv) — portable Linux container.
#
# Builds blakcomp, compiles the Blakod, and builds blakserv from source. No
# Windows toolchain and no prebuilt artifacts, so this runs anywhere docker
# does, Steam Deck included (SteamOS is x86_64 Arch — plain linux/amd64).
#
#   docker compose -f docker/docker-compose.yml up --build
#
# Ports: 5959 game, 9998 maintenance (admin socket — bound to loopback only
# by the compose file, because it is authenticated by IP mask alone).

# ---------------------------------------------------------------- build

FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential flex bison make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# The tree is built with -Werror, which is a reasonable policy for the project
# and a bad one for a portability build on whatever g++ the base image ships.
# Warnings here are not our finding; a failed build would just be noise.
RUN sed -i 's/ -Werror//' common.mak.linux

# blakcomp produces bc, the Blakod compiler.
RUN make -C blakcomp -f makefile.linux RELEASE=1 -j"$(nproc)"

# Compile the game code to .bof bytecode. The kod makefile copies output into
# run/server/loadkod itself.
RUN make -C kod -f makefile.linux RELEASE=1

# The server itself.
RUN make -C blakserv -f makefile.linux RELEASE=1 -j"$(nproc)"

RUN find . -name blakserv -type f -perm -u+x -newer common.mak.linux \
      -exec cp {} /tmp/blakserv \; -quit && test -x /tmp/blakserv

# -------------------------------------------------------------- runtime

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -u 1000 blak

WORKDIR /m59
COPY --from=build /tmp/blakserv            /m59/blakserv
COPY --from=build /src/run/server/         /m59/
COPY --from=build /src/resource/rooms/     /m59/rooms/
COPY --from=build /src/kod/kodbase.txt     /m59/kod/kodbase.txt

# The config ships with Windows path separators, and the README notes it needs
# manual changes on Linux. Rewrite the [Path] block to container-absolute
# paths, and open the maintenance mask to the docker bridge and LAN.
#
# Mask semantics (blakserv/async.c): each non-zero byte must match, zero bytes
# are wildcards. 172.0.0.0 therefore means "any 172.x.x.x", which is the
# docker gateway. Publishing 9998 on loopback only is what actually protects it.
RUN set -eux; \
    sed -i \
      -e 's#^Bof .*#Bof                  /m59/loadkod/#' \
      -e 's#^Memmap .*#Memmap               /m59/memmap/#' \
      -e 's#^Rsc .*#Rsc                  /m59/rsc/#' \
      -e 's#^Rooms .*#Rooms                /m59/rooms/#' \
      -e 's#^Motd .*#Motd                 /m59/#' \
      -e 's#^Channel .*#Channel              /m59/channel/#' \
      -e 's#^LoadSave .*#LoadSave             /m59/savegame/#' \
      -e 's#^Forms .*#Forms                /m59/#' \
      -e 's#^Kodbase .*#Kodbase              /m59/kod#' \
      -e 's#^PackageFile .*#PackageFile          /m59/#' \
      -e 's#^MaintenanceMask .*#MaintenanceMask      127.0.0.1;172.0.0.0;192.168.0.0;10.0.0.0#' \
      blakserv.cfg; \
    grep -q '^MaintenanceMask' blakserv.cfg || printf '\nMaintenanceMask      127.0.0.1;172.0.0.0;192.168.0.0;10.0.0.0\n' >> blakserv.cfg; \
    grep -q '^MaintenancePort' blakserv.cfg || printf 'MaintenancePort      9998\n' >> blakserv.cfg; \
    mkdir -p /m59/channel /m59/savegame /m59/memmap; \
    chown -R blak:blak /m59

USER blak
EXPOSE 5959 9998

# channel/ is where player chat lands once tracing is on; savegame/ is state.
VOLUME ["/m59/channel", "/m59/savegame"]

CMD ["/m59/blakserv"]
