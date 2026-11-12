# Architecture

## Headers

`include/` holds the genuine public headers: X11 and Xt from the X.Org
releases (MIT) and Motif 2.3.8 (LGPL), including the Motif 2.3 extension
widgets. Programs compile against exactly the API they were written for and
the implementation is type-checked against the same prototypes.

Private headers:

- Real: `X11/IntrinsicP.h`, `CoreP.h`, `CompositeP.h`, `ConstrainP.h`,
  `ObjectP.h`, `RectObjP.h`, `ShellP.h`, `VendorP.h`, and Motif's `XmP.h`,
  `ManagerP.h`, `PrimitiveP.h`, `GadgetP.h`, `BaseClassP.h`, `ColorP.h`.
  The instance and class records they describe exist (see below).
- Shims, for the other widgets' `*P.h`: they include the public header and
  `XmP.h`, so programs that only include them compile. Those widgets'
  instance parts (`XmPushButtonPart` ...) are not populated.

`private.h` includes these and then undefines the Xt convenience macros
(`XtWindow`, `XtIsRealized` ...) that this library implements as functions,
as libXt does.

## Widgets and classes

```
  block = [ MqRec (our C++ data) | Xt instance record: CorePart, CompositePart, ... ]
                                   ^ Widget
```

`R(w)` reaches the `MqRec`: the class hooks (`MqClass`), the Qt object
(QWidget, or QAction for menu entries), resources as set, callbacks,
children, event handlers, translations. The Xt record is what programs see;
`mq_sync_core` and `mq_sync_children` keep `core.*`, `composite.children`,
and the Manager/Primitive colour fields current (geometry comes from Qt on
resize/move events; before layout, `core.width/height` hold the preferred
size, as after an Xt widget's initialize).

Each class has a genuine Xt class record (`widgetClassRec`,
`xmManagerClassRec`, or a generic `MqXtClassRec`) registered with its
`MqClass` (`MQ_DEFINE_CLASS[_XT]`). `MqClass` holds the hooks: `create`
(build the Qt object), `apply` (react to a resource), `fetch` (dynamic
values for XtGetValues), `place`/`unplace` (managed children), `relayout`,
`initialize` (internal children), `set_other`/`get_other` (forward
resources to internal children, e.g. XmDropDown).

## Program-defined classes (xt_foreign.cpp)

A class record not registered by us belongs to the program. `mq_foreign_class`
builds an `MqClass` from its nearest known superclass, runs class and
class-part initialization (resolving `XtInherit*` from the superclass
record), and registers its actions. Its widgets get a canvas QWidget and
are driven through the class methods: resources are written at their
offsets (args > database > default procs), then initialize/initialize_hook,
realize, expose (from Qt paint events, with an Xlib Region), resize,
set_values (+ set_values_almost, geometry requests), destroy,
query_geometry; composites get change_managed and geometry_manager, and
their children's constraint records and constraint methods. Children of
such composites are positioned by the class (XtConfigureWidget,
XmeConfigureObject); gadget children have no window and are drawn into the
parent's window when the parent's expose calls their class expose.

## Resources

`src/generated/resources.inc` has every XtResource of Motif 2.3 (name,
class, type, size, default), extracted by `tools/gen_from_motif.py`.
Resolution: defaults < Xrm database < arguments. The database implements
the X precedence rules over app-defaults, fallback resources,
`~/.Xdefaults`, `XENVIRONMENT`, `-xrm` and the standard options.
XtGetValues writes `size` bytes; XmString values are copied.

## Layout

Managers are QLayouts implementing Motif's rules: Form attachments (an
iterative solver for the preferred size), RowColumn packing/columns,
BulletinBoard/DrawingArea positions, PanedWindow (QSplitter), MainWindow
areas, ScrolledWindow (automatic or application-defined). Shells size to
their content's full preferred size.

## Xlib

Windows and pixmaps are ids in a registry; a widget's window draws into a
backing QImage painted by its QWidget. GCs carry X state; drawing calls are
QPainter operations with X pixel conventions (no antialiasing, XOR via
raster ops, arcs in 1/64 degrees). Fonts are XLFD/fontconfig names mapped to
QFont. Colours parse X names, `rgb:` and `#` specs; the visual is 24-bit
TrueColor. Showing, resizing and XClearArea produce Expose events.

## Events

`XtAppMainLoop` runs the Qt event loop. An event filter per widget turns Qt
events into XEvents for event handlers and translation tables (Xt syntax,
our parser) whose actions come from XtAppAddActions, program classes, or
built-ins (`DrawingAreaInput`, `ArmAndActivate` ...). Timeouts, work procs
and input sources map onto QTimer/QSocketNotifier; XtAppNextEvent works for
programs with their own loop. Selections map onto QClipboard (PRIMARY to the
selection clipboard).

## Linking

Static archive with hidden visibility: programs resolve every X/Xt/Motif
symbol at link time and export none, so Qt's own use of the system libX11
(xcb platform, libGL) never meets them. `lib/libXm.so` etc. are GNU ld
scripts naming the archive and Qt. The optional shared build versions its
exports and forwards Xlib calls for foreign displays to the system libX11.
