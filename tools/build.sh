#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 KinkyTechieKitty
#
# Build the .fap against a Blue++ checkout that has had one full ./build.sh.
#
#   tools/build.sh                 -> coyote.fap          (21 waveforms, publishable)
#   tools/build.sh --with-dglab    -> coyote-with-dg.fap  (45, local only)
#
# BLUEPP points at the Blue++ checkout (default ~/flipper-bluepp); SEQUENCES at
# Feeder's coyote-sequences.js, if it is not where gen_waveforms.py expects it.
#
# Either way the waveform table is left regenerated WITHOUT the DG-Lab presets
# when the script finishes, so the working tree matches what is committed and a
# local DG build can never be committed or released by accident.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
BLUEPP=${BLUEPP:-$HOME/flipper-bluepp}
FW="$BLUEPP/flipperzero-firmware"

WITH_DGLAB=0
[ "${1:-}" = "--with-dglab" ] && WITH_DGLAB=1

if [ ! -d "$FW/applications_user" ]; then
    echo "No Blue++ build at $FW." >&2
    echo "Clone github.com/andydixon/flipper-bluepp, run ./build.sh once, then" >&2
    echo "set BLUEPP=/path/to/flipper-bluepp." >&2
    exit 1
fi

if [ "$WITH_DGLAB" = 1 ]; then
    APPID=coyote_dg
    APPNAME="Coyote +DG"
    OUT="$HERE/coyote-with-dg.fap"
    python3 "$HERE/tools/gen_waveforms.py" ${SEQUENCES:+"$SEQUENCES"} --with-dglab
else
    APPID=coyote
    APPNAME="Flipper Coyote"
    OUT="$HERE/coyote.fap"
    python3 "$HERE/tools/gen_waveforms.py" ${SEQUENCES:+"$SEQUENCES"}
fi

# Stage the app plus Blue++'s BLE layer, which this repo does not carry.
STAGE="$FW/applications_user/$APPID"
rm -rf "$STAGE"
cp -r "$HERE/app" "$STAGE"
cp "$BLUEPP"/libble/ble_central.c "$BLUEPP"/libble/ble_central.h "$STAGE/"

# A DG build is a separate app on the Flipper, so both can be installed at once.
sed -i.bak -e "s/^    appid=\"coyote\",/    appid=\"$APPID\",/" \
           -e "s/^    name=\"Flipper Coyote\",/    name=\"$APPNAME\",/" \
    "$STAGE/application.fam"
rm -f "$STAGE/application.fam.bak"

cd "$FW"
./fbt -j"${JOBS:-4}" COMPACT=1 DEBUG=0 \
    COPRO_STACK_TYPE=ble_full \
    COPRO_STACK_BIN=stm32wb5x_BLE_Stack_full_fw.bin "fap_$APPID"
cp "build/f7-firmware-C/.extapps/$APPID.fap" "$OUT"

# Leave the tree on the committed 21-waveform table whatever was just built.
python3 "$HERE/tools/gen_waveforms.py" ${SEQUENCES:+"$SEQUENCES"} >/dev/null

echo
echo "built: $OUT"
if [ "$WITH_DGLAB" = 1 ]; then
    echo "This build carries the DG-Lab presets: local use only, never released."
    echo "It installs as a separate app and keeps its own settings-dg.bin."
fi
echo "Working tree is back on the committed 21-waveform table."
