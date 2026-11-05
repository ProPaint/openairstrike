#!/usr/bin/env bash
# Inside debian:bookworm-slim (see tools/make_archive_fixtures.py): makes the 7z, CAB and RAR 4 /
# RAR 5 fixtures of testdata/archives/ from /fx/tree into /fx/out. 7zz and gcab from Debian, rar
# 7 from non-free for RAR 5 and RAR 6.24 from rarlab for RAR 4 (rar 7 no longer writes RAR 4).
set -eu
mkdir -p /fx/out
sed -i 's/^Components: main$/Components: main contrib non-free non-free-firmware/' /etc/apt/sources.list.d/debian.sources
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq 7zip gcab rar curl ca-certificates >/dev/null
# RAR 7 (Debian's rar) no longer writes RAR 4 archives: RAR 6.24 from rarlab does (-ma4).
mkdir -p /opt/rar6 && curl -fsSL https://www.rarlab.com/rar/rarlinux-x64-624.tar.gz | tar xz -C /opt/rar6
cd /fx/tree
7zz a -t7z -mx=9 -bd /fx/out/game.7z game >/dev/null
# gcab stores the paths as given; -z: MSZIP compression.
find game -type f -print0 | sort -z | xargs -0 gcab -c -z /fx/out/game.cab
/opt/rar6/rar/rar a -ma4 -m5 -idq /fx/out/game_rar4.rar game
rar a -ma5 -m5 -idq /fx/out/game_rar5.rar game
{ rar | head -2 | tail -1; /opt/rar6/rar/rar | head -2 | tail -1; } > /fx/out/versions.txt
7zz | head -2 | tail -1 >> /fx/out/versions.txt
gcab --version >> /fx/out/versions.txt
chown -R "$HOST_UID:$HOST_GID" /fx/out
ls -la /fx/out
