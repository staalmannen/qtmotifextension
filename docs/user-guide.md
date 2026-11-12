# User guide

Motif on Qt lets you build an old Motif program from its unmodified sources
and run it on any system with Qt 6 - Wayland, X11, Windows, macOS - without
libXm, libXt or an X server.

## Install

From a distribution package (`motif-on-qt`), or from source:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DMQ_BUILD_EXAMPLES=OFF
```

```bash
cmake --build build
```

```bash
sudo cmake --install build
```

This installs a static library, headers and link helpers under
`/usr/lib/motif-on-qt` and `/usr/include/motif-on-qt` (see packaging.md).
Nothing of it is needed to *run* the programs you build: they only need Qt.

## Build a Motif program

`motif-on-qt-env` points a build at Motif on Qt: headers first in the
include path, `-lXm -lXt -lX11` (and `-lXext`, `-lXmu`, `-lXpm` ...)
resolving to it, and pkg-config answering for `x11`, `xt` and `motif`.

**configure scripts:**

```bash
motif-on-qt-env ./configure --prefix=/usr
```

```bash
make
```

**Plain Makefiles** that use `CPPFLAGS`/`LDFLAGS`:

```bash
motif-on-qt-env make
```

Makefiles that hard-code their flags: pass the paths yourself, for example
`make CFLAGS="-O2 $(pkg-config --cflags motif-on-qt)" LDFLAGS="$(pkg-config --libs-only-L motif-on-qt)"`.

**Imake (xmkmf) projects:** generate the Makefile as usual, then most
templates accept

```bash
make EXTRA_INCLUDES="-I/usr/include/motif-on-qt" EXTRA_LDOPTIONS="-L/usr/lib/motif-on-qt"
```

(use `pkg-config --variable=libdir motif-on-qt` for the library directory on
multiarch systems).

**By hand:**

```bash
cc $(pkg-config --cflags motif-on-qt) prog.c -o prog $(pkg-config --libs motif-on-qt)
```

C++ programs and C programs both work; the link step brings in Qt and the
C++ runtime automatically.

## Run

The program is an ordinary Qt application:

- It follows the desktop's Qt style and theme. Choose another with
  `-style fusion` or `QT_STYLE_OVERRIDE=fusion`.
- `QT_QPA_PLATFORM=wayland`, `xcb`, `offscreen` ... select the platform.
- X resources work as before: app-defaults files (searched in
  `$XAPPLRESDIR`, `~`, `/usr/share/X11/app-defaults`,
  `/etc/X11/app-defaults`), `~/.Xdefaults`, `XENVIRONMENT`, and the usual
  options (`-xrm`, `-geometry`, `-fg`, `-bg`, `-fn`, `-title`, `-name`).
- Fonts named in X style (`-*-helvetica-bold-r-*-*-14-*`, `fixed`, `9x15`)
  are mapped to the closest system font.

## What works and what does not

See the README: almost all of Xt and the Motif widget set, program-defined
widget classes, Xlib drawing, selections and the clipboard. Not available:
UIL/Mrm, drag and drop, a few Motif 2.2+ extension widgets (Container,
Notebook, TabStack, Column ...), PseudoColor colormap tricks.

## When a build fails

- *undefined reference to `Xm...`/`Xt...`/`X...`*: a function Motif on Qt
  does not have yet. Please report it with the program's name.
- *errors about `XmPushButtonPart`, `primitive.foreground` and the like in
  your code*: the program reads the internals of a Motif widget class other
  than the base classes; that part of Motif is not reproduced.
- The program finds the system's libXm instead: make sure the build uses
  `motif-on-qt-env` (or the include/library paths) and that nothing adds
  `-I/usr/include` before them.

## Debugging and testing helpers

Environment variables, all optional:

- `MOTIFQT_DUMP=1` with `MOTIFQT_AUTOQUIT=<ms>`: print the widget tree.
- `MOTIFQT_SCREENSHOT=<file.png>` with `MOTIFQT_AUTOQUIT`: save the windows.
- `MOTIFQT_SCRIPT="..."`: drive the program (see testing.md).
