# Packaging

This describes the installation standard for Motif on Qt and for
applications ported with it. gtk-made-qt follows the same scheme, so one
convention covers both (for example in the qtify package collection).

## The library package: `motif-on-qt`

A build-time package. Installed layout (`libdir` is `/usr/lib` on Arch,
`/usr/lib/<triplet>` on Debian-style multiarch):

```
/usr/bin/motif-on-qt-env                 sets CPPFLAGS, LDFLAGS, PKG_CONFIG_PATH
/usr/include/motif-on-qt/{X11,Xm}/       the X11, Xt and Motif headers
<libdir>/motif-on-qt/libmotif-on-qt.a    the library (static)
<libdir>/motif-on-qt/libXm.so, libXt.so, libX11.so, libXext.so, libXmu.so,
                     libXpm.so, libICE.so, libSM.so, libMrm.so
                                         GNU ld scripts: the archive + Qt
<libdir>/motif-on-qt/pkgconfig/          x11.pc, xt.pc, motif.pc ... for builds
<libdir>/pkgconfig/motif-on-qt.pc        found without extra settings
/usr/share/doc/motif-on-qt/
```

Everything sits in its own directories, so the package can be installed
next to the real libX11/libXt/libXm without conflicts, and a build only sees
it when asked to (`motif-on-qt-env`).

Rules for the library package:

- Depends on `qt6-base` (the link scripts name `-lQt6Widgets -lQt6Gui
  -lQt6Core`).
- Keep the static library: on Arch `options=(staticlibs !lto)` - makepkg
  removes `.a` files by default, and LTO would leave objects only an LTO
  build can link.
- Do not strip the `.so` link scripts (they are text; makepkg skips them).

## Ported application packages

- Name: `<app>-qtify` (for example `xpdf-qtify`), with `provides=(<app>)`
  and `conflicts=(<app>)` when it replaces the original.
- `makedepends`: `motif-on-qt` (or `gtk-made-qt`) plus the program's own
  build tools; `depends`: `qt6-base` and the program's other libraries -
  never libXm/libXt/libX11 or GTK for the parts the port replaces.
- Build with the program's own build system wrapped in `motif-on-qt-env`;
  do not patch the sources unless the program needs it for unrelated reasons.
- Install where the original package would (`/usr/bin`, app-defaults in
  `/usr/share/X11/app-defaults`, ...).
- Run something headless in `check()` when possible:
  `QT_QPA_PLATFORM=offscreen MOTIFQT_AUTOQUIT=1000 ./prog`.

## Example: PKGBUILD for the library

```bash
pkgname=motif-on-qt-git
pkgver=r1
pkgrel=1
pkgdesc="The Motif, Xt and Xlib APIs on Qt 6: build Motif programs without X11 or libXm"
arch=(x86_64 aarch64)
url="https://github.com/staalmannen/motif-on-qt"
license=(LGPL-2.1-or-later)
depends=(qt6-base)
makedepends=(cmake git)
provides=(motif-on-qt)
conflicts=(motif-on-qt)
options=(staticlibs !lto)
source=("git+https://github.com/staalmannen/motif-on-qt.git")
sha256sums=(SKIP)

pkgver() {
  cd motif-on-qt
  printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cmake -B build -S motif-on-qt -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release \
    -DMQ_BUILD_EXAMPLES=OFF
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
```

## Example: PKGBUILD for xpdf 3 (Motif) on Qt

```bash
pkgname=xpdf-qtify
_debver=3.04+git20240202
pkgver=3.04.git20240202
pkgrel=1
pkgdesc="xpdf 3 (the Motif PDF viewer, poppler based) built on Qt with Motif on Qt"
arch=(x86_64 aarch64)
url="https://tracker.debian.org/pkg/xpdf"
license=(GPL-2.0-only OR GPL-3.0-only)
depends=(qt6-base poppler)
makedepends=(motif-on-qt autoconf automake pkgconf)
provides=(xpdf)
conflicts=(xpdf)
source=("https://deb.debian.org/debian/pool/main/x/xpdf/xpdf_${_debver}.orig.tar.xz")
sha256sums=('3a020caedde7e75997373e6eb2076c81e25586d8422cd161d6d17203c51f76ae')

build() {
  cd "xpdf-${_debver}"
  autoreconf -fi
  motif-on-qt-env ./configure --prefix=/usr --sysconfdir=/etc
  make
}

check() {
  cd "xpdf-${_debver}"
  # -v prints the version and exits with status 99 (xpdf's convention)
  QT_QPA_PLATFORM=offscreen ./xpdf/xpdf -v 2>&1 | grep -q 'xpdf version 3'
}

package() {
  cd "xpdf-${_debver}"
  make DESTDIR="$pkgdir" install
}
```

The xpdf build needs poppler's private headers (Arch's `poppler` package
ships them; on Debian they are `libpoppler-private-dev`).
