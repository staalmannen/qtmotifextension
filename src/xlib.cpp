/*
 * Motif on Qt - the part of Xlib Motif programs use, without an X server.
 *
 * A Display is a single fake connection. Windows and pixmaps are ids in a
 * registry: a widget's window draws into a backing QImage that its QWidget
 * paints; pixmaps are QImages. GCs hold X graphics state and drawing calls
 * become QPainter operations with X's pixel conventions (no antialiasing,
 * outlines one pixel larger than the rectangle, arcs in 1/64 degrees).
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"

#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>

#include <QScreen>
#include <QPainterPath>
#include <QFontDatabase>
#include <cstring>
#include <cstdlib>
#include <type_traits>

/* ======================================================================
 * Display, screen, visual
 * ====================================================================== */
typedef std::remove_pointer_t<_XPrivDisplay> PrivDisplay;
Display *mq_display = nullptr;
static Screen s_screen;
static Visual s_visual;
static Depth s_depths[2];
static GC s_default_gc;

struct _XGC {
  XGCValues v;
  QVector<qreal> dashes;
  QList<QRect> clip_rects;
  bool has_clip_rects = false;
};

enum { ROOT_WINDOW = 1, DEFAULT_COLORMAP = 2 };

static XID s_next_id = 0x100;
static XID alloc_id () { return s_next_id++; }

void mq_xlib_init ()
{
  if (mq_display) return;
  auto *d = new PrivDisplay;
  memset (d, 0, sizeof (*d));
  d->fd = -1;
  d->proto_major_version = 11;
  d->proto_minor_version = 0;
  d->vendor = (char *) "Motif on Qt";
  d->release = 1;
  d->byte_order = LSBFirst;
  d->bitmap_unit = 32;
  d->bitmap_pad = 32;
  d->bitmap_bit_order = LSBFirst;
  d->display_name = (char *) ":qt";
  d->default_screen = 0;
  d->nscreens = 1;
  d->screens = &s_screen;
  d->min_keycode = 8;
  d->max_keycode = 255;
  mq_display = (Display *) d;

  s_visual.visualid = 0x21;
  s_visual.c_class = TrueColor;
  s_visual.red_mask = 0xff0000;
  s_visual.green_mask = 0x00ff00;
  s_visual.blue_mask = 0x0000ff;
  s_visual.bits_per_rgb = 8;
  s_visual.map_entries = 256;
  s_depths[0].depth = 24;
  s_depths[0].nvisuals = 1;
  s_depths[0].visuals = &s_visual;
  s_depths[1].depth = 1;

  QRect geo = QGuiApplication::primaryScreen () ? QGuiApplication::primaryScreen ()->geometry () : QRect (0, 0, 1280, 1024);
  s_screen.display = mq_display;
  s_screen.root = ROOT_WINDOW;
  s_screen.width = geo.width ();
  s_screen.height = geo.height ();
  s_screen.mwidth = geo.width () * 254 / 960;
  s_screen.mheight = geo.height () * 254 / 960;
  s_screen.ndepths = 2;
  s_screen.depths = s_depths;
  s_screen.root_depth = 24;
  s_screen.root_visual = &s_visual;
  s_screen.cmap = DEFAULT_COLORMAP;
  s_screen.white_pixel = 0xffffff;
  s_screen.black_pixel = 0x000000;
  s_screen.max_maps = s_screen.min_maps = 1;
  s_screen.backing_store = WhenMapped;
  s_screen.save_unders = False;
  s_default_gc = new _XGC;
  memset (&s_default_gc->v, 0, sizeof (XGCValues));
  s_default_gc->v.function = GXcopy;
  s_default_gc->v.foreground = 0;
  s_default_gc->v.background = 0xffffff;
  s_default_gc->v.line_width = 0;
  s_default_gc->v.plane_mask = ~0UL;
  s_screen.default_gc = s_default_gc;
}

/* ======================================================================
 * Window / pixmap registry
 * ====================================================================== */
class MqCanvas;

struct DrawableRec {
  Widget widget = nullptr;       /* windows that belong to widgets */
  QPointer<QWidget> qw;          /* windows created with XCreateWindow */
  QImage image;                  /* backing store / pixmap contents */
  int depth = 24;
  bool pixmap = false;
  unsigned long background = 0xd9d9d9;
  QHash<Atom, QByteArray> props;
};

static QHash<XID, DrawableRec> &drawables ()
{
  static QHash<XID, DrawableRec> h;
  return h;
}

Window mq_window_for (Widget w)
{
  if (!w) return 0;
  if (!w->window) {
    while (w && !mq_qw (w) && w->parent) w = w->parent;   /* gadgets use their parent's window */
    if (!w) return 0;
    if (w->window) return w->window;
    w->window = alloc_id ();
    DrawableRec &r = drawables ()[w->window];
    r.widget = w;
    r.qw = mq_qw (w);
  }
  return w->window;
}

Widget mq_widget_for_window (Window win)
{
  auto it = drawables ().constFind (win);
  return it != drawables ().constEnd () ? it.value ().widget : nullptr;
}

unsigned long mq_pixel (const QColor &c) { return ((unsigned long) c.red () << 16) | (c.green () << 8) | c.blue (); }
QColor mq_color (unsigned long p) { return QColor ((p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff); }

static QWidget *window_widget (Window win)
{
  auto it = drawables ().find (win);
  if (it == drawables ().end ()) return nullptr;
  if (it->widget) return mq_qw (it->widget);
  return it->qw.data ();
}

QImage *mq_window_image (Window win, bool create)
{
  auto it = drawables ().find (win);
  if (it == drawables ().end ()) return nullptr;
  DrawableRec &r = it.value ();
  if (r.pixmap) return &r.image;
  QWidget *qw = window_widget (win);
  QSize want = qw ? qw->size ().expandedTo (QSize (1, 1)) : QSize (1, 1);
  if (r.widget && mq_isset (r.widget, XmNwidth) && !qw->isVisible ())
    want = QSize (qMax (1, mq_geti (r.widget, XmNwidth)), qMax (1, mq_geti (r.widget, XmNheight)));
  if (r.image.isNull () && create) {
    r.image = QImage (want, QImage::Format_RGB32);
    QColor bg = qw ? qw->palette ().color (QPalette::Window) : QColor (0xd9, 0xd9, 0xd9);
    if (r.widget && mq_isset (r.widget, XmNbackground)) bg = mq_color (mq_get (r.widget, XmNbackground));
    r.image.fill (bg);
  }
  return r.image.isNull () ? nullptr : &r.image;
}

void mq_window_resize (Window win, int w, int h)
{
  auto it = drawables ().find (win);
  if (it == drawables ().end () || it->pixmap) return;
  QImage old = it->image;
  QImage img (QSize (qMax (1, w), qMax (1, h)), QImage::Format_RGB32);
  QWidget *qw = window_widget (win);
  QColor bg = qw ? qw->palette ().color (QPalette::Window) : QColor (0xd9, 0xd9, 0xd9);
  if (it->widget && mq_isset (it->widget, XmNbackground)) bg = mq_color (mq_get (it->widget, XmNbackground));
  img.fill (bg);
  if (!old.isNull ()) { QPainter p (&img); p.drawImage (0, 0, old); }   /* NorthWest bit gravity */
  it->image = img;
}

void mq_window_flush (Window win)
{
  if (QWidget *qw = window_widget (win)) qw->update ();
}

static QImage *target (Drawable d)
{
  QImage *img = mq_window_image (d, true);
  return img;
}

QPixmap mq_pixmap_qt (Pixmap p)
{
  auto it = drawables ().constFind (p);
  if (it == drawables ().constEnd () || !it->pixmap) return QPixmap ();
  return QPixmap::fromImage (it->image);
}

static Pixmap new_pixmap (const QImage &img, int depth)
{
  Pixmap id = alloc_id ();
  DrawableRec &r = drawables ()[id];
  r.pixmap = true;
  r.image = img;
  r.depth = depth;
  return id;
}

Pixmap mq_pixmap_from_image (const QImage &img) { return new_pixmap (img.convertToFormat (QImage::Format_ARGB32), 24); }

/* ======================================================================
 * Painting with a GC
 * ====================================================================== */
static QColor gc_color (unsigned long pixel, int depth)
{
  if (depth == 1) return (pixel & 1) ? QColor (Qt::white) : QColor (Qt::black);
  return mq_color (pixel);
}

class GcPainter {
public:
  QPainter p;
  bool ok = false;
  Drawable d;
  GcPainter (Drawable dr, GC gc, bool fill) : d (dr)
  {
    QImage *img = target (dr);
    if (!img || !gc) return;
    ok = p.begin (img);
    if (!ok) return;
    int depth = drawables ().value (dr).depth;
    p.setRenderHint (QPainter::Antialiasing, false);
    QColor fg = gc_color (gc->v.foreground, depth);
    switch (gc->v.function) {
    case GXxor: p.setCompositionMode (QPainter::RasterOp_SourceXorDestination); break;
    case GXinvert: p.setCompositionMode (QPainter::RasterOp_NotDestination); break;
    case GXand: p.setCompositionMode (QPainter::RasterOp_SourceAndDestination); break;
    case GXor: p.setCompositionMode (QPainter::RasterOp_SourceOrDestination); break;
    case GXclear: fg = Qt::black; break;
    case GXset: fg = Qt::white; break;
    default: break;
    }
    QPen pen (fg);
    pen.setWidth (gc->v.line_width);
    pen.setCosmetic (gc->v.line_width == 0);
    pen.setCapStyle (gc->v.cap_style == CapRound ? Qt::RoundCap : gc->v.cap_style == CapProjecting ? Qt::SquareCap : Qt::FlatCap);
    pen.setJoinStyle (gc->v.join_style == JoinRound ? Qt::RoundJoin : gc->v.join_style == JoinBevel ? Qt::BevelJoin : Qt::MiterJoin);
    if (gc->v.line_style != LineSolid) {
      QVector<qreal> pattern = gc->dashes;
      if (pattern.isEmpty ()) pattern = { 4, 4 };
      qreal w = qMax (1, gc->v.line_width);
      for (qreal &x : pattern) x /= w;
      pen.setDashPattern (pattern);
    }
    p.setPen (fill ? QPen (Qt::NoPen) : pen);
    QBrush brush (fg);
    if ((gc->v.fill_style == FillTiled) && gc->v.tile) brush.setTextureImage (drawables ().value (gc->v.tile).image);
    else if ((gc->v.fill_style == FillStippled || gc->v.fill_style == FillOpaqueStippled) && gc->v.stipple) {
      QImage st = drawables ().value (gc->v.stipple).image;
      QBitmap mask = QBitmap::fromImage (st.createMaskFromColor (QColor (Qt::black).rgb (), Qt::MaskOutColor));
      brush = QBrush (fg, mask);
    }
    p.setBrush (fill ? brush : QBrush (Qt::NoBrush));
    if (gc->has_clip_rects && !gc->clip_rects.isEmpty ()) {
      QRegion r;
      for (const QRect &cr : gc->clip_rects) r += cr.translated (gc->v.clip_x_origin, gc->v.clip_y_origin);
      p.setClipRegion (r);
    } else if (gc->v.clip_mask && gc->v.clip_mask != None) {
      QImage m = drawables ().value (gc->v.clip_mask).image;
      if (!m.isNull ()) {
        QBitmap bm = QBitmap::fromImage (m.createMaskFromColor (QColor (Qt::black).rgb (), Qt::MaskOutColor));
        p.setClipRegion (QRegion (bm).translated (gc->v.clip_x_origin, gc->v.clip_y_origin));
      }
    }
  }
  ~GcPainter ()
  {
    if (ok) p.end ();
    mq_window_flush (d);
  }
};


/* XmStringDraw: lines of text in a box starting at (x, y) = top-left */
void mq_draw_text_lines (Drawable d, GC gc, const QFont &f, int x, int y, int width, int align,
                         const QStringList &lines, bool image, int ul_line, int ul_from, int ul_len)
{
  GcPainter g (d, gc, false);
  if (!g.ok) return;
  g.p.setFont (f);
  QFontMetrics fm (f);
  int ly = y;
  for (int i = 0; i < lines.size (); i++) {
    const QString &t = lines[i];
    int tw = fm.horizontalAdvance (t);
    int tx = x;
    if (align == XmALIGNMENT_CENTER) tx = x + (width - tw) / 2;
    else if (align == XmALIGNMENT_END) tx = x + width - tw;
    if (image) g.p.fillRect (tx, ly, tw, fm.height (), gc_color (gc->v.background, drawables ().value (d).depth));
    g.p.drawText (tx, ly + fm.ascent (), t);
    if (i == ul_line) {
      int ux = tx + fm.horizontalAdvance (t.left (ul_from));
      int uw = fm.horizontalAdvance (t.mid (ul_from, ul_len));
      g.p.drawLine (ux, ly + fm.ascent () + 1, ux + uw - 1, ly + fm.ascent () + 1);
    }
    ly += fm.height ();
  }
}

/* ======================================================================
 * Colours
 * ====================================================================== */
static bool parse_color (const char *spec, QColor *out)
{
  if (!spec) return false;
  QString s = QString::fromLatin1 (spec).trimmed ();
  if (s.startsWith ("rgb:", Qt::CaseInsensitive)) {
    QStringList parts = s.mid (4).split ('/');
    if (parts.size () != 3) return false;
    auto comp = [] (const QString &h) { bool ok; uint v = h.toUInt (&ok, 16); uint max = (1u << (4 * h.size ())) - 1; return ok ? v * 255 / max : 0; };
    *out = QColor (comp (parts[0]), comp (parts[1]), comp (parts[2]));
    return true;
  }
  if (s.startsWith ('#')) {
    QString h = s.mid (1);
    int n = h.size () / 3;
    if (n < 1 || n > 4 || h.size () % 3) return false;
    auto comp = [n] (const QString &x) { bool ok; uint v = x.toUInt (&ok, 16); uint max = (1u << (4 * n)) - 1; return v * 255 / max; };
    *out = QColor (comp (h.mid (0, n)), comp (h.mid (n, n)), comp (h.mid (2 * n, n)));
    return true;
  }
  QString name = s.toLower ().remove (' ');
  static const QRegularExpression grey ("^gr[ae]y(\\d+)$");
  auto m = grey.match (name);
  if (m.hasMatch ()) { int v = m.captured (1).toInt () * 255 / 100; *out = QColor (v, v, v); return true; }
  static const QRegularExpression numbered ("^([a-z]+)([1-4])$");   /* X11 "red3" style shades */
  m = numbered.match (name);
  if (m.hasMatch ()) {
    QColor base = QColor::fromString (m.captured (1));
    if (base.isValid ()) {
      static const int factor[] = { 100, 93, 80, 55 };
      int f = factor[m.captured (2).toInt () - 1];
      *out = QColor (base.red () * f / 100, base.green () * f / 100, base.blue () * f / 100);
      return true;
    }
  }
  if (name == "navyblue") name = "navy";
  QColor c = QColor::fromString (name);
  if (!c.isValid ()) return false;
  *out = c;
  return true;
}

/* ======================================================================
 * Fonts
 * ====================================================================== */
static QHash<Font, QFont> &fonts ()
{
  static QHash<Font, QFont> h;
  return h;
}

QFont mq_font_from_xlfd (const QString &name, bool *ok)
{
  QString n = name.trimmed ();
  QFont f = QFontDatabase::systemFont (QFontDatabase::GeneralFont);
  *ok = true;
  QRegularExpression cell ("^(\\d+)x(\\d+)(bold)?$");
  auto m = cell.match (n);
  if (n == "fixed" || m.hasMatch ()) {
    f = QFontDatabase::systemFont (QFontDatabase::FixedFont);
    f.setPixelSize (m.hasMatch () ? m.captured (2).toInt () - 2 : 11);
    if (m.hasMatch () && !m.captured (3).isEmpty ()) f.setBold (true);
    return f;
  }
  if (n == "variable") { f.setPixelSize (12); return f; }
  QStringList parts = n.split ('-');
  if (n.startsWith ('-') && parts.size () >= 15) {
    QString family = parts[2].toLower ();
    if (family == "courier" || family == "fixed" || family.contains ("mono") || family == "lucidatypewriter" || family == "terminal")
      f = QFontDatabase::systemFont (QFontDatabase::FixedFont);
    else if (family == "times" || family.contains ("serif") || family == "new century schoolbook" || family == "charter")
      f.setFamilies ({ "Serif", "DejaVu Serif", "Times New Roman" });
    else if (family != "*")
      f.setFamilies ({ parts[2], "Sans Serif" });
    QString weight = parts[3].toLower ();
    if (weight == "bold" || weight == "demibold" || weight == "black") f.setBold (true);
    QString slant = parts[4].toLower ();
    if (slant == "i" || slant == "o") f.setItalic (true);
    bool pok;
    int pixel = parts[7].toInt (&pok);
    if (pok && pixel > 0) f.setPixelSize (pixel);
    else {
      int point = parts[8].toInt (&pok);
      if (pok && point > 0) f.setPointSizeF (point / 10.0);
    }
    return f;
  }
  if (n.contains ('*') || n.contains ('-')) return f;
  /* fontconfig-style "family-size" or bare family */
  QRegularExpression fs ("^(.+?)[- ](\\d+(?:\\.\\d+)?)$");
  m = fs.match (n);
  if (m.hasMatch ()) { f.setFamilies ({ m.captured (1) }); f.setPointSizeF (m.captured (2).toDouble ()); }
  else f.setFamilies ({ n });
  return f;
}

static XFontStruct *make_font_struct (Font fid, const QFont &f)
{
  QFontMetrics fm (f);
  auto *fs = (XFontStruct *) calloc (1, sizeof (XFontStruct));
  fs->fid = fid;
  fs->direction = FontLeftToRight;
  fs->min_char_or_byte2 = 0x20;
  fs->max_char_or_byte2 = 0xff;
  fs->default_char = ' ';
  fs->all_chars_exist = True;
  fs->ascent = fm.ascent ();
  fs->descent = fm.descent ();
  fs->max_bounds.width = fm.maxWidth ();
  fs->max_bounds.ascent = fm.ascent ();
  fs->max_bounds.descent = fm.descent ();
  fs->max_bounds.rbearing = fm.maxWidth ();
  fs->min_bounds.width = fm.horizontalAdvance ('i');
  fs->min_bounds.ascent = fm.ascent ();
  fs->min_bounds.descent = fm.descent ();
  return fs;
}

QFont mq_font_for (Font fid)
{
  auto it = fonts ().constFind (fid);
  return it != fonts ().constEnd () ? it.value () : QFontDatabase::systemFont (QFontDatabase::GeneralFont);
}

/* ======================================================================
 * Keysyms
 * ====================================================================== */
static QHash<QByteArray, KeySym> &keysym_names ()
{
  static QHash<QByteArray, KeySym> h;
  if (h.isEmpty ()) {
#define KS(n, v) h.insert (n, v);
#include "generated/keysyms.inc"
#undef KS
  }
  return h;
}

extern "C" {

/* ---------- display / screen ---------- */
Display *XOpenDisplay (_Xconst char *) { mq_ensure_app (); return mq_display; }
int XCloseDisplay (Display *) { return 0; }
char *XDisplayName (_Xconst char *s) { return (char *) (s ? s : ":qt"); }
int XDefaultScreen (Display *) { return 0; }
Screen *XDefaultScreenOfDisplay (Display *) { return &s_screen; }
Screen *XScreenOfDisplay (Display *, int) { return &s_screen; }
Display *XDisplayOfScreen (Screen *) { return mq_display; }
int XScreenNumberOfScreen (Screen *) { return 0; }
int XScreenCount (Display *) { return 1; }
Window XDefaultRootWindow (Display *) { return ROOT_WINDOW; }
Window XRootWindow (Display *, int) { return ROOT_WINDOW; }
Window XRootWindowOfScreen (Screen *) { return ROOT_WINDOW; }
int XDisplayWidth (Display *, int) { return s_screen.width; }
int XDisplayHeight (Display *, int) { return s_screen.height; }
int XDisplayWidthMM (Display *, int) { return s_screen.mwidth; }
int XDisplayHeightMM (Display *, int) { return s_screen.mheight; }
int XWidthOfScreen (Screen *) { return s_screen.width; }
int XHeightOfScreen (Screen *) { return s_screen.height; }
int XWidthMMOfScreen (Screen *) { return s_screen.mwidth; }
int XHeightMMOfScreen (Screen *) { return s_screen.mheight; }
int XDefaultDepth (Display *, int) { return 24; }
int XDefaultDepthOfScreen (Screen *) { return 24; }
int XDisplayPlanes (Display *, int) { return 24; }
int XPlanesOfScreen (Screen *) { return 24; }
int XDisplayCells (Display *, int) { return 256; }
int XCellsOfScreen (Screen *) { return 256; }
Visual *XDefaultVisual (Display *, int) { return &s_visual; }
Visual *XDefaultVisualOfScreen (Screen *) { return &s_visual; }
Colormap XDefaultColormap (Display *, int) { return DEFAULT_COLORMAP; }
Colormap XDefaultColormapOfScreen (Screen *) { return DEFAULT_COLORMAP; }
unsigned long XBlackPixel (Display *, int) { return 0; }
unsigned long XWhitePixel (Display *, int) { return 0xffffff; }
unsigned long XBlackPixelOfScreen (Screen *) { return 0; }
unsigned long XWhitePixelOfScreen (Screen *) { return 0xffffff; }
GC XDefaultGC (Display *, int) { return s_default_gc; }
GC XDefaultGCOfScreen (Screen *) { return s_default_gc; }
int XConnectionNumber (Display *) { return -1; }
char *XServerVendor (Display *) { return (char *) "Motif on Qt"; }
int XVendorRelease (Display *) { return 1; }
int XProtocolVersion (Display *) { return 11; }
int XProtocolRevision (Display *) { return 0; }
long XMaxRequestSize (Display *) { return 65535; }
VisualID XVisualIDFromVisual (Visual *v) { return v ? v->visualid : 0; }
int XFlush (Display *) { return 0; }
int XSync (Display *, Bool) { QCoreApplication::processEvents (); return 0; }
int XPending (Display *) { return 0; }
int XEventsQueued (Display *, int) { return 0; }
int XBell (Display *, int) { QApplication::beep (); return 0; }
int XFree (void *data) { free (data); return 1; }
int XNoOp (Display *) { return 0; }
int (*XSynchronize (Display *, Bool)) (Display *) { return nullptr; }
int (*XSetErrorHandler (XErrorHandler)) (Display *, XErrorEvent *) { return nullptr; }
int (*XSetIOErrorHandler (XIOErrorHandler)) (Display *) { return nullptr; }

int *XListDepths (Display *, int, int *count)
{
  int *d = (int *) malloc (2 * sizeof (int));
  d[0] = 24; d[1] = 1;
  if (count) *count = 2;
  return d;
}

XVisualInfo *XGetVisualInfo (Display *, long, XVisualInfo *, int *n)
{
  auto *vi = (XVisualInfo *) calloc (1, sizeof (XVisualInfo));
  vi->visual = &s_visual;
  vi->visualid = s_visual.visualid;
  vi->depth = 24;
  vi->c_class = TrueColor;
  vi->red_mask = 0xff0000; vi->green_mask = 0xff00; vi->blue_mask = 0xff;
  vi->colormap_size = 256;
  vi->bits_per_rgb = 8;
  if (n) *n = 1;
  return vi;
}

Status XMatchVisualInfo (Display *, int, int depth, int c_class, XVisualInfo *vi)
{
  if (depth != 24 || (c_class != TrueColor && c_class != DirectColor)) return 0;
  memset (vi, 0, sizeof (*vi));
  vi->visual = &s_visual; vi->visualid = s_visual.visualid; vi->depth = 24; vi->c_class = TrueColor;
  vi->red_mask = 0xff0000; vi->green_mask = 0xff00; vi->blue_mask = 0xff; vi->colormap_size = 256; vi->bits_per_rgb = 8;
  return 1;
}

/* ---------- atoms ---------- */
static QHash<QByteArray, Atom> &atoms ()
{
  static QHash<QByteArray, Atom> h;
  if (h.isEmpty ()) {
    static const char *predefined[] = { nullptr, "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL", "COLORMAP",
      "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2", "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6",
      "CUT_BUFFER7", "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP",
      "RGB_BEST_MAP", "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING",
      "VISUALID", "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE", "WM_NAME",
      "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS", "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE",
      "SUPERSCRIPT_X", "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y", "UNDERLINE_POSITION", "UNDERLINE_THICKNESS",
      "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT", "POINT_SIZE",
      "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME", "FULL_NAME", "CAP_HEIGHT", "WM_CLASS",
      "WM_TRANSIENT_FOR" };
    for (int i = 1; i < (int) (sizeof predefined / sizeof *predefined); i++) h.insert (predefined[i], i);
  }
  return h;
}

Atom XInternAtom (Display *, _Xconst char *name, Bool only_if_exists)
{
  if (!name) return None;
  auto it = atoms ().constFind (name);
  if (it != atoms ().constEnd ()) return it.value ();
  if (only_if_exists) return None;
  Atom a = 1000 + atoms ().size ();
  atoms ().insert (name, a);
  return a;
}

Status XInternAtoms (Display *d, char **names, int count, Bool only_if_exists, Atom *out)
{
  for (int i = 0; i < count; i++) out[i] = XInternAtom (d, names[i], only_if_exists);
  return 1;
}

char *XGetAtomName (Display *, Atom atom)
{
  for (auto it = atoms ().constBegin (); it != atoms ().constEnd (); ++it)
    if (it.value () == atom) return strdup (it.key ().constData ());
  return nullptr;
}

/* ---------- windows ---------- */
Window XCreateSimpleWindow (Display *, Window parent, int x, int y, unsigned int w, unsigned int h, unsigned int, unsigned long, unsigned long background)
{
  Window id = alloc_id ();
  DrawableRec &r = drawables ()[id];
  QWidget *pw = window_widget (parent);
  auto *qw = new QWidget (pw);
  qw->setGeometry (x, y, w, h);
  r.qw = qw;
  r.background = background;
  return id;
}

Window XCreateWindow (Display *d, Window parent, int x, int y, unsigned int w, unsigned int h, unsigned int bw, int, unsigned int,
                      Visual *, unsigned long, XSetWindowAttributes *attrs)
{
  return XCreateSimpleWindow (d, parent, x, y, w, h, bw, 0, attrs ? attrs->background_pixel : 0xffffff);
}

int XDestroyWindow (Display *, Window w)
{
  auto it = drawables ().find (w);
  if (it == drawables ().end ()) return 0;
  if (!it->widget && it->qw) it->qw->deleteLater ();
  drawables ().erase (it);
  return 1;
}

int XMapWindow (Display *, Window w)
{
  if (Widget wd = mq_widget_for_window (w)) {
    if (wd->klass->kind & K_SHELL) mq_popup_shell (wd, true);
    else if (QWidget *qw = mq_qw (wd)) qw->show ();
  } else if (QWidget *qw = window_widget (w)) qw->show ();
  return 1;
}

int XMapRaised (Display *d, Window w) { XMapWindow (d, w); if (QWidget *qw = window_widget (w)) qw->raise (); return 1; }
int XMapSubwindows (Display *, Window) { return 1; }

int XUnmapWindow (Display *, Window w)
{
  if (QWidget *qw = window_widget (w)) qw->hide ();
  return 1;
}

int XRaiseWindow (Display *, Window w) { if (QWidget *qw = window_widget (w)) qw->raise (); return 1; }
int XLowerWindow (Display *, Window w) { if (QWidget *qw = window_widget (w)) qw->lower (); return 1; }
int XIconifyWindow (Display *, Window w, int) { if (QWidget *qw = window_widget (w)) qw->window ()->showMinimized (); return 1; }
int XMoveWindow (Display *, Window w, int x, int y) { if (QWidget *qw = window_widget (w)) qw->move (x, y); return 1; }
int XResizeWindow (Display *, Window w, unsigned int width, unsigned int height) { if (QWidget *qw = window_widget (w)) qw->resize (width, height); return 1; }
int XMoveResizeWindow (Display *, Window w, int x, int y, unsigned int width, unsigned int height)
{
  if (QWidget *qw = window_widget (w)) qw->setGeometry (x, y, width, height);
  return 1;
}

int XClearArea (Display *, Window w, int x, int y, unsigned int width, unsigned int height, Bool exposures)
{
  QImage *img = mq_window_image (w, true);
  if (!img) return 0;
  QWidget *qw = window_widget (w);
  if (!width) width = img->width () - x;
  if (!height) height = img->height () - y;
  QColor bg = qw ? qw->palette ().color (QPalette::Window) : QColor (0xd9, 0xd9, 0xd9);
  Widget wd = mq_widget_for_window (w);
  if (wd && mq_isset (wd, XmNbackground)) bg = mq_color (mq_get (wd, XmNbackground));
  {
    QPainter p (img);
    p.fillRect (x, y, width, height, bg);
  }
  if (exposures && qw) {
    QVariant pending = qw->property ("_mq_expose");
    QRegion r = pending.isValid () ? pending.value<QRegion> () : QRegion ();
    r += QRect (x, y, width, height);
    qw->setProperty ("_mq_expose", QVariant::fromValue (r));
  }
  mq_window_flush (w);
  return 1;
}

int XClearWindow (Display *d, Window w) { return XClearArea (d, w, 0, 0, 0, 0, False); }

int XSetWindowBackground (Display *, Window w, unsigned long pixel)
{
  if (Widget wd = mq_widget_for_window (w)) XtVaSetValues (wd, XmNbackground, (XtArgVal) pixel, NULL);
  drawables ()[w].background = pixel;
  return 1;
}

int XSetWindowBackgroundPixmap (Display *, Window, Pixmap) { return 1; }
int XSetWindowBorder (Display *, Window, unsigned long) { return 1; }
int XSetWindowBorderWidth (Display *, Window, unsigned int) { return 1; }

int XChangeWindowAttributes (Display *d, Window w, unsigned long mask, XSetWindowAttributes *a)
{
  if (mask & CWBackPixel) XSetWindowBackground (d, w, a->background_pixel);
  if (mask & CWCursor) XDefineCursor (d, w, a->cursor);
  return 1;
}

Status XGetWindowAttributes (Display *, Window w, XWindowAttributes *a)
{
  memset (a, 0, sizeof (*a));
  QWidget *qw = window_widget (w);
  a->depth = 24;
  a->visual = &s_visual;
  a->root = ROOT_WINDOW;
  a->colormap = DEFAULT_COLORMAP;
  a->screen = &s_screen;
  a->c_class = InputOutput;
  a->bit_gravity = NorthWestGravity;
  if (w == ROOT_WINDOW) { a->width = s_screen.width; a->height = s_screen.height; a->map_state = IsViewable; return 1; }
  if (!qw) return 0;
  a->x = qw->x (); a->y = qw->y (); a->width = qw->width (); a->height = qw->height ();
  a->map_state = qw->isVisible () ? IsViewable : IsUnmapped;
  return 1;
}

Status XGetGeometry (Display *, Drawable d, Window *root, int *x, int *y, unsigned int *w, unsigned int *h, unsigned int *bw, unsigned int *depth)
{
  if (root) *root = ROOT_WINDOW;
  if (bw) *bw = 0;
  auto it = drawables ().constFind (d);
  int dx = 0, dy = 0, dw = 1, dh = 1, dd = 24;
  if (d == ROOT_WINDOW) { dw = s_screen.width; dh = s_screen.height; }
  else if (it != drawables ().constEnd ()) {
    if (it->pixmap) { dw = it->image.width (); dh = it->image.height (); dd = it->depth; }
    else if (QWidget *qw = window_widget (d)) { dx = qw->x (); dy = qw->y (); dw = qw->width (); dh = qw->height (); }
  } else return 0;
  if (x) *x = dx; if (y) *y = dy; if (w) *w = dw; if (h) *h = dh; if (depth) *depth = dd;
  return 1;
}

Bool XTranslateCoordinates (Display *, Window src, Window dst, int sx, int sy, int *dx, int *dy, Window *child)
{
  QWidget *a = window_widget (src), *b = window_widget (dst);
  QPoint g = a ? a->mapToGlobal (QPoint (sx, sy)) : QPoint (sx, sy);
  QPoint r = b ? b->mapFromGlobal (g) : g;
  if (dx) *dx = r.x ();
  if (dy) *dy = r.y ();
  if (child) *child = None;
  return True;
}

Bool XQueryPointer (Display *, Window w, Window *root, Window *child, int *rx, int *ry, int *wx, int *wy, unsigned int *mask)
{
  QPoint g = QCursor::pos ();
  QWidget *qw = window_widget (w);
  QPoint l = qw ? qw->mapFromGlobal (g) : g;
  if (root) *root = ROOT_WINDOW;
  if (child) *child = None;
  if (rx) *rx = g.x (); if (ry) *ry = g.y (); if (wx) *wx = l.x (); if (wy) *wy = l.y ();
  if (mask) {
    unsigned m = 0;
    Qt::MouseButtons b = QGuiApplication::mouseButtons ();
    Qt::KeyboardModifiers k = QGuiApplication::keyboardModifiers ();
    if (b & Qt::LeftButton) m |= Button1Mask;
    if (b & Qt::MiddleButton) m |= Button2Mask;
    if (b & Qt::RightButton) m |= Button3Mask;
    if (k & Qt::ShiftModifier) m |= ShiftMask;
    if (k & Qt::ControlModifier) m |= ControlMask;
    *mask = m;
  }
  return True;
}

int XWarpPointer (Display *, Window, Window dst, int, int, unsigned int, unsigned int, int x, int y)
{
  QWidget *qw = window_widget (dst);
  QCursor::setPos (qw ? qw->mapToGlobal (QPoint (x, y)) : QPoint (x, y));
  return 1;
}

int XGrabPointer (Display *, Window, Bool, unsigned int, int, int, Window, Cursor, Time) { return GrabSuccess; }
int XUngrabPointer (Display *, Time) { return 1; }
int XGrabKeyboard (Display *, Window, Bool, int, int, Time) { return GrabSuccess; }
int XUngrabKeyboard (Display *, Time) { return 1; }
int XGrabServer (Display *) { return 1; }
int XUngrabServer (Display *) { return 1; }
int XSetInputFocus (Display *, Window w, int, Time) { if (QWidget *qw = window_widget (w)) qw->setFocus (); return 1; }
int XSelectInput (Display *, Window w, long mask)
{
  if (QWidget *qw = window_widget (w)) qw->setMouseTracking (mask & PointerMotionMask);
  return 1;
}

int XStoreName (Display *, Window w, _Xconst char *name)
{
  if (QWidget *qw = window_widget (w)) qw->window ()->setWindowTitle (QString::fromLocal8Bit (name));
  return 1;
}

Status XFetchName (Display *, Window w, char **name)
{
  QWidget *qw = window_widget (w);
  *name = qw ? strdup (qw->window ()->windowTitle ().toLocal8Bit ().constData ()) : nullptr;
  return qw != nullptr;
}

int XSetIconName (Display *, Window, _Xconst char *) { return 1; }
Status XSetWMProtocols (Display *, Window, Atom *, int) { return 1; }
int XSetTransientForHint (Display *, Window, Window) { return 1; }
void XSetWMNormalHints (Display *, Window, XSizeHints *) {}
int XSetNormalHints (Display *, Window, XSizeHints *) { return 1; }
int XSetWMHints (Display *, Window, XWMHints *) { return 1; }
XSizeHints *XAllocSizeHints (void) { return (XSizeHints *) calloc (1, sizeof (XSizeHints)); }
XWMHints *XAllocWMHints (void) { return (XWMHints *) calloc (1, sizeof (XWMHints)); }
XClassHint *XAllocClassHint (void) { return (XClassHint *) calloc (1, sizeof (XClassHint)); }
int XSetClassHint (Display *, Window, XClassHint *) { return 1; }

int XChangeProperty (Display *, Window w, Atom property, Atom, int format, int mode, _Xconst unsigned char *data, int n)
{
  int bytes = n * (format == 32 ? sizeof (long) : format / 8);
  QByteArray &v = drawables ()[w].props[property];
  if (mode == PropModeReplace) v = QByteArray ((const char *) data, bytes);
  else if (mode == PropModeAppend) v.append ((const char *) data, bytes);
  else v.prepend ((const char *) data, bytes);
  return 1;
}

int XGetWindowProperty (Display *, Window w, Atom property, long, long, Bool del, Atom, Atom *actual_type, int *actual_format,
                        unsigned long *nitems, unsigned long *bytes_after, unsigned char **prop)
{
  auto it = drawables ().find (w);
  *actual_type = None; *actual_format = 0; *nitems = 0; *bytes_after = 0; *prop = nullptr;
  if (it == drawables ().end () || !it->props.contains (property)) return Success;
  QByteArray v = it->props.value (property);
  *actual_type = XA_STRING;
  *actual_format = 8;
  *nitems = v.size ();
  *prop = (unsigned char *) malloc (v.size () + 1);
  memcpy (*prop, v.constData (), v.size ());
  (*prop)[v.size ()] = 0;
  if (del) it->props.remove (property);
  return Success;
}

int XDeleteProperty (Display *, Window w, Atom property) { drawables ()[w].props.remove (property); return 1; }
Status XSendEvent (Display *, Window, Bool, long, XEvent *) { return 1; }

Bool XCheckWindowEvent (Display *, Window, long, XEvent *) { return False; }
Bool XCheckMaskEvent (Display *, long, XEvent *) { return False; }
Bool XCheckTypedEvent (Display *, int, XEvent *) { return False; }
Bool XCheckTypedWindowEvent (Display *, Window, int, XEvent *) { return False; }
Bool XCheckIfEvent (Display *, XEvent *, Bool (*) (Display *, XEvent *, XPointer), XPointer) { return False; }
int XNextEvent (Display *, XEvent *e) { XtAppNextEvent (mq_app, e); return 0; }
int XPeekEvent (Display *, XEvent *e) { XtAppPeekEvent (mq_app, e); return 0; }
int XPutBackEvent (Display *, XEvent *) { return 0; }

/* ---------- cursors ---------- */
Cursor XCreateFontCursor (Display *, unsigned int shape)
{
  switch (shape) {
  case XC_watch: return 1 + Qt::WaitCursor;
  case XC_crosshair: case XC_cross: case XC_tcross: case XC_crosshair + 1: return 1 + Qt::CrossCursor;
  case XC_xterm: return 1 + Qt::IBeamCursor;
  case XC_hand1: case XC_hand2: return 1 + Qt::PointingHandCursor;
  case XC_fleur: return 1 + Qt::SizeAllCursor;
  case XC_sb_h_double_arrow: return 1 + Qt::SizeHorCursor;
  case XC_sb_v_double_arrow: return 1 + Qt::SizeVerCursor;
  case XC_question_arrow: return 1 + Qt::WhatsThisCursor;
  default: return 1 + Qt::ArrowCursor;
  }
}

Cursor XCreatePixmapCursor (Display *, Pixmap, Pixmap, XColor *, XColor *, unsigned int, unsigned int) { return 1 + Qt::ArrowCursor; }
Cursor XCreateGlyphCursor (Display *, Font, Font, unsigned int, unsigned int, XColor _Xconst *, XColor _Xconst *) { return 1 + Qt::ArrowCursor; }
int XFreeCursor (Display *, Cursor) { return 1; }

int XDefineCursor (Display *, Window w, Cursor c)
{
  if (QWidget *qw = window_widget (w)) {
    if (c) qw->setCursor (mq_qcursor ((int) c - 1)); else qw->unsetCursor ();
  }
  return 1;
}

int XUndefineCursor (Display *, Window w) { if (QWidget *qw = window_widget (w)) qw->unsetCursor (); return 1; }

/* ---------- colours ---------- */
Colormap XCreateColormap (Display *, Window, Visual *, int) { return alloc_id (); }
int XFreeColormap (Display *, Colormap) { return 1; }
Colormap XCopyColormapAndFree (Display *, Colormap) { return alloc_id (); }
int XInstallColormap (Display *, Colormap) { return 1; }
int XSetWindowColormap (Display *, Window, Colormap) { return 1; }

Status XParseColor (Display *, Colormap, _Xconst char *spec, XColor *def)
{
  QColor c;
  if (!parse_color (spec, &c)) return 0;
  def->red = c.red () * 257;
  def->green = c.green () * 257;
  def->blue = c.blue () * 257;
  def->flags = DoRed | DoGreen | DoBlue;
  def->pixel = mq_pixel (c);
  return 1;
}

Status XAllocColor (Display *, Colormap, XColor *def)
{
  def->pixel = ((unsigned long) (def->red >> 8) << 16) | ((def->green >> 8) << 8) | (def->blue >> 8);
  def->red = (def->red >> 8) * 257;
  def->green = (def->green >> 8) * 257;
  def->blue = (def->blue >> 8) * 257;
  return 1;
}

Status XLookupColor (Display *d, Colormap cm, _Xconst char *name, XColor *exact, XColor *screen)
{
  if (!XParseColor (d, cm, name, exact)) return 0;
  *screen = *exact;
  return 1;
}

Status XAllocNamedColor (Display *d, Colormap cm, _Xconst char *name, XColor *screen, XColor *exact)
{
  if (!XLookupColor (d, cm, name, exact, screen)) return 0;
  return XAllocColor (d, cm, screen);
}

int XQueryColor (Display *, Colormap, XColor *def)
{
  QColor c = mq_color (def->pixel);
  def->red = c.red () * 257;
  def->green = c.green () * 257;
  def->blue = c.blue () * 257;
  def->flags = DoRed | DoGreen | DoBlue;
  return 1;
}

int XQueryColors (Display *d, Colormap cm, XColor *defs, int n) { for (int i = 0; i < n; i++) XQueryColor (d, cm, &defs[i]); return 1; }
int XFreeColors (Display *, Colormap, unsigned long *, int, unsigned long) { return 1; }
Status XAllocColorCells (Display *, Colormap, Bool, unsigned long *, unsigned int, unsigned long *, unsigned int) { return 0; }
int XStoreColor (Display *, Colormap, XColor *) { return 1; }
int XStoreColors (Display *, Colormap, XColor *, int) { return 1; }
int XStoreNamedColor (Display *, Colormap, _Xconst char *, unsigned long, int) { return 1; }

/* ---------- GC ---------- */
static void gc_apply (GC gc, unsigned long mask, XGCValues *v)
{
  if (!v) return;
#define F(bit, field) if (mask & bit) gc->v.field = v->field;
  F (GCFunction, function) F (GCPlaneMask, plane_mask) F (GCForeground, foreground) F (GCBackground, background)
  F (GCLineWidth, line_width) F (GCLineStyle, line_style) F (GCCapStyle, cap_style) F (GCJoinStyle, join_style)
  F (GCFillStyle, fill_style) F (GCFillRule, fill_rule) F (GCTile, tile) F (GCStipple, stipple)
  F (GCTileStipXOrigin, ts_x_origin) F (GCTileStipYOrigin, ts_y_origin) F (GCFont, font)
  F (GCSubwindowMode, subwindow_mode) F (GCGraphicsExposures, graphics_exposures) F (GCClipXOrigin, clip_x_origin)
  F (GCClipYOrigin, clip_y_origin) F (GCDashOffset, dash_offset) F (GCArcMode, arc_mode)
#undef F
  if (mask & GCClipMask) { gc->v.clip_mask = v->clip_mask; gc->has_clip_rects = false; }
  if (mask & GCDashList) gc->dashes = { (qreal) (unsigned char) v->dashes, (qreal) (unsigned char) v->dashes };
}

GC XCreateGC (Display *, Drawable, unsigned long mask, XGCValues *values)
{
  GC gc = new _XGC;
  memset (&gc->v, 0, sizeof (XGCValues));
  gc->v.function = GXcopy;
  gc->v.plane_mask = ~0UL;
  gc->v.foreground = 0;
  gc->v.background = 1;
  gc->v.graphics_exposures = True;
  gc_apply (gc, mask, values);
  return gc;
}

int XChangeGC (Display *, GC gc, unsigned long mask, XGCValues *values) { gc_apply (gc, mask, values); return 1; }
int XCopyGC (Display *, GC src, unsigned long mask, GC dst) { gc_apply (dst, mask, &src->v); return 1; }
Status XGetGCValues (Display *, GC gc, unsigned long, XGCValues *values) { *values = gc->v; return 1; }
int XFreeGC (Display *, GC gc) { if (gc != s_default_gc) delete gc; return 1; }
GContext XGContextFromGC (GC gc) { return (GContext) (quintptr) gc; }
int XSetForeground (Display *, GC gc, unsigned long p) { gc->v.foreground = p; return 1; }
int XSetBackground (Display *, GC gc, unsigned long p) { gc->v.background = p; return 1; }
int XSetFunction (Display *, GC gc, int f) { gc->v.function = f; return 1; }
int XSetPlaneMask (Display *, GC gc, unsigned long m) { gc->v.plane_mask = m; return 1; }
int XSetFillStyle (Display *, GC gc, int s) { gc->v.fill_style = s; return 1; }
int XSetFillRule (Display *, GC gc, int r) { gc->v.fill_rule = r; return 1; }
int XSetArcMode (Display *, GC gc, int m) { gc->v.arc_mode = m; return 1; }
int XSetFont (Display *, GC gc, Font f) { gc->v.font = f; return 1; }
int XSetTile (Display *, GC gc, Pixmap t) { gc->v.tile = t; return 1; }
int XSetStipple (Display *, GC gc, Pixmap s) { gc->v.stipple = s; return 1; }
int XSetTSOrigin (Display *, GC gc, int x, int y) { gc->v.ts_x_origin = x; gc->v.ts_y_origin = y; return 1; }
int XSetGraphicsExposures (Display *, GC gc, Bool b) { gc->v.graphics_exposures = b; return 1; }
int XSetSubwindowMode (Display *, GC gc, int m) { gc->v.subwindow_mode = m; return 1; }
int XSetClipOrigin (Display *, GC gc, int x, int y) { gc->v.clip_x_origin = x; gc->v.clip_y_origin = y; return 1; }
int XSetClipMask (Display *, GC gc, Pixmap m) { gc->v.clip_mask = m; gc->has_clip_rects = false; return 1; }

int XSetLineAttributes (Display *, GC gc, unsigned int width, int style, int cap, int join)
{
  gc->v.line_width = width; gc->v.line_style = style; gc->v.cap_style = cap; gc->v.join_style = join;
  return 1;
}

int XSetDashes (Display *, GC gc, int offset, _Xconst char *list, int n)
{
  gc->v.dash_offset = offset;
  gc->dashes.clear ();
  for (int i = 0; i < n; i++) gc->dashes << (unsigned char) list[i];
  if (gc->dashes.size () % 2) gc->dashes += gc->dashes;
  return 1;
}

int XSetClipRectangles (Display *, GC gc, int x, int y, XRectangle *rects, int n, int)
{
  gc->clip_rects.clear ();
  for (int i = 0; i < n; i++) gc->clip_rects << QRect (rects[i].x, rects[i].y, rects[i].width, rects[i].height);
  gc->v.clip_x_origin = x;
  gc->v.clip_y_origin = y;
  gc->has_clip_rects = true;
  return 1;
}

GC XtGetGC (Widget, XtGCMask mask, XGCValues *values) { return XCreateGC (mq_display, 0, mask, values); }
GC XtAllocateGC (Widget, Cardinal, XtGCMask mask, XGCValues *values, XtGCMask, XtGCMask) { return XCreateGC (mq_display, 0, mask, values); }
void XtReleaseGC (Widget, GC gc) { XFreeGC (mq_display, gc); }
void XtDestroyGC (GC gc) { XFreeGC (mq_display, gc); }

/* ---------- drawing ---------- */
int XDrawPoint (Display *, Drawable d, GC gc, int x, int y)
{
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawPoint (x, y);
  return 1;
}

int XDrawPoints (Display *, Drawable d, GC gc, XPoint *pts, int n, int mode)
{
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  int x = 0, y = 0;
  for (int i = 0; i < n; i++) {
    if (mode == CoordModePrevious && i) { x += pts[i].x; y += pts[i].y; } else { x = pts[i].x; y = pts[i].y; }
    g.p.drawPoint (x, y);
  }
  return 1;
}

int XDrawLine (Display *, Drawable d, GC gc, int x1, int y1, int x2, int y2)
{
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawLine (x1, y1, x2, y2);
  return 1;
}

static QPolygon make_poly (XPoint *pts, int n, int mode)
{
  QPolygon poly;
  int x = 0, y = 0;
  for (int i = 0; i < n; i++) {
    if (mode == CoordModePrevious && i) { x += pts[i].x; y += pts[i].y; } else { x = pts[i].x; y = pts[i].y; }
    poly << QPoint (x, y);
  }
  return poly;
}

int XDrawLines (Display *, Drawable d, GC gc, XPoint *pts, int n, int mode)
{
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawPolyline (make_poly (pts, n, mode));
  return 1;
}

int XDrawSegments (Display *, Drawable d, GC gc, XSegment *segs, int n)
{
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawLine (segs[i].x1, segs[i].y1, segs[i].x2, segs[i].y2);
  return 1;
}

int XDrawRectangle (Display *, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h)
{
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawRect (x, y, w, h);
  return 1;
}

int XDrawRectangles (Display *, Drawable d, GC gc, XRectangle *r, int n)
{
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawRect (r[i].x, r[i].y, r[i].width, r[i].height);
  return 1;
}

int XFillRectangle (Display *, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h)
{
  GcPainter g (d, gc, true);
  if (g.ok) g.p.drawRect (x, y, w, h);
  return 1;
}

int XFillRectangles (Display *, Drawable d, GC gc, XRectangle *r, int n)
{
  GcPainter g (d, gc, true);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawRect (r[i].x, r[i].y, r[i].width, r[i].height);
  return 1;
}

int XDrawArc (Display *, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h, int a1, int a2)
{
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawArc (x, y, w, h, a1 / 4, a2 / 4);
  return 1;
}

int XDrawArcs (Display *dpy, Drawable d, GC gc, XArc *arcs, int n)
{
  for (int i = 0; i < n; i++) XDrawArc (dpy, d, gc, arcs[i].x, arcs[i].y, arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
  return 1;
}

int XFillArc (Display *, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h, int a1, int a2)
{
  GcPainter g (d, gc, true);
  if (!g.ok) return 0;
  if (qAbs (a2) >= 360 * 64) g.p.drawEllipse (x, y, w, h);
  else if (gc->v.arc_mode == ArcChord) g.p.drawChord (x, y, w, h, a1 / 4, a2 / 4);
  else g.p.drawPie (x, y, w, h, a1 / 4, a2 / 4);
  return 1;
}

int XFillArcs (Display *dpy, Drawable d, GC gc, XArc *arcs, int n)
{
  for (int i = 0; i < n; i++) XFillArc (dpy, d, gc, arcs[i].x, arcs[i].y, arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
  return 1;
}

int XFillPolygon (Display *, Drawable d, GC gc, XPoint *pts, int n, int, int mode)
{
  GcPainter g (d, gc, true);
  if (g.ok) g.p.drawPolygon (make_poly (pts, n, mode), gc->v.fill_rule == WindingRule ? Qt::WindingFill : Qt::OddEvenFill);
  return 1;
}

static void draw_text (Drawable d, GC gc, int x, int y, const QString &text, bool image)
{
  GcPainter g (d, gc, false);
  if (!g.ok) return;
  QFont f = mq_font_for (gc->v.font);
  g.p.setFont (f);
  if (image) {
    QFontMetrics fm (f);
    g.p.fillRect (x, y - fm.ascent (), fm.horizontalAdvance (text), fm.ascent () + fm.descent (),
                  gc_color (gc->v.background, drawables ().value (d).depth));
  }
  g.p.drawText (x, y, text);
}

int XDrawString (Display *, Drawable d, GC gc, int x, int y, _Xconst char *s, int len)
{
  draw_text (d, gc, x, y, QString::fromLatin1 (s, len), false);
  return 1;
}

int XDrawImageString (Display *, Drawable d, GC gc, int x, int y, _Xconst char *s, int len)
{
  draw_text (d, gc, x, y, QString::fromLatin1 (s, len), true);
  return 1;
}

int XDrawString16 (Display *, Drawable d, GC gc, int x, int y, _Xconst XChar2b *s, int len)
{
  QString t;
  for (int i = 0; i < len; i++) t += QChar ((s[i].byte1 << 8) | s[i].byte2);
  draw_text (d, gc, x, y, t, false);
  return 1;
}

int XDrawText (Display *dpy, Drawable d, GC gc, int x, int y, XTextItem *items, int n)
{
  for (int i = 0; i < n; i++) {
    x += items[i].delta;
    if (items[i].font != None) XSetFont (dpy, gc, items[i].font);
    XDrawString (dpy, d, gc, x, y, items[i].chars, items[i].nchars);
    x += XTextWidth (XQueryFont (dpy, gc->v.font), items[i].chars, items[i].nchars);
  }
  return 1;
}

int XCopyArea (Display *, Drawable src, Drawable dst, GC gc, int sx, int sy, unsigned int w, unsigned int h, int dx, int dy)
{
  QImage *s = target (src);
  if (!s) return 0;
  QImage piece = s->copy (sx, sy, w, h);
  GcPainter g (dst, gc, true);
  if (g.ok) g.p.drawImage (dx, dy, piece);
  return 1;
}

int XCopyPlane (Display *, Drawable src, Drawable dst, GC gc, int sx, int sy, unsigned int w, unsigned int h, int dx, int dy, unsigned long)
{
  QImage *s = target (src);
  if (!s) return 0;
  QImage piece = s->copy (sx, sy, w, h);
  QImage out (piece.size (), QImage::Format_ARGB32);
  QColor fg = mq_color (gc->v.foreground), bg = mq_color (gc->v.background);
  for (int y = 0; y < piece.height (); y++)
    for (int x = 0; x < piece.width (); x++)
      out.setPixelColor (x, y, (piece.pixel (x, y) & 0xffffff) ? fg : bg);
  GcPainter g (dst, gc, true);
  if (g.ok) g.p.drawImage (dx, dy, out);
  return 1;
}

/* ---------- pixmaps and bitmaps ---------- */
Pixmap XCreatePixmap (Display *, Drawable, unsigned int w, unsigned int h, unsigned int depth)
{
  QImage img (QSize (qMax (1u, w), qMax (1u, h)), QImage::Format_RGB32);
  img.fill (Qt::black);
  return new_pixmap (img, depth == 1 ? 1 : 24);
}

int XFreePixmap (Display *, Pixmap p) { drawables ().remove (p); return 1; }

static QImage bitmap_image (const char *data, unsigned int w, unsigned int h, QColor fg, QColor bg)
{
  QImage img (w, h, QImage::Format_RGB32);
  int bpl = (w + 7) / 8;
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < w; x++) {
      bool bit = ((unsigned char) data[y * bpl + x / 8]) & (1 << (x % 8));   /* XBM: LSB first */
      img.setPixelColor (x, y, bit ? fg : bg);
    }
  return img;
}

Pixmap XCreateBitmapFromData (Display *, Drawable, _Xconst char *data, unsigned int w, unsigned int h)
{
  return new_pixmap (bitmap_image (data, w, h, Qt::white, Qt::black), 1);
}

Pixmap XCreatePixmapFromBitmapData (Display *, Drawable, char *data, unsigned int w, unsigned int h, unsigned long fg, unsigned long bg, unsigned int depth)
{
  return new_pixmap (bitmap_image (data, w, h, depth == 1 ? QColor (fg ? Qt::white : Qt::black) : mq_color (fg),
                                   depth == 1 ? QColor (bg ? Qt::white : Qt::black) : mq_color (bg)), depth == 1 ? 1 : 24);
}

int XReadBitmapFile (Display *, Drawable, _Xconst char *filename, unsigned int *w, unsigned int *h, Pixmap *bitmap, int *xh, int *yh)
{
  QImage img (QString::fromLocal8Bit (filename));
  if (img.isNull ()) return BitmapOpenFailed;
  QImage mono (img.size (), QImage::Format_RGB32);
  for (int y = 0; y < img.height (); y++)
    for (int x = 0; x < img.width (); x++)
      mono.setPixelColor (x, y, qGray (img.pixel (x, y)) < 128 ? Qt::white : Qt::black);
  *bitmap = new_pixmap (mono, 1);
  if (w) *w = img.width ();
  if (h) *h = img.height ();
  if (xh) *xh = -1;
  if (yh) *yh = -1;
  return BitmapSuccess;
}

/* XImage: 32 bits per pixel, ZPixmap */
static unsigned long img_get_pixel (XImage *im, int x, int y) { return ((uint32_t *) (im->data + y * im->bytes_per_line))[x] & 0xffffff; }
static int img_put_pixel (XImage *im, int x, int y, unsigned long p) { ((uint32_t *) (im->data + y * im->bytes_per_line))[x] = p; return 1; }
static int img_destroy (XImage *im) { free (im->data); free (im); return 1; }

static XImage *make_ximage (int w, int h, char *data)
{
  auto *im = (XImage *) calloc (1, sizeof (XImage));
  im->width = w; im->height = h; im->format = ZPixmap; im->data = data;
  im->byte_order = LSBFirst; im->bitmap_unit = 32; im->bitmap_bit_order = LSBFirst; im->bitmap_pad = 32;
  im->depth = 24; im->bytes_per_line = w * 4; im->bits_per_pixel = 32;
  im->red_mask = 0xff0000; im->green_mask = 0xff00; im->blue_mask = 0xff;
  im->f.get_pixel = img_get_pixel;
  im->f.put_pixel = img_put_pixel;
  im->f.destroy_image = img_destroy;
  return im;
}

XImage *XCreateImage (Display *, Visual *, unsigned int, int, int, char *data, unsigned int w, unsigned int h, int, int)
{
  return make_ximage (w, h, data);
}

XImage *XGetImage (Display *, Drawable d, int x, int y, unsigned int w, unsigned int h, unsigned long, int)
{
  QImage *s = target (d);
  if (!s) return nullptr;
  QImage part = s->copy (x, y, w, h).convertToFormat (QImage::Format_RGB32);
  char *data = (char *) malloc (w * h * 4);
  for (unsigned r = 0; r < h; r++) memcpy (data + r * w * 4, part.constScanLine (r), w * 4);
  return make_ximage (w, h, data);
}

int XPutImage (Display *, Drawable d, GC gc, XImage *im, int sx, int sy, int dx, int dy, unsigned int w, unsigned int h)
{
  QImage img (w, h, QImage::Format_RGB32);
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < w; x++) img.setPixel (x, y, 0xff000000 | XGetPixel (im, sx + x, sy + y));
  GcPainter g (d, gc, true);
  if (g.ok) g.p.drawImage (dx, dy, img);
  return 1;
}

/* ---------- fonts ---------- */
Font XLoadFont (Display *, _Xconst char *name)
{
  bool ok;
  QFont f = mq_font_from_xlfd (QString::fromLatin1 (name), &ok);
  Font id = alloc_id ();
  fonts ().insert (id, f);
  return id;
}

XFontStruct *XQueryFont (Display *, XID fid) { return make_font_struct (fid, mq_font_for (fid)); }

XFontStruct *XLoadQueryFont (Display *d, _Xconst char *name)
{
  Font id = XLoadFont (d, name);
  return make_font_struct (id, mq_font_for (id));
}

int XFreeFont (Display *, XFontStruct *fs) { if (fs) { fonts ().remove (fs->fid); free (fs); } return 1; }
int XUnloadFont (Display *, Font f) { fonts ().remove (f); return 1; }

int XTextWidth (XFontStruct *fs, _Xconst char *s, int len)
{
  return QFontMetrics (mq_font_for (fs ? fs->fid : 0)).horizontalAdvance (QString::fromLatin1 (s, len));
}

int XTextWidth16 (XFontStruct *fs, _Xconst XChar2b *s, int len)
{
  QString t;
  for (int i = 0; i < len; i++) t += QChar ((s[i].byte1 << 8) | s[i].byte2);
  return QFontMetrics (mq_font_for (fs ? fs->fid : 0)).horizontalAdvance (t);
}

int XTextExtents (XFontStruct *fs, _Xconst char *s, int len, int *dir, int *ascent, int *descent, XCharStruct *overall)
{
  QFontMetrics fm (mq_font_for (fs ? fs->fid : 0));
  QString t = QString::fromLatin1 (s, len);
  QRect br = fm.boundingRect (t);
  if (dir) *dir = FontLeftToRight;
  if (ascent) *ascent = fm.ascent ();
  if (descent) *descent = fm.descent ();
  if (overall) {
    overall->width = fm.horizontalAdvance (t);
    overall->ascent = -br.top ();
    overall->descent = br.bottom ();
    overall->lbearing = br.left ();
    overall->rbearing = br.right ();
  }
  return 1;
}

char **XListFonts (Display *, _Xconst char *pattern, int, int *count)
{
  char **list = (char **) malloc (2 * sizeof (char *));
  list[0] = strdup (pattern ? pattern : "fixed");
  list[1] = nullptr;
  *count = 1;
  return list;
}

int XFreeFontNames (char **list) { if (list) { for (char **p = list; *p; p++) free (*p); free (list); } return 1; }

/* ---------- keysyms ---------- */
KeySym XStringToKeysym (_Xconst char *s)
{
  if (!s || !*s) return NoSymbol;
  auto it = keysym_names ().constFind (s);
  if (it != keysym_names ().constEnd ()) return it.value ();
  if (!s[1]) return (unsigned char) s[0];
  if (s[0] == 'U' && strlen (s) >= 5) return 0x01000000 | strtoul (s + 1, nullptr, 16);
  return NoSymbol;
}

char *XKeysymToString (KeySym k)
{
  for (auto it = keysym_names ().constBegin (); it != keysym_names ().constEnd (); ++it)
    if (it.value () == k) return const_cast<char *> (it.key ().constData ());
  return nullptr;
}

KeySym XLookupKeysym (XKeyEvent *e, int index)
{
  KeySym k = e->keycode;
  if (index == 0 && k < 0x100) return tolower ((int) k);
  return k;
}

KeySym XKeycodeToKeysym (Display *, KeyCode kc, int index)
{
  KeySym k = kc;
  if (index == 0 && k < 0x100) return tolower ((int) k);
  return k;
}

KeyCode XKeysymToKeycode (Display *, KeySym k) { return (KeyCode) (k & 0xff); }

int XLookupString (XKeyEvent *e, char *buf, int n, KeySym *keysym, XComposeStatus *)
{
  KeySym k = e->keycode;
  if (keysym) *keysym = k;
  char c = 0;
  if (k >= 0x20 && k < 0x100) c = (char) k;
  else if (k == XK_Return || k == XK_KP_Enter) c = '\r';
  else if (k == XK_BackSpace) c = '\b';
  else if (k == XK_Tab) c = '\t';
  else if (k == XK_Escape) c = 0x1b;
  else if (k == XK_Delete) c = 0x7f;
  if (c && (e->state & ControlMask) && c >= '@' && c <= '~') c &= 0x1f;
  if (!c || n < 1) return 0;
  buf[0] = c;
  if (n > 1) buf[1] = 0;
  return 1;
}

void XConvertCase (KeySym k, KeySym *lower, KeySym *upper)
{
  *lower = k < 0x100 ? tolower ((int) k) : k;
  *upper = k < 0x100 ? toupper ((int) k) : k;
}

int XDisplayKeycodes (Display *, int *min, int *max) { *min = 8; *max = 255; return 1; }
int XRefreshKeyboardMapping (XMappingEvent *) { return 1; }
XModifierKeymap *XGetModifierMapping (Display *) { return (XModifierKeymap *) calloc (1, sizeof (XModifierKeymap)); }
int XFreeModifiermap (XModifierKeymap *m) { free (m); return 1; }

/* ---------- geometry strings ---------- */
int XParseGeometry (_Xconst char *s, int *x, int *y, unsigned int *w, unsigned int *h)
{
  int mask = NoValue;
  if (!s) return mask;
  const char *p = s;
  if (*p == '=') p++;
  char *end;
  if (*p && *p != '+' && *p != '-' && *p != 'x') {
    *w = strtoul (p, &end, 10);
    if (end != p) { mask |= WidthValue; p = end; }
  }
  if (*p == 'x' || *p == 'X') {
    p++;
    *h = strtoul (p, &end, 10);
    if (end != p) { mask |= HeightValue; p = end; }
  }
  if (*p == '+' || *p == '-') {
    bool neg = *p == '-';
    p++;
    *x = strtol (p, &end, 10);
    if (neg) { *x = -*x; mask |= XNegative; }
    mask |= XValue;
    p = end;
    if (*p == '+' || *p == '-') {
      neg = *p == '-';
      p++;
      *y = strtol (p, &end, 10);
      if (neg) { *y = -*y; mask |= YNegative; }
      mask |= YValue;
    }
  }
  return mask;
}

int XWMGeometry (Display *, int, _Xconst char *user, _Xconst char *def, unsigned int, XSizeHints *, int *x, int *y, int *w, int *h, int *gravity)
{
  unsigned int uw = 0, uh = 0;
  int ux = 0, uy = 0;
  int mask = XParseGeometry (def, &ux, &uy, &uw, &uh);
  mask |= XParseGeometry (user, &ux, &uy, &uw, &uh);
  if (mask & WidthValue) *w = uw;
  if (mask & HeightValue) *h = uh;
  if (mask & XValue) *x = ux;
  if (mask & YValue) *y = uy;
  if (gravity) *gravity = NorthWestGravity;
  return mask;
}

/* ---------- shape extension: window masks ---------- */
Bool XShapeQueryExtension (Display *, int *event_base, int *error_base)
{
  if (event_base) *event_base = 64;
  if (error_base) *error_base = 128;
  return True;
}

Status XShapeQueryVersion (Display *, int *major, int *minor) { *major = 1; *minor = 1; return 1; }

void XShapeCombineMask (Display *, Window dest, int, int x, int y, Pixmap src, int op)
{
  QWidget *qw = window_widget (dest);
  if (!qw) return;
  qw = qw->window ();
  if (src == None) { qw->clearMask (); return; }
  QImage img = drawables ().value (src).image;
  if (img.isNull ()) return;
  QRegion r (QBitmap::fromImage (img.createMaskFromColor (QColor (Qt::black).rgb (), Qt::MaskOutColor)));
  r.translate (x, y);
  qw->setMask (op == ShapeSet || qw->mask ().isEmpty () ? r : qw->mask () + r);
}

void XShapeCombineRectangles (Display *, Window dest, int, int x, int y, XRectangle *rects, int n, int, int)
{
  QWidget *qw = window_widget (dest);
  if (!qw) return;
  QRegion r;
  for (int i = 0; i < n; i++) r += QRect (rects[i].x + x, rects[i].y + y, rects[i].width, rects[i].height);
  qw->window ()->setMask (r);
}

} /* extern "C" */
