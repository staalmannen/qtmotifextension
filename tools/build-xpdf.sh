#!/bin/sh
# Build xpdf 3.04 (the last Motif version, as maintained by Debian) against
# Motif on Qt, unmodified. The outline pane is a custom Xt widget class
# (XPDFTree), which Motif on Qt does not support, so it is configured out
# with xpdf's own DISABLE_OUTLINE switch.
#
# usage: tools/build-xpdf.sh <xpdf-source-dir> <motif-on-qt-build-dir> [output]
#   get the source with:  apt-get source xpdf   (needs deb-src entries)
#   needs:                libpoppler-private-dev, pkg-config
set -e
src=$1; build=$2; out=${3:-xpdf}
here=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$src/xpdf/XPDFViewer.cc" ] || { echo "not an xpdf source tree: $src" >&2; exit 1; }

# what xpdf's configure.ac defines for the installed poppler
v=$(pkg-config --modversion poppler)
defs="-DSYSTEM_XPDFRC='\"/etc/xpdf/xpdfrc\"' -DDISABLE_OUTLINE"
atleast () { [ "$(printf '%s\n%s\n' "$1" "$v" | sort -V | head -1)" = "$1" ]; }
atleast 0.23.0 && defs="$defs -DSTARTPAGE_XREF"
atleast 0.25.0 && defs="$defs -DPSOUTPUTDEV_NOCROP"
atleast 0.30.0 && defs="$defs -DPSOUTPUTDEV_PAGE_LIST"
atleast 0.58.0 && defs="$defs -DOBJECT_NO_FREE"
atleast 0.64.0 && defs="$defs -DPOPPLER_64_CONST_API"
atleast 0.65.0 && defs="$defs -DADDFONTFILE_NO_OWN"
atleast 0.72.0 && defs="$defs -DGOOSTRING_NO_GETCSTRING"
atleast 0.73.0 && defs="$defs -DTEXTPAGE_FINDTEXT_IGNOREDIACRITICS"
atleast 0.76.0 && defs="$defs -DNO_GOOLIST -DFINDPAGE_REF"
atleast 0.83.0 && defs="$defs -DGLOBALPARAMS_UNIQUE_PTR"
atleast 0.84.0 && defs="$defs -DTEXTPAGE_GETTEXT_EOL"
atleast 0.90.0 && defs="$defs -DPSOUTPUTDEV_FORCERASTERIZE"
atleast 20.12.0 && defs="$defs -DGLOBALPARAMS_NO_PSLEVEL"
atleast 21.05.0 && defs="$defs -DTEXTPAGE_FINDTEXT_MATCHACROSSLINES"
atleast 21.11.0 && defs="$defs -DPDFDOC_GETLINKS_UNIQUE_PTR -DGLOBALPARAMS_NO_EXPAND_SHRINK_OVERPRINT"
atleast 22.03.0 && defs="$defs -DNEED_OPTIONAL -DPDFDOC_FILENAME_UNIQUE_PTR -DPDFDOC_PASSWORDS_OPTIONAL -DPDFDOC_SAVEAS_GOOSTRING"
atleast 22.05.0 && defs="$defs -DLINKS_GETLINKS"
atleast 23.06.0 && defs="$defs -DADDFONTFILE_STRING"
atleast 24.02.0 && defs="$defs -DOUTLINEITEM_TITLE_VECTOR"

objs=""
tmp=$(mktemp -d)
for f in goo/gfile-xpdf.cc goo/parseargs.cc xpdf/CoreOutputDev.cc xpdf/PDFCore.cc xpdf/XPDFApp.cc \
         xpdf/XPDFCore.cc xpdf/XPDFParams.cc xpdf/XPDFViewer.cc xpdf/xpdf.cc; do
  o=$tmp/$(basename "$f" .cc).o
  eval g++ -std=c++17 -c -w -I"$here/include" -I"$src/goo" $defs $(pkg-config --cflags poppler) "$src/$f" -o "$o"
  objs="$objs $o"
done
g++ -o "$out" $objs -L"$build/lib" -Wl,-rpath,"$build/lib" -lXm -lXt -lX11 $(pkg-config --libs poppler) -lpthread
rm -rf "$tmp"
echo "built $out"
