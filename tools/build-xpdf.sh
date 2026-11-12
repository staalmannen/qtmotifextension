#!/bin/sh
# Build xpdf 3.04 (the last Motif version, as Debian maintains it) against
# Motif on Qt with xpdf's own unmodified build system.
#
# usage: tools/build-xpdf.sh <xpdf-source-dir> <motif-on-qt-build-dir>
#   get the source with:  apt-get source xpdf   (needs deb-src entries)
#   needs:                autoconf, automake, libpoppler-private-dev, pkg-config
set -e
src=$1; build=$2
[ -f "$src/xpdf/XPDFViewer.cc" ] || { echo "not an xpdf source tree: $src" >&2; exit 1; }
cd "$src"
[ -x configure ] || autoreconf -fi
"$build/motif-on-qt-env" ./configure
make
echo "built $src/xpdf/xpdf"
