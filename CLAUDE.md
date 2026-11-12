# Motif on Qt (qtmotifextension)

The Xt/Motif C API, and the part of Xlib Motif programs use, implemented on
Qt 6, so unmodified Motif programs compile and run without X11 or libXm.
Static library; programs depend only on Qt at run time.

## Commands

```bash
cmake -B build && cmake --build build -j8 && ctest --test-dir build
```

Headless runs: `QT_QPA_PLATFORM=offscreen MOTIFQT_AUTOQUIT=800
MOTIFQT_SCREENSHOT=out.png ./prog`, or `MOTIFQT_SCRIPT="wait 500; click
quit"` (see docs/testing.md).

## Layout

- `include/` vendored genuine X11/Xt (MIT) and Motif 2.3.8 (LGPL) headers;
  most widget `*P.h` are generated shims (tools/gen_private_shims.py), the
  core private headers (IntrinsicP.h, CoreP.h, XmP.h, ManagerP.h ...) are real
- `src/generated/` resource tables, enum and keysym names from the Motif
  sources (tools/gen_from_motif.py) - do not edit by hand
- `src/xt_*.cpp` Xt (core, resources, events, selections, program-defined
  classes), `src/xlib.cpp` Xlib on QPainter, `src/xm_*.cpp` Motif widgets
- `examples/` Motif's own demos, unmodified; `tests/` our regression programs

## Conventions

- `private.h` includes Qt before X11 (X macros break Qt); use `MQ_QKeyPress`
  etc. instead of `QEvent::KeyPress`
- A `Widget` points at a genuine Xt instance record; our C++ data is `R(w)`
  (an `MqRec` just before it). Class hooks live in `MqClass` (`mq_class()`),
  the Xt class record is `MqClass::xt`. Keep core fields in step via
  `mq_sync_core` / `mq_sync_children`
- Classes are defined with `MQ_DEFINE_CLASS` / `MQ_DEFINE_CLASS_XT`;
  superclasses by `&xxx_rec`, never `mq_class(...)` (static init order)
- Verify against real Motif: same program built with system libXm under
  Xvfb, screenshots side by side (docs/testing.md); run valgrind on changes
  to memory layout
- Commits end with the session attribution trailers; no model names in
  commits; branch `claude/motif-on-qt`

## More

- docs/architecture.md - how it works
- docs/testing.md - headless testing, comparison with real Motif, xpdf
- docs/lessons.md - pitfalls found the hard way
- docs/user-guide.md, docs/packaging.md - for users and packagers
