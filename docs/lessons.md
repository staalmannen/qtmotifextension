# Lessons learned

Things that cost time once; check here before debugging something similar.

## Headers and C++

- Include Qt before X11: X defines `None`, `Bool`, `Status`, `KeyPress`,
  `FocusIn`, `CursorShape`... which break Qt headers. Use the `MQ_Q*` event
  type aliases and `mq_qcursor()`.
- Qt's `emit` is a macro: don't name variables `emit`.
- Global variables declared `extern "C"` in the vendored headers keep C
  linkage when defined without `extern "C"` (C++ does not mangle variables).
- Match the vendored prototypes exactly (`NeedWidePrototypes` is 0: use
  `Position`/`Dimension`/`Boolean`, not `int`).
- `IntrinsicP.h`/`XmP.h` define macros for functions we implement
  (`XtWindow`, `XtIsRealized`, `XtIsRectObj`...): `private.h` undefines them.

## Xt records

- Object and RectObj records are shorter than Core: never write
  `core.name`, `core.window`, `core.popup_list` (Core-only fields) for
  gadgets or objects - it silently corrupts the heap (valgrind finds it).
- Gadgets in Motif have no window: a composite redraws them by calling their
  class `expose`. Every class record must have a callable `expose`.
- Class records referenced as superclasses must be constant-initialized
  (`&core_rec`), not looked up at static-init time.
- A widget's preferred size belongs in `core.width/height` before layout:
  Xt layout code reads it.
- XtGetValues writes the resource's size, not an XtArgVal. Forwarded
  get_values must write through the owning child (`get_other` gets the
  destination pointer).

## Behaviour that differs from intuition

- QWidget::adjustSize caps top-levels at 2/3 of the screen; Motif shells take
  their content's full size (`size_to_content`).
- DrawingArea needs Motif's default translations (keys and buttons to
  `DrawingAreaInput`) so programs' `XtOverrideTranslations` merge with them.
- TextField `activate()` also activates the dialog's default button.
- resizeCallback / resize methods fire for every resize of a realized widget,
  including the first layout (xpdf keeps the size it last heard).
- Toolkit-wide: a constraint `set_values` returning True means "redisplay
  the child": XClearArea on its window (for a gadget, the parent's window).
- Motif computes some defaults at run time (`XmRCallProc`): background,
  foreground and shadow colours come from the Qt palette.

## Build and link

- Generated tables: names/types only defined in Motif's private headers
  (e.g. `XmRPositionValue`) are emitted as the string literals they stand for.
- GNU ld scripts as `libXm.so` hide the archive from CMake's dependency
  tracking: set `LINK_DEPENDS` on programs, or they are not relinked.
- glibc accepts a single versioned definition for an unversioned reference
  per object, so symbol versioning alone cannot keep Qt's Xlib calls away
  from a shared libXm replacement; static linking with hidden visibility
  does (the shared build forwards foreign displays instead).
- Arch's makepkg strips static libraries and builds LTO objects by default:
  library packages need `options=(staticlibs !lto)`.

## Testing

- Xvfb without a window manager keeps old windows' pixels: restart it
  between captures.
- `import -window <id>` with an empty id waits for a click: never call it
  without a window id (use `-window root` and trim).
- A flat PDF outline from LibreOffice HTML export is not a nested outline;
  `tests/mkpdf-outline.py` writes one.
