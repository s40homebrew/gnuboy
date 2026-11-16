# SPDX-License-Identifier: GPL-2.0-or-later
ARTIFACT=Gnuboy
APP_FLAGS=(-O3 -DIS_LITTLE_ENDIAN -DGNUBOY_S40 "-I$EXAMPLE_DIR/.." "-I$EXAMPLE_DIR/native" "-ffile-prefix-map=$EXAMPLE_DIR/..=.")
for unit in cpu mem lcd lcdc refresh palette hw sound rtc debug; do
    APP_SOURCES+=("$EXAMPLE_DIR/../$unit.c")
done
for unit in frontend cartridge emulation audio_output state ui; do
    APP_SOURCES+=("$EXAMPLE_DIR/native/$unit.c")
done
