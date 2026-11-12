# Testing

## Unit of truth: real Motif

The reference for behaviour and looks is the same program built against the
real libXm 2.3.8 (Debian/Ubuntu `libmotif-dev`) and run under Xvfb. Compare
screenshots side by side; our layout should match Motif's structure (the
Qt style differs, by design).

```bash
gcc -Iexamples -o prog-real examples/prog.c -lXm -lXt -lX11
```

```bash
Xvfb :45 -screen 0 1024x768x24 &
```

```bash
DISPLAY=:45 ./prog-real & sleep 3; DISPLAY=:45 import -window root real.png; convert real.png -trim +repage real.png
```

Restart Xvfb between captures (without a window manager, old windows'
pixels stay on the root window).

## Headless runs of our build

- `QT_QPA_PLATFORM=offscreen` no display needed
- `MOTIFQT_AUTOQUIT=<ms>` exit after a delay; `MOTIFQT_SCREENSHOT=<png>`
  saves the windows first (`-1.png`, `-2.png` for further windows, menus last)
- `MOTIFQT_DUMP=1` prints the Qt widget tree with Motif names and classes
- `MOTIFQT_SCRIPT="wait 1500; click find; type text hello; key text Return;
  shot out.png; quit"` - steps: `wait <ms>`, `key [<widget>] <keys>`
  (QKeySequence syntax), `click <widget> [button] [x y]`,
  `drag <widget> x1 y1 x2 y2`, `type <widget> <text>`, `shot <png>`, `dump`,
  `quit`. Widgets are found by Motif name, optionally as a path (`a.b`).

ctest runs the examples and `tests/` headless (`ctest --test-dir build`).

## Memory

Run changed areas under valgrind; layout code that writes Xt records is the
usual suspect (an Object or RectObj record is shorter than a Core record).

```bash
QT_QPA_PLATFORM=offscreen MOTIFQT_AUTOQUIT=800 valgrind -q ./build/examples/autopopups
```

## X11 platform

Run with `QT_QPA_PLATFORM=xcb` on Xvfb too: Qt's own Xlib use must not
meet ours. `xclip -o -selection primary` checks selections.

## xpdf (real application)

```bash
apt-get source xpdf
```

```bash
tools/build-xpdf.sh xpdf-3.04+git20240202 build
```

`tests/mkpdf-outline.py` writes a 3-page PDF with a nested outline and a
link. Things worth scripting: toolbar and key navigation, page number entry,
zoom (`z`, `w`), Find dialog, right-click menu, Open dialog, outline
expand/collapse and selection, link click, drag selection, printing to a file.
