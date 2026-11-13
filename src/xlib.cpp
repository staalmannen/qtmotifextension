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
#include <dlfcn.h>
#include <link.h>

/* ======================================================================
 * Display, screen, visual
 * ====================================================================== */
typedef std::remove_pointer_t<_XPrivDisplay> PrivDisplay;
Display *mq_display = nullptr;
static Screen s_screen;
static Visual s_visual;
static Depth s_depths[2];
static GC s_default_gc;
Screen *mq_screen_ptr = &s_screen;

void *mq_real_symbol (const char *name)
{
  void *p = dlsym (RTLD_NEXT, name);
  if (!p) {
    /* not loaded: open the system libX11 privately */
    static void *x11 = dlopen ("libX11.so.6", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (x11) p = dlsym (x11, name);
  }
  return p;
}

/* Is code at this address in an object that does not use this library
 * (Qt, libGL ...)? Programs and libraries built against Motif on Qt name
 * libmotif-on-qt in their DT_NEEDED entries. */
static bool foreign_caller (void *addr)
{
  Dl_info info;
  struct link_map *lm = nullptr;
  if (!dladdr1 (addr, &info, (void **) &lm, RTLD_DL_LINKMAP) || !lm) return false;
  Dl_info self;
  if (dladdr ((void *) &foreign_caller, &self) && self.dli_fbase == info.dli_fbase) return false;
  const char *strtab = nullptr;
  for (const ElfW(Dyn) *d = lm->l_ld; d && d->d_tag != DT_NULL; d++)
    if (d->d_tag == DT_STRTAB) strtab = (const char *) d->d_un.d_ptr;
  if (!strtab) return false;
  if ((uintptr_t) strtab < lm->l_addr) strtab += lm->l_addr;   /* unrelocated (e.g. vDSO-like objects) */
  for (const ElfW(Dyn) *d = lm->l_ld; d->d_tag != DT_NULL; d++)
    if (d->d_tag == DT_NEEDED && strstr (strtab + d->d_un.d_val, "libmotif-on-qt")) return false;
  return true;
}

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
  XWMHints wm_hints {};          /* as set with XSetWMHints (flags 0: none) */
  XSizeHints normal_hints {};    /* as set with XSetWMNormalHints */
};

static QHash<XID, DrawableRec> &drawables ()
{
  static QHash<XID, DrawableRec> h;
  return h;
}

bool mq_foreign_places_children (Widget parent);

Window mq_window_for (Widget w)
{
  if (!w) return 0;
  /* gadgets drawn by a program's own composite use the parent's window */
  if ((R (w)->klass->kind & K_GADGET) && R (w)->parent && mq_foreign_places_children (R (w)->parent)) w = R (w)->parent;
  if (!R (w)->window) {
    while (w && !mq_qw (w) && R (w)->parent) w = R (w)->parent;   /* gadgets use their parent's window */
    if (!w) return 0;
    if (R (w)->window) return R (w)->window;
    R (w)->window = alloc_id ();
    DrawableRec &r = drawables ()[R (w)->window];
    r.widget = w;
    r.qw = mq_qw (w);
  }
  return R (w)->window;
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
Display *XOpenDisplay (_Xconst char *name)
{
  if (foreign_caller (__builtin_return_address (0))) {
    static auto real = (decltype (&XOpenDisplay)) mq_real_symbol ("XOpenDisplay");
    if (real) return real (name);
  }
  mq_ensure_app ();
  return mq_display;
}
int XCloseDisplay (Display * a0_) { MQ_FWD (XCloseDisplay, a0_, a0_); return 0; }
char *XDisplayName (_Xconst char *s) { return (char *) (s ? s : ":qt"); }
int XDefaultScreen (Display * a0_) { MQ_FWD (XDefaultScreen, a0_, a0_); return 0; }
Screen *XDefaultScreenOfDisplay (Display * a0_) { MQ_FWD (XDefaultScreenOfDisplay, a0_, a0_); return &s_screen; }
Screen *XScreenOfDisplay (Display * a0_, int a1_) { MQ_FWD (XScreenOfDisplay, a0_, a0_, a1_); return &s_screen; }
Display *XDisplayOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XDisplayOfScreen, a0_, a0_); return mq_display; }
int XScreenNumberOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XScreenNumberOfScreen, a0_, a0_); return 0; }
int XScreenCount (Display * a0_) { MQ_FWD (XScreenCount, a0_, a0_); return 1; }
Window XDefaultRootWindow (Display * a0_) { MQ_FWD (XDefaultRootWindow, a0_, a0_); return ROOT_WINDOW; }
Window XRootWindow (Display * a0_, int a1_) { MQ_FWD (XRootWindow, a0_, a0_, a1_); return ROOT_WINDOW; }
Window XRootWindowOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XRootWindowOfScreen, a0_, a0_); return ROOT_WINDOW; }
int XDisplayWidth (Display * a0_, int a1_) { MQ_FWD (XDisplayWidth, a0_, a0_, a1_); return s_screen.width; }
int XDisplayHeight (Display * a0_, int a1_) { MQ_FWD (XDisplayHeight, a0_, a0_, a1_); return s_screen.height; }
int XDisplayWidthMM (Display * a0_, int a1_) { MQ_FWD (XDisplayWidthMM, a0_, a0_, a1_); return s_screen.mwidth; }
int XDisplayHeightMM (Display * a0_, int a1_) { MQ_FWD (XDisplayHeightMM, a0_, a0_, a1_); return s_screen.mheight; }
int XWidthOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XWidthOfScreen, a0_, a0_); return s_screen.width; }
int XHeightOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XHeightOfScreen, a0_, a0_); return s_screen.height; }
int XWidthMMOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XWidthMMOfScreen, a0_, a0_); return s_screen.mwidth; }
int XHeightMMOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XHeightMMOfScreen, a0_, a0_); return s_screen.mheight; }
int XDefaultDepth (Display * a0_, int a1_) { MQ_FWD (XDefaultDepth, a0_, a0_, a1_); return 24; }
int XDefaultDepthOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XDefaultDepthOfScreen, a0_, a0_); return 24; }
int XDisplayPlanes (Display * a0_, int a1_) { MQ_FWD (XDisplayPlanes, a0_, a0_, a1_); return 24; }
int XPlanesOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XPlanesOfScreen, a0_, a0_); return 24; }
int XDisplayCells (Display * a0_, int a1_) { MQ_FWD (XDisplayCells, a0_, a0_, a1_); return 256; }
int XCellsOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XCellsOfScreen, a0_, a0_); return 256; }
Visual *XDefaultVisual (Display * a0_, int a1_) { MQ_FWD (XDefaultVisual, a0_, a0_, a1_); return &s_visual; }
Visual *XDefaultVisualOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XDefaultVisualOfScreen, a0_, a0_); return &s_visual; }
Colormap XDefaultColormap (Display * a0_, int a1_) { MQ_FWD (XDefaultColormap, a0_, a0_, a1_); return DEFAULT_COLORMAP; }
Colormap XDefaultColormapOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XDefaultColormapOfScreen, a0_, a0_); return DEFAULT_COLORMAP; }
unsigned long XBlackPixel (Display * a0_, int a1_) { MQ_FWD (XBlackPixel, a0_, a0_, a1_); return 0; }
unsigned long XWhitePixel (Display * a0_, int a1_) { MQ_FWD (XWhitePixel, a0_, a0_, a1_); return 0xffffff; }
unsigned long XBlackPixelOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XBlackPixelOfScreen, a0_, a0_); return 0; }
unsigned long XWhitePixelOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XWhitePixelOfScreen, a0_, a0_); return 0xffffff; }
GC XDefaultGC (Display * a0_, int a1_) { MQ_FWD (XDefaultGC, a0_, a0_, a1_); return s_default_gc; }
GC XDefaultGCOfScreen (Screen * a0_) { MQ_FWD_SCREEN (XDefaultGCOfScreen, a0_, a0_); return s_default_gc; }
int XConnectionNumber (Display * a0_) { MQ_FWD (XConnectionNumber, a0_, a0_); return -1; }
char *XServerVendor (Display * a0_) { MQ_FWD (XServerVendor, a0_, a0_); return (char *) "Motif on Qt"; }
int XVendorRelease (Display * a0_) { MQ_FWD (XVendorRelease, a0_, a0_); return 1; }
int XProtocolVersion (Display * a0_) { MQ_FWD (XProtocolVersion, a0_, a0_); return 11; }
int XProtocolRevision (Display * a0_) { MQ_FWD (XProtocolRevision, a0_, a0_); return 0; }
long XMaxRequestSize (Display * a0_) { MQ_FWD (XMaxRequestSize, a0_, a0_); return 65535; }
VisualID XVisualIDFromVisual (Visual *v) { return v ? v->visualid : 0; }
int XFlush (Display * a0_) { MQ_FWD (XFlush, a0_, a0_); return 0; }
int XSync (Display * a0_, Bool a1_) { MQ_FWD (XSync, a0_, a0_, a1_); QCoreApplication::processEvents (); return 0; }
int XPending (Display * a0_) { MQ_FWD (XPending, a0_, a0_); return 0; }
int XEventsQueued (Display * a0_, int a1_) { MQ_FWD (XEventsQueued, a0_, a0_, a1_); return 0; }
int XBell (Display * a0_, int a1_) { MQ_FWD (XBell, a0_, a0_, a1_); QApplication::beep (); return 0; }
int XFree (void *data) { free (data); return 1; }
int XNoOp (Display * a0_) { MQ_FWD (XNoOp, a0_, a0_); return 0; }
int (*XSynchronize (Display *, Bool)) (Display *) { return nullptr; }
int (*XSetErrorHandler (XErrorHandler)) (Display *, XErrorEvent *) { return nullptr; }
int (*XSetIOErrorHandler (XIOErrorHandler)) (Display *) { return nullptr; }

int *XListDepths (Display * a0_, int a1_, int *count)
{ MQ_FWD (XListDepths, a0_, a0_, a1_, count);
  int *d = (int *) malloc (2 * sizeof (int));
  d[0] = 24; d[1] = 1;
  if (count) *count = 2;
  return d;
}

XVisualInfo *XGetVisualInfo (Display * a0_, long a1_, XVisualInfo * a2_, int *n)
{ MQ_FWD (XGetVisualInfo, a0_, a0_, a1_, a2_, n);
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

Status XMatchVisualInfo (Display * a0_, int a1_, int depth, int c_class, XVisualInfo *vi)
{ MQ_FWD (XMatchVisualInfo, a0_, a0_, a1_, depth, c_class, vi);
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

Atom XInternAtom (Display * a0_, _Xconst char *name, Bool only_if_exists)
{ MQ_FWD (XInternAtom, a0_, a0_, name, only_if_exists);
  if (!name) return None;
  auto it = atoms ().constFind (name);
  if (it != atoms ().constEnd ()) return it.value ();
  if (only_if_exists) return None;
  Atom a = 1000 + atoms ().size ();
  atoms ().insert (name, a);
  return a;
}

Status XInternAtoms (Display *d, char **names, int count, Bool only_if_exists, Atom *out)
{ MQ_FWD (XInternAtoms, d, d, names, count, only_if_exists, out);
  for (int i = 0; i < count; i++) out[i] = XInternAtom (d, names[i], only_if_exists);
  return 1;
}

char *XGetAtomName (Display * a0_, Atom atom)
{ MQ_FWD (XGetAtomName, a0_, a0_, atom);
  for (auto it = atoms ().constBegin (); it != atoms ().constEnd (); ++it)
    if (it.value () == atom) return strdup (it.key ().constData ());
  return nullptr;
}

/* ---------- windows ---------- */
Window XCreateSimpleWindow (Display * a0_, Window parent, int x, int y, unsigned int w, unsigned int h, unsigned int a6_, unsigned long a7_, unsigned long background)
{ MQ_FWD (XCreateSimpleWindow, a0_, a0_, parent, x, y, w, h, a6_, a7_, background);
  Window id = alloc_id ();
  DrawableRec &r = drawables ()[id];
  QWidget *pw = window_widget (parent);
  auto *qw = new QWidget (pw);
  qw->setGeometry (x, y, w, h);
  r.qw = qw;
  r.background = background;
  return id;
}

Window XCreateWindow (Display *d, Window parent, int x, int y, unsigned int w, unsigned int h, unsigned int bw, int a7_, unsigned int a8_, Visual * a9_, unsigned long a10_, XSetWindowAttributes *attrs)
{ MQ_FWD (XCreateWindow, d, d, parent, x, y, w, h, bw, a7_, a8_, a9_, a10_, attrs);
  return XCreateSimpleWindow (d, parent, x, y, w, h, bw, 0, attrs ? attrs->background_pixel : 0xffffff);
}

int XDestroyWindow (Display * a0_, Window w)
{ MQ_FWD (XDestroyWindow, a0_, a0_, w);
  auto it = drawables ().find (w);
  if (it == drawables ().end ()) return 0;
  if (!it->widget && it->qw) it->qw->deleteLater ();
  drawables ().erase (it);
  return 1;
}

int XMapWindow (Display * a0_, Window w)
{ MQ_FWD (XMapWindow, a0_, a0_, w);
  if (Widget wd = mq_widget_for_window (w)) {
    if (R (wd)->klass->kind & K_SHELL) mq_popup_shell (wd, true);
    else if (QWidget *qw = mq_qw (wd)) qw->show ();
  } else if (QWidget *qw = window_widget (w)) qw->show ();
  return 1;
}

int XMapRaised (Display *d, Window w) { MQ_FWD (XMapRaised, d, d, w); XMapWindow (d, w); if (QWidget *qw = window_widget (w)) qw->raise (); return 1; }
int XMapSubwindows (Display * a0_, Window a1_) { MQ_FWD (XMapSubwindows, a0_, a0_, a1_); return 1; }

int XUnmapWindow (Display * a0_, Window w)
{ MQ_FWD (XUnmapWindow, a0_, a0_, w);
  if (QWidget *qw = window_widget (w)) qw->hide ();
  return 1;
}

int XRaiseWindow (Display * a0_, Window w) { MQ_FWD (XRaiseWindow, a0_, a0_, w); if (QWidget *qw = window_widget (w)) qw->raise (); return 1; }
int XLowerWindow (Display * a0_, Window w) { MQ_FWD (XLowerWindow, a0_, a0_, w); if (QWidget *qw = window_widget (w)) qw->lower (); return 1; }
int XIconifyWindow (Display * a0_, Window w, int a2_) { MQ_FWD (XIconifyWindow, a0_, a0_, w, a2_); if (QWidget *qw = window_widget (w)) qw->window ()->showMinimized (); return 1; }
int XMoveWindow (Display * a0_, Window w, int x, int y) { MQ_FWD (XMoveWindow, a0_, a0_, w, x, y); if (QWidget *qw = window_widget (w)) qw->move (x, y); return 1; }
int XResizeWindow (Display * a0_, Window w, unsigned int width, unsigned int height) { MQ_FWD (XResizeWindow, a0_, a0_, w, width, height); if (QWidget *qw = window_widget (w)) qw->resize (width, height); return 1; }
int XMoveResizeWindow (Display * a0_, Window w, int x, int y, unsigned int width, unsigned int height)
{ MQ_FWD (XMoveResizeWindow, a0_, a0_, w, x, y, width, height);
  if (QWidget *qw = window_widget (w)) qw->setGeometry (x, y, width, height);
  return 1;
}

int XClearArea (Display * a0_, Window w, int x, int y, unsigned int width, unsigned int height, Bool exposures)
{ MQ_FWD (XClearArea, a0_, a0_, w, x, y, width, height, exposures);
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

int XClearWindow (Display *d, Window w) { MQ_FWD (XClearWindow, d, d, w); return XClearArea (d, w, 0, 0, 0, 0, False); }

int XSetWindowBackground (Display * a0_, Window w, unsigned long pixel)
{ MQ_FWD (XSetWindowBackground, a0_, a0_, w, pixel);
  if (Widget wd = mq_widget_for_window (w)) XtVaSetValues (wd, XmNbackground, (XtArgVal) pixel, NULL);
  drawables ()[w].background = pixel;
  return 1;
}

int XSetWindowBackgroundPixmap (Display * a0_, Window a1_, Pixmap a2_) { MQ_FWD (XSetWindowBackgroundPixmap, a0_, a0_, a1_, a2_); return 1; }
int XSetWindowBorder (Display * a0_, Window a1_, unsigned long a2_) { MQ_FWD (XSetWindowBorder, a0_, a0_, a1_, a2_); return 1; }
int XSetWindowBorderWidth (Display * a0_, Window a1_, unsigned int a2_) { MQ_FWD (XSetWindowBorderWidth, a0_, a0_, a1_, a2_); return 1; }

int XChangeWindowAttributes (Display *d, Window w, unsigned long mask, XSetWindowAttributes *a)
{ MQ_FWD (XChangeWindowAttributes, d, d, w, mask, a);
  if (mask & CWBackPixel) XSetWindowBackground (d, w, a->background_pixel);
  if (mask & CWCursor) XDefineCursor (d, w, a->cursor);
  return 1;
}

Status XGetWindowAttributes (Display * a0_, Window w, XWindowAttributes *a)
{ MQ_FWD (XGetWindowAttributes, a0_, a0_, w, a);
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

Status XGetGeometry (Display * a0_, Drawable d, Window *root, int *x, int *y, unsigned int *w, unsigned int *h, unsigned int *bw, unsigned int *depth)
{ MQ_FWD (XGetGeometry, a0_, a0_, d, root, x, y, w, h, bw, depth);
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

Bool XTranslateCoordinates (Display * a0_, Window src, Window dst, int sx, int sy, int *dx, int *dy, Window *child)
{ MQ_FWD (XTranslateCoordinates, a0_, a0_, src, dst, sx, sy, dx, dy, child);
  QWidget *a = window_widget (src), *b = window_widget (dst);
  QPoint g = a ? a->mapToGlobal (QPoint (sx, sy)) : QPoint (sx, sy);
  QPoint r = b ? b->mapFromGlobal (g) : g;
  if (dx) *dx = r.x ();
  if (dy) *dy = r.y ();
  if (child) *child = None;
  return True;
}

Bool XQueryPointer (Display * a0_, Window w, Window *root, Window *child, int *rx, int *ry, int *wx, int *wy, unsigned int *mask)
{ MQ_FWD (XQueryPointer, a0_, a0_, w, root, child, rx, ry, wx, wy, mask);
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

int XWarpPointer (Display * a0_, Window a1_, Window dst, int a3_, int a4_, unsigned int a5_, unsigned int a6_, int x, int y)
{ MQ_FWD (XWarpPointer, a0_, a0_, a1_, dst, a3_, a4_, a5_, a6_, x, y);
  QWidget *qw = window_widget (dst);
  QCursor::setPos (qw ? qw->mapToGlobal (QPoint (x, y)) : QPoint (x, y));
  return 1;
}

int XGrabPointer (Display * a0_, Window a1_, Bool a2_, unsigned int a3_, int a4_, int a5_, Window a6_, Cursor a7_, Time a8_) { MQ_FWD (XGrabPointer, a0_, a0_, a1_, a2_, a3_, a4_, a5_, a6_, a7_, a8_); return GrabSuccess; }
int XUngrabPointer (Display * a0_, Time a1_) { MQ_FWD (XUngrabPointer, a0_, a0_, a1_); return 1; }
int XGrabKeyboard (Display * a0_, Window a1_, Bool a2_, int a3_, int a4_, Time a5_) { MQ_FWD (XGrabKeyboard, a0_, a0_, a1_, a2_, a3_, a4_, a5_); return GrabSuccess; }
int XUngrabKeyboard (Display * a0_, Time a1_) { MQ_FWD (XUngrabKeyboard, a0_, a0_, a1_); return 1; }
int XGrabServer (Display * a0_) { MQ_FWD (XGrabServer, a0_, a0_); return 1; }
int XUngrabServer (Display * a0_) { MQ_FWD (XUngrabServer, a0_, a0_); return 1; }
int XSetInputFocus (Display * a0_, Window w, int a2_, Time a3_) { MQ_FWD (XSetInputFocus, a0_, a0_, w, a2_, a3_); if (QWidget *qw = window_widget (w)) qw->setFocus (); return 1; }
int XSelectInput (Display * a0_, Window w, long mask)
{ MQ_FWD (XSelectInput, a0_, a0_, w, mask);
  if (QWidget *qw = window_widget (w)) qw->setMouseTracking (mask & PointerMotionMask);
  return 1;
}

int XStoreName (Display * a0_, Window w, _Xconst char *name)
{ MQ_FWD (XStoreName, a0_, a0_, w, name);
  if (QWidget *qw = window_widget (w)) qw->window ()->setWindowTitle (QString::fromLocal8Bit (name));
  return 1;
}

Status XFetchName (Display * a0_, Window w, char **name)
{ MQ_FWD (XFetchName, a0_, a0_, w, name);
  QWidget *qw = window_widget (w);
  *name = qw ? strdup (qw->window ()->windowTitle ().toLocal8Bit ().constData ()) : nullptr;
  return qw != nullptr;
}

int XSetIconName (Display * a0_, Window a1_, _Xconst char * a2_) { MQ_FWD (XSetIconName, a0_, a0_, a1_, a2_); return 1; }
Status XSetWMProtocols (Display * a0_, Window a1_, Atom * a2_, int a3_) { MQ_FWD (XSetWMProtocols, a0_, a0_, a1_, a2_, a3_); return 1; }
int XSetTransientForHint (Display * a0_, Window a1_, Window a2_) { MQ_FWD (XSetTransientForHint, a0_, a0_, a1_, a2_); return 1; }
void XSetWMNormalHints (Display * a0_, Window a1_, XSizeHints * a2_)
{ MQ_FWD (XSetWMNormalHints, a0_, a0_, a1_, a2_);
  if (a2_ && drawables ().contains (a1_)) drawables ()[a1_].normal_hints = *a2_;
}

/* the window's size hints: as set, else what Xt sets for a shell (its size) */
Status XGetWMNormalHints (Display * a0_, Window w, XSizeHints *hints, long *supplied)
{ MQ_FWD (XGetWMNormalHints, a0_, a0_, w, hints, supplied);
  auto it = drawables ().find (w);
  if (it == drawables ().end () || !hints) return 0;
  *hints = it->normal_hints;
  if (!hints->flags)
    if (QWidget *qw = window_widget (w)) {
      hints->flags = PSize | PPosition;
      hints->x = qw->x (); hints->y = qw->y (); hints->width = qw->width (); hints->height = qw->height ();
    }
  if (supplied) *supplied = PAllHints;
  return 1;
}
int XSetNormalHints (Display * a0_, Window a1_, XSizeHints * a2_) { MQ_FWD (XSetNormalHints, a0_, a0_, a1_, a2_); return 1; }
int XSetWMHints (Display * a0_, Window a1_, XWMHints * a2_)
{ MQ_FWD (XSetWMHints, a0_, a0_, a1_, a2_);
  if (a2_ && drawables ().contains (a1_)) drawables ()[a1_].wm_hints = *a2_;
  return 1;
}

/* as set, else what Xt sets for a shell: input and the normal state.
 * Programs change one field and set them back. */
XWMHints *XGetWMHints (Display * a0_, Window w)
{ MQ_FWD (XGetWMHints, a0_, a0_, w);
  auto it = drawables ().find (w);
  if (it == drawables ().end ()) return nullptr;
  auto *h = (XWMHints *) calloc (1, sizeof (XWMHints));
  *h = it->wm_hints;
  if (!h->flags) { h->flags = InputHint | StateHint; h->input = True; h->initial_state = NormalState; }
  return h;
}

int XSetCommand (Display * a0_, Window a1_, char **a2_, int a3_) { MQ_FWD (XSetCommand, a0_, a0_, a1_, a2_, a3_); return 1; }

Atom *XListProperties (Display * a0_, Window w, int *n)
{ MQ_FWD (XListProperties, a0_, a0_, w, n);
  auto it = drawables ().find (w);
  QList<Atom> l = it == drawables ().end () ? QList<Atom> () : it->props.keys ();
  if (n) *n = l.size ();
  if (l.isEmpty ()) return nullptr;
  auto *a = (Atom *) malloc (l.size () * sizeof (Atom));
  for (int i = 0; i < l.size (); i++) a[i] = l[i];
  return a;
}

/* the window tree follows the widget tree: a shell's window is a child of
 * the root window, other windows of their nearest ancestor with a window */
Status XQueryTree (Display * a0_, Window w, Window *root, Window *parent, Window **children, unsigned int *n)
{ MQ_FWD (XQueryTree, a0_, a0_, w, root, parent, children, n);
  if (root) *root = ROOT_WINDOW;
  if (parent) *parent = None;
  QList<Window> kids;
  if (w == ROOT_WINDOW) {
    for (auto it = drawables ().cbegin (); it != drawables ().cend (); ++it)
      if (it->widget && (R (it->widget)->klass->kind & K_SHELL)) kids << it.key ();
  } else {
    auto it = drawables ().constFind (w);
    if (it == drawables ().constEnd ()) return 0;
    Widget wd = it->widget;
    if (parent) {
      *parent = ROOT_WINDOW;
      if (wd && !(R (wd)->klass->kind & K_SHELL))
        for (Widget a = R (wd)->parent; a; a = R (a)->parent)
          if (R (a)->window && R (a)->window != w) { *parent = R (a)->window; break; }
    }
    if (wd)
      for (Widget c : R (wd)->children + R (wd)->popups)
        if (R (c)->window && R (c)->window != w) kids << R (c)->window;
  }
  if (n) *n = kids.size ();
  if (children) {
    *children = kids.isEmpty () ? nullptr : (Window *) malloc (kids.size () * sizeof (Window));
    for (int i = 0; i < kids.size (); i++) (*children)[i] = kids[i];
  }
  return 1;
}

/* one locale, handled by Qt */
Bool XSupportsLocale (void) { return True; }
char *XSetLocaleModifiers (_Xconst char *) { return (char *) ""; }
XSizeHints *XAllocSizeHints (void) { return (XSizeHints *) calloc (1, sizeof (XSizeHints)); }
XWMHints *XAllocWMHints (void) { return (XWMHints *) calloc (1, sizeof (XWMHints)); }
XClassHint *XAllocClassHint (void) { return (XClassHint *) calloc (1, sizeof (XClassHint)); }
int XSetClassHint (Display * a0_, Window a1_, XClassHint * a2_) { MQ_FWD (XSetClassHint, a0_, a0_, a1_, a2_); return 1; }

int XChangeProperty (Display * a0_, Window w, Atom property, Atom a3_, int format, int mode, _Xconst unsigned char *data, int n)
{ MQ_FWD (XChangeProperty, a0_, a0_, w, property, a3_, format, mode, data, n);
  int bytes = n * (format == 32 ? sizeof (long) : format / 8);
  QByteArray &v = drawables ()[w].props[property];
  if (mode == PropModeReplace) v = QByteArray ((const char *) data, bytes);
  else if (mode == PropModeAppend) v.append ((const char *) data, bytes);
  else v.prepend ((const char *) data, bytes);
  return 1;
}

int XGetWindowProperty (Display * a0_, Window w, Atom property, long a3_, long a4_, Bool del, Atom a6_, Atom *actual_type, int *actual_format, unsigned long *nitems, unsigned long *bytes_after, unsigned char **prop)
{ MQ_FWD (XGetWindowProperty, a0_, a0_, w, property, a3_, a4_, del, a6_, actual_type, actual_format, nitems, bytes_after, prop);
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

int XDeleteProperty (Display * a0_, Window w, Atom property) { MQ_FWD (XDeleteProperty, a0_, a0_, w, property); drawables ()[w].props.remove (property); return 1; }
Status XSendEvent (Display * a0_, Window a1_, Bool a2_, long a3_, XEvent * a4_) { MQ_FWD (XSendEvent, a0_, a0_, a1_, a2_, a3_, a4_); return 1; }

Bool XCheckWindowEvent (Display * a0_, Window a1_, long a2_, XEvent * a3_) { MQ_FWD (XCheckWindowEvent, a0_, a0_, a1_, a2_, a3_); return False; }
Bool XCheckMaskEvent (Display * a0_, long a1_, XEvent * a2_) { MQ_FWD (XCheckMaskEvent, a0_, a0_, a1_, a2_); return False; }
Bool XCheckTypedEvent (Display * a0_, int a1_, XEvent * a2_) { MQ_FWD (XCheckTypedEvent, a0_, a0_, a1_, a2_); return False; }
Bool XCheckTypedWindowEvent (Display * a0_, Window a1_, int a2_, XEvent * a3_) { MQ_FWD (XCheckTypedWindowEvent, a0_, a0_, a1_, a2_, a3_); return False; }
Bool XCheckIfEvent (Display *, XEvent *, Bool (*) (Display *, XEvent *, XPointer), XPointer) { return False; }
int XNextEvent (Display * a0_, XEvent *e) { MQ_FWD (XNextEvent, a0_, a0_, e); XtAppNextEvent (mq_app, e); return 0; }
int XPeekEvent (Display * a0_, XEvent *e) { MQ_FWD (XPeekEvent, a0_, a0_, e); XtAppPeekEvent (mq_app, e); return 0; }
int XPutBackEvent (Display * a0_, XEvent * a1_) { MQ_FWD (XPutBackEvent, a0_, a0_, a1_); return 0; }

/* ---------- cursors ---------- */
Cursor XCreateFontCursor (Display * a0_, unsigned int shape)
{ MQ_FWD (XCreateFontCursor, a0_, a0_, shape);
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

Cursor XCreatePixmapCursor (Display * a0_, Pixmap a1_, Pixmap a2_, XColor * a3_, XColor * a4_, unsigned int a5_, unsigned int a6_) { MQ_FWD (XCreatePixmapCursor, a0_, a0_, a1_, a2_, a3_, a4_, a5_, a6_); return 1 + Qt::ArrowCursor; }
Cursor XCreateGlyphCursor (Display * a0_, Font a1_, Font a2_, unsigned int a3_, unsigned int a4_, XColor _Xconst * a5_, XColor _Xconst * a6_) { MQ_FWD (XCreateGlyphCursor, a0_, a0_, a1_, a2_, a3_, a4_, a5_, a6_); return 1 + Qt::ArrowCursor; }
int XFreeCursor (Display * a0_, Cursor a1_) { MQ_FWD (XFreeCursor, a0_, a0_, a1_); return 1; }

int XDefineCursor (Display * a0_, Window w, Cursor c)
{ MQ_FWD (XDefineCursor, a0_, a0_, w, c);
  if (QWidget *qw = window_widget (w)) {
    if (c) qw->setCursor (mq_qcursor ((int) c - 1)); else qw->unsetCursor ();
  }
  return 1;
}

int XUndefineCursor (Display * a0_, Window w) { MQ_FWD (XUndefineCursor, a0_, a0_, w); if (QWidget *qw = window_widget (w)) qw->unsetCursor (); return 1; }

/* ---------- colours ---------- */
Colormap XCreateColormap (Display * a0_, Window a1_, Visual * a2_, int a3_) { MQ_FWD (XCreateColormap, a0_, a0_, a1_, a2_, a3_); return alloc_id (); }
int XFreeColormap (Display * a0_, Colormap a1_) { MQ_FWD (XFreeColormap, a0_, a0_, a1_); return 1; }
Colormap XCopyColormapAndFree (Display * a0_, Colormap a1_) { MQ_FWD (XCopyColormapAndFree, a0_, a0_, a1_); return alloc_id (); }
int XInstallColormap (Display * a0_, Colormap a1_) { MQ_FWD (XInstallColormap, a0_, a0_, a1_); return 1; }
int XSetWindowColormap (Display * a0_, Window a1_, Colormap a2_) { MQ_FWD (XSetWindowColormap, a0_, a0_, a1_, a2_); return 1; }

Status XParseColor (Display * a0_, Colormap a1_, _Xconst char *spec, XColor *def)
{ MQ_FWD (XParseColor, a0_, a0_, a1_, spec, def);
  QColor c;
  if (!parse_color (spec, &c)) return 0;
  def->red = c.red () * 257;
  def->green = c.green () * 257;
  def->blue = c.blue () * 257;
  def->flags = DoRed | DoGreen | DoBlue;
  def->pixel = mq_pixel (c);
  return 1;
}

Status XAllocColor (Display * a0_, Colormap a1_, XColor *def)
{ MQ_FWD (XAllocColor, a0_, a0_, a1_, def);
  def->pixel = ((unsigned long) (def->red >> 8) << 16) | ((def->green >> 8) << 8) | (def->blue >> 8);
  def->red = (def->red >> 8) * 257;
  def->green = (def->green >> 8) * 257;
  def->blue = (def->blue >> 8) * 257;
  return 1;
}

Status XLookupColor (Display *d, Colormap cm, _Xconst char *name, XColor *exact, XColor *screen)
{ MQ_FWD (XLookupColor, d, d, cm, name, exact, screen);
  if (!XParseColor (d, cm, name, exact)) return 0;
  *screen = *exact;
  return 1;
}

Status XAllocNamedColor (Display *d, Colormap cm, _Xconst char *name, XColor *screen, XColor *exact)
{ MQ_FWD (XAllocNamedColor, d, d, cm, name, screen, exact);
  if (!XLookupColor (d, cm, name, exact, screen)) return 0;
  return XAllocColor (d, cm, screen);
}

int XQueryColor (Display * a0_, Colormap a1_, XColor *def)
{ MQ_FWD (XQueryColor, a0_, a0_, a1_, def);
  QColor c = mq_color (def->pixel);
  def->red = c.red () * 257;
  def->green = c.green () * 257;
  def->blue = c.blue () * 257;
  def->flags = DoRed | DoGreen | DoBlue;
  return 1;
}

int XQueryColors (Display *d, Colormap cm, XColor *defs, int n) { MQ_FWD (XQueryColors, d, d, cm, defs, n); for (int i = 0; i < n; i++) XQueryColor (d, cm, &defs[i]); return 1; }
int XFreeColors (Display * a0_, Colormap a1_, unsigned long * a2_, int a3_, unsigned long a4_) { MQ_FWD (XFreeColors, a0_, a0_, a1_, a2_, a3_, a4_); return 1; }
Status XAllocColorCells (Display * a0_, Colormap a1_, Bool a2_, unsigned long * a3_, unsigned int a4_, unsigned long * a5_, unsigned int a6_) { MQ_FWD (XAllocColorCells, a0_, a0_, a1_, a2_, a3_, a4_, a5_, a6_); return 0; }
int XStoreColor (Display * a0_, Colormap a1_, XColor * a2_) { MQ_FWD (XStoreColor, a0_, a0_, a1_, a2_); return 1; }
int XStoreColors (Display * a0_, Colormap a1_, XColor * a2_, int a3_) { MQ_FWD (XStoreColors, a0_, a0_, a1_, a2_, a3_); return 1; }
int XStoreNamedColor (Display * a0_, Colormap a1_, _Xconst char * a2_, unsigned long a3_, int a4_) { MQ_FWD (XStoreNamedColor, a0_, a0_, a1_, a2_, a3_, a4_); return 1; }

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

GC XCreateGC (Display * a0_, Drawable a1_, unsigned long mask, XGCValues *values)
{ MQ_FWD (XCreateGC, a0_, a0_, a1_, mask, values);
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

int XChangeGC (Display * a0_, GC gc, unsigned long mask, XGCValues *values) { MQ_FWD (XChangeGC, a0_, a0_, gc, mask, values); gc_apply (gc, mask, values); return 1; }
int XCopyGC (Display * a0_, GC src, unsigned long mask, GC dst) { MQ_FWD (XCopyGC, a0_, a0_, src, mask, dst); gc_apply (dst, mask, &src->v); return 1; }
Status XGetGCValues (Display * a0_, GC gc, unsigned long a2_, XGCValues *values) { MQ_FWD (XGetGCValues, a0_, a0_, gc, a2_, values); *values = gc->v; return 1; }
int XFreeGC (Display * a0_, GC gc) { MQ_FWD (XFreeGC, a0_, a0_, gc); if (gc != s_default_gc) delete gc; return 1; }
GContext XGContextFromGC (GC gc) { return (GContext) (quintptr) gc; }
int XSetForeground (Display * a0_, GC gc, unsigned long p) { MQ_FWD (XSetForeground, a0_, a0_, gc, p); gc->v.foreground = p; return 1; }
int XSetBackground (Display * a0_, GC gc, unsigned long p) { MQ_FWD (XSetBackground, a0_, a0_, gc, p); gc->v.background = p; return 1; }
int XSetFunction (Display * a0_, GC gc, int f) { MQ_FWD (XSetFunction, a0_, a0_, gc, f); gc->v.function = f; return 1; }
int XSetPlaneMask (Display * a0_, GC gc, unsigned long m) { MQ_FWD (XSetPlaneMask, a0_, a0_, gc, m); gc->v.plane_mask = m; return 1; }
int XSetFillStyle (Display * a0_, GC gc, int s) { MQ_FWD (XSetFillStyle, a0_, a0_, gc, s); gc->v.fill_style = s; return 1; }
int XSetFillRule (Display * a0_, GC gc, int r) { MQ_FWD (XSetFillRule, a0_, a0_, gc, r); gc->v.fill_rule = r; return 1; }
int XSetArcMode (Display * a0_, GC gc, int m) { MQ_FWD (XSetArcMode, a0_, a0_, gc, m); gc->v.arc_mode = m; return 1; }
int XSetFont (Display * a0_, GC gc, Font f) { MQ_FWD (XSetFont, a0_, a0_, gc, f); gc->v.font = f; return 1; }
int XSetTile (Display * a0_, GC gc, Pixmap t) { MQ_FWD (XSetTile, a0_, a0_, gc, t); gc->v.tile = t; return 1; }
int XSetStipple (Display * a0_, GC gc, Pixmap s) { MQ_FWD (XSetStipple, a0_, a0_, gc, s); gc->v.stipple = s; return 1; }
int XSetTSOrigin (Display * a0_, GC gc, int x, int y) { MQ_FWD (XSetTSOrigin, a0_, a0_, gc, x, y); gc->v.ts_x_origin = x; gc->v.ts_y_origin = y; return 1; }
int XSetGraphicsExposures (Display * a0_, GC gc, Bool b) { MQ_FWD (XSetGraphicsExposures, a0_, a0_, gc, b); gc->v.graphics_exposures = b; return 1; }
int XSetSubwindowMode (Display * a0_, GC gc, int m) { MQ_FWD (XSetSubwindowMode, a0_, a0_, gc, m); gc->v.subwindow_mode = m; return 1; }
int XSetClipOrigin (Display * a0_, GC gc, int x, int y) { MQ_FWD (XSetClipOrigin, a0_, a0_, gc, x, y); gc->v.clip_x_origin = x; gc->v.clip_y_origin = y; return 1; }
int XSetClipMask (Display * a0_, GC gc, Pixmap m) { MQ_FWD (XSetClipMask, a0_, a0_, gc, m); gc->v.clip_mask = m; gc->has_clip_rects = false; return 1; }

int XSetLineAttributes (Display * a0_, GC gc, unsigned int width, int style, int cap, int join)
{ MQ_FWD (XSetLineAttributes, a0_, a0_, gc, width, style, cap, join);
  gc->v.line_width = width; gc->v.line_style = style; gc->v.cap_style = cap; gc->v.join_style = join;
  return 1;
}

int XSetDashes (Display * a0_, GC gc, int offset, _Xconst char *list, int n)
{ MQ_FWD (XSetDashes, a0_, a0_, gc, offset, list, n);
  gc->v.dash_offset = offset;
  gc->dashes.clear ();
  for (int i = 0; i < n; i++) gc->dashes << (unsigned char) list[i];
  if (gc->dashes.size () % 2) gc->dashes += gc->dashes;
  return 1;
}

int XSetClipRectangles (Display * a0_, GC gc, int x, int y, XRectangle *rects, int n, int a6_)
{ MQ_FWD (XSetClipRectangles, a0_, a0_, gc, x, y, rects, n, a6_);
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
int XDrawPoint (Display * a0_, Drawable d, GC gc, int x, int y)
{ MQ_FWD (XDrawPoint, a0_, a0_, d, gc, x, y);
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawPoint (x, y);
  return 1;
}

int XDrawPoints (Display * a0_, Drawable d, GC gc, XPoint *pts, int n, int mode)
{ MQ_FWD (XDrawPoints, a0_, a0_, d, gc, pts, n, mode);
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  int x = 0, y = 0;
  for (int i = 0; i < n; i++) {
    if (mode == CoordModePrevious && i) { x += pts[i].x; y += pts[i].y; } else { x = pts[i].x; y = pts[i].y; }
    g.p.drawPoint (x, y);
  }
  return 1;
}

int XDrawLine (Display * a0_, Drawable d, GC gc, int x1, int y1, int x2, int y2)
{ MQ_FWD (XDrawLine, a0_, a0_, d, gc, x1, y1, x2, y2);
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

int XDrawLines (Display * a0_, Drawable d, GC gc, XPoint *pts, int n, int mode)
{ MQ_FWD (XDrawLines, a0_, a0_, d, gc, pts, n, mode);
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawPolyline (make_poly (pts, n, mode));
  return 1;
}

int XDrawSegments (Display * a0_, Drawable d, GC gc, XSegment *segs, int n)
{ MQ_FWD (XDrawSegments, a0_, a0_, d, gc, segs, n);
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawLine (segs[i].x1, segs[i].y1, segs[i].x2, segs[i].y2);
  return 1;
}

int XDrawRectangle (Display * a0_, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h)
{ MQ_FWD (XDrawRectangle, a0_, a0_, d, gc, x, y, w, h);
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawRect (x, y, w, h);
  return 1;
}

int XDrawRectangles (Display * a0_, Drawable d, GC gc, XRectangle *r, int n)
{ MQ_FWD (XDrawRectangles, a0_, a0_, d, gc, r, n);
  GcPainter g (d, gc, false);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawRect (r[i].x, r[i].y, r[i].width, r[i].height);
  return 1;
}

int XFillRectangle (Display * a0_, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h)
{ MQ_FWD (XFillRectangle, a0_, a0_, d, gc, x, y, w, h);
  GcPainter g (d, gc, true);
  if (g.ok) g.p.drawRect (x, y, w, h);
  return 1;
}

int XFillRectangles (Display * a0_, Drawable d, GC gc, XRectangle *r, int n)
{ MQ_FWD (XFillRectangles, a0_, a0_, d, gc, r, n);
  GcPainter g (d, gc, true);
  if (!g.ok) return 0;
  for (int i = 0; i < n; i++) g.p.drawRect (r[i].x, r[i].y, r[i].width, r[i].height);
  return 1;
}

int XDrawArc (Display * a0_, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h, int a1, int a2)
{ MQ_FWD (XDrawArc, a0_, a0_, d, gc, x, y, w, h, a1, a2);
  GcPainter g (d, gc, false);
  if (g.ok) g.p.drawArc (x, y, w, h, a1 / 4, a2 / 4);
  return 1;
}

int XDrawArcs (Display *dpy, Drawable d, GC gc, XArc *arcs, int n)
{ MQ_FWD (XDrawArcs, dpy, dpy, d, gc, arcs, n);
  for (int i = 0; i < n; i++) XDrawArc (dpy, d, gc, arcs[i].x, arcs[i].y, arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
  return 1;
}

int XFillArc (Display * a0_, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h, int a1, int a2)
{ MQ_FWD (XFillArc, a0_, a0_, d, gc, x, y, w, h, a1, a2);
  GcPainter g (d, gc, true);
  if (!g.ok) return 0;
  if (qAbs (a2) >= 360 * 64) g.p.drawEllipse (x, y, w, h);
  else if (gc->v.arc_mode == ArcChord) g.p.drawChord (x, y, w, h, a1 / 4, a2 / 4);
  else g.p.drawPie (x, y, w, h, a1 / 4, a2 / 4);
  return 1;
}

int XFillArcs (Display *dpy, Drawable d, GC gc, XArc *arcs, int n)
{ MQ_FWD (XFillArcs, dpy, dpy, d, gc, arcs, n);
  for (int i = 0; i < n; i++) XFillArc (dpy, d, gc, arcs[i].x, arcs[i].y, arcs[i].width, arcs[i].height, arcs[i].angle1, arcs[i].angle2);
  return 1;
}

int XFillPolygon (Display * a0_, Drawable d, GC gc, XPoint *pts, int n, int a5_, int mode)
{ MQ_FWD (XFillPolygon, a0_, a0_, d, gc, pts, n, a5_, mode);
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

int XDrawString (Display * a0_, Drawable d, GC gc, int x, int y, _Xconst char *s, int len)
{ MQ_FWD (XDrawString, a0_, a0_, d, gc, x, y, s, len);
  draw_text (d, gc, x, y, QString::fromLatin1 (s, len), false);
  return 1;
}

int XDrawImageString (Display * a0_, Drawable d, GC gc, int x, int y, _Xconst char *s, int len)
{ MQ_FWD (XDrawImageString, a0_, a0_, d, gc, x, y, s, len);
  draw_text (d, gc, x, y, QString::fromLatin1 (s, len), true);
  return 1;
}

int XDrawString16 (Display * a0_, Drawable d, GC gc, int x, int y, _Xconst XChar2b *s, int len)
{ MQ_FWD (XDrawString16, a0_, a0_, d, gc, x, y, s, len);
  QString t;
  for (int i = 0; i < len; i++) t += QChar ((s[i].byte1 << 8) | s[i].byte2);
  draw_text (d, gc, x, y, t, false);
  return 1;
}

int XDrawText (Display *dpy, Drawable d, GC gc, int x, int y, XTextItem *items, int n)
{ MQ_FWD (XDrawText, dpy, dpy, d, gc, x, y, items, n);
  for (int i = 0; i < n; i++) {
    x += items[i].delta;
    if (items[i].font != None) XSetFont (dpy, gc, items[i].font);
    XDrawString (dpy, d, gc, x, y, items[i].chars, items[i].nchars);
    x += XTextWidth (XQueryFont (dpy, gc->v.font), items[i].chars, items[i].nchars);
  }
  return 1;
}

int XCopyArea (Display * a0_, Drawable src, Drawable dst, GC gc, int sx, int sy, unsigned int w, unsigned int h, int dx, int dy)
{ MQ_FWD (XCopyArea, a0_, a0_, src, dst, gc, sx, sy, w, h, dx, dy);
  QImage *s = target (src);
  if (!s) return 0;
  QImage piece = s->copy (sx, sy, w, h);
  GcPainter g (dst, gc, true);
  if (g.ok) g.p.drawImage (dx, dy, piece);
  return 1;
}

int XCopyPlane (Display * a0_, Drawable src, Drawable dst, GC gc, int sx, int sy, unsigned int w, unsigned int h, int dx, int dy, unsigned long a10_)
{ MQ_FWD (XCopyPlane, a0_, a0_, src, dst, gc, sx, sy, w, h, dx, dy, a10_);
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
Pixmap XCreatePixmap (Display * a0_, Drawable a1_, unsigned int w, unsigned int h, unsigned int depth)
{ MQ_FWD (XCreatePixmap, a0_, a0_, a1_, w, h, depth);
  QImage img (QSize (qMax (1u, w), qMax (1u, h)), QImage::Format_RGB32);
  img.fill (Qt::black);
  return new_pixmap (img, depth == 1 ? 1 : 24);
}

int XFreePixmap (Display * a0_, Pixmap p) { MQ_FWD (XFreePixmap, a0_, a0_, p); drawables ().remove (p); return 1; }

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

Pixmap XCreateBitmapFromData (Display * a0_, Drawable a1_, _Xconst char *data, unsigned int w, unsigned int h)
{ MQ_FWD (XCreateBitmapFromData, a0_, a0_, a1_, data, w, h);
  return new_pixmap (bitmap_image (data, w, h, Qt::white, Qt::black), 1);
}

Pixmap XCreatePixmapFromBitmapData (Display * a0_, Drawable a1_, char *data, unsigned int w, unsigned int h, unsigned long fg, unsigned long bg, unsigned int depth)
{ MQ_FWD (XCreatePixmapFromBitmapData, a0_, a0_, a1_, data, w, h, fg, bg, depth);
  return new_pixmap (bitmap_image (data, w, h, depth == 1 ? QColor (fg ? Qt::white : Qt::black) : mq_color (fg),
                                   depth == 1 ? QColor (bg ? Qt::white : Qt::black) : mq_color (bg)), depth == 1 ? 1 : 24);
}

int XReadBitmapFile (Display * a0_, Drawable a1_, _Xconst char *filename, unsigned int *w, unsigned int *h, Pixmap *bitmap, int *xh, int *yh)
{ MQ_FWD (XReadBitmapFile, a0_, a0_, a1_, filename, w, h, bitmap, xh, yh);
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

XImage *XCreateImage (Display * a0_, Visual * a1_, unsigned int a2_, int a3_, int a4_, char *data, unsigned int w, unsigned int h, int a8_, int a9_)
{ MQ_FWD (XCreateImage, a0_, a0_, a1_, a2_, a3_, a4_, data, w, h, a8_, a9_);
  return make_ximage (w, h, data);
}

XImage *XGetImage (Display * a0_, Drawable d, int x, int y, unsigned int w, unsigned int h, unsigned long a6_, int a7_)
{ MQ_FWD (XGetImage, a0_, a0_, d, x, y, w, h, a6_, a7_);
  QImage *s = target (d);
  if (!s) return nullptr;
  QImage part = s->copy (x, y, w, h).convertToFormat (QImage::Format_RGB32);
  char *data = (char *) malloc (w * h * 4);
  for (unsigned r = 0; r < h; r++) memcpy (data + r * w * 4, part.constScanLine (r), w * 4);
  return make_ximage (w, h, data);
}

int XPutImage (Display * a0_, Drawable d, GC gc, XImage *im, int sx, int sy, int dx, int dy, unsigned int w, unsigned int h)
{ MQ_FWD (XPutImage, a0_, a0_, d, gc, im, sx, sy, dx, dy, w, h);
  QImage img (w, h, QImage::Format_RGB32);
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < w; x++) img.setPixel (x, y, 0xff000000 | XGetPixel (im, sx + x, sy + y));
  GcPainter g (d, gc, true);
  if (g.ok) g.p.drawImage (dx, dy, img);
  return 1;
}

/* ---------- fonts ---------- */
Font XLoadFont (Display * a0_, _Xconst char *name)
{ MQ_FWD (XLoadFont, a0_, a0_, name);
  bool ok;
  QFont f = mq_font_from_xlfd (QString::fromLatin1 (name), &ok);
  Font id = alloc_id ();
  fonts ().insert (id, f);
  return id;
}

XFontStruct *XQueryFont (Display * a0_, XID fid) { MQ_FWD (XQueryFont, a0_, a0_, fid); return make_font_struct (fid, mq_font_for (fid)); }

XFontStruct *XLoadQueryFont (Display *d, _Xconst char *name)
{ MQ_FWD (XLoadQueryFont, d, d, name);
  Font id = XLoadFont (d, name);
  return make_font_struct (id, mq_font_for (id));
}

int XFreeFont (Display * a0_, XFontStruct *fs) { MQ_FWD (XFreeFont, a0_, a0_, fs); if (fs) { fonts ().remove (fs->fid); free (fs); } return 1; }
int XUnloadFont (Display * a0_, Font f) { MQ_FWD (XUnloadFont, a0_, a0_, f); fonts ().remove (f); return 1; }

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

char **XListFonts (Display * a0_, _Xconst char *pattern, int a2_, int *count)
{ MQ_FWD (XListFonts, a0_, a0_, pattern, a2_, count);
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

KeySym XKeycodeToKeysym (Display * a0_, KeyCode kc, int index)
{ MQ_FWD (XKeycodeToKeysym, a0_, a0_, kc, index);
  KeySym k = kc;
  if (index == 0 && k < 0x100) return tolower ((int) k);
  return k;
}

KeyCode XKeysymToKeycode (Display * a0_, KeySym k) { MQ_FWD (XKeysymToKeycode, a0_, a0_, k); return (KeyCode) (k & 0xff); }

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

int XDisplayKeycodes (Display * a0_, int *min, int *max) { MQ_FWD (XDisplayKeycodes, a0_, a0_, min, max); *min = 8; *max = 255; return 1; }
int XRefreshKeyboardMapping (XMappingEvent *) { return 1; }
XModifierKeymap *XGetModifierMapping (Display * a0_) { MQ_FWD (XGetModifierMapping, a0_, a0_); return (XModifierKeymap *) calloc (1, sizeof (XModifierKeymap)); }
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

int XWMGeometry (Display * a0_, int a1_, _Xconst char *user, _Xconst char *def, unsigned int a4_, XSizeHints * a5_, int *x, int *y, int *w, int *h, int *gravity)
{ MQ_FWD (XWMGeometry, a0_, a0_, a1_, user, def, a4_, a5_, x, y, w, h, gravity);
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
Bool XShapeQueryExtension (Display * a0_, int *event_base, int *error_base)
{ MQ_FWD (XShapeQueryExtension, a0_, a0_, event_base, error_base);
  if (event_base) *event_base = 64;
  if (error_base) *error_base = 128;
  return True;
}

Status XShapeQueryVersion (Display * a0_, int *major, int *minor) { MQ_FWD (XShapeQueryVersion, a0_, a0_, major, minor); *major = 1; *minor = 1; return 1; }

void XShapeCombineMask (Display * a0_, Window dest, int a2_, int x, int y, Pixmap src, int op)
{ MQ_FWD (XShapeCombineMask, a0_, a0_, dest, a2_, x, y, src, op);
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

void XShapeCombineRectangles (Display * a0_, Window dest, int a2_, int x, int y, XRectangle *rects, int n, int a7_, int a8_)
{ MQ_FWD (XShapeCombineRectangles, a0_, a0_, dest, a2_, x, y, rects, n, a7_, a8_);
  QWidget *qw = window_widget (dest);
  if (!qw) return;
  QRegion r;
  for (int i = 0; i < n; i++) r += QRect (rects[i].x + x, rects[i].y + y, rects[i].width, rects[i].height);
  qw->window ()->setMask (r);
}

} /* extern "C" */

/* ======================================================================
 * Regions (QRegion) and the context manager
 * ====================================================================== */
struct _XRegion { QRegion r; };
Region mq_region_for (const QRect &r) { auto *g = new _XRegion; g->r = QRegion (r); return g; }
void mq_region_free (Region r) { delete r; }

extern "C" {

Region XCreateRegion (void) { return new _XRegion; }
int XDestroyRegion (Region r) { delete r; return 1; }
Bool XEmptyRegion (Region r) { return !r || r->r.isEmpty (); }
Bool XEqualRegion (Region a, Region b) { return a->r == b->r; }
Bool XPointInRegion (Region r, int x, int y) { return r->r.contains (QPoint (x, y)); }
int XRectInRegion (Region r, int x, int y, unsigned int w, unsigned int h)
{
  QRect rect (x, y, w, h);
  if (!r->r.intersects (rect)) return RectangleOut;
  return (r->r & QRegion (rect)) == QRegion (rect) ? RectangleIn : RectanglePart;
}
int XClipBox (Region r, XRectangle *out)
{
  QRect b = r->r.boundingRect ();
  out->x = b.x (); out->y = b.y (); out->width = b.width (); out->height = b.height ();
  return 1;
}
int XOffsetRegion (Region r, int dx, int dy) { r->r.translate (dx, dy); return 1; }
int XShrinkRegion (Region r, int dx, int dy)
{
  QRegion out;
  for (const QRect &q : r->r) out += q.adjusted (dx, dy, -dx, -dy);
  r->r = out;
  return 1;
}
int XUnionRectWithRegion (XRectangle *rect, Region src, Region dst) { dst->r = src->r + QRect (rect->x, rect->y, rect->width, rect->height); return 1; }
int XUnionRegion (Region a, Region b, Region dst) { dst->r = a->r + b->r; return 1; }
int XIntersectRegion (Region a, Region b, Region dst) { dst->r = a->r & b->r; return 1; }
int XSubtractRegion (Region a, Region b, Region dst) { dst->r = a->r - b->r; return 1; }
int XXorRegion (Region a, Region b, Region dst) { dst->r = a->r ^ b->r; return 1; }
Region XPolygonRegion (XPoint *pts, int n, int rule)
{
  QPolygon p;
  for (int i = 0; i < n; i++) p << QPoint (pts[i].x, pts[i].y);
  auto *r = new _XRegion;
  r->r = QRegion (p, rule == WindingRule ? Qt::WindingFill : Qt::OddEvenFill);
  return r;
}

int XSetRegion (Display *, GC gc, Region r)
{
  gc->clip_rects.clear ();
  for (const QRect &q : r->r) gc->clip_rects << q;
  gc->v.clip_x_origin = gc->v.clip_y_origin = 0;
  gc->has_clip_rects = true;
  return 1;
}

/* the X context manager: data per (resource id, context) */
static QHash<QPair<XID, XContext>, XPointer> &contexts ()
{
  static QHash<QPair<XID, XContext>, XPointer> h;
  return h;
}

int XSaveContext (Display *, XID rid, XContext ctx, _Xconst char *data) { contexts ().insert ({ rid, ctx }, (XPointer) data); return 0; }
int XFindContext (Display *, XID rid, XContext ctx, XPointer *data)
{
  auto it = contexts ().constFind ({ rid, ctx });
  if (it == contexts ().constEnd ()) return XCNOENT;
  *data = it.value ();
  return 0;
}
int XDeleteContext (Display *, XID rid, XContext ctx) { return contexts ().remove ({ rid, ctx }) ? 0 : XCNOENT; }

} /* extern "C" */
