#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
# https://github.com/mathoudebine/turing-smart-screen-python/
#
# make-dist.sh: Windows distribution of the programs, packaged by turing-system-monitor.iss (called by "make dist",
# from the MSYS2 UCRT64 shell).
#
# usage: make-dist.sh <distribution directory> <directory of the built programs> <version>
#
# Same layout as MSYS2, so that GTK finds its files next to its DLLs:
#   bin/        programs and the DLLs they need (found with ldd)
#   lib/, share/, etc/   GTK image loaders and settings schemas, CA certificates for libcurl
#   res/, external/, config.json, README...   resources of the system monitor (main directory of the programs)

set -eu

DIST=$1
BIN=$2
VERSION=$3
PREFIX=${MINGW_PREFIX:-/ucrt64}

rm -rf "$DIST"
mkdir -p "$DIST/bin"

# Programs
for program in main configure theme-editor lhm-bridge; do
    cp "$BIN/$program.exe" "$DIST/bin/"
done

# Helper programs of GLib, needed by Glib::spawn_async() on Windows: configure starts main and theme-editor with it
# ("Failed to execute helper program" without them). Copied before ldd, so that their DLLs are copied too.
for helper in gspawn-win64-helper gspawn-win64-helper-console; do
    cp "$PREFIX/bin/$helper.exe" "$DIST/bin/"
done

# Files of GTK and libcurl, from the MSYS2 environment
copy_from_prefix() {
    if [ -e "$PREFIX/$1" ]; then
        mkdir -p "$DIST/$(dirname "$1")"
        cp -r "$PREFIX/$1" "$DIST/$1"
    else
        echo "warning: $PREFIX/$1 not found" >&2
    fi
}
copy_from_prefix lib/gdk-pixbuf-2.0
copy_from_prefix share/glib-2.0/schemas/gschemas.compiled
copy_from_prefix share/icons/hicolor
copy_from_prefix share/icons/Adwaita
copy_from_prefix etc/ssl/certs/ca-bundle.crt
# libcurl also looks for this file next to the program
cp "$DIST/etc/ssl/certs/ca-bundle.crt" "$DIST/bin/curl-ca-bundle.crt" 2>/dev/null || true

# DLLs of the MSYS2 environment needed by the programs and the image loaders (ldd lists them recursively)
find "$DIST" -name '*.exe' -o -name '*.dll' | while read -r file; do
    ldd "$file" || true
done | awk -v prefix="$PREFIX/bin/" 'index(tolower($3), tolower(prefix)) == 1 { print $3 }' | sort -u |
    while read -r dll; do
        cp "$dll" "$DIST/bin/"
    done

# Resources of the system monitor: the directory containing res/themes and config.json is its main directory
cp -r res "$DIST/"
rm -rf "$DIST/res/themes/--Theme examples"  # Theme examples for theme authors (800 MB): not distributed
mkdir -p "$DIST/external"
cp -r external/LibreHardwareMonitor external/PawnIO "$DIST/external/"
cp config.json.example "$DIST/"
cp config.json.example "$DIST/config.json"
cp LICENSE README.md README.config "$DIST/"
echo "$VERSION" > "$DIST/version.txt"

echo "Distribution of version $VERSION in $DIST: $(find "$DIST/bin" -name '*.dll' | wc -l) DLLs"
