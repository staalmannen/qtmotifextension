/*
 * Motif on Qt - widget classes that programs define themselves.
 *
 * A program's class record is a genuine Xt class record whose superclass is
 * one of ours (xmManagerClassRec, widgetClassRec ...). Such widgets get the
 * same Qt side as a Core widget or drawing area (a canvas for Xlib drawing),
 * and are driven through their class methods the way libXt drives them:
 * class and class-part initialization with method inheritance, resources
 * written into the instance record at their offsets, initialize, realize,
 * expose, resize, set_values, destroy, query_geometry, and for composites
 * change_managed, geometry_manager and the constraint methods. Children of
 * such composites are placed where the class puts them (XtConfigureWidget,
 * XmeConfigureObject).
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstring>

struct ForeignRes { const XtResource *r; };

struct ForeignInfo {
  QList<ForeignRes> resources;            /* the foreign classes' own resources, root first */
  QList<ForeignRes> constraints;          /* constraint resources (composite classes) */
  QList<WidgetClass> chain;               /* foreign classes, root first */
};

static QHash<WidgetClass, MqClass *> &foreign_classes ()
{
  static QHash<WidgetClass, MqClass *> h;
  return h;
}

static QHash<MqClass *, ForeignInfo> &foreign_info ()
{
  static QHash<MqClass *, ForeignInfo> h;
  return h;
}

extern "C" void _XtInherit (void)
{
  fprintf (stderr, "Motif on Qt: _XtInherit called (an inherited class method was not resolved)\n");
}
int _XtInheritTranslations = 0;

static bool inherit (const void *p) { return p == (const void *) _XtInherit; }

static void noop_expose (Widget, XEvent *, Region) {}

/* methods inherited from our classes: our records leave them NULL */
static void resolve_inheritance (WidgetClass c, MqClass *mc)
{
  WidgetClass sup = c->core_class.superclass;
  CoreClassPart &cc = c->core_class, &sc = sup->core_class;
#define INH(field) if (inherit ((const void *) cc.field)) cc.field = sc.field;
  INH (realize) INH (resize) INH (expose) INH (set_values_almost) INH (accept_focus) INH (query_geometry) INH (display_accelerator)
#undef INH
  if (cc.tm_table == XtInheritTranslations) cc.tm_table = sc.tm_table == XtInheritTranslations ? nullptr : sc.tm_table;
  if (!cc.expose) cc.expose = noop_expose;
  if (mc->kind & K_COMPOSITE) {
    CompositeClassPart &cp = reinterpret_cast<CompositeWidgetClass> (c)->composite_class;
    CompositeClassPart &sp = reinterpret_cast<CompositeWidgetClass> (sup)->composite_class;
#define INH(field) if (inherit ((const void *) cp.field)) cp.field = sp.field;
    INH (geometry_manager) INH (change_managed) INH (insert_child) INH (delete_child)
#undef INH
  }
  if (mc->kind & K_MANAGER) {
    XmManagerClassPart &mp = reinterpret_cast<XmManagerWidgetClass> (c)->manager_class;
    XmManagerClassPart &smp = reinterpret_cast<XmManagerWidgetClass> (sup)->manager_class;
    if (inherit ((const void *) mp.parent_process)) mp.parent_process = smp.parent_process;
    if (mp.translations == XtInheritTranslations) mp.translations = smp.translations;
  } else if (mc->kind & K_PRIMITIVE) {
    XmPrimitiveClassPart &pp = reinterpret_cast<XmPrimitiveWidgetClass> (c)->primitive_class;
    XmPrimitiveClassPart &spp = reinterpret_cast<XmPrimitiveWidgetClass> (sup)->primitive_class;
#define INH(field) if (inherit ((const void *) pp.field)) pp.field = spp.field;
    INH (border_highlight) INH (border_unhighlight) INH (arm_and_activate)
#undef INH
    if (pp.translations == XtInheritTranslations) pp.translations = spp.translations;
  }
}

static void foreign_place (Widget parent, Widget child);
static void foreign_unplace (Widget parent, Widget child);

MqClass *mq_foreign_class (WidgetClass c)
{
  if (!c) return nullptr;
  auto it = foreign_classes ().constFind (c);
  if (it != foreign_classes ().constEnd ()) return it.value ();
  WidgetClass sup = c->core_class.superclass;
  MqClass *smc = sup ? mq_class (sup) : nullptr;
  if (!smc) {
    fprintf (stderr, "Motif on Qt: class %s has no known superclass\n", c->core_class.class_name ? c->core_class.class_name : "?");
    return nullptr;
  }
  auto *mc = new MqClass (*smc);
  mc->class_name = c->core_class.class_name;
  mc->superclass = smc;
  mc->xt = c;
  mc->foreign = true;
  mc->res_class = smc->res_class ? smc->res_class : smc->class_name;   /* our resources: the nearest known class's tables */
  mc->resolved = mc->resolved_constraints = nullptr;
  mc->set_other = nullptr;
  mc->get_other = nullptr;
  mc->initialize = nullptr;
  if (!mc->create && (mc->kind & K_WIDGET)) mc->create = mq_canvas_create;
  foreign_classes ().insert (c, mc);

  /* class initialization, as XtInitializeWidgetClass does it */
  resolve_inheritance (c, mc);
  ForeignInfo info;
  if (smc->foreign) info = foreign_info ().value (smc);
  info.chain << c;
  for (Cardinal i = 0; i < c->core_class.num_resources; i++) info.resources << ForeignRes { &c->core_class.resources[i] };
  if (mc->kind & K_CONSTRAINT) {
    ConstraintClassPart &k = reinterpret_cast<ConstraintWidgetClass> (c)->constraint_class;
    for (Cardinal i = 0; i < k.num_resources; i++) info.constraints << ForeignRes { &k.resources[i] };
  }
  foreign_info ().insert (mc, info);
  if (c->core_class.class_initialize && !(c->core_class.class_inited & 0x100)) c->core_class.class_initialize ();
  for (WidgetClass k : info.chain)
    if (k->core_class.class_part_initialize) k->core_class.class_part_initialize (c);
  if (c->core_class.actions && c->core_class.num_actions) {
    mq_ensure_app ();
    XtAppAddActions (mq_app, c->core_class.actions, c->core_class.num_actions);
  }
  c->core_class.class_inited = sup->core_class.class_inited | 0x100;

  /* a composite that lays out its own children */
  if (mc->kind & K_COMPOSITE) {
    CompositeClassPart &cp = reinterpret_cast<CompositeWidgetClass> (c)->composite_class;
    if (cp.change_managed || cp.geometry_manager) { mc->place = foreign_place; mc->unplace = foreign_unplace; mc->relayout = nullptr; }
  }
  return mc;
}

static const ForeignInfo *info_of (Widget w)
{
  if (!w || !R (w)->klass->foreign) return nullptr;
  auto it = foreign_info ().constFind (R (w)->klass);
  return it == foreign_info ().constEnd () ? nullptr : &it.value ();
}

bool mq_is_foreign (Widget w) { return w && R (w)->klass->foreign; }
bool mq_foreign_places_children (Widget p) { return p && R (p)->klass->foreign && R (p)->klass->place == foreign_place; }

/* ---------------------------------------------------------------- resources */
static void write_value (char *dst, unsigned size, XtArgVal v)
{
  switch (size) {
  case 1: *(unsigned char *) dst = (unsigned char) v; break;
  case 2: *(unsigned short *) dst = (unsigned short) v; break;
  case 4: *(unsigned int *) dst = (unsigned int) v; break;
  default: memcpy (dst, &v, qMin ((size_t) size, sizeof (XtArgVal))); break;
  }
}

static ResDef def_for (const XtResource *r)
{
  ResDef d;
  d.cls = "";
  d.constraint = false;
  d.name = r->resource_name;
  d.rclass = r->resource_class;
  d.rtype = r->resource_type;
  d.size = r->resource_size;
  d.dtype = r->default_type;
  d.dimm = 0;
  d.dstr = nullptr;
  return d;
}

static void callback_field (Widget w, char *dst, const char *name)
{
  QVector<XtCallbackRec> &arr = R (w)->xt_callback_arrays[name];
  arr.clear ();
  for (const XtCallbackRec &c : R (w)->callbacks.value (name)) arr << c;
  arr << XtCallbackRec { nullptr, nullptr };
  *(XtCallbackList *) dst = arr.size () > 1 ? arr.data () : nullptr;
}

/* one resource's initial value: arguments > database > default */
static void init_resource (Widget w, char *base, const XtResource *r, ArgList args, Cardinal n, Widget db_widget)
{
  char *dst = base + r->resource_offset;
  const char *name = r->resource_name;
  bool is_cb = !strcmp (r->resource_type, XtRCallback);
  for (Cardinal i = 0; i < n; i++)
    if (!strcmp (args[i].name, name)) {
      if (is_cb) {
        for (XtCallbackList l = (XtCallbackList) args[i].value; l && l->callback; l++) XtAddCallback (w, name, l->callback, l->closure);
        callback_field (w, dst, name);
      } else write_value (dst, r->resource_size, args[i].value);
      return;
    }
  if (is_cb) { callback_field (w, dst, name); return; }
  ResDef d = def_for (r);
  if (const char *s = mq_lookup_db (db_widget, name, r->resource_class)) {
    XtArgVal v = 0;
    if (mq_convert (w, &d, s, &v)) { write_value (dst, r->resource_size, v); return; }
  }
  const char *dt = r->default_type;
  if (!strcmp (dt, XtRImmediate)) write_value (dst, r->resource_size, (XtArgVal) r->default_addr);
  else if (!strcmp (dt, XtRString)) {
    XtArgVal v = 0;
    if (r->default_addr && mq_convert (w, &d, (const char *) r->default_addr, &v)) write_value (dst, r->resource_size, v);
  } else if (!strcmp (dt, XtRCallProc)) {
    XrmValue val = { 0, nullptr };
    ((XtResourceDefaultProc) r->default_addr) (w, r->resource_offset, &val);
    if (val.addr) memcpy (dst, val.addr, qMin<unsigned> (val.size ? val.size : r->resource_size, r->resource_size));
  } else if (r->default_addr && !strcmp (dt, r->resource_type)) {
    memcpy (dst, r->default_addr, r->resource_size);
  }
}

static const XtResource *find (const QList<ForeignRes> &list, const char *name)
{
  for (int i = list.size () - 1; i >= 0; i--)
    if (!strcmp (list[i].r->resource_name, name)) return list[i].r;
  return nullptr;
}

/* XtSetValues on a resource of a foreign class (or a foreign parent's
 * constraint); false if the name is not one of those */
bool mq_foreign_store (Widget w, const char *name, XtArgVal v)
{
  if (const ForeignInfo *fi = info_of (w))
    if (const XtResource *r = find (fi->resources, name)) {
      char *dst = (char *) w + r->resource_offset;
      if (!strcmp (r->resource_type, XtRCallback)) {
        R (w)->callbacks.remove (name);
        for (XtCallbackList l = (XtCallbackList) v; l && l->callback; l++) XtAddCallback (w, name, l->callback, l->closure);
        callback_field (w, dst, name);
      } else write_value (dst, r->resource_size, v);
      return true;
    }
  if (Widget p = R (w)->parent)
    if (const ForeignInfo *fi = info_of (p))
      if (const XtResource *r = find (fi->constraints, name)) {
        if (w->core.constraints) write_value ((char *) w->core.constraints + r->resource_offset, r->resource_size, v);
        return true;
      }
  return false;
}

bool mq_foreign_fetch (Widget w, const char *name, XtArgVal dest)
{
  const XtResource *r = nullptr;
  const char *src = nullptr;
  if (const ForeignInfo *fi = info_of (w))
    if ((r = find (fi->resources, name))) src = (const char *) w + r->resource_offset;
  if (!r && R (w)->parent)
    if (const ForeignInfo *fi = info_of (R (w)->parent))
      if ((r = find (fi->constraints, name)) && w->core.constraints) src = (const char *) w->core.constraints + r->resource_offset;
  if (!r || !src) return false;
  if (dest) memcpy ((void *) dest, src, r->resource_size);
  return true;
}

void mq_foreign_callbacks_changed (Widget w, const char *name)
{
  if (const ForeignInfo *fi = info_of (w))
    if (const XtResource *r = find (fi->resources, name))
      if (!strcmp (r->resource_type, XtRCallback)) callback_field (w, (char *) w + r->resource_offset, name);
}

/* ---------------------------------------------------------------- lifecycle */
static char *copy_record (Widget w)
{
  Cardinal size = qMax<Cardinal> (w->core.widget_class->core_class.widget_size, mq_instance_size (R (w)->klass->kind));
  char *c = (char *) malloc (size);
  memcpy (c, w, size);
  return c;
}

void mq_foreign_initialize (Widget w, ArgList args, Cardinal n)
{
  const ForeignInfo *fi = info_of (w);
  if (!fi) return;
  for (const ForeignRes &fr : fi->resources) init_resource (w, (char *) w, fr.r, args, n, w);
  /* the class's translations under the program's own */
  if (const char *tm = w->core.widget_class->core_class.tm_table)
    if (XtTranslations t = XtParseTranslationTable (tm)) XtAugmentTranslations (w, t);
  char *request = copy_record (w);
  for (WidgetClass k : fi->chain) {
    if (k->core_class.initialize) k->core_class.initialize ((Widget) request, w, args, &n);
    if (k->core_class.initialize_hook) k->core_class.initialize_hook (w, args, &n);
  }
  free (request);
  if (QWidget *q = mq_qw (w)) q->updateGeometry ();
}

void mq_foreign_constraint_initialize (Widget child, ArgList args, Cardinal n)
{
  Widget parent = R (child)->parent;
  const ForeignInfo *fi = info_of (parent);
  if (!fi || !child->core.constraints) return;
  for (const ForeignRes &fr : fi->constraints) init_resource (child, (char *) child->core.constraints, fr.r, args, n, child);
  Cardinal csize = reinterpret_cast<ConstraintWidgetClass> (parent->core.widget_class)->constraint_class.constraint_size;
  char *request = copy_record (child);
  void *rcons = malloc (qMax<Cardinal> (csize, 16));
  memcpy (rcons, child->core.constraints, csize);
  ((Widget) request)->core.constraints = rcons;
  for (WidgetClass k : fi->chain) {
    ConstraintClassPart &cp = reinterpret_cast<ConstraintWidgetClass> (k)->constraint_class;
    if (cp.initialize) cp.initialize ((Widget) request, child, args, &n);
  }
  free (rcons);
  free (request);
}

void mq_foreign_destroy (Widget w)
{
  if (Widget p = R (w)->parent)
    if (const ForeignInfo *pfi = info_of (p))
      for (int i = pfi->chain.size () - 1; i >= 0; i--) {
        ConstraintClassPart &cp = reinterpret_cast<ConstraintWidgetClass> (pfi->chain[i])->constraint_class;
        if (cp.destroy && w->core.constraints) cp.destroy (w);
      }
  if (const ForeignInfo *fi = info_of (w))
    for (int i = fi->chain.size () - 1; i >= 0; i--)
      if (fi->chain[i]->core_class.destroy) fi->chain[i]->core_class.destroy (w);
}

/* XtSetValues: run the set_values methods, then act on geometry changes and
 * redisplay requests */
struct ForeignSnapshot { char *old = nullptr; void *old_cons = nullptr; };

void *mq_foreign_set_values_begin (Widget w)
{
  bool own = info_of (w) != nullptr, cons = R (w)->parent && info_of (R (w)->parent);
  if (!own && !cons) return nullptr;
  auto *s = new ForeignSnapshot;
  s->old = copy_record (w);
  if (cons && w->core.constraints) {
    Cardinal csize = reinterpret_cast<ConstraintWidgetClass> (R (w)->parent->core.widget_class)->constraint_class.constraint_size;
    s->old_cons = malloc (qMax<Cardinal> (csize, 16));
    memcpy (s->old_cons, w->core.constraints, csize);
    ((Widget) s->old)->core.constraints = s->old_cons;
  }
  return s;
}

XtGeometryResult mq_make_geometry_request (Widget w, XtWidgetGeometry *req, XtWidgetGeometry *reply);

void mq_foreign_set_values_end (Widget w, void *snap, ArgList args, Cardinal n)
{
  auto *s = static_cast<ForeignSnapshot *> (snap);
  if (!s) return;
  Widget old = (Widget) s->old;
  char *request = copy_record (w);
  bool redisplay = false;
  if (const ForeignInfo *fi = info_of (w))
    for (WidgetClass k : fi->chain) {
      if (k->core_class.set_values && k->core_class.set_values (old, (Widget) request, w, args, &n)) redisplay = true;
      if (k->core_class.set_values_hook && k->core_class.set_values_hook (w, args, &n)) redisplay = true;
    }
  if (R (w)->parent)
    if (const ForeignInfo *pfi = info_of (R (w)->parent))
      for (WidgetClass k : pfi->chain) {
        ConstraintClassPart &cp = reinterpret_cast<ConstraintWidgetClass> (k)->constraint_class;
        if (cp.set_values && cp.set_values (old, (Widget) request, w, args, &n)) redisplay = true;
      }
  /* geometry changed by the class: ask the parent */
  if (old->core.width != w->core.width || old->core.height != w->core.height || old->core.x != w->core.x || old->core.y != w->core.y) {
    XtWidgetGeometry req, reply;
    req.request_mode = CWX | CWY | CWWidth | CWHeight;
    req.x = w->core.x; req.y = w->core.y; req.width = w->core.width; req.height = w->core.height;
    w->core.x = old->core.x; w->core.y = old->core.y; w->core.width = old->core.width; w->core.height = old->core.height;
    XtGeometryResult res = mq_make_geometry_request (w, &req, &reply);
    if (res == XtGeometryAlmost && w->core.widget_class->core_class.set_values_almost) {
      w->core.widget_class->core_class.set_values_almost (old, w, &req, &reply);
      mq_make_geometry_request (w, &req, &reply);
    }
  }
  if (redisplay && XtIsRealized (w)) if (Window win = mq_window_for (w)) XClearArea (mq_display, win, 0, 0, 0, 0, True);
  free (request);
  free (s->old_cons);
  free (s->old);
  delete s;
}

/* ---------------------------------------------------------------- drawing */
Region mq_region_for (const QRect &r);
void mq_region_free (Region r);

void mq_foreign_expose (Widget w, XEvent *xe, const QRect &area)
{
  XtExposeProc expose = w->core.widget_class->core_class.expose;
  if (!R (w)->klass->foreign || !expose || expose == noop_expose) return;
  Region r = mq_region_for (area);
  expose (w, xe, r);
  mq_region_free (r);
}

void mq_foreign_resize (Widget w)
{
  if (!R (w)->klass->foreign) return;
  if (XtWidgetProc resize = w->core.widget_class->core_class.resize) resize (w);
}

/* ---------------------------------------------------------------- geometry */
static void apply_geometry (Widget w)
{
  QWidget *q = mq_qw (w);
  if (!q) return;
  Widget p = R (w)->parent;
  if (p && R (p)->klass->foreign && R (p)->klass->place == foreign_place) {
    QSize old = q->size ();
    q->setGeometry (w->core.x, w->core.y, qMax<int> (1, w->core.width), qMax<int> (1, w->core.height));
    if (old != q->size () && R (w)->klass->foreign) mq_foreign_resize (w);
  } else q->updateGeometry ();
}

/* XtConfigureWidget and friends for children of foreign composites */
bool mq_foreign_configure (Widget w, Position x, Position y, Dimension width, Dimension height, Dimension bw)
{
  Widget p = R (w)->parent;
  if (!p || !R (p)->klass->foreign || R (p)->klass->place != foreign_place) return false;
  bool resized = w->core.width != width || w->core.height != height;
  w->core.x = x; w->core.y = y; w->core.width = width; w->core.height = height; w->core.border_width = bw;
  if (QWidget *q = mq_qw (w)) q->setGeometry (x, y, qMax<int> (1, width), qMax<int> (1, height));
  if (resized && R (w)->klass->foreign) mq_foreign_resize (w);
  return true;
}

XtGeometryResult mq_make_geometry_request (Widget w, XtWidgetGeometry *req, XtWidgetGeometry *reply)
{
  Widget p = R (w)->parent;
  bool query = req->request_mode & XtCWQueryOnly;
  if (p && R (p)->klass->foreign && R (p)->klass->place == foreign_place) {
    XtGeometryHandler gm = reinterpret_cast<CompositeWidgetClass> (p->core.widget_class)->composite_class.geometry_manager;
    if (!gm) return XtGeometryNo;
    XtWidgetGeometry dummy;
    XtGeometryResult res = gm (w, req, reply ? reply : &dummy);
    if (res == XtGeometryYes && !query) {
      if (req->request_mode & CWWidth) w->core.width = req->width;
      if (req->request_mode & CWHeight) w->core.height = req->height;
      apply_geometry (w);
    }
    return res;
  }
  /* our own managers: grant it, our layout decides the final geometry */
  if (!query) {
    if (req->request_mode & CWX) w->core.x = req->x;
    if (req->request_mode & CWY) w->core.y = req->y;
    if (req->request_mode & CWWidth) w->core.width = req->width;
    if (req->request_mode & CWHeight) w->core.height = req->height;
    if (req->request_mode & CWBorderWidth) w->core.border_width = req->border_width;
    if (QWidget *q = mq_qw (w)) {
      if (R (w)->klass->foreign) q->updateGeometry ();
      else if (!(R (w)->klass->kind & K_SHELL)) q->setMinimumSize (w->core.width, w->core.height);
      else q->resize (w->core.width, w->core.height);
    }
  }
  return XtGeometryYes;
}

bool mq_foreign_query_geometry (Widget w, XtWidgetGeometry *req, XtWidgetGeometry *pref, XtGeometryResult *res)
{
  if (!R (w)->klass->foreign) return false;
  XtGeometryHandler qg = w->core.widget_class->core_class.query_geometry;
  if (!qg) return false;
  XtWidgetGeometry dummy;
  dummy.request_mode = 0;
  *res = qg (w, req ? req : &dummy, pref);
  return true;
}

QSize mq_foreign_size_hint (Widget w)
{
  XtWidgetGeometry pref;
  pref.request_mode = 0;
  pref.width = w->core.width;
  pref.height = w->core.height;
  XtGeometryResult res;
  XtWidgetGeometry req;
  req.request_mode = 0;
  if (mq_foreign_query_geometry (w, &req, &pref, &res)) {
    int width = (pref.request_mode & CWWidth) ? pref.width : w->core.width;
    int height = (pref.request_mode & CWHeight) ? pref.height : w->core.height;
    return QSize (qMax (1, width), qMax (1, height));
  }
  return QSize (qMax<int> (1, w->core.width), qMax<int> (1, w->core.height));
}

/* ---------------------------------------------------------------- children */
void mq_foreign_change_managed (Widget parent)
{
  if (!R (parent)->klass->foreign || R (parent)->klass->place != foreign_place) return;
  mq_sync_children (parent);
  if (XtWidgetProc cm = reinterpret_cast<CompositeWidgetClass> (parent->core.widget_class)->composite_class.change_managed) cm (parent);
}

static void foreign_place (Widget parent, Widget child)
{
  QWidget *cw = mq_qw (child), *pw = mq_qw (parent);
  if (cw && pw) {
    if (R (child)->klass->kind & K_GADGET) cw->setAttribute (Qt::WA_DontShowOnScreen);   /* drawn by the parent's expose */
    else if (cw->parentWidget () != pw) cw->setParent (pw);
    cw->setGeometry (child->core.x, child->core.y, qMax<int> (1, child->core.width), qMax<int> (1, child->core.height));
  }
  if (R (parent)->realized) mq_foreign_change_managed (parent);
}

static void foreign_unplace (Widget parent, Widget child)
{
  if (QWidget *cw = mq_qw (child)) cw->hide ();
  if (R (parent)->realized) mq_foreign_change_managed (parent);
}
