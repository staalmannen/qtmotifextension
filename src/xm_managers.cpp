/*
 * Motif on Qt - managers: XmManager, XmBulletinBoard, XmForm (attachment
 * layout), XmRowColumn (work areas, radio boxes, menu bars, pulldown, popup
 * and option menus), XmFrame, XmDrawingArea, XmScrolledWindow, XmMainWindow,
 * XmPanedWindow, and the menu creation functions.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstring>

#define MGRK (K_RECT | K_WIDGET | K_COMPOSITE | K_CONSTRAINT | K_MANAGER)

static QList<Widget> managed_children (Widget w)
{
  QList<Widget> out;
  for (Widget c : R (w)->children) if (R (c)->managed && !R (c)->destroying) out << c;
  return out;
}

static QSize pref_size (Widget c)
{
  QWidget *qw = mq_qw (c);
  if (!qw) return QSize ();
  QSize s = qw->sizeHint ().expandedTo (qw->minimumSizeHint ()).expandedTo (qw->minimumSize ());
  if (mq_isset (c, XmNwidth) && mq_geti (c, XmNwidth) > 0) s.setWidth (qMax (mq_geti (c, XmNwidth), qw->minimumWidth ()));
  if (mq_isset (c, XmNheight) && mq_geti (c, XmNheight) > 0) s.setHeight (qMax (mq_geti (c, XmNheight), qw->minimumHeight ()));
  return s.expandedTo (QSize (1, 1));
}

static void manager_apply (Widget w, const QByteArray &res) { mq_apply_common (w, res); }

/* A QLayout over the managed children of a widget; subclasses compute
 * geometry from the Motif resources. */
class MqLayout : public QLayout {
public:
  Widget w;
  QList<QLayoutItem *> items;
  explicit MqLayout (Widget wd, QWidget *parent) : QLayout (parent), w (wd) { setContentsMargins (0, 0, 0, 0); setSpacing (0); }
  ~MqLayout () override { qDeleteAll (items); }
  void addItem (QLayoutItem *i) override { items << i; }
  int count () const override { return items.size (); }
  QLayoutItem *itemAt (int i) const override { return items.value (i); }
  QLayoutItem *takeAt (int i) override { return i >= 0 && i < items.size () ? items.takeAt (i) : nullptr; }
  QSize minimumSize () const override { return sizeHint (); }
  Widget child_of (QLayoutItem *it) const { return it->widget () ? mq_from_qobject (it->widget ()) : nullptr; }
  QList<QPair<Widget, QLayoutItem *>> placed () const
  {
    QList<QPair<Widget, QLayoutItem *>> out;
    for (Widget c : R (w)->children)
      for (QLayoutItem *it : items)
        if (it->widget () && it->widget () == mq_qw (c) && R (c)->managed) out << qMakePair (c, it);
    return out;
  }
};

template <class L> static L *layout_of (Widget w)
{
  QWidget *content = mq_content (w);
  if (!content) return nullptr;
  if (auto *l = dynamic_cast<L *> (content->layout ())) return l;
  delete content->layout ();
  return new L (w, content);
}

template <class L> static void layout_place (Widget parent, Widget child)
{
  QWidget *cw = mq_qw (child), *content = mq_content (parent);
  if (!cw || !content) return;
  L *l = layout_of<L> (parent);
  if (cw->parentWidget () != content) cw->setParent (content);
  if (l->indexOf (cw) < 0) l->addWidget (cw);
  l->invalidate ();
}

static void layout_unplace (Widget parent, Widget child)
{
  QWidget *cw = mq_qw (child), *content = mq_content (parent);
  if (cw) cw->hide ();
  if (cw && content && content->layout ()) { content->layout ()->removeWidget (cw); content->layout ()->invalidate (); }
}

static void layout_relayout (Widget w)
{
  if (QWidget *c = mq_content (w)) if (c->layout ()) { c->layout ()->invalidate (); c->updateGeometry (); }
}

/* ======================================================================
 * XmBulletinBoard and XmDrawingArea: children at their x/y
 * ====================================================================== */
static int margin (Widget w, const char *res, int fallback)
{
  return mq_isset (w, res) ? mq_geti (w, res) : fallback;
}

class PlaceLayout : public MqLayout {
public:
  using MqLayout::MqLayout;
  int default_margin () const { return mq_class_is (w, "XmDrawingArea") ? 10 : 10; }
  QSize sizeHint () const override
  {
    int mw = margin (w, XmNmarginWidth, default_margin ()), mh = margin (w, XmNmarginHeight, default_margin ());
    int r = 0, b = 0;
    for (auto &p : placed ()) {
      QSize s = pref_size (p.first);
      r = qMax (r, mq_geti (p.first, XmNx) + s.width ());
      b = qMax (b, mq_geti (p.first, XmNy) + s.height ());
    }
    return QSize (r + mw, b + mh);
  }
  void setGeometry (const QRect &rect) override
  {
    QLayout::setGeometry (rect);
    int mw = margin (w, XmNmarginWidth, default_margin ()), mh = margin (w, XmNmarginHeight, default_margin ());
    for (auto &p : placed ()) {
      int x = mq_geti (p.first, XmNx), y = mq_geti (p.first, XmNy);
      if (!mq_isset (p.first, XmNx) && x == 0) x = mw;
      if (!mq_isset (p.first, XmNy) && y == 0) y = mh;
      p.second->setGeometry (QRect (rect.topLeft () + QPoint (x, y), pref_size (p.first)));
    }
  }
};

static QObject *bb_create (Widget) { return new QWidget; }

static void bb_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (res == XmNmarginWidth || res == XmNmarginHeight) layout_relayout (w);
  if (res.isEmpty () || res == XmNdefaultButton) {
    Widget def = (Widget) mq_get (w, XmNdefaultButton);
    if (auto *b = def ? mq_q<QPushButton> (def) : nullptr) { b->setDefault (true); b->setAutoDefault (true); }
  }
}

static void bb_initialize (Widget w)
{
  /* Return activates the default button; Escape the cancel button */
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  auto *esc = new QShortcut (QKeySequence (Qt::Key_Escape), qw);
  esc->setContext (Qt::WidgetWithChildrenShortcut);
  QObject::connect (esc, &QShortcut::activated, qw, [w] () {
    Widget c = (Widget) mq_get (w, XmNcancelButton);
    if (auto *b = c ? mq_q<QAbstractButton> (c) : nullptr) b->click ();
  });
}

/* ======================================================================
 * XmForm
 * ====================================================================== */
class FormLayout : public MqLayout {
public:
  using MqLayout::MqLayout;
  mutable QSize cached;
  void invalidate () override { cached = QSize (); MqLayout::invalidate (); }

  struct Edge { int lo, hi; };

  /* one axis: 0 horizontal (left/right), 1 vertical (top/bottom) */
  QHash<Widget, Edge> solve (int axis, int size) const
  {
    QHash<Widget, Edge> done;
    QSet<Widget> visiting;
    int fb = qMax (1, mq_geti (w, XmNfractionBase));
    int m = axis == 0 ? margin (w, XmNmarginWidth, 0) : margin (w, XmNmarginHeight, 0);
    int spacing = axis == 0 ? mq_geti (w, XmNhorizontalSpacing) : mq_geti (w, XmNverticalSpacing);
    const char *att[2] = { axis == 0 ? XmNleftAttachment : XmNtopAttachment, axis == 0 ? XmNrightAttachment : XmNbottomAttachment };
    const char *wid[2] = { axis == 0 ? XmNleftWidget : XmNtopWidget, axis == 0 ? XmNrightWidget : XmNbottomWidget };
    const char *pos[2] = { axis == 0 ? XmNleftPosition : XmNtopPosition, axis == 0 ? XmNrightPosition : XmNbottomPosition };
    const char *off[2] = { axis == 0 ? XmNleftOffset : XmNtopOffset, axis == 0 ? XmNrightOffset : XmNbottomOffset };

    std::function<Edge (Widget)> edge = [&] (Widget c) -> Edge {
      auto it = done.constFind (c);
      if (it != done.constEnd ()) return it.value ();
      int p = axis == 0 ? pref_size (c).width () : pref_size (c).height ();
      int coord = mq_geti (c, axis == 0 ? XmNx : XmNy);
      if (visiting.contains (c)) return Edge { coord, coord + p };
      visiting.insert (c);
      bool known[2] = { false, false };
      int v[2] = { 0, 0 };
      for (int side = 0; side < 2; side++) {
        int type = mq_geti (c, att[side]);
        int o = mq_isset (c, off[side]) ? mq_geti (c, off[side]) : (type == XmATTACH_POSITION ? 0 : spacing);
        Widget other = (Widget) mq_get (c, wid[side]);
        if ((type == XmATTACH_WIDGET || type == XmATTACH_OPPOSITE_WIDGET) && (!other || R (other)->parent != w)) type = XmATTACH_FORM;
        switch (type) {
        case XmATTACH_FORM: v[side] = side == 0 ? m + o : size - m - o; known[side] = true; break;
        case XmATTACH_OPPOSITE_FORM: v[side] = side == 0 ? size - m - o : m + o; known[side] = true; break;
        case XmATTACH_POSITION: v[side] = size * mq_geti (c, pos[side]) / fb + (side == 0 ? o : -o); known[side] = true; break;
        case XmATTACH_WIDGET: {
          Edge e = edge (other);
          v[side] = side == 0 ? e.hi + o : e.lo - o;
          known[side] = true;
          break;
        }
        case XmATTACH_OPPOSITE_WIDGET: {
          Edge e = edge (other);
          v[side] = side == 0 ? e.lo + o : e.hi - o;
          known[side] = true;
          break;
        }
        default: break;
        }
      }
      Edge e;
      if (known[0] && known[1]) e = Edge { v[0], v[1] };   /* may be negative while sizing; clamped when placed */
      else if (known[0]) e = Edge { v[0], v[0] + p };
      else if (known[1]) e = Edge { v[1] - p, v[1] };
      else e = Edge { coord, coord + p };
      visiting.remove (c);
      done.insert (c, e);
      return e;
    };
    for (Widget c : R (w)->children) if (!R (c)->destroying && mq_qw (c)) edge (c);
    return done;
  }

  int preferred (int axis) const
  {
    int m = axis == 0 ? margin (w, XmNmarginWidth, 0) : margin (w, XmNmarginHeight, 0);
    int size = 2 * m;
    QHash<Widget, int> last;
    QSet<Widget> rigid;
    for (int iter = 0; iter < 80; iter++) {
      QHash<Widget, Edge> e = solve (axis, size);
      int need = size;
      for (Widget c : managed_children (w)) {
        if (!e.contains (c) || rigid.contains (c)) continue;
        int p = axis == 0 ? pref_size (c).width () : pref_size (c).height ();
        int have = e[c].hi - e[c].lo;
        if (have < p) {
          if (last.contains (c) && last[c] >= have && iter > 0) { rigid.insert (c); continue; }
          need = qMax (need, size + (p - have));
        }
        last[c] = have;
        need = qMax (need, e[c].hi + m);
        if (e[c].lo < m) need = qMax (need, size + (m - e[c].lo));
      }
      if (need == size) break;
      size = need;
    }
    return size;
  }

  QSize sizeHint () const override
  {
    if (!cached.isValid ()) {
      int width = preferred (0), height = preferred (1);
      cached = QSize (width, height);
    }
    return cached;
  }

  void setGeometry (const QRect &rect) override
  {
    QLayout::setGeometry (rect);
    QHash<Widget, Edge> h = solve (0, rect.width ()), v = solve (1, rect.height ());
    for (auto &p : placed ()) {
      if (!h.contains (p.first) || !v.contains (p.first)) continue;
      Edge eh = h[p.first], ev = v[p.first];
      p.second->setGeometry (QRect (rect.x () + eh.lo, rect.y () + ev.lo, qMax (1, eh.hi - eh.lo), qMax (1, ev.hi - ev.lo)));
    }
  }
};

/* ======================================================================
 * XmRowColumn
 * ====================================================================== */
struct RcExt {
  QPoint popup_pos;
  bool have_pos = false;
  Widget posted_from = nullptr;
  QPointer<QObject> popup_filter;
};

unsigned char mq_menu_type (Widget w)
{
  if (!w || !mq_class_is (w, "XmRowColumn")) return XmWORK_AREA;
  return (unsigned char) mq_geti (w, XmNrowColumnType);
}

bool mq_in_menu (Widget w)
{
  if (!w || !R (w)->parent) return false;
  int t = mq_menu_type (R (w)->parent);
  return t == XmMENU_PULLDOWN || t == XmMENU_POPUP || t == XmMENU_BAR;
}

static int rc_orientation (Widget w)
{
  int o = mq_geti (w, XmNorientation);
  if (o == XmVERTICAL || o == XmHORIZONTAL) return o;
  int t = mq_menu_type (w);
  return t == XmMENU_BAR || t == XmMENU_OPTION ? XmHORIZONTAL : XmVERTICAL;
}

static int rc_packing (Widget w)
{
  int p = mq_geti (w, XmNpacking);
  if (p == XmPACK_TIGHT || p == XmPACK_COLUMN || p == XmPACK_NONE) return p;
  return mq_getb (w, XmNradioBehavior) && mq_menu_type (w) == XmWORK_AREA ? XmPACK_COLUMN : XmPACK_TIGHT;
}

class RcLayout : public MqLayout {
public:
  using MqLayout::MqLayout;
  int mw () const { return margin (w, XmNmarginWidth, 3); }
  int mh () const { return margin (w, XmNmarginHeight, 3); }
  int sp () const { return margin (w, XmNspacing, 3); }

  /* cell rectangles for children, relative to the content origin */
  QList<QRect> cells (const QList<Widget> &kids, QSize avail, QSize *total) const
  {
    QList<QRect> out;
    int orient = rc_orientation (w), pack = rc_packing (w);
    int n = kids.size ();
    int s = sp ();
    if (pack == XmPACK_NONE) {
      int r = 0, b = 0;
      for (Widget c : kids) {
        QRect rr (mq_geti (c, XmNx), mq_geti (c, XmNy), pref_size (c).width (), pref_size (c).height ());
        out << rr;
        r = qMax (r, rr.right () + 1);
        b = qMax (b, rr.bottom () + 1);
      }
      if (total) *total = QSize (r + mw (), b + mh ());
      return out;
    }
    int ncols = qMax (1, mq_geti (w, XmNnumColumns));
    if (pack == XmPACK_TIGHT) ncols = 1;
    int major = (n + ncols - 1) / qMax (1, ncols);   /* entries per column (vertical) or per row (horizontal) */
    if (major < 1) major = 1;
    /* uniform cells for PACK_COLUMN, per-line sizes for PACK_TIGHT */
    int cw = 0, ch = 0;
    for (Widget c : kids) { QSize p = pref_size (c); cw = qMax (cw, p.width ()); ch = qMax (ch, p.height ()); }
    bool adjust_last = !mq_isset (w, XmNadjustLast) || mq_getb (w, XmNadjustLast);
    if (orient == XmVERTICAL) {
      int cols = pack == XmPACK_COLUMN ? ncols : 1;
      int per = pack == XmPACK_COLUMN ? major : n;
      int content_w = cols * cw + (cols - 1) * s;
      int width = qMax (content_w, avail.width () - 2 * mw ());
      int colw = cols > 0 ? (pack == XmPACK_TIGHT ? width : cw) : cw;
      int y = 0, maxh = 0;
      for (int i = 0; i < n; i++) {
        int col = per ? i / per : 0, row = per ? i % per : i;
        int h = pack == XmPACK_COLUMN ? ch : pref_size (kids[i]).height ();
        if (row == 0) y = 0;
        int x = col * (colw + s);
        int wdt = colw;
        if (adjust_last && col == cols - 1) wdt = width - x;
        out << QRect (x, y, wdt, h);
        y += h + s;
        maxh = qMax (maxh, y - s);
      }
      if (total) *total = QSize (content_w + 2 * mw (), maxh + 2 * mh ());
    } else {
      int rows = pack == XmPACK_COLUMN ? ncols : 1;
      int per = pack == XmPACK_COLUMN ? major : n;
      int content_h = rows * ch + (rows - 1) * s;
      int height = qMax (content_h, avail.height () - 2 * mh ());
      int rowh = pack == XmPACK_TIGHT ? height : ch;
      int x = 0, maxw = 0;
      for (int i = 0; i < n; i++) {
        int row = per ? i / per : 0, col = per ? i % per : i;
        int wd = pack == XmPACK_COLUMN ? cw : pref_size (kids[i]).width ();
        if (col == 0) x = 0;
        int y = row * (rowh + s);
        int h = rowh;
        if (adjust_last && row == rows - 1 && pack == XmPACK_COLUMN) h = height - y;
        out << QRect (x, y, wd, h);
        x += wd + s;
        maxw = qMax (maxw, x - s);
      }
      if (total) *total = QSize (maxw + 2 * mw (), content_h + 2 * mh ());
    }
    return out;
  }

  QSize sizeHint () const override
  {
    QList<Widget> kids;
    for (auto &p : placed ()) kids << p.first;
    QSize total;
    cells (kids, QSize (), &total);
    return total.expandedTo (QSize (2 * mw (), 2 * mh ()));
  }

  void setGeometry (const QRect &rect) override
  {
    QLayout::setGeometry (rect);
    QList<QPair<Widget, QLayoutItem *>> p = placed ();
    QList<Widget> kids;
    for (auto &x : p) kids << x.first;
    QList<QRect> c = cells (kids, rect.size (), nullptr);
    for (int i = 0; i < p.size () && i < c.size (); i++)
      p[i].second->setGeometry (c[i].translated (rect.topLeft () + QPoint (mw (), mh ())));
  }
};

void mq_rowcolumn_rebuild_menu (Widget menu)
{
  QWidget *qw = mq_qw (menu);
  auto *m = qobject_cast<QMenu *> (qw);
  auto *bar = qobject_cast<QMenuBar *> (qw);
  if (!m && !bar) return;
  for (QAction *a : qw->actions ()) qw->removeAction (a);
  for (Widget c : R (menu)->children) {
    QAction *a = mq_action (c);
    if (!a || !R (c)->managed || R (c)->destroying) continue;
    qw->addAction (a);
  }
  if (bar) {
    Widget help = (Widget) mq_get (menu, XmNmenuHelpWidget);
    if (help && mq_action (help)) { bar->removeAction (mq_action (help)); bar->addAction (mq_action (help)); }
  }
}

void mq_option_menu_sync (Widget option)
{
  Widget button = XtNameToWidget (option, "OptionButton");
  Widget label = XtNameToWidget (option, "OptionLabel");
  Widget sub = (Widget) mq_get (option, XmNsubMenuId);
  if (label) {
    XmString s = (XmString) mq_get (option, XmNlabelString);
    XtVaSetValues (label, XmNlabelString, (XtArgVal) s, NULL);
    if (!s || XmStringEmpty (s)) XtUnmanageChild (label); else XtManageChild (label);
  }
  if (!button) return;
  if ((Widget) mq_get (button, XmNsubMenuId) != sub) XtVaSetValues (button, XmNsubMenuId, (XtArgVal) sub, NULL);
  Widget hist = (Widget) mq_get (option, XmNmenuHistory);
  if (!hist && sub) hist = (Widget) mq_get (sub, XmNmenuHistory);
  if (!hist && sub) for (Widget c : R (sub)->children) if (R (c)->managed && mq_action (c) && !mq_action (c)->isSeparator ()) { hist = c; break; }
  if (hist) {
    XmString s = (XmString) mq_get (hist, XmNlabelString);
    XmString t = s ? XmStringCopy (s) : XmStringCreateLocalized (R (hist)->name.data ());
    XtVaSetValues (button, XmNlabelString, (XtArgVal) t, NULL);
    XmStringFree (t);
    if (const ResDef *d = mq_find_res (option, XmNmenuHistory)) mq_store (option, d, (XtArgVal) hist);
  }
}

void mq_option_menu_follow (Widget pulldown)
{
  auto *m = mq_q<QMenu> (pulldown);
  if (!m) return;
  Widget cascade = static_cast<Widget> (m->property ("_mq_cascade").value<void *> ());
  if (cascade && R (cascade)->parent && mq_menu_type (R (cascade)->parent) == XmMENU_OPTION) {
    Widget option = R (cascade)->parent;
    if (const ResDef *d = mq_find_res (option, XmNmenuHistory)) mq_store (option, d, mq_get (pulldown, XmNmenuHistory));
    mq_option_menu_sync (option);
  }
}

static void popup_menu_post (Widget menu, QPoint global)
{
  auto *m = mq_q<QMenu> (menu);
  if (!m) return;
  mq_call (menu, XmNmapCallback, nullptr);
  m->popup (global);
}

class PopupFilter : public QObject {
public:
  Widget menu;
  explicit PopupFilter (Widget m) : menu (m) {}
  bool eventFilter (QObject *o, QEvent *e) override
  {
    if (e->type () != QEvent::MouseButtonPress || R (menu)->destroying) return false;
    auto *me = static_cast<QMouseEvent *> (e);
    int which = mq_geti (menu, XmNwhichButton);
    Qt::MouseButton want = which == 1 ? Qt::LeftButton : which == 2 ? Qt::MiddleButton : Qt::RightButton;
    if (me->button () != want || !mq_geti (menu, XmNpopupEnabled)) return false;
    Widget owner = mq_from_qobject (o);
    for (Widget a = owner; a; a = R (a)->parent)
      for (const EventHandlerRec &h : R (a)->handlers) if (h.mask & ButtonPressMask) return false;   /* the program posts it */
    mq_ext<RcExt> (menu)->posted_from = owner;
    popup_menu_post (menu, me->globalPosition ().toPoint ());
    return true;
  }
};

static QObject *rc_create (Widget w)
{
  switch (mq_menu_type (w)) {
  case XmMENU_BAR: {
    auto *bar = new QMenuBar;
    bar->setNativeMenuBar (false);
    return bar;
  }
  case XmMENU_PULLDOWN:
  case XmMENU_POPUP: {
    auto *m = new QMenu;
    QObject::connect (m, &QMenu::aboutToShow, m, [w, m] () {
      Widget cascade = static_cast<Widget> (m->property ("_mq_cascade").value<void *> ());
      if (cascade && !R (cascade)->destroying) {
        mq_ext<RcExt> (w)->posted_from = cascade;
        XmAnyCallbackStruct cs = { XmCR_CASCADING, mq_current_event () };
        mq_call (cascade, XmNcascadingCallback, &cs);
      }
      if (mq_menu_type (w) == XmMENU_PULLDOWN) mq_call (w, XmNmapCallback, nullptr);
    });
    QObject::connect (m, &QMenu::aboutToHide, m, [w] () {
      mq_call (w, XmNunmapCallback, nullptr);
      if (mq_menu_type (w) == XmMENU_POPUP && R (w)->managed) { R (w)->managed = false; }
    });
    if (mq_menu_type (w) == XmMENU_POPUP) {
      Widget owner = R (w)->parent && R (R (w)->parent)->parent ? R (R (w)->parent)->parent : nullptr;   /* menu shell's parent */
      if (QWidget *ow = owner ? mq_qw (owner) : nullptr) {
        auto *f = new PopupFilter (w);
        f->setParent (m);
        ow->installEventFilter (f);
      }
    }
    return m;
  }
  default:
    return new QWidget;
  }
}

static void rc_place (Widget parent, Widget child)
{
  int type = mq_menu_type (parent);
  if (type == XmMENU_BAR || type == XmMENU_PULLDOWN || type == XmMENU_POPUP) {
    if (mq_action (child)) { mq_rowcolumn_rebuild_menu (parent); return; }
    if (QWidget *cw = mq_qw (child)) {   /* a non-button widget in a menu */
      if (auto *m = mq_q<QMenu> (parent)) {
        auto *wa = new QWidgetAction (m);
        wa->setDefaultWidget (cw);
        m->addAction (wa);
      } else if (auto *bar = mq_q<QMenuBar> (parent)) bar->setCornerWidget (cw);
    }
    return;
  }
  /* work areas: entries line up (isAligned / entryAlignment) */
  bool aligned = !mq_isset (parent, XmNisAligned) || mq_getb (parent, XmNisAligned);
  if (aligned && type == XmWORK_AREA && mq_class_is (child, "XmLabel") && !mq_isset (child, XmNalignment)
      && !mq_class_is (child, "XmCascadeButton"))
    if (const ResDef *d = mq_find_res (child, XmNalignment)) {
      mq_store (child, d, mq_geti (parent, XmNentryAlignment));
      for (MqClass *k = R (child)->klass; k; k = k->superclass) if (k->apply) { k->apply (child, XmNalignment); break; }
    }
  if (mq_getb (parent, XmNradioBehavior) && (mq_class_is (child, "XmToggleButton") || mq_class_is (child, "XmToggleButtonGadget"))) {
    /* radio boxes show diamond/round indicators */
    if (auto *cb = mq_q<QCheckBox> (child)) Q_UNUSED (cb);
  }
  layout_place<RcLayout> (parent, child);
}

static void rc_unplace (Widget parent, Widget child)
{
  int type = mq_menu_type (parent);
  if (type == XmMENU_BAR || type == XmMENU_PULLDOWN || type == XmMENU_POPUP) { mq_rowcolumn_rebuild_menu (parent); return; }
  layout_unplace (parent, child);
}

static void rc_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  int type = mq_menu_type (w);
  bool all = res.isEmpty ();
  if (type == XmMENU_OPTION && (all || res == XmNlabelString || res == XmNsubMenuId || res == XmNmenuHistory)) {
    if (!all) mq_option_menu_sync (w);
  }
  if ((type == XmMENU_PULLDOWN || type == XmMENU_POPUP) && (all || res == XmNtearOffModel))
    if (auto *m = mq_q<QMenu> (w)) m->setTearOffEnabled (mq_geti (w, XmNtearOffModel) == XmTEAR_OFF_ENABLED);
  if ((type == XmMENU_PULLDOWN) && res == XmNmenuHistory) mq_option_menu_follow (w);
  if (type == XmMENU_BAR && res == XmNmenuHelpWidget) mq_rowcolumn_rebuild_menu (w);
  if (type == XmWORK_AREA && !all) layout_relayout (w);
}

static void rc_initialize (Widget w)
{
  if (mq_menu_type (w) != XmMENU_OPTION) return;
  Widget label = XtVaCreateManagedWidget ("OptionLabel", xmLabelGadgetClass, w, NULL);
  Widget button = XtVaCreateManagedWidget ("OptionButton", xmCascadeButtonGadgetClass, w, XmNalignment, XmALIGNMENT_CENTER, NULL);
  Q_UNUSED (label);
  Q_UNUSED (button);
  mq_option_menu_sync (w);
}

/* menu shells post popup menus when the menu is managed */
static void menu_shell_place (Widget shell, Widget menu)
{
  Q_UNUSED (shell);
  if (mq_menu_type (menu) != XmMENU_POPUP) return;
  RcExt *x = mq_ext<RcExt> (menu);
  QPoint at = x->have_pos ? x->popup_pos : QCursor::pos ();
  x->have_pos = false;
  popup_menu_post (menu, at);
}

static void menu_shell_unplace (Widget, Widget menu)
{
  if (auto *m = mq_q<QMenu> (menu)) m->hide ();
}

static struct MenuShellHooks {
  MenuShellHooks () { mq_menu_shell_rec.place = menu_shell_place; mq_menu_shell_rec.unplace = menu_shell_unplace; }
} s_menu_shell_hooks;

/* ======================================================================
 * XmFrame
 * ====================================================================== */
static QObject *frame_create (Widget)
{
  auto *g = new QGroupBox;
  auto *l = new QVBoxLayout (g);
  l->setContentsMargins (0, 0, 0, 0);
  l->setSpacing (0);
  return g;
}

static void frame_update_title (Widget w)
{
  auto *g = mq_q<QGroupBox> (w);
  if (!g) return;
  QString title;
  for (Widget c : R (w)->children)
    if (R (c)->managed && mq_geti (c, XmNchildType) == XmFRAME_TITLE_CHILD && mq_class_is (c, "XmLabel")) title = mq_label_text (c);
  g->setTitle (title);
  int align = XmALIGNMENT_BEGINNING;
  for (Widget c : R (w)->children)
    if (mq_geti (c, XmNchildType) == XmFRAME_TITLE_CHILD && mq_isset (c, XmNchildHorizontalAlignment)) align = mq_geti (c, XmNchildHorizontalAlignment);
  g->setAlignment (align == XmALIGNMENT_CENTER ? Qt::AlignHCenter : align == XmALIGNMENT_END ? Qt::AlignRight : Qt::AlignLeft);
}

static void frame_place (Widget parent, Widget child)
{
  QWidget *cw = mq_qw (child);
  auto *g = mq_q<QGroupBox> (parent);
  if (!cw || !g) return;
  if (mq_geti (child, XmNchildType) == XmFRAME_TITLE_CHILD && mq_class_is (child, "XmLabel")) {
    cw->setParent (g);
    cw->hide ();
    frame_update_title (parent);
    return;
  }
  int mw = mq_geti (parent, XmNmarginWidth), mh = mq_geti (parent, XmNmarginHeight);
  g->layout ()->setContentsMargins (mw + 2, mh + 2, mw + 2, mh + 2);
  cw->setParent (g);
  auto *l = static_cast<QVBoxLayout *> (g->layout ());
  if (l->indexOf (cw) < 0) {
    if (mq_geti (child, XmNchildType) == XmFRAME_TITLE_CHILD) l->insertWidget (0, cw);
    else l->addWidget (cw, 1);
  }
}

static void frame_unplace (Widget parent, Widget child)
{
  mq_unplace_default (parent, child);
  frame_update_title (parent);
}

static void frame_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  auto *g = mq_q<QGroupBox> (w);
  if (g && (res.isEmpty () || res == XmNshadowType))
    g->setFlat (mq_geti (w, XmNshadowType) == XmSHADOW_OUT && mq_isset (w, XmNshadowType));
}

/* ======================================================================
 * XmDrawingArea
 * ====================================================================== */
static QObject *da_create (Widget w) { return mq_canvas_create (w); }

/* Motif's default DrawingArea translations: input goes to inputCallback.
 * Augmented, so programs' own tables (XtOverrideTranslations) merge on top. */
static void da_initialize (Widget w)
{
  static XtTranslations defaults = XtParseTranslationTable (
    "<KeyDown>: DrawingAreaInput() ManagerGadgetKeyInput()\n"
    "<KeyUp>: DrawingAreaInput()\n"
    "<BtnDown>: DrawingAreaInput() ManagerGadgetArm()\n"
    "<BtnUp>: DrawingAreaInput() ManagerGadgetActivate()\n"
    "<BtnMotion>: ManagerGadgetButtonMotion()");
  XtAugmentTranslations (w, defaults);
}

/* ======================================================================
 * XmScrolledWindow and XmMainWindow
 * ====================================================================== */
struct SwExt {
  QPointer<QScrollArea> area;
  QPointer<QGridLayout> grid;
  QPointer<QVBoxLayout> column;   /* main window */
  QPointer<QFrame> seps[3];
};

static bool automatic (Widget w) { return mq_geti (w, XmNscrollingPolicy) == XmAUTOMATIC; }

static QObject *sw_create (Widget w)
{
  auto *outer = new QWidget;
  SwExt *x = mq_ext<SwExt> (w);
  x->grid = new QGridLayout (outer);
  x->grid->setContentsMargins (0, 0, 0, 0);
  x->grid->setSpacing (mq_isset (w, XmNspacing) ? mq_geti (w, XmNspacing) : 4);
  if (automatic (w)) {
    x->area = new QScrollArea;
    x->area->setWidgetResizable (true);
    x->area->setFrameShape (QFrame::StyledPanel);
    x->grid->addWidget (x->area, 0, 0);
    if (mq_geti (w, XmNscrollBarDisplayPolicy) == XmSTATIC) {
      x->area->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOn);
      x->area->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOn);
    }
  }
  return outer;
}

static void sw_arrange (Widget w)
{
  SwExt *x = mq_ext<SwExt> (w);
  QWidget *outer = mq_qw (w);
  if (!outer || !x->grid) return;
  Widget work = (Widget) mq_get (w, XmNworkWindow);
  Widget vsb = (Widget) mq_get (w, XmNverticalScrollBar), hsb = (Widget) mq_get (w, XmNhorizontalScrollBar);
  for (Widget c : managed_children (w)) {
    if (c == vsb || c == hsb) continue;
    if (mq_class_is (c, "XmScrollBar")) {
      if (mq_geti (c, XmNorientation) == XmHORIZONTAL) { if (!hsb) hsb = c; }
      else if (!vsb) vsb = c;
      continue;
    }
    if (!work) work = c;
  }
  bool top = false, left = false;
  switch (mq_geti (w, XmNscrollBarPlacement)) {
  case XmTOP_LEFT: top = left = true; break;
  case XmTOP_RIGHT: top = true; break;
  case XmBOTTOM_LEFT: left = true; break;
  default: break;
  }
  int wr = top ? 1 : 0, wc = left ? 1 : 0;
  if (work && R (work)->managed && mq_qw (work)) {
    QWidget *ww = mq_qw (work);
    if (x->area) {
      if (x->area->widget () != ww) { x->area->takeWidget (); x->area->setWidget (ww); }
      ww->show ();
    } else {
      ww->setParent (outer);
      x->grid->addWidget (ww, wr, wc);
      ww->setVisible (true);
    }
  }
  if (!x->area) {
    if (vsb && R (vsb)->managed && mq_qw (vsb)) { mq_qw (vsb)->setParent (outer); x->grid->addWidget (mq_qw (vsb), wr, left ? 0 : 1); mq_qw (vsb)->show (); }
    if (hsb && R (hsb)->managed && mq_qw (hsb)) { mq_qw (hsb)->setParent (outer); x->grid->addWidget (mq_qw (hsb), top ? 0 : 1, wc); mq_qw (hsb)->show (); }
  }
  x->grid->setRowStretch (wr, 1);
  x->grid->setColumnStretch (wc, 1);
}

static void sw_place (Widget parent, Widget) { sw_arrange (parent); }

static void sw_unplace (Widget parent, Widget child)
{
  SwExt *x = mq_ext<SwExt> (parent);
  QWidget *cw = mq_qw (child);
  if (x->area && cw && x->area->widget () == cw) x->area->takeWidget ();
  if (cw) { cw->hide (); if (x->grid) x->grid->removeWidget (cw); }
}

static void sw_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (res == XmNworkWindow || res == XmNverticalScrollBar || res == XmNhorizontalScrollBar || res == XmNscrollBarPlacement) sw_arrange (w);
}

/* main window: menu bar, command window, work area, message window in a column */
static QObject *mw_create (Widget w)
{
  auto *outer = new QWidget;
  SwExt *x = mq_ext<SwExt> (w);
  x->column = new QVBoxLayout (outer);
  x->column->setContentsMargins (0, 0, 0, 0);
  x->column->setSpacing (0);
  if (automatic (w)) {
    x->area = new QScrollArea;
    x->area->setWidgetResizable (true);
  }
  return outer;
}

static void mw_arrange (Widget w)
{
  SwExt *x = mq_ext<SwExt> (w);
  QWidget *outer = mq_qw (w);
  if (!outer || !x->column) return;
  Widget menubar = (Widget) mq_get (w, XmNmenuBar), command = (Widget) mq_get (w, XmNcommandWindow);
  Widget work = (Widget) mq_get (w, XmNworkWindow), message = (Widget) mq_get (w, XmNmessageWindow);
  QList<Widget> kids = managed_children (w);
  if (!menubar) for (Widget c : kids) if (mq_menu_type (c) == XmMENU_BAR) { menubar = c; break; }
  if (!work) for (Widget c : kids) if (c != menubar && c != command && c != message && !mq_class_is (c, "XmScrollBar")) { work = c; break; }
  while (x->column->count ()) delete x->column->takeAt (0);
  bool cmd_above = mq_geti (w, XmNcommandWindowLocation) != XmCOMMAND_BELOW_WORKSPACE;
  bool seps = mq_getb (w, XmNshowSeparator);
  auto sep = [&] (int i) {
    if (!seps) return;
    if (!x->seps[i]) { x->seps[i] = new QFrame (outer); x->seps[i]->setFrameShape (QFrame::HLine); x->seps[i]->setFrameShadow (QFrame::Sunken); }
    x->column->addWidget (x->seps[i]);
    x->seps[i]->show ();
  };
  auto add = [&] (Widget c, int stretch) {
    if (!c || !R (c)->managed || !mq_qw (c)) return;
    QWidget *cw = mq_qw (c);
    if (stretch && x->area) {
      if (x->area->widget () != cw) { x->area->takeWidget (); x->area->setWidget (cw); }
      cw->show ();
      x->area->setParent (outer);
      x->column->addWidget (x->area, 1);
      x->area->show ();
      return;
    }
    cw->setParent (outer);
    x->column->addWidget (cw, stretch);
    cw->setVisible (true);
  };
  add (menubar, 0);
  sep (0);
  if (cmd_above) { add (command, 0); if (command) sep (1); }
  bool has_work = work && R (work)->managed && mq_qw (work);
  add (work, 1);
  if (!cmd_above) { if (command) sep (1); add (command, 0); }
  if (message) sep (2);
  add (message, 0);
  /* children that have no role still show up (e.g. a second form) */
  for (Widget c : kids)
    if (c != menubar && c != command && c != work && c != message && mq_qw (c) && x->column->indexOf (mq_qw (c)) < 0) add (c, 0);
  /* as Motif: the other areas get their preferred heights and only the work
   * window takes what is left; without one the rest stays empty (NEdit's
   * command area would otherwise grow, and its tab folder keeps the size) */
  if (!has_work) x->column->addStretch (1);
}

static void mw_place (Widget parent, Widget) { mw_arrange (parent); }

static void mw_unplace (Widget parent, Widget child)
{
  if (QWidget *cw = mq_qw (child)) cw->hide ();
  mw_arrange (parent);
}

static void mw_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (res == XmNmenuBar || res == XmNworkWindow || res == XmNcommandWindow || res == XmNmessageWindow || res == XmNshowSeparator)
    mw_arrange (w);
}

/* ======================================================================
 * XmPanedWindow
 * ====================================================================== */
static QObject *pw_create (Widget w)
{
  auto *s = new QSplitter (mq_geti (w, XmNorientation) == XmHORIZONTAL ? Qt::Horizontal : Qt::Vertical);
  s->setChildrenCollapsible (false);
  return s;
}

static void pw_place (Widget parent, Widget child)
{
  auto *s = mq_q<QSplitter> (parent);
  QWidget *cw = mq_qw (child);
  if (!s || !cw) return;
  int index = 0;
  for (Widget c : R (parent)->children) {
    if (c == child) break;
    if (R (c)->managed && mq_qw (c)) index++;
  }
  s->insertWidget (index, cw);
  bool vertical = s->orientation () == Qt::Vertical;
  int pmin = mq_geti (child, XmNpaneMinimum), pmax = mq_geti (child, XmNpaneMaximum);
  if (pmin > 1) vertical ? cw->setMinimumHeight (pmin) : cw->setMinimumWidth (pmin);
  if (pmax > 0 && pmax < 1000) vertical ? cw->setMaximumHeight (pmax) : cw->setMaximumWidth (pmax);
  s->setStretchFactor (s->indexOf (cw), mq_getb (child, XmNskipAdjust) ? 0 : 1);
}

static void pw_unplace (Widget, Widget child)
{
  if (QWidget *cw = mq_qw (child)) { cw->hide (); cw->setParent (nullptr); }
}

/* ======================================================================
 * Class records
 * ====================================================================== */
XmManagerClassRec xmManagerClassRec;

MQ_DEFINE_CLASS_XT (xmManagerWidgetClass, mq_manager_rec, xmManagerClassRec, "XmManager", &constraint_rec, MGRK, nullptr, manager_apply, nullptr, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmBulletinBoardClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmBulletinBoardWidgetClass, mq_bb_rec, xmBulletinBoardClassRec, "XmBulletinBoard", &mq_manager_rec, MGRK, bb_create, bb_apply, nullptr,
                 layout_place<PlaceLayout>, layout_unplace, layout_relayout, bb_initialize)
MqXtClassRec xmFormClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmFormWidgetClass, mq_form_rec, xmFormClassRec, "XmForm", &mq_bb_rec, MGRK, bb_create, bb_apply, nullptr,
                 layout_place<FormLayout>, layout_unplace, layout_relayout, bb_initialize)
MqXtClassRec xmRowColumnClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmRowColumnWidgetClass, mq_rc_rec, xmRowColumnClassRec, "XmRowColumn", &mq_manager_rec, MGRK, rc_create, rc_apply, nullptr,
                 rc_place, rc_unplace, layout_relayout, rc_initialize)
MqXtClassRec xmFrameClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmFrameWidgetClass, mq_frame_rec, xmFrameClassRec, "XmFrame", &mq_manager_rec, MGRK, frame_create, frame_apply, nullptr,
                 frame_place, frame_unplace, nullptr, nullptr)
MqXtClassRec xmDrawingAreaClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmDrawingAreaWidgetClass, mq_da_rec, xmDrawingAreaClassRec, "XmDrawingArea", &mq_manager_rec, MGRK, da_create, manager_apply, nullptr,
                 layout_place<PlaceLayout>, layout_unplace, layout_relayout, da_initialize)
MqXtClassRec xmScrolledWindowClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmScrolledWindowWidgetClass, mq_sw_rec, xmScrolledWindowClassRec, "XmScrolledWindow", &mq_manager_rec, MGRK, sw_create, sw_apply, nullptr,
                 sw_place, sw_unplace, nullptr, nullptr)
MqXtClassRec xmMainWindowClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmMainWindowWidgetClass, mq_mw_rec, xmMainWindowClassRec, "XmMainWindow", &mq_sw_rec, MGRK, mw_create, mw_apply, nullptr,
                 mw_place, mw_unplace, nullptr, nullptr)
MqXtClassRec xmPanedWindowClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmPanedWindowWidgetClass, mq_pw_rec, xmPanedWindowClassRec, "XmPanedWindow", &mq_manager_rec, MGRK, pw_create, manager_apply, nullptr,
                 pw_place, pw_unplace, nullptr, nullptr)

/* ======================================================================
 * Creation functions
 * ====================================================================== */
static ArgList with_args (ArgList args, Cardinal n, std::initializer_list<Arg> extra, Cardinal *out_n)
{
  /* extra args go first so the caller's args win */
  auto *all = (ArgList) XtMalloc ((n + extra.size ()) * sizeof (Arg));
  Cardinal i = 0;
  for (const Arg &a : extra) all[i++] = a;
  for (Cardinal j = 0; j < n; j++) all[i++] = args[j];
  *out_n = i;
  return all;
}

static Arg A (const char *name, XtArgVal v) { Arg a; a.name = (String) name; a.value = v; return a; }

static Widget create_menu (Widget parent, const char *name, ArgList args, Cardinal n, unsigned char type)
{
  /* Motif puts each menu in a menu shell named "popup_<name>" */
  QByteArray shell_name = QByteArray ("popup_") + (name ? name : "");
  Widget shell_parent = parent;
  Widget shell = XtVaCreatePopupShell (shell_name.constData (), xmMenuShellWidgetClass, shell_parent, XmNwidth, 5, XmNheight, 5, NULL);
  Cardinal nn;
  ArgList all = with_args (args, n, { A (XmNrowColumnType, type) }, &nn);
  Widget menu = XtCreateWidget (name, xmRowColumnWidgetClass, shell, all, nn);
  XtFree ((char *) all);
  return menu;
}

extern "C" {

Widget XmCreateRowColumn (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmRowColumnWidgetClass, p, al, ac); }

Widget XmCreateWorkArea (Widget p, char *name, ArgList al, Cardinal ac)
{
  Cardinal n;
  ArgList all = with_args (al, ac, { A (XmNrowColumnType, XmWORK_AREA) }, &n);
  Widget w = XtCreateWidget (name, xmRowColumnWidgetClass, p, all, n);
  XtFree ((char *) all);
  return w;
}

Widget XmCreateRadioBox (Widget p, char *name, ArgList al, Cardinal ac)
{
  Cardinal n;
  ArgList all = with_args (al, ac, { A (XmNrowColumnType, XmWORK_AREA), A (XmNradioBehavior, True), A (XmNradioAlwaysOne, True),
                                     A (XmNpacking, XmPACK_COLUMN), A (XmNisHomogeneous, True) }, &n);
  Widget w = XtCreateWidget (name, xmRowColumnWidgetClass, p, all, n);
  XtFree ((char *) all);
  return w;
}

Widget XmCreateMenuBar (Widget p, char *name, ArgList al, Cardinal ac)
{
  Cardinal n;
  ArgList all = with_args (al, ac, { A (XmNrowColumnType, XmMENU_BAR), A (XmNorientation, XmHORIZONTAL), A (XmNpacking, XmPACK_TIGHT) }, &n);
  Widget w = XtCreateWidget (name, xmRowColumnWidgetClass, p, all, n);
  XtFree ((char *) all);
  return w;
}

Widget XmCreatePulldownMenu (Widget p, char *name, ArgList al, Cardinal ac) { return create_menu (p, name, al, ac, XmMENU_PULLDOWN); }
Widget XmCreatePopupMenu (Widget p, char *name, ArgList al, Cardinal ac) { return create_menu (p, name, al, ac, XmMENU_POPUP); }

Widget XmCreateOptionMenu (Widget p, char *name, ArgList al, Cardinal ac)
{
  Cardinal n;
  ArgList all = with_args (al, ac, { A (XmNrowColumnType, XmMENU_OPTION), A (XmNorientation, XmHORIZONTAL), A (XmNpacking, XmPACK_TIGHT) }, &n);
  Widget w = XtCreateWidget (name, xmRowColumnWidgetClass, p, all, n);
  XtFree ((char *) all);
  return w;
}

Widget XmOptionLabelGadget (Widget m) { return XtNameToWidget (m, "OptionLabel"); }
Widget XmOptionButtonGadget (Widget m) { return XtNameToWidget (m, "OptionButton"); }

void XmMenuPosition (Widget p, XButtonPressedEvent *event)
{
  if (!p || !event) return;
  RcExt *x = mq_ext<RcExt> (p);
  x->popup_pos = QPoint (event->x_root, event->y_root);
  x->have_pos = true;
  x->posted_from = mq_widget_for_window (event->window);
}

Widget XmGetPostedFromWidget (Widget menu)
{
  if (!menu) return nullptr;
  Widget from = mq_ext<RcExt> (menu)->posted_from;
  if (from) {
    /* the posting widget, or for cascades the top of the menu hierarchy */
    while (from && R (from)->parent && mq_in_menu (from) && mq_menu_type (R (from)->parent) != XmMENU_BAR) {
      Widget pm = R (from)->parent;
      Widget up = mq_ext<RcExt> (pm)->posted_from;
      if (!up) break;
      from = up;
    }
    if (from && mq_in_menu (from) && R (from)->parent && mq_menu_type (R (from)->parent) == XmMENU_BAR) return R (from)->parent;
    return from;
  }
  return R (menu)->parent && R (R (menu)->parent)->parent ? R (R (menu)->parent)->parent : nullptr;
}

Widget XmGetTearOffControl (Widget) { return nullptr; }

void XmAddToPostFromList (Widget m, Widget widget)
{
  QWidget *ow = mq_qw (widget);
  if (!ow || mq_menu_type (m) != XmMENU_POPUP) return;
  auto *f = new PopupFilter (m);
  f->setParent (mq_qw (m));
  ow->installEventFilter (f);
}

void XmRemoveFromPostFromList (Widget m, Widget widget)
{
  QWidget *ow = mq_qw (widget);
  if (!ow || !mq_qw (m)) return;
  for (auto *f : mq_qw (m)->findChildren<PopupFilter *> (Qt::FindDirectChildrenOnly)) ow->removeEventFilter (f);
}

Widget XmCreateForm (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmFormWidgetClass, p, al, ac); }
Widget XmCreateBulletinBoard (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmBulletinBoardWidgetClass, p, al, ac); }
Widget XmCreateFrame (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmFrameWidgetClass, p, al, ac); }
Widget XmCreateDrawingArea (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmDrawingAreaWidgetClass, p, al, ac); }
Widget XmCreateScrolledWindow (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmScrolledWindowWidgetClass, p, al, ac); }
Widget XmCreateMainWindow (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmMainWindowWidgetClass, p, al, ac); }
Widget XmCreatePanedWindow (Widget p, char *name, ArgList al, Cardinal ac) { return XtCreateWidget (name, xmPanedWindowWidgetClass, p, al, ac); }

void XmMainWindowSetAreas (Widget w, Widget menu, Widget command, Widget hscroll, Widget vscroll, Widget work)
{
  XtVaSetValues (w, XmNmenuBar, (XtArgVal) menu, XmNcommandWindow, (XtArgVal) command, XmNhorizontalScrollBar, (XtArgVal) hscroll,
                 XmNverticalScrollBar, (XtArgVal) vscroll, XmNworkWindow, (XtArgVal) work, NULL);
  mw_arrange (w);
}

Widget XmMainWindowSep1 (Widget w) { return w ? XtNameToWidget (w, "Separator1") : nullptr; }
Widget XmMainWindowSep2 (Widget w) { return w ? XtNameToWidget (w, "Separator2") : nullptr; }
Widget XmMainWindowSep3 (Widget w) { return w ? XtNameToWidget (w, "Separator3") : nullptr; }

void XmScrolledWindowSetAreas (Widget w, Widget hscroll, Widget vscroll, Widget work)
{
  XtVaSetValues (w, XmNhorizontalScrollBar, (XtArgVal) hscroll, XmNverticalScrollBar, (XtArgVal) vscroll, XmNworkWindow, (XtArgVal) work, NULL);
  sw_arrange (w);
}

void XmScrollVisible (Widget sw, Widget child, Dimension lr, Dimension tb)
{
  SwExt *x = mq_ext<SwExt> (sw);
  if (x->area && mq_qw (child)) x->area->ensureWidgetVisible (mq_qw (child), lr, tb);
}

/* ---------- simple menus ---------- */
struct SimpleSpec {
  int count = 0;
  XmStringTable buttons = nullptr;
  XmButtonTypeTable types = nullptr;
  KeySym *mnemonics = nullptr;
  String *accelerators = nullptr;
  XmStringTable accel_text = nullptr;
  XtCallbackProc callback = nullptr;
  int button_set = -1;
  int post_from = -1;
  XmString option_label = nullptr;
  KeySym option_mnemonic = 0;
};

static SimpleSpec simple_spec (ArgList args, Cardinal n)
{
  SimpleSpec s;
  for (Cardinal i = 0; i < n; i++) {
    const char *nm = args[i].name;
    XtArgVal v = args[i].value;
    if (!strcmp (nm, XmNbuttonCount)) s.count = (int) v;
    else if (!strcmp (nm, XmNbuttons)) s.buttons = (XmStringTable) v;
    else if (!strcmp (nm, XmNbuttonType)) s.types = (XmButtonTypeTable) v;
    else if (!strcmp (nm, XmNbuttonMnemonics)) s.mnemonics = (KeySym *) v;
    else if (!strcmp (nm, XmNbuttonAccelerators)) s.accelerators = (String *) v;
    else if (!strcmp (nm, XmNbuttonAcceleratorText)) s.accel_text = (XmStringTable) v;
    else if (!strcmp (nm, XmNsimpleCallback)) s.callback = (XtCallbackProc) v;
    else if (!strcmp (nm, XmNbuttonSet)) s.button_set = (int) v;
    else if (!strcmp (nm, XmNpostFromButton)) s.post_from = (int) v;
    else if (!strcmp (nm, XmNoptionLabel)) s.option_label = (XmString) v;
    else if (!strcmp (nm, XmNoptionMnemonic)) s.option_mnemonic = (KeySym) v;
  }
  return s;
}

static void simple_children (Widget rc, const SimpleSpec &s, bool menu_bar, int default_type)
{
  int button = 0, sep = 0, label = 0;
  for (int i = 0; i < s.count; i++) {
    int type = s.types ? s.types[i] : default_type;
    char name[32];
    Widget c = nullptr;
    switch (type) {
    case XmSEPARATOR: case XmDOUBLE_SEPARATOR:
      snprintf (name, sizeof name, "separator_%d", sep++);
      c = XtVaCreateManagedWidget (name, xmSeparatorGadgetClass, rc, XmNseparatorType,
                                   type == XmDOUBLE_SEPARATOR ? XmDOUBLE_LINE : XmSHADOW_ETCHED_IN, NULL);
      continue;
    case XmTITLE:
      snprintf (name, sizeof name, "label_%d", label++);
      c = XtVaCreateManagedWidget (name, xmLabelGadgetClass, rc, NULL);
      break;
    case XmCASCADEBUTTON:
      snprintf (name, sizeof name, "button_%d", button);
      c = XtVaCreateManagedWidget (name, menu_bar ? xmCascadeButtonWidgetClass : xmCascadeButtonGadgetClass, rc, NULL);
      break;
    case XmTOGGLEBUTTON: case XmRADIOBUTTON:
      snprintf (name, sizeof name, "button_%d", button);
      c = XtVaCreateManagedWidget (name, xmToggleButtonGadgetClass, rc, XmNindicatorType,
                                   type == XmRADIOBUTTON ? XmONE_OF_MANY : XmN_OF_MANY, NULL);
      if (s.button_set == button) XmToggleButtonSetState (c, True, False);
      if (s.callback) XtAddCallback (c, XmNvalueChangedCallback, s.callback, (XtPointer) (intptr_t) button);
      break;
    default:
      snprintf (name, sizeof name, "button_%d", button);
      c = XtVaCreateManagedWidget (name, xmPushButtonGadgetClass, rc, NULL);
      if (s.callback) XtAddCallback (c, XmNactivateCallback, s.callback, (XtPointer) (intptr_t) button);
      break;
    }
    if (s.buttons && s.buttons[i]) XtVaSetValues (c, XmNlabelString, (XtArgVal) s.buttons[i], NULL);
    if (s.mnemonics && s.mnemonics[i]) XtVaSetValues (c, XmNmnemonic, (XtArgVal) s.mnemonics[i], NULL);
    if (s.accelerators && s.accelerators[i]) XtVaSetValues (c, XmNaccelerator, (XtArgVal) s.accelerators[i], NULL);
    if (s.accel_text && s.accel_text[i]) XtVaSetValues (c, XmNacceleratorText, (XtArgVal) s.accel_text[i], NULL);
    if (type != XmTITLE) button++;
  }
}

Widget XmCreateSimpleMenuBar (Widget parent, String name, ArgList args, Cardinal n)
{
  Widget rc = XmCreateMenuBar (parent, name, args, n);
  simple_children (rc, simple_spec (args, n), true, XmCASCADEBUTTON);
  return rc;
}

Widget XmCreateSimplePopupMenu (Widget parent, String name, ArgList args, Cardinal n)
{
  Widget rc = XmCreatePopupMenu (parent, name, args, n);
  simple_children (rc, simple_spec (args, n), false, XmPUSHBUTTON);
  return rc;
}

Widget XmCreateSimplePulldownMenu (Widget parent, String name, ArgList args, Cardinal n)
{
  Widget rc = XmCreatePulldownMenu (parent, name, args, n);
  SimpleSpec s = simple_spec (args, n);
  simple_children (rc, s, false, XmPUSHBUTTON);
  if (s.post_from >= 0) {
    /* attach to the post_from'th cascade button of the parent */
    int k = 0;
    for (Widget c : R (parent)->children)
      if (mq_class_is (c, "XmCascadeButton") || mq_class_is (c, "XmCascadeButtonGadget")) {
        if (k++ == s.post_from) { XtVaSetValues (c, XmNsubMenuId, (XtArgVal) rc, NULL); break; }
      }
  }
  return rc;
}

Widget XmCreateSimpleOptionMenu (Widget parent, String name, ArgList args, Cardinal n)
{
  SimpleSpec s = simple_spec (args, n);
  QByteArray pd = QByteArray (name ? name : "") ;
  Widget pulldown = XmCreatePulldownMenu (parent, (char *) pd.constData (), nullptr, 0);
  simple_children (pulldown, s, false, XmPUSHBUTTON);
  Cardinal nn;
  ArgList all = with_args (args, n, { A (XmNsubMenuId, (XtArgVal) pulldown) }, &nn);
  Widget option = XmCreateOptionMenu (parent, name, all, nn);
  XtFree ((char *) all);
  if (s.option_label) XtVaSetValues (option, XmNlabelString, (XtArgVal) s.option_label, NULL);
  if (s.option_mnemonic) XtVaSetValues (option, XmNmnemonic, (XtArgVal) s.option_mnemonic, NULL);
  if (s.button_set >= 0) {
    int k = 0;
    for (Widget c : R (pulldown)->children)
      if (!mq_class_is (c, "XmSeparator") && !mq_class_is (c, "XmSeparatorGadget") && k++ == s.button_set) {
        XtVaSetValues (option, XmNmenuHistory, (XtArgVal) c, NULL);
        break;
      }
  }
  mq_option_menu_sync (option);
  return option;
}

Widget XmCreateSimpleRadioBox (Widget parent, String name, ArgList args, Cardinal n)
{
  Widget rc = XmCreateRadioBox (parent, name, args, n);
  simple_children (rc, simple_spec (args, n), false, XmRADIOBUTTON);
  return rc;
}

Widget XmCreateSimpleCheckBox (Widget parent, String name, ArgList args, Cardinal n)
{
  Cardinal nn;
  ArgList all = with_args (args, n, { A (XmNrowColumnType, XmWORK_AREA), A (XmNradioBehavior, False) }, &nn);
  Widget rc = XtCreateWidget (name, xmRowColumnWidgetClass, parent, all, nn);
  XtFree ((char *) all);
  simple_children (rc, simple_spec (args, n), false, XmCHECKBUTTON);
  return rc;
}

/* Varargs forms: entries are XmVaPUSHBUTTON label mnemonic accelerator accel_text,
 * XmVaCASCADEBUTTON label mnemonic, XmVaTOGGLEBUTTON/XmVaCHECKBUTTON/XmVaRADIOBUTTON
 * label mnemonic accelerator accel_text, XmVaSEPARATOR, XmVaDOUBLE_SEPARATOR,
 * XmVaTITLE label; anything else is a resource name/value pair. */
struct VaSimple {
  QList<XmString> labels;
  QList<XmButtonType> types;
  QList<KeySym> mnemonics;
  QList<String> accels;
  QList<XmString> accel_texts;
  QList<Arg> args;
};

static void va_simple (va_list ap, VaSimple &v)
{
  for (char *name = va_arg (ap, char *); name; name = va_arg (ap, char *)) {
    auto entry = [&] (XmButtonType t, bool mnemonic, bool accel) {
      v.types << t;
      v.labels << (t == XmSEPARATOR || t == XmDOUBLE_SEPARATOR ? nullptr : va_arg (ap, XmString));
      v.mnemonics << (mnemonic ? va_arg (ap, KeySym) : 0);
      if (accel) { v.accels << va_arg (ap, String); v.accel_texts << va_arg (ap, XmString); }
      else { v.accels << nullptr; v.accel_texts << nullptr; }
    };
    if (!strcmp (name, XmVaPUSHBUTTON)) entry (XmPUSHBUTTON, true, true);
    else if (!strcmp (name, XmVaCHECKBUTTON)) entry (XmCHECKBUTTON, true, true);
    else if (!strcmp (name, XmVaRADIOBUTTON)) entry (XmRADIOBUTTON, true, true);
    else if (!strcmp (name, XmVaCASCADEBUTTON)) entry (XmCASCADEBUTTON, true, false);
    else if (!strcmp (name, XmVaSEPARATOR)) entry (XmSEPARATOR, false, false);
    else if (!strcmp (name, XmVaDOUBLE_SEPARATOR)) entry (XmDOUBLE_SEPARATOR, false, false);
    else if (!strcmp (name, XmVaTITLE)) entry (XmTITLE, false, false);
    else if (!strcmp (name, XtVaTypedArg)) {   /* skip typed args: name, type, value, size */
      (void) va_arg (ap, char *); (void) va_arg (ap, char *); (void) va_arg (ap, XtArgVal); (void) va_arg (ap, int);
    } else v.args << A (name, va_arg (ap, XtArgVal));
  }
}

static QVector<Arg> va_simple_args (VaSimple &v, XtCallbackProc cb, std::initializer_list<Arg> extra)
{
  QVector<Arg> a;
  for (const Arg &x : extra) a << x;
  a << A (XmNbuttonCount, v.types.size ()) << A (XmNbuttons, (XtArgVal) v.labels.data ()) << A (XmNbuttonType, (XtArgVal) v.types.data ())
    << A (XmNbuttonMnemonics, (XtArgVal) v.mnemonics.data ()) << A (XmNbuttonAccelerators, (XtArgVal) v.accels.data ())
    << A (XmNbuttonAcceleratorText, (XtArgVal) v.accel_texts.data ());
  if (cb) a << A (XmNsimpleCallback, (XtArgVal) cb);
  a += QVector<Arg> (v.args.begin (), v.args.end ());
  return a;
}

Widget XmVaCreateSimpleMenuBar (Widget parent, String name, ...)
{
  va_list ap;
  va_start (ap, name);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  QVector<Arg> a = va_simple_args (v, nullptr, {});
  return XmCreateSimpleMenuBar (parent, name, a.data (), a.size ());
}

Widget XmVaCreateSimplePopupMenu (Widget parent, String name, XtCallbackProc callback, ...)
{
  va_list ap;
  va_start (ap, callback);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  QVector<Arg> a = va_simple_args (v, callback, {});
  return XmCreateSimplePopupMenu (parent, name, a.data (), a.size ());
}

Widget XmVaCreateSimplePulldownMenu (Widget parent, String name, int post_from_button, XtCallbackProc callback, ...)
{
  va_list ap;
  va_start (ap, callback);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  QVector<Arg> a = va_simple_args (v, callback, { A (XmNpostFromButton, post_from_button) });
  return XmCreateSimplePulldownMenu (parent, name, a.data (), a.size ());
}

Widget XmVaCreateSimpleOptionMenu (Widget parent, String name, XmString option_label, KeySym option_mnemonic, int button_set,
                                   XtCallbackProc callback, ...)
{
  va_list ap;
  va_start (ap, callback);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  QVector<Arg> a = va_simple_args (v, callback, { A (XmNoptionLabel, (XtArgVal) option_label), A (XmNoptionMnemonic, option_mnemonic),
                                                  A (XmNbuttonSet, button_set) });
  return XmCreateSimpleOptionMenu (parent, name, a.data (), a.size ());
}

Widget XmVaCreateSimpleRadioBox (Widget parent, String name, int button_set, XtCallbackProc callback, ...)
{
  va_list ap;
  va_start (ap, callback);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  for (XmButtonType &t : v.types) if (t == XmPUSHBUTTON) t = XmRADIOBUTTON;
  QVector<Arg> a = va_simple_args (v, callback, { A (XmNbuttonSet, button_set) });
  return XmCreateSimpleRadioBox (parent, name, a.data (), a.size ());
}

Widget XmVaCreateSimpleCheckBox (Widget parent, String name, XtCallbackProc callback, ...)
{
  va_list ap;
  va_start (ap, callback);
  VaSimple v;
  va_simple (ap, v);
  va_end (ap);
  QVector<Arg> a = va_simple_args (v, callback, {});
  return XmCreateSimpleCheckBox (parent, name, a.data (), a.size ());
}

} /* extern "C" */
