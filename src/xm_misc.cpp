/*
 * Motif on Qt - the rest of the Xm API: XmCreate* / XmVaCreate* for every
 * class, built-in translation actions, pixmaps and the image cache, colours,
 * keyboard traversal, the clipboard, representation types and other
 * utilities.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <QMimeData>
#include <cstring>

const char *mq_app_name ();

/* ======================================================================
 * Built-in actions (what Motif widgets register for their translations)
 * ====================================================================== */
void mq_builtin_action (Widget w, const QByteArray &name, XEvent *xe, String *params, Cardinal *n)
{
  MqEventScope scope (xe);
  if (name == "DrawingAreaInput") {
    XmDrawingAreaCallbackStruct cs = { XmCR_INPUT, xe, mq_window_for (w) };
    mq_call (w, XmNinputCallback, &cs);
  } else if (name == "ArmAndActivate" || name == "Activate" || name == "activate") {
    if (auto *b = mq_q<QAbstractButton> (w)) b->click ();
    else if (QAction *a = mq_action (w)) a->trigger ();
    else if (mq_class_is (w, "XmText") || mq_class_is (w, "XmTextField")) {
      XmAnyCallbackStruct cs = { XmCR_ACTIVATE, xe };
      mq_call (w, XmNactivateCallback, &cs);
    } else {
      XmAnyCallbackStruct cs = { XmCR_ACTIVATE, xe };
      mq_call (w, XmNactivateCallback, &cs);
    }
  } else if (name == "Arm") {
    XmAnyCallbackStruct cs = { XmCR_ARM, xe };
    mq_call (w, XmNarmCallback, &cs);
  } else if (name == "Disarm") {
    XmAnyCallbackStruct cs = { XmCR_DISARM, xe };
    mq_call (w, XmNdisarmCallback, &cs);
  } else if (name == "Select") {
    if (auto *b = mq_q<QAbstractButton> (w)) b->click ();
  } else if (name == "self-insert" || name == "insert-string") {
    QString s;
    if (name == "insert-string") for (Cardinal i = 0; i < *n; i++) s += QString::fromLocal8Bit (params[i]);
    else if (xe && (xe->type == KeyPress)) {
      char buf[16];
      KeySym ks;
      int len = XLookupString (&xe->xkey, buf, sizeof buf, &ks, nullptr);
      s = QString::fromLocal8Bit (buf, len);
    }
    if (!s.isEmpty ()) {
      QByteArray b = s.toLocal8Bit ();
      XmTextInsert (w, XmTextGetInsertionPosition (w), b.data ());
      XmTextSetInsertionPosition (w, XmTextGetInsertionPosition (w) + s.size ());
    }
  } else if (name == "delete-previous-character") {
    XmTextPosition p = XmTextGetInsertionPosition (w);
    if (p > 0) XmTextReplace (w, p - 1, p, (char *) "");
  } else if (name == "delete-next-character") {
    XmTextPosition p = XmTextGetInsertionPosition (w);
    XmTextReplace (w, p, p + 1, (char *) "");
  } else if (name == "beginning-of-line") XmTextSetInsertionPosition (w, 0);
  else if (name == "end-of-line") XmTextSetInsertionPosition (w, XmTextGetLastPosition (w));
  else if (name == "process-cancel" || name == "ManagerParentCancel") {
    for (Widget p = w; p; p = p->parent)
      if (Widget c = mq_find_res (p, XmNcancelButton) ? (Widget) mq_get (p, XmNcancelButton) : nullptr) {
        if (auto *b = mq_q<QAbstractButton> (c)) b->click ();
        break;
      }
  } else if (name == "ManagerParentActivate") {
    for (Widget p = w; p; p = p->parent)
      if (Widget c = mq_find_res (p, XmNdefaultButton) ? (Widget) mq_get (p, XmNdefaultButton) : nullptr) {
        if (auto *b = mq_q<QAbstractButton> (c)) b->click ();
        break;
      }
  } else if (name == "ManagerGadgetArm" || name == "ManagerGadgetActivate" || name == "ManagerGadgetSelect"
             || name == "ManagerGadgetButtonMotion" || name == "ManagerGadgetKeyInput" || name == "ManagerFocusIn" || name == "ManagerFocusOut"
             || name == "ManagerEnter" || name == "ManagerLeave" || name == "ManagerGadgetHelp" || name == "PrimitiveHelp"
             || name == "Help" || name == "Enter" || name == "Leave" || name == "PrimitiveFocusIn" || name == "PrimitiveFocusOut"
             || name == "MenuBtnDown" || name == "MenuBtnUp") {
    if ((name.endsWith ("Help") || name == "Help")) {
      XmAnyCallbackStruct cs = { XmCR_HELP, xe };
      for (Widget p = w; p; p = p->parent) if (mq_has_callbacks (p, XmNhelpCallback)) { mq_call (p, XmNhelpCallback, &cs); break; }
    }
    /* the Qt widgets already behave this way */
  } else {
    qWarning ("Motif on Qt: action \"%s\" is not available", name.constData ());
  }
}

/* ======================================================================
 * Pixmaps: XmGetPixmap and the image cache
 * ====================================================================== */
static QHash<QByteArray, QImage> &installed_images ()
{
  static QHash<QByteArray, QImage> h;
  return h;
}

static QImage builtin_image (const QByteArray &name)
{
  auto pattern = [] (std::initializer_list<const char *> rows) {
    int h = rows.size (), w = (int) strlen (*rows.begin ());
    QImage img (w, h, QImage::Format_Mono);
    img.setColor (0, qRgb (0, 0, 0));
    img.setColor (1, qRgb (255, 255, 255));
    int y = 0;
    for (const char *r : rows) { for (int x = 0; x < w; x++) img.setPixel (x, y, r[x] == '#' ? 1 : 0); y++; }
    return img;
  };
  if (name == "background") return pattern ({ "...." , "....", "....", "...." });
  if (name == "25_foreground") return pattern ({ "#...", "....", "..#.", "...." });
  if (name == "50_foreground") return pattern ({ "#.#.", ".#.#", "#.#.", ".#.#" });
  if (name == "75_foreground") return pattern ({ "###.", "####", "#.##", "####" });
  if (name == "vertical") return pattern ({ "#...", "#...", "#...", "#..." });
  if (name == "horizontal") return pattern ({ "####", "....", "....", "...." });
  if (name == "slant_left") return pattern ({ "#...", ".#..", "..#.", "...#" });
  if (name == "slant_right") return pattern ({ "...#", "..#.", ".#..", "#..." });
  if (name == "cross_weave") return pattern ({ "#..#", ".##.", ".##.", "#..#" });
  QStyle::StandardPixmap sp;
  if (name.endsWith ("xm_error")) sp = QStyle::SP_MessageBoxCritical;
  else if (name.endsWith ("xm_information")) sp = QStyle::SP_MessageBoxInformation;
  else if (name.endsWith ("xm_question")) sp = QStyle::SP_MessageBoxQuestion;
  else if (name.endsWith ("xm_warning")) sp = QStyle::SP_MessageBoxWarning;
  else if (name.endsWith ("xm_working")) sp = QStyle::SP_BrowserReload;
  else return QImage ();
  return QApplication::style ()->standardIcon (sp).pixmap (32, 32).toImage ();
}

static QImage find_image (const QByteArray &name)
{
  auto it = installed_images ().constFind (name);
  if (it != installed_images ().constEnd ()) return it.value ();
  QImage img = builtin_image (name);
  if (!img.isNull ()) return img;
  QString n = QString::fromLocal8Bit (name);
  QStringList candidates;
  if (QDir::isAbsolutePath (n)) candidates << n;
  else {
    QByteArray env = qgetenv ("XBMLANGPATH");
    for (const QByteArray &p : env.split (':'))
      if (!p.isEmpty ()) candidates << QString::fromLocal8Bit (p).replace ("%B", n).replace ("%N", QCoreApplication::applicationName ()).replace ("%L", "").replace ("%l", "");
    candidates << n << QDir::homePath () + "/" + n << "/usr/include/X11/bitmaps/" + n << "/usr/share/X11/bitmaps/" + n
               << "/usr/include/X11/pixmaps/" + n;
  }
  for (const QString &c : candidates) {
    for (const QString &ext : { QString (), QString (".xpm"), QString (".xbm"), QString (".png") }) {
      QImage i (c + ext);
      if (!i.isNull ()) return i;
    }
  }
  return QImage ();
}

static QHash<Pixmap, QByteArray> &pixmap_names ()
{
  static QHash<Pixmap, QByteArray> h;
  return h;
}

extern "C" {

Boolean XmInstallImage (XImage *image, char *name)
{
  if (!image || !name) return False;
  QImage img (image->width, image->height, QImage::Format_RGB32);
  for (int y = 0; y < image->height; y++)
    for (int x = 0; x < image->width; x++) {
      unsigned long p;
      if (image->depth == 1 || image->format == XYBitmap) {
        int bpl = image->bytes_per_line;
        unsigned char byte = ((unsigned char *) image->data)[y * bpl + x / 8];
        bool bit = image->bitmap_bit_order == LSBFirst ? (byte >> (x % 8)) & 1 : (byte >> (7 - x % 8)) & 1;
        p = bit ? 0xffffff : 0;
      } else p = XGetPixel (image, x, y);
      img.setPixel (x, y, 0xff000000 | p);
    }
  if (image->depth == 1 || image->format == XYBitmap) img = img.convertToFormat (QImage::Format_Mono);
  installed_images ().insert (name, img);
  return True;
}

Boolean XmUninstallImage (XImage *) { return True; }

Pixmap XmGetPixmapByDepth (Screen *, char *name, Pixel fg, Pixel bg, int depth)
{
  if (!name) return XmUNSPECIFIED_PIXMAP;
  QImage img = find_image (name);
  if (img.isNull ()) return XmUNSPECIFIED_PIXMAP;
  QImage out;
  if (img.depth () == 1) {
    out = QImage (img.size (), QImage::Format_ARGB32);
    QColor f = depth == 1 ? QColor (Qt::white) : mq_color (fg), b = depth == 1 ? QColor (Qt::black) : mq_color (bg);
    QImage mono = img.convertToFormat (QImage::Format_Mono);
    for (int y = 0; y < img.height (); y++)
      for (int x = 0; x < img.width (); x++) out.setPixelColor (x, y, mono.pixelIndex (x, y) && qGray (mono.color (mono.pixelIndex (x, y))) > 127 ? f : b);
  } else out = img.convertToFormat (QImage::Format_ARGB32);
  Pixmap p = mq_pixmap_from_image (out);
  pixmap_names ().insert (p, name);
  return p;
}

Pixmap XmGetPixmap (Screen *s, char *name, Pixel fg, Pixel bg) { return XmGetPixmapByDepth (s, name, fg, bg, 24); }
Pixmap XmGetScaledPixmap (Widget, char *name, Pixel fg, Pixel bg, int depth, double) { return XmGetPixmapByDepth (nullptr, name, fg, bg, depth); }

Boolean XmDestroyPixmap (Screen *, Pixmap p)
{
  if (!pixmap_names ().remove (p)) return False;
  XFreePixmap (mq_display, p);
  return True;
}

/* ======================================================================
 * Colours
 * ====================================================================== */
static XmColorProc s_color_proc = nullptr;

static void default_colors (XColor *bg, XColor *fg, XColor *sel, XColor *ts, XColor *bs)
{
  QColor b (bg->red >> 8, bg->green >> 8, bg->blue >> 8);
  auto set = [] (XColor *x, const QColor &c) {
    x->red = c.red () * 257; x->green = c.green () * 257; x->blue = c.blue () * 257;
    x->pixel = mq_pixel (c);
    x->flags = DoRed | DoGreen | DoBlue;
  };
  set (fg, qGray (b.rgb ()) > 140 ? QColor (Qt::black) : QColor (Qt::white));
  set (sel, b.darker (125));
  set (ts, b.lighter (150));
  set (bs, b.darker (200));
}

XmColorProc XmSetColorCalculation (XmColorProc proc)
{
  XmColorProc old = s_color_proc ? s_color_proc : default_colors;
  s_color_proc = proc;
  return old;
}

XmColorProc XmGetColorCalculation (void) { return s_color_proc ? s_color_proc : default_colors; }

void XmGetColors (Screen *, Colormap, Pixel background, Pixel *fg, Pixel *ts, Pixel *bs, Pixel *sel)
{
  XColor b, f, s, t, bo;
  QColor c = mq_color (background);
  b.red = c.red () * 257; b.green = c.green () * 257; b.blue = c.blue () * 257; b.pixel = background;
  XmGetColorCalculation () (&b, &f, &s, &t, &bo);
  if (fg) *fg = mq_pixel (QColor (f.red >> 8, f.green >> 8, f.blue >> 8));
  if (ts) *ts = mq_pixel (QColor (t.red >> 8, t.green >> 8, t.blue >> 8));
  if (bs) *bs = mq_pixel (QColor (bo.red >> 8, bo.green >> 8, bo.blue >> 8));
  if (sel) *sel = mq_pixel (QColor (s.red >> 8, s.green >> 8, s.blue >> 8));
}

void XmChangeColor (Widget w, Pixel background)
{
  Pixel fg, ts, bs, sel;
  XmGetColors (nullptr, 0, background, &fg, &ts, &bs, &sel);
  XtVaSetValues (w, XmNbackground, (XtArgVal) background, XmNforeground, (XtArgVal) fg, XmNtopShadowColor, (XtArgVal) ts,
                 XmNbottomShadowColor, (XtArgVal) bs, NULL);
  if (mq_find_res (w, XmNselectColor)) XtVaSetValues (w, XmNselectColor, (XtArgVal) sel, NULL);
  if (mq_find_res (w, XmNarmColor)) XtVaSetValues (w, XmNarmColor, (XtArgVal) sel, NULL);
}

/* ======================================================================
 * Traversal and focus
 * ====================================================================== */
Boolean XmProcessTraversal (Widget w, XmTraversalDirection dir)
{
  QWidget *qw = mq_qw (w);
  if (!qw) return False;
  switch (dir) {
  case XmTRAVERSE_CURRENT: qw->setFocus (Qt::OtherFocusReason); return True;
  case XmTRAVERSE_NEXT: case XmTRAVERSE_NEXT_TAB_GROUP: case XmTRAVERSE_RIGHT: case XmTRAVERSE_DOWN:
    if (QWidget *f = QApplication::focusWidget ()) f->nextInFocusChain ()->setFocus (Qt::TabFocusReason);
    return True;
  case XmTRAVERSE_PREV: case XmTRAVERSE_PREV_TAB_GROUP: case XmTRAVERSE_LEFT: case XmTRAVERSE_UP:
    if (QWidget *f = QApplication::focusWidget ()) f->previousInFocusChain ()->setFocus (Qt::BacktabFocusReason);
    return True;
  default: qw->setFocus (); return True;
  }
}

void XmAddTabGroup (Widget) {}
void XmRemoveTabGroup (Widget) {}
Boolean XmIsTraversable (Widget w) { QWidget *q = mq_qw (w); return q && q->isEnabled () && q->isVisible () && q->focusPolicy () != Qt::NoFocus; }
XmVisibility XmGetVisibility (Widget w)
{
  QWidget *q = mq_qw (w);
  if (!q || !q->isVisible ()) return XmVISIBILITY_FULLY_OBSCURED;
  return q->visibleRegion ().boundingRect () == q->rect () ? XmVISIBILITY_UNOBSCURED : XmVISIBILITY_PARTIALLY_OBSCURED;
}
Widget XmGetTabGroup (Widget w) { return w ? w->parent : nullptr; }
Widget XmGetFocusWidget (Widget) { return mq_from_qobject (QApplication::focusWidget ()); }
Widget XmGetDestination (Display *) { return mq_from_qobject (QApplication::focusWidget ()); }

/* ======================================================================
 * Miscellaneous
 * ====================================================================== */
void XmUpdateDisplay (Widget) { QCoreApplication::processEvents (QEventLoop::ExcludeUserInputEvents); }

Widget XmObjectAtPoint (Widget w, Position x, Position y)
{
  QWidget *q = mq_qw (w);
  QWidget *c = q ? q->childAt (x, y) : nullptr;
  Widget r = c ? mq_from_qobject (c) : nullptr;
  while (r && r->parent != w) r = r->parent;
  return r;
}

Boolean XmWidgetGetBaselines (Widget w, Dimension **baselines, int *count)
{
  QWidget *q = mq_qw (w);
  if (!q) return False;
  *count = 1;
  *baselines = (Dimension *) XtMalloc (sizeof (Dimension));
  (*baselines)[0] = (q->height () - q->fontMetrics ().height ()) / 2 + q->fontMetrics ().ascent ();
  return True;
}

Boolean XmWidgetGetDisplayRect (Widget w, XRectangle *r)
{
  QWidget *q = mq_qw (w);
  if (!q) return False;
  QRect c = q->contentsRect ();
  r->x = c.x (); r->y = c.y (); r->width = c.width (); r->height = c.height ();
  return True;
}

Boolean XmIsMotifWMRunning (Widget) { return True; }

static Cursor s_menu_cursor = 0;
void XmSetMenuCursor (Display *, Cursor c) { s_menu_cursor = c; }
Cursor XmGetMenuCursor (Display *) { return s_menu_cursor; }

static int s_font_unit_h = 10, s_font_unit_v = 10;
void XmSetFontUnits (Display *, int h, int v) { s_font_unit_h = h; s_font_unit_v = v; }
void XmSetFontUnit (Display *d, int v) { XmSetFontUnits (d, v, v); }

static double unit_to_pixels (int value, int type, bool horizontal)
{
  double dpi = horizontal ? QGuiApplication::primaryScreen ()->logicalDotsPerInchX () : QGuiApplication::primaryScreen ()->logicalDotsPerInchY ();
  switch (type) {
  case Xm100TH_MILLIMETERS: return value * dpi / 2540.0;
  case Xm1000TH_INCHES: return value * dpi / 1000.0;
  case Xm100TH_POINTS: return value * dpi / 7200.0;
  case Xm100TH_FONT_UNITS: return value * (horizontal ? s_font_unit_h : s_font_unit_v) / 100.0;
  case XmINCHES: return value * dpi;
  case XmCENTIMETERS: return value * dpi / 2.54;
  case XmMILLIMETERS: return value * dpi / 25.4;
  case XmPOINTS: return value * dpi / 72.0;
  case XmFONT_UNITS: return value * (horizontal ? s_font_unit_h : s_font_unit_v);
  default: return value;
  }
}

int XmConvertUnits (Widget, int orientation, int from_type, int from_val, int to_type)
{
  bool h = orientation == XmHORIZONTAL;
  double px = unit_to_pixels (from_val, from_type, h);
  double one = unit_to_pixels (1, to_type, h);
  return one > 0 ? qRound (px / one) : 0;
}

int XmCvtToHorizontalPixels (Screen *, int v, int type) { return qRound (unit_to_pixels (v, type, true)); }
int XmCvtToVerticalPixels (Screen *, int v, int type) { return qRound (unit_to_pixels (v, type, false)); }
int XmCvtFromHorizontalPixels (Screen *, int v, int type) { double one = unit_to_pixels (1, type, true); return one > 0 ? qRound (v / one) : v; }
int XmCvtFromVerticalPixels (Screen *, int v, int type) { double one = unit_to_pixels (1, type, false); return one > 0 ? qRound (v / one) : v; }

int XmConvertStringToUnits (Screen *, String spec, int orientation, int to_type, XtEnum *parse_error)
{
  if (parse_error) *parse_error = False;
  QString s = QString::fromLatin1 (spec ? spec : "").trimmed ();
  static const QRegularExpression re ("^([-+]?[0-9.]+)\\s*([a-z]*)$");
  auto m = re.match (s);
  if (!m.hasMatch ()) { if (parse_error) *parse_error = True; return 0; }
  double v = m.captured (1).toDouble ();
  QString u = m.captured (2);
  int type = u == "in" || u == "inches" ? XmINCHES : u == "cm" ? XmCENTIMETERS : u == "mm" ? XmMILLIMETERS
           : u == "pt" || u == "points" ? XmPOINTS : u == "fu" ? XmFONT_UNITS : XmPIXELS;
  double px = unit_to_pixels (1, type, orientation == XmHORIZONTAL) * v;
  double one = unit_to_pixels (1, to_type, orientation == XmHORIZONTAL);
  return one > 0 ? qRound (px / one) : 0;
}

void XmCvtStringToUnitType (XrmValuePtr, Cardinal *, XrmValue *from, XrmValue *to)
{
  static unsigned char result;
  bool ok;
  int v = mq_enum_value ((const char *) from->addr, &ok);
  result = ok ? v : XmPIXELS;
  to->addr = (XPointer) &result;
  to->size = sizeof result;
}

char *XmRegisterSegmentEncoding (char *, char *) { return nullptr; }
char *XmMapSegmentEncoding (char *) { return XtNewString ("ISO8859-1"); }

Widget XmTrackingLocate (Widget widget, Cursor, Boolean)
{
  /* modal: wait for a click and report the widget under it */
  QEventLoop loop;
  Widget found = nullptr;
  struct Picker : QObject {
    QEventLoop *loop; Widget *out;
    bool eventFilter (QObject *o, QEvent *e) override
    {
      if (e->type () == QEvent::MouseButtonPress) {
        QWidget *under = QApplication::widgetAt (static_cast<QMouseEvent *> (e)->globalPosition ().toPoint ());
        *out = mq_from_qobject (under ? under : o);
        loop->quit ();
        return true;
      }
      return false;
    }
  } picker;
  picker.loop = &loop;
  picker.out = &found;
  qApp->installEventFilter (&picker);
  QApplication::setOverrideCursor (Qt::WhatsThisCursor);
  loop.exec ();
  QApplication::restoreOverrideCursor ();
  qApp->removeEventFilter (&picker);
  Q_UNUSED (widget);
  return found;
}

Widget XmTrackingEvent (Widget widget, Cursor cursor, Boolean confine, XEvent *ev)
{
  Widget w = XmTrackingLocate (widget, cursor, confine);
  if (ev) {
    memset (ev, 0, sizeof *ev);
    ev->type = ButtonRelease;
    ev->xbutton.window = mq_window_for (w);
  }
  return w;
}

Atom XmInternAtom (Display *d, String name, Boolean only_if_exists) { return XInternAtom (d, name, only_if_exists); }
String XmGetAtomName (Display *d, Atom atom) { return XGetAtomName (d, atom); }

/* XmDisplay / XmScreen: per-display and per-screen objects that only carry
 * resources (drag and drop styles, default pixmaps, colours ...) */
static WidgetClassRec xm_display_rec = { "XmDisplay", objectClass, K_OBJECT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec xm_screen_rec = { "XmScreen", objectClass, K_OBJECT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClass xmDisplayClass = &xm_display_rec;
WidgetClass xmScreenClass = &xm_screen_rec;

Widget XmGetXmDisplay (Display *)
{
  static Widget d = nullptr;
  if (!d) d = XtCreateWidget (mq_app_name (), xmDisplayClass, nullptr, nullptr, 0);
  return d;
}

Widget XmGetXmScreen (Screen *)
{
  static Widget s = nullptr;
  if (!s) s = XtCreateWidget ("screen0", xmScreenClass, XmGetXmDisplay (mq_display), nullptr, 0);
  return s;
}

void XmResolveAllPartOffsets (WidgetClass, XmOffsetPtr *offset, XmOffsetPtr *constraint_offset)
{
  if (offset) *offset = nullptr;
  if (constraint_offset) *constraint_offset = nullptr;
  MQ_WARN_ONCE ("Motif on Qt: XmResolveAllPartOffsets (widget subclassing) is not supported");
}
void XmResolvePartOffsets (WidgetClass c, XmOffsetPtr *offset) { XmResolveAllPartOffsets (c, offset, nullptr); }

/* ======================================================================
 * Representation types (the converters work from the generated enum
 * table, so these mostly report success)
 * ====================================================================== */
XmRepTypeId XmRepTypeRegister (String, String *, unsigned char *, unsigned char) { static XmRepTypeId next = 1000; return next++; }
void XmRepTypeAddReverse (XmRepTypeId) {}
Boolean XmRepTypeValidValue (XmRepTypeId, unsigned char, Widget) { return True; }
XmRepTypeList XmRepTypeGetRegistered (void) { return nullptr; }
XmRepTypeEntry XmRepTypeGetRecord (XmRepTypeId) { return nullptr; }
XmRepTypeId XmRepTypeGetId (String) { return XmREP_TYPE_INVALID; }
String *XmRepTypeGetNameList (XmRepTypeId, Boolean) { return nullptr; }
void XmRepTypeInstallTearOffModelConverter (void) {}

/* ======================================================================
 * Clipboard (the system clipboard through Qt)
 * ====================================================================== */
struct ClipItem { QMimeData *data = nullptr; QByteArray label; };
static QHash<long, ClipItem> s_clip_pending;
static long s_clip_next = 1;

static QString clip_mime (const char *format)
{
  QByteArray f (format ? format : "STRING");
  if (f == "STRING" || f == "TEXT" || f == "COMPOUND_TEXT" || f == "UTF8_STRING") return "text/plain";
  return "application/x-motif-" + QString::fromLatin1 (f);
}

int XmClipboardStartCopy (Display *, Window, XmString label, Time, Widget, XmCutPasteProc, long *itemid)
{
  long id = s_clip_next++;
  s_clip_pending[id].data = new QMimeData;
  s_clip_pending[id].label = label ? mq_xmstring_text (label).toUtf8 () : QByteArray ();
  if (itemid) *itemid = id;
  return ClipboardSuccess;
}

int XmClipboardBeginCopy (Display *d, Window w, XmString label, Widget widget, VoidProc, long *itemid)
{
  return XmClipboardStartCopy (d, w, label, CurrentTime, widget, nullptr, itemid);
}

int XmClipboardCopy (Display *, Window, long itemid, char *format, XtPointer buffer, unsigned long length, long, long *dataid)
{
  auto it = s_clip_pending.find (itemid);
  if (it == s_clip_pending.end ()) return ClipboardFail;
  QByteArray data ((const char *) buffer, buffer ? (int) length : 0);
  if (clip_mime (format) == "text/plain") it->data->setText (QString::fromLocal8Bit (data));
  else it->data->setData (clip_mime (format), data);
  if (dataid) *dataid = itemid;
  return ClipboardSuccess;
}

int XmClipboardCopyByName (Display *, Window, long, XtPointer, unsigned long, long) { return ClipboardSuccess; }

int XmClipboardEndCopy (Display *, Window, long itemid)
{
  auto it = s_clip_pending.find (itemid);
  if (it == s_clip_pending.end ()) return ClipboardFail;
  QGuiApplication::clipboard ()->setMimeData (it->data);
  s_clip_pending.erase (it);
  return ClipboardSuccess;
}

int XmClipboardCancelCopy (Display *, Window, long itemid)
{
  auto it = s_clip_pending.find (itemid);
  if (it != s_clip_pending.end ()) { delete it->data; s_clip_pending.erase (it); }
  return ClipboardSuccess;
}

int XmClipboardWithdrawFormat (Display *, Window, long) { return ClipboardSuccess; }
int XmClipboardUndoCopy (Display *, Window) { return ClipboardSuccess; }
int XmClipboardLock (Display *, Window) { return ClipboardSuccess; }
int XmClipboardUnlock (Display *, Window, Boolean) { return ClipboardSuccess; }
int XmClipboardStartRetrieve (Display *, Window, Time) { return ClipboardSuccess; }
int XmClipboardEndRetrieve (Display *, Window) { return ClipboardSuccess; }

static QByteArray clip_get (const char *format)
{
  const QMimeData *m = QGuiApplication::clipboard ()->mimeData ();
  if (!m) return QByteArray ();
  QString mime = clip_mime (format);
  if (mime == "text/plain") return m->text ().toLocal8Bit ();
  return m->data (mime);
}

int XmClipboardRetrieve (Display *, Window, char *format, XtPointer buffer, unsigned long length, unsigned long *outlength, long *private_id)
{
  QByteArray d = clip_get (format);
  if (private_id) *private_id = 0;
  if (d.isEmpty ()) { if (outlength) *outlength = 0; return ClipboardNoData; }
  unsigned long n = qMin ((unsigned long) d.size (), length);
  memcpy (buffer, d.constData (), n);
  if (outlength) *outlength = n;
  return n < (unsigned long) d.size () ? ClipboardTruncate : ClipboardSuccess;
}

int XmClipboardInquireCount (Display *, Window, int *count, unsigned long *maxlength)
{
  const QMimeData *m = QGuiApplication::clipboard ()->mimeData ();
  int n = m ? m->formats ().size () : 0;
  if (count) *count = n;
  if (maxlength) *maxlength = 64;
  return ClipboardSuccess;
}

int XmClipboardInquireFormat (Display *, Window, int n, XtPointer buffer, unsigned long len, unsigned long *outlength)
{
  const QMimeData *m = QGuiApplication::clipboard ()->mimeData ();
  QStringList f = m ? m->formats () : QStringList ();
  if (n < 1 || n > f.size ()) { if (outlength) *outlength = 0; return ClipboardNoData; }
  QByteArray name = f[n - 1] == "text/plain" ? QByteArray ("STRING") : f[n - 1].toLatin1 ().replace ("application/x-motif-", "");
  unsigned long c = qMin ((unsigned long) name.size (), len);
  memcpy (buffer, name.constData (), c);
  if (outlength) *outlength = c;
  return ClipboardSuccess;
}

int XmClipboardInquireLength (Display *, Window, char *format, unsigned long *length)
{
  QByteArray d = clip_get (format);
  if (length) *length = d.size ();
  return d.isEmpty () ? ClipboardNoData : ClipboardSuccess;
}

int XmClipboardInquirePendingItems (Display *, Window, char *, XmClipboardPendingList *list, unsigned long *count)
{
  if (list) *list = nullptr;
  if (count) *count = 0;
  return ClipboardSuccess;
}

int XmClipboardRegisterFormat (Display *, char *, int) { return ClipboardSuccess; }

/* ======================================================================
 * XmCreate* / XmVaCreate* for the remaining classes
 * ====================================================================== */
#define CREATE(Name, cls) \
  Widget XmCreate##Name (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, cls, p, a, n); }

CREATE (Label, xmLabelWidgetClass)
CREATE (LabelGadget, xmLabelGadgetClass)
CREATE (PushButton, xmPushButtonWidgetClass)
CREATE (PushButtonGadget, xmPushButtonGadgetClass)
CREATE (ToggleButton, xmToggleButtonWidgetClass)
CREATE (ToggleButtonGadget, xmToggleButtonGadgetClass)
CREATE (CascadeButton, xmCascadeButtonWidgetClass)
CREATE (CascadeButtonGadget, xmCascadeButtonGadgetClass)
CREATE (ArrowButton, xmArrowButtonWidgetClass)
CREATE (ArrowButtonGadget, xmArrowButtonGadgetClass)
CREATE (Separator, xmSeparatorWidgetClass)
CREATE (SeparatorGadget, xmSeparatorGadgetClass)
CREATE (DrawnButton, xmDrawnButtonWidgetClass)

static Widget va_create (const char *name, WidgetClass cls, Widget parent, va_list ap, bool managed)
{
  /* collect name/value pairs and typed args, then go through the Va core */
  QVector<Arg> args;
  struct Typed { const char *name, *type; XtArgVal value; int size; };
  QList<Typed> typed;
  for (char *n = va_arg (ap, char *); n; n = va_arg (ap, char *)) {
    if (!strcmp (n, XtVaTypedArg)) {
      Typed t;
      t.name = va_arg (ap, char *);
      t.type = va_arg (ap, char *);
      t.value = va_arg (ap, XtArgVal);
      t.size = va_arg (ap, int);
      typed << t;
    } else if (!strcmp (n, XtVaNestedList)) {
      MQ_WARN_ONCE ("Motif on Qt: nested lists in XmVaCreate* are not supported");
      (void) va_arg (ap, XtPointer);
    } else {
      Arg a;
      a.name = n;
      a.value = va_arg (ap, XtArgVal);
      args << a;
    }
  }
  Widget w = XtCreateWidget (name, cls, parent, args.data (), args.size ());
  for (const Typed &t : typed) XtVaSetValues (w, XtVaTypedArg, t.name, t.type, t.value, t.size, NULL);
  if (managed) XtManageChild (w);
  return w;
}

#define VA_CREATE(Name, cls) \
  Widget XmVaCreate##Name (Widget parent, char *name, ...) \
  { va_list ap; va_start (ap, name); Widget w = va_create (name, cls, parent, ap, false); va_end (ap); return w; } \
  Widget XmVaCreateManaged##Name (Widget parent, char *name, ...) \
  { va_list ap; va_start (ap, name); Widget w = va_create (name, cls, parent, ap, true); va_end (ap); return w; }

VA_CREATE (ArrowButton, xmArrowButtonWidgetClass)
VA_CREATE (ArrowButtonGadget, xmArrowButtonGadgetClass)
VA_CREATE (BulletinBoard, xmBulletinBoardWidgetClass)
VA_CREATE (CascadeButton, xmCascadeButtonWidgetClass)
VA_CREATE (CascadeButtonGadget, xmCascadeButtonGadgetClass)
VA_CREATE (ComboBox, xmComboBoxWidgetClass)
VA_CREATE (Command, xmCommandWidgetClass)
VA_CREATE (DrawingArea, xmDrawingAreaWidgetClass)
VA_CREATE (DrawnButton, xmDrawnButtonWidgetClass)
VA_CREATE (FileSelectionBox, xmFileSelectionBoxWidgetClass)
VA_CREATE (Form, xmFormWidgetClass)
VA_CREATE (Frame, xmFrameWidgetClass)
VA_CREATE (Label, xmLabelWidgetClass)
VA_CREATE (LabelGadget, xmLabelGadgetClass)
VA_CREATE (List, xmListWidgetClass)
VA_CREATE (MainWindow, xmMainWindowWidgetClass)
VA_CREATE (MessageBox, xmMessageBoxWidgetClass)
VA_CREATE (PanedWindow, xmPanedWindowWidgetClass)
VA_CREATE (PushButton, xmPushButtonWidgetClass)
VA_CREATE (PushButtonGadget, xmPushButtonGadgetClass)
VA_CREATE (RowColumn, xmRowColumnWidgetClass)
VA_CREATE (Scale, xmScaleWidgetClass)
VA_CREATE (ScrollBar, xmScrollBarWidgetClass)
VA_CREATE (ScrolledWindow, xmScrolledWindowWidgetClass)
VA_CREATE (SelectionBox, xmSelectionBoxWidgetClass)
VA_CREATE (Separator, xmSeparatorWidgetClass)
VA_CREATE (SeparatorGadget, xmSeparatorGadgetClass)
VA_CREATE (SimpleSpinBox, xmSimpleSpinBoxWidgetClass)
VA_CREATE (SpinBox, xmSpinBoxWidgetClass)
VA_CREATE (Text, xmTextWidgetClass)
VA_CREATE (TextField, xmTextFieldWidgetClass)
VA_CREATE (ToggleButton, xmToggleButtonWidgetClass)
VA_CREATE (ToggleButtonGadget, xmToggleButtonGadgetClass)

} /* extern "C" */

/* default resource strings referenced by Xm.h macros */
char _XmSDEFAULT_FONT[] = "fixed";
char _XmSDEFAULT_BACKGROUND[] = "#c4c4c4";

/* ======================================================================
 * Accessors and Xme functions from the private-header shims
 * ====================================================================== */
extern "C" {

static QRect widget_geometry (Widget w)
{
  QWidget *q = mq_qw (w);
  if (!q) return QRect (mq_geti (w, XmNx), mq_geti (w, XmNy), mq_geti (w, XmNwidth), mq_geti (w, XmNheight));
  QSize s = q->isVisible () ? q->size () : q->sizeHint ().expandedTo (q->minimumSize ());
  return QRect (q->pos (), s);
}

Dimension _MqXtWidth (Widget w) { return widget_geometry (w).width (); }
Dimension _MqXtHeight (Widget w) { return widget_geometry (w).height (); }
Position _MqXtX (Widget w) { return widget_geometry (w).x (); }
Position _MqXtY (Widget w) { return widget_geometry (w).y (); }
Dimension _MqXtBorderWidth (Widget w) { return mq_find_res (w, XmNborderWidth) ? mq_geti (w, XmNborderWidth) : 0; }
Boolean _MqXtIsVisible (Widget w) { QWidget *q = mq_qw (w); return q && q->isVisible () && !q->visibleRegion ().isEmpty (); }

#undef XmeWarning
void XmeWarning (Widget w, char *message)
{
  fprintf (stderr, "Warning:\n    Name: %s\n    Class: %s\n    %s\n\n", w ? w->name.constData () : "", w ? w->klass->class_name : "", message ? message : "");
}

void XmeDrawShadows (Display *d, Drawable dr, GC top, GC bottom, Position x, Position y, Dimension width, Dimension height,
                     Dimension st, unsigned int type)
{
  if (!st || width < 2 * st || height < 2 * st) return;
  bool in = type == XmSHADOW_IN || type == XmSHADOW_ETCHED_IN;
  GC a = in ? bottom : top, b = in ? top : bottom;
  if (type == XmSHADOW_ETCHED_IN || type == XmSHADOW_ETCHED_OUT) {
    Dimension half = st / 2;
    XmeDrawShadows (d, dr, a, b, x, y, width, height, half ? half : 1, XmSHADOW_OUT);
    XmeDrawShadows (d, dr, b, a, x + half, y + half, width - 2 * half, height - 2 * half, half ? half : 1, XmSHADOW_OUT);
    return;
  }
  for (int i = 0; i < st; i++) {
    XDrawLine (d, dr, a, x + i, y + i, x + width - 1 - i, y + i);
    XDrawLine (d, dr, a, x + i, y + i, x + i, y + height - 1 - i);
    XDrawLine (d, dr, b, x + i, y + height - 1 - i, x + width - 1 - i, y + height - 1 - i);
    XDrawLine (d, dr, b, x + width - 1 - i, y + i, x + width - 1 - i, y + height - 1 - i);
  }
}

void XmeClearBorder (Display *d, Window w, Position x, Position y, Dimension width, Dimension height, Dimension st)
{
  if (!st) return;
  XClearArea (d, w, x, y, width, st, False);
  XClearArea (d, w, x, y + height - st, width, st, False);
  XClearArea (d, w, x, y, st, height, False);
  XClearArea (d, w, x + width - st, y, st, height, False);
}

void XmeDrawSeparator (Display *d, Drawable dr, GC top, GC bottom, GC sep, Position x, Position y, Dimension width,
                       Dimension height, Dimension st, Dimension margin, unsigned char orientation, unsigned char type)
{
  if (type == XmNO_LINE) return;
  bool v = orientation == XmVERTICAL;
  int c = v ? x + width / 2 : y + height / 2;
  if (type == XmSINGLE_LINE || type == XmDOUBLE_LINE || type == XmSINGLE_DASHED_LINE || type == XmDOUBLE_DASHED_LINE) {
    if (v) XDrawLine (d, dr, sep, c, y + margin, c, y + height - margin);
    else XDrawLine (d, dr, sep, x + margin, c, x + width - margin, c);
    return;
  }
  bool in = type == XmSHADOW_ETCHED_IN || type == XmSHADOW_ETCHED_IN_DASH;
  if (v) { XDrawLine (d, dr, in ? bottom : top, c - 1, y + margin, c - 1, y + height - margin); XDrawLine (d, dr, in ? top : bottom, c, y + margin, c, y + height - margin); }
  else { XDrawLine (d, dr, in ? bottom : top, x + margin, c - 1, x + width - margin, c - 1); XDrawLine (d, dr, in ? top : bottom, x + margin, c, x + width - margin, c); }
  Q_UNUSED (st);
}

void XmeDrawHighlight (Display *d, Drawable dr, GC gc, Position x, Position y, Dimension width, Dimension height, Dimension ht)
{
  for (int i = 0; i < ht; i++) XDrawRectangle (d, dr, gc, x + i, y + i, width - 1 - 2 * i, height - 1 - 2 * i);
}

} /* extern "C" */

/* anchors the first symbol version node (see motif-on-qt.map) */
extern "C" { int _MqVersionAnchor = 0; }
