# Target Applications for Motif on Qt (`motif-on-qt`)

This document catalogs real-world C/C++ Motif applications suitable for compiling, testing, and benchmarking against `libmotif-on-qt` (Motif 2.3.8 / Xt / X11 on Qt 6).

---

## 1. Build Redirection & Headless Smoke-Testing

`motif-on-qt` provides genuine X11, Xt, and Motif 2.3 headers alongside drop-in shared library symlinks (`libXm.so`, `libXt.so`, `libX11.so`, `libXext.so`, `libXmu.so`, `libXpm.so`) pointing to `libmotif-on-qt.so`.

### Standard Autotools / Makefile Redirection
Point `CPPFLAGS` and `LDFLAGS` to your build directory when configuring or building legacy C codebases:

```bash
export MOTIFQT_ROOT=/path/to/motif-on-qt
export MOTIFQT_BUILD=$MOTIFQT_ROOT/build

./configure \
  CPPFLAGS="-I$MOTIFQT_ROOT/include" \
  LDFLAGS="-L$MOTIFQT_BUILD/lib -Wl,-rpath,$MOTIFQT_BUILD/lib"
```

### Imake (`xmkmf`) Projects
Many classic 1990s/2000s Motif programs use `Imakefile` instead of `configure`. Generate the `Makefile` first, then override the X11/Motif paths:

```bash
xmkmf -a
make \
  EXTRA_INCLUDES="-I$MOTIFQT_ROOT/include" \
  EXTRA_LOAD_FLAGS="-L$MOTIFQT_BUILD/lib -Wl,-rpath,$MOTIFQT_BUILD/lib"
```

### Automated Headless Testing
Use the built-in environment variables to script UI interactions and capture screenshots without an X11/Wayland display:

```bash
QT_QPA_PLATFORM=offscreen \
MOTIFQT_SCRIPT="wait 500; key n; shot page2.png; dump; quit" \
./target_app
```

---

## 2. High-Compatibility Targets (Standard Motif 1.2 / 2.1 Widgets)

These applications avoid custom `Xt` widget subclassing (`*P.h` internals), PseudoColor colormaps, and UIL, making them immediate candidates to compile and run out of the box.

### **NEdit (Nirvana Editor) 5.7**
* **Domain**: Classic tabbed programmer's text editor.
* **Why it fits**: Written in pure C against Motif 2.x. Note that NEdit's custom text widget (`textDisp.c` / `textBuf.c`) subclasses `XmPrimitive`, which touches private headers (`XmP.h`)—making NEdit the premier benchmark if you decide to support basic `XmPrimitive` subclassing, whereas its dialogs, menus, and macro panels exercise standard `XmForm`, `XmRowColumn`, `XmList`, and `XmFileSelectionBox`.
* **Key Exercises**: Complex `XmForm` attachments, tear-off menus, keyboard translations/actions, clipboard, and Xrm fallback resources.

### **DDD (Data Display Debugger) 3.4.x**
* **Domain**: Graphical frontend for `gdb`, `dbx`, `jdb`, and Python/Perl debuggers.
* **Why it fits**: Actively maintained C++ application that still depends on Motif/LessTif. Exercises `XmPanedWindow`, `XmScrolledWindow`, `XmText`, `XmDrawingArea` (for pointer/data structure graphs), and pseudo-terminal I/O via `XtAppAddInput`.
* **Build Notes**: Configure with `--with-motif-includes=$MOTIFQT_ROOT/include --with-motif-libraries=$MOTIFQT_BUILD/lib`.

### **XEphem 4.x**
* **Domain**: Interactive astronomical ephemeris and planetarium software (now open-source under MIT).
* **Why it fits**: One of the largest, cleanest pure-C Motif applications ever written. Uses standard Motif dialogs (`XmForm`, `XmScale`, `XmToggleButton`, `XmRowColumn`) and heavy Xlib 2D drawing (`XDrawLine`, `XDrawArc`, `XFillPolygon`, pixmaps, and fonts) inside `XmDrawingArea` widgets.
* **Build Notes**: Uses plain Makefiles (`GUI/xephem/Makefile`); override `MOTIFI` and `MOTIFL`.

### **Plan (v1.10.x)**
* **Domain**: Lightweight Motif calendar, day planner, and reminder daemon (`pland`).
* **Why it fits**: Small, self-contained C codebase with zero exotic dependencies. Heavy use of `XmForm`, `XmBulletinBoard`, `XmDrawingArea`, and custom Xlib text/grid rendering.

### **Xmbdfed 4.7**
* **Domain**: Bitmap font editor (BDF/PCF/PSF formats).
* **Why it fits**: Clean C Motif application relying on `XmDrawingArea` pixel grids, `XmMainWindow`, `XmFileSelectionBox`, and Xlib `XImage` / GC bit-blitting operations.

### **Xbae / XmHTML Standalone Demos (or MMosaic)**
* **Domain**: Early Motif web browser (`MMosaic` / NCSA Mosaic 2.7ck).
* **Why it fits**: Exercises `XmScrolledWindow`, `XmTextField` URL bars, `XmOptionMenu`, `XmDrawingArea` hypertext rendering, and `XtAppAddTimeOut` / `XtAppAddInput` networking.

---

## 3. Domain-Specific & Scientific Targets

Motif remains entrenched in legacy engineering, CAD, and simulation suites that keep `libXm` alive on enterprise Linux distributions.

| Application | Category | Primary Motif / Xlib Features Exercised |
| :--- | :--- | :--- |
| **Grace (`xmgrace` 5.1.25)** | 2D XY Scientific Plotting | `XmDrawingArea`, `XmForm`, `XmComboBox`, `XmSpinBox`, `XmScale`, heavy Xlib vector/font rendering, PostScript export |
| **Ted (2.23)** | Rich Text Word Processor | `XmDrawingArea` page canvas, `XmRowColumn` toolbars, `XmFontList`, `XmClipboard`, Xlib text measurement |
| **Xfig (3.2.x — Motif build)** | Vector Drawing Tool | `XmForm`, `XmDrawingArea`, XOR rubber-banding (`GXxor`), `XCopyArea`, custom `Pixmap` tool buttons |
| **OpenDX (IBM Data Explorer)** | Scientific Visualization | Complex `XmForm` control panels, `XmScrolledWindow`, `XmPanedWindow` |
| **XbaeMatrix Examples** | Spreadsheet / Grid Widget | Stress-tests `XmClipWindow`, `XmScrollBar`, and `XmTextField` cell editing |

---

## 4. Roadmap Coverage Matrix (Testing "Not Supported" Features)

Use these specific applications when implementing or testing features listed under **Not supported** in `README.md`:

| Missing Feature | Best Target Application(s) | What It Exercises |
| :--- | :--- | :--- |
| **Custom Widget Subclassing (`*P.h`)** | **xpdf 3.04** (with outline enabled), **NEdit**, **XbaeMatrix** | Subclassing `Core` / `XmPrimitive` / `XmManager`, reading `w->core.width` and `XtWidgetProc` methods (`resize`, `expose`, `set_values`). |
| **Motif 2.2+ Extension Widgets** | **CDE (Common Desktop Environment)** apps (`dtfile`, `dtpad`, `dtmail`) | `XmNotebook`, `XmContainer`, `XmIconGadget`, `XmDropSite` drag-and-drop, and `XmTabStack`. |
| **UIL / Mrm (`libMrm`)** | **Motif 2.3.8 `demos/programs/hellomotif`**, **CDE `dtcreate`** | Compiling `.uil` interface descriptions via `uil` and loading widget hierarchies at runtime with `MrmOpenHierarchyPerDisplay`. |
| **PseudoColor / Colormap Tricks** | **XEphem** (night-vision mode), **xlockmore** | `XAllocColorCells`, `XStoreColors`, and dynamic palette rotation. |
| **Motif Drag and Drop (`XmDragDrop`)** | **NEdit** (tab dragging), **CDE `dtfile`** | `XmDragStart`, `XmDropSiteRegister`, and `XmDropTransfer`. |

---

## 5. Quick Verification Checklist for New Targets

When testing an unfamiliar Motif codebase against `motif-on-qt`:

1. **Check for private header access**: `grep -rnE "XmP\.h|IntrinsicP\.h|->core\.|->manager\.|->primitive\." src/`
   * *If matches exist*: The app defines custom Xt/Motif widgets or reads internal widget structs directly.
2. **Check for UIL files**: `find . -name "*.uil"`
   * *If matches exist*: Requires `libMrm` support before the UI can instantiate.
3. **Check for Imake vs. Autotools**: Look for `Imakefile` vs. `configure.ac` / `CMakeLists.txt`.
4. **Run headless geometry dump**: Launch with `QT_QPA_PLATFORM=offscreen MOTIFQT_AUTOQUIT=1000 MOTIFQT_DUMP=1` to verify that `XmForm` attachments and `RowColumn` packing resolve without zero-size collapse or overlapping widgets.
