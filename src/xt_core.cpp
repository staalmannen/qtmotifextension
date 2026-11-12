/*
 * Motif on Qt - Xt intrinsics: classes, widget lifecycle, values,
 * callbacks, shells, application start-up.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"

#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>

void mq_resources_init (const char *app_name, const char *app_class, String *fallback);
void mq_resources_from_db (Widget w, ArgList args, Cardinal n);
void mq_free_resources (Widget w);
void mq_parse_command_line (XrmOptionDescRec *opts, Cardinal n, int *argc, char **argv);
const char *mq_app_name ();
const char *mq_app_class ();
void mq_xlib_init ();
void mq_events_install (Widget w);

/* ======================================================================
 * Application context and display
 * ====================================================================== */
struct _XtAppStruct {
  String *fallback = nullptr;
  bool exit_flag = false;
  XtErrorHandler error_handler = nullptr, warning_handler = nullptr;
  XtErrorMsgHandler error_msg_handler = nullptr, warning_msg_handler = nullptr;
};

XtAppContext mq_app = nullptr;
static QList<Widget> s_shells;   /* top-level shells, for XtWindowToWidget & co. */

void mq_ensure_app (int *argc, char **argv)
{
  if (!mq_app) mq_app = new _XtAppStruct;
  if (!QCoreApplication::instance ()) {
    static int static_argc = 1;
    static char *static_argv[] = { (char *) "motif", nullptr };
    if (argc && argv) new QApplication (*argc, argv);
    else new QApplication (static_argc, static_argv);
    qApp->setQuitOnLastWindowClosed (false);
    /* Headless testing: MOTIFQT_AUTOQUIT=<ms> exits after that long, saving
     * the top-level windows to MOTIFQT_SCREENSHOT (a .png path) first. */
    bool ok = false;
    int ms = qEnvironmentVariableIntValue ("MOTIFQT_AUTOQUIT", &ok);
    if (ok) {
      QTimer::singleShot (ms, qApp, [] () {
        QString shot = qEnvironmentVariable ("MOTIFQT_SCREENSHOT");
        if (!shot.isEmpty ()) {
          QList<QWidget *> wins;
          for (QWidget *t : QApplication::topLevelWidgets ())
            if (t->isVisible () && !qobject_cast<QMenu *> (t)) wins << t;
          for (int i = 0; i < wins.size (); i++) {
            QString name = i == 0 ? shot : QString (shot).replace (".png", QString ("-%1.png").arg (i));
            wins[i]->grab ().save (name);
          }
        }
        if (qEnvironmentVariableIsSet ("MOTIFQT_DUMP")) {
          std::function<void (QWidget *, int)> dump = [&] (QWidget *q, int depth) {
            Widget w = mq_from_qobject (q);
            bool own = w && mq_qw (w) == q;
            fprintf (stderr, "%*s%s %s%s %dx%d+%d+%d%s\n", depth * 2, "", q->metaObject ()->className (),
                     own ? w->name.constData () : "", own ? QByteArray (" (").append (w->klass->class_name).append (")").constData () : "",
                     q->width (), q->height (), q->x (), q->y (), q->isVisible () ? "" : " hidden");
            for (QObject *c : q->children ()) if (c->isWidgetType ()) dump (static_cast<QWidget *> (c), depth + 1);
          };
          for (QWidget *t : QApplication::topLevelWidgets ()) if (t->isVisible ()) dump (t, 0);
        }
        fflush (stdout);
        ::exit (0);
      });
    }
  }
  mq_xlib_init ();
}

/* ======================================================================
 * Class records for the Xt classes
 * ====================================================================== */
static QObject *shell_create (Widget w);
static void shell_place (Widget shell, Widget child);
static void shell_unplace (Widget shell, Widget child);
static void shell_apply (Widget w, const QByteArray &res);

static WidgetClassRec object_rec = { "Object", nullptr, K_OBJECT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec rectobj_rec = { "RectObj", &object_rec, K_RECT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec core_rec = { "Core", &rectobj_rec, K_RECT | K_WIDGET, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec composite_rec = { "Composite", &core_rec, K_RECT | K_WIDGET | K_COMPOSITE, nullptr, nullptr, nullptr,
                                        mq_place_default, mq_unplace_default, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec constraint_rec = { "Constraint", &composite_rec, K_RECT | K_WIDGET | K_COMPOSITE | K_CONSTRAINT, nullptr, nullptr,
                                         nullptr, mq_place_default, mq_unplace_default, nullptr, nullptr, nullptr, nullptr, nullptr };
#define SHELLK (K_RECT | K_WIDGET | K_COMPOSITE | K_SHELL)
WidgetClassRec mq_shell_rec = { "Shell", &composite_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec override_shell_rec = { "OverrideShell", &mq_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec wm_shell_rec = { "WMShell", &mq_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec vendor_shell_rec = { "VendorShell", &wm_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClassRec mq_transient_shell_rec = { "TransientShell", &vendor_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClassRec mq_toplevel_shell_rec = { "TopLevelShell", &vendor_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClassRec mq_application_shell_rec = { "ApplicationShell", &mq_toplevel_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
static WidgetClassRec session_shell_rec = { "SessionShell", &mq_application_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClassRec mq_dialog_shell_rec = { "XmDialogShell", &mq_transient_shell_rec, SHELLK, shell_create, shell_apply, nullptr, shell_place, shell_unplace, nullptr, nullptr, nullptr, nullptr, nullptr };
WidgetClassRec mq_menu_shell_rec = { "XmMenuShell", &override_shell_rec, SHELLK, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

/* No extern "C" here: each variable keeps the linkage of its declaration in
 * the vendored Xt/Motif headers (C++ never mangles global variable names). */
WidgetClass objectClass = &object_rec;
WidgetClass rectObjClass = &rectobj_rec;
WidgetClass widgetClass = &core_rec;
WidgetClass coreWidgetClass = &core_rec;
WidgetClass compositeWidgetClass = &composite_rec;
WidgetClass constraintWidgetClass = &constraint_rec;
WidgetClass shellWidgetClass = &mq_shell_rec;
WidgetClass overrideShellWidgetClass = &override_shell_rec;
WidgetClass wmShellWidgetClass = &wm_shell_rec;
WidgetClass vendorShellWidgetClass = &vendor_shell_rec;
WidgetClass transientShellWidgetClass = &mq_transient_shell_rec;
WidgetClass topLevelShellWidgetClass = &mq_toplevel_shell_rec;
WidgetClass applicationShellWidgetClass = &mq_application_shell_rec;
WidgetClass sessionShellWidgetClass = &session_shell_rec;
WidgetClass xmDialogShellWidgetClass = &mq_dialog_shell_rec;
WidgetClass xmMenuShellWidgetClass = &mq_menu_shell_rec;

/* ======================================================================
 * Helpers
 * ====================================================================== */
QWidget *mq_qw (Widget w) { return w ? qobject_cast<QWidget *> (w->obj.data ()) : nullptr; }
QAction *mq_action (Widget w) { return w ? qobject_cast<QAction *> (w->obj.data ()) : nullptr; }
QWidget *mq_content (Widget w) { return w ? (w->content ? w->content.data () : mq_qw (w)) : nullptr; }

Widget mq_from_qobject (QObject *o)
{
  for (; o; o = o->parent ()) {
    QVariant v = o->property ("_mq_widget");
    if (v.isValid ()) return static_cast<Widget> (v.value<void *> ());
  }
  return nullptr;
}

bool mq_is (Widget w, WidgetClass c)
{
  if (!w || !c) return false;
  for (WidgetClass k = w->klass; k; k = k->superclass)
    if (k == c) return true;
  return false;
}

bool mq_kind (Widget w, unsigned kind) { return w && (w->klass->kind & kind) == kind; }

Widget mq_shell_of (Widget w)
{
  while (w && !(w->klass->kind & K_SHELL)) w = w->parent;
  return w;
}

void mq_call (Widget w, const char *callback, XtPointer call_data)
{
  if (!w || w->destroying) return;
  auto it = w->callbacks.constFind (callback);
  if (it == w->callbacks.constEnd ()) return;
  QList<XtCallbackRec> list = it.value ();   /* copy: callbacks may change the list */
  for (const XtCallbackRec &r : list) {
    r.callback (w, r.closure, call_data);
    if (w->destroying) break;
  }
}

bool mq_has_callbacks (Widget w, const char *callback)
{
  return w && !w->callbacks.value (callback).isEmpty ();
}

/* ======================================================================
 * Visibility and placement
 * ====================================================================== */
/* gadgets (RectObj) have no mappedWhenManaged: they are always mapped */
static bool mapped (Widget w) { return !mq_find_res (w, XmNmappedWhenManaged) || mq_getb (w, XmNmappedWhenManaged); }

void mq_sync_visible (Widget w)
{
  if (!w) return;
  if (QAction *a = mq_action (w)) { a->setVisible (w->managed && mapped (w)); return; }
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  if (w->klass->kind & K_SHELL) return;   /* shells are shown by realize/popup */
  bool vis = w->managed && mapped (w);
  if (!qw->parentWidget () && vis) return;   /* not placed yet */
  if (qobject_cast<QStackedWidget *> (qw->parentWidget ())) return;   /* pages: the stack decides */
  qw->setVisible (vis);
}

void mq_apply_size (Widget w)
{
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  int width = mq_isset (w, XmNwidth) ? mq_geti (w, XmNwidth) : 0;
  int height = mq_isset (w, XmNheight) ? mq_geti (w, XmNheight) : 0;
  if (w->klass->kind & K_SHELL) {
    if (width > 0 || height > 0) qw->resize (width > 0 ? width : qw->width (), height > 0 ? height : qw->height ());
    return;
  }
  /* A requested size is a minimum in Qt terms: attachments may still stretch it. */
  qw->setMinimumSize (qMax (width, qw->minimumWidth () > 0 && !width ? qw->minimumWidth () : width),
                      qMax (height, qw->minimumHeight () > 0 && !height ? qw->minimumHeight () : height));
  if (width > 0 || height > 0) qw->updateGeometry ();
}

void mq_place_default (Widget parent, Widget child)
{
  QWidget *pw = mq_content (parent), *cw = mq_qw (child);
  if (!pw || !cw) return;
  if (!pw->layout ()) {
    auto *l = new QVBoxLayout (pw);
    l->setContentsMargins (0, 0, 0, 0);
    l->setSpacing (0);
  }
  if (cw->parentWidget () != pw) cw->setParent (pw);
  if (pw->layout ()->indexOf (cw) < 0) pw->layout ()->addWidget (cw);
}

void mq_unplace_default (Widget parent, Widget child)
{
  QWidget *pw = mq_content (parent), *cw = mq_qw (child);
  if (cw) cw->hide ();
  if (pw && cw && pw->layout ()) pw->layout ()->removeWidget (cw);
}

void mq_relayout (Widget manager)
{
  if (manager && manager->klass->relayout) manager->klass->relayout (manager);
}

/* ======================================================================
 * Shells
 * ====================================================================== */
static QList<QPair<Widget, QPair<Atom, XtCallbackRec>>> s_wm_protocols;

class MqShellWidget : public QWidget {
public:
  Widget shell = nullptr;
  explicit MqShellWidget (Qt::WindowFlags f) : QWidget (nullptr, f) {}
protected:
  void closeEvent (QCloseEvent *e) override;
};

class MqDialogWidget : public QDialog {
public:
  Widget shell = nullptr;
protected:
  void closeEvent (QCloseEvent *e) override;
  void reject () override { close (); }
};

static void shell_close_request (Widget shell, QCloseEvent *e)
{
  e->ignore ();
  if (!shell) return;
  /* WM_DELETE_WINDOW protocol callbacks first (XmAddWMProtocolCallback) */
  Atom del = XInternAtom (mq_display, "WM_DELETE_WINDOW", False);
  XmAnyCallbackStruct cbs { XmCR_PROTOCOLS, nullptr };
  for (auto &p : QList<QPair<Widget, QPair<Atom, XtCallbackRec>>> (s_wm_protocols))
    if (p.first == shell && p.second.first == del) p.second.second.callback (shell, p.second.second.closure, &cbs);
  if (shell->destroying) return;
  unsigned char response = XmDESTROY;
  if (mq_find_res (shell, XmNdeleteResponse)) response = (unsigned char) mq_geti (shell, XmNdeleteResponse);
  if (response == XmDESTROY) {
    bool app_shell = mq_is (shell, applicationShellWidgetClass) || (!shell->parent && !shell->is_popup);
    if (mq_is (shell, xmDialogShellWidgetClass)) {
      for (Widget c : shell->children) if (c->managed) XtUnmanageChild (c);   /* Motif unmanages the dialog */
    } else {
      XtDestroyWidget (shell);
      if (app_shell) { fflush (stdout); exit (0); }
    }
  } else if (response == XmUNMAP) {
    if (mq_is (shell, xmDialogShellWidgetClass)) {
      for (Widget c : shell->children) if (c->managed) XtUnmanageChild (c);
    } else {
      XtPopdown (shell);
      if (QWidget *qw = mq_qw (shell)) qw->hide ();
    }
  }
}

void MqShellWidget::closeEvent (QCloseEvent *e) { shell_close_request (shell, e); }
void MqDialogWidget::closeEvent (QCloseEvent *e) { shell_close_request (shell, e); }

static QObject *shell_create (Widget w)
{
  QWidget *win;
  if (mq_is (w, xmDialogShellWidgetClass)) {
    auto *d = new MqDialogWidget;
    d->shell = w;
    win = d;
  } else if (mq_is (w, overrideShellWidgetClass)) {
    auto *s = new MqShellWidget (Qt::ToolTip | Qt::FramelessWindowHint);
    s->shell = w;
    win = s;
  } else {
    auto *s = new MqShellWidget (Qt::Window);
    s->shell = w;
    win = s;
  }
  auto *l = new QVBoxLayout (win);
  l->setContentsMargins (0, 0, 0, 0);
  l->setSpacing (0);
  return win;
}

static void shell_apply (Widget w, const QByteArray &res)
{
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  bool all = res.isEmpty ();
  if (all || res == XmNtitle) {
    QString t = mq_gets (w, XmNtitle);
    if (t.isEmpty () && !w->parent) t = QString::fromLocal8Bit (w->name);
    if (!t.isEmpty ()) qw->setWindowTitle (t);
  }
  if (all || res == XmNwidth || res == XmNheight) mq_apply_size (w);
  if ((all || res == XmNx || res == XmNy) && (mq_isset (w, XmNx) || mq_isset (w, XmNy)))
    qw->move (mq_geti (w, XmNx), mq_geti (w, XmNy));
  if (all || res == XmNgeometry) {
    QString g = mq_gets (w, XmNgeometry);
    if (!g.isEmpty ()) {
      int x, y;
      unsigned int gw, gh;
      int flags = XParseGeometry (g.toLocal8Bit ().constData (), &x, &y, &gw, &gh);
      if ((flags & WidthValue) && (flags & HeightValue)) qw->resize (gw, gh);
      if ((flags & XValue) && (flags & YValue)) qw->move (x, y);
    }
  }
  if (all || res == XmNminWidth || res == XmNminHeight) {
    int mw = mq_geti (w, XmNminWidth), mh = mq_geti (w, XmNminHeight);
    if (mw > 0 || mh > 0) qw->setMinimumSize (qMax (mw, 0), qMax (mh, 0));
  }
  if (all || res == XmNallowShellResize) { /* Qt windows always follow their content */ }
}

static void shell_place (Widget shell, Widget child)
{
  QWidget *win = mq_qw (shell), *cw = mq_qw (child);
  if (!win || !cw) return;
  cw->setParent (win);
  if (win->layout ()->indexOf (cw) < 0) win->layout ()->addWidget (cw);
  cw->setVisible (true);
  /* Managing the child of a dialog shell pops the dialog up (Motif's rule). */
  if (mq_is (shell, xmDialogShellWidgetClass) && shell->realized) mq_popup_shell (shell, true);
  else if (mq_is (shell, xmDialogShellWidgetClass) && shell->parent) {
    shell->realized = true;
    mq_popup_shell (shell, true);
  }
}

static void shell_unplace (Widget shell, Widget child)
{
  if (QWidget *cw = mq_qw (child)) cw->hide ();
  if (mq_is (shell, xmDialogShellWidgetClass)) {
    bool any = false;
    for (Widget c : shell->children) if (c->managed && c != child) any = true;
    if (!any) mq_popup_shell (shell, false);
  }
}

void mq_popup_shell (Widget shell, bool up)
{
  QWidget *win = mq_qw (shell);
  if (!win) return;
  if (up) {
    if (shell->popped_up && win->isVisible ()) { win->raise (); return; }
    shell->popped_up = true;
    if (mq_is (shell, xmDialogShellWidgetClass)) {
      Widget box = shell->children.isEmpty () ? nullptr : shell->children.first ();
      unsigned char style = box && mq_find_res (box, XmNdialogStyle) ? (unsigned char) mq_geti (box, XmNdialogStyle) : XmDIALOG_MODELESS;
      win->setWindowModality (style == XmDIALOG_FULL_APPLICATION_MODAL || style == XmDIALOG_PRIMARY_APPLICATION_MODAL ||
                              style == XmDIALOG_APPLICATION_MODAL || style == XmDIALOG_SYSTEM_MODAL ? Qt::ApplicationModal : Qt::NonModal);
      if (box) {
        QString title = mq_gets (box, XmNdialogTitle);
        if (!title.isEmpty ()) win->setWindowTitle (title);
        else if (mq_gets (shell, XmNtitle).isEmpty ()) win->setWindowTitle (QString::fromLocal8Bit (box->name));
      }
    }
    win->adjustSize ();
    mq_apply_size (shell);
    /* center transient windows over their owner, like mwm does for dialogs */
    Widget owner = shell->parent ? mq_shell_of (shell->parent) : nullptr;
    if (owner && mq_qw (owner) && !mq_isset (shell, XmNx)) {
      QRect r = mq_qw (owner)->frameGeometry ();
      win->move (r.center () - QPoint (win->width () / 2, win->height () / 2));
    }
    win->show ();
    win->raise ();
    mq_call (shell, XmNpopupCallback, nullptr);
  } else {
    if (!shell->popped_up && !win->isVisible ()) return;
    shell->popped_up = false;
    win->hide ();
    mq_call (shell, XmNpopdownCallback, nullptr);
  }
}

/* ======================================================================
 * Widget creation and destruction
 * ====================================================================== */
struct TypedArg { QByteArray name; QByteArray type; XtArgVal value; int size; };
struct ArgCollection { QVector<Arg> args; QList<TypedArg> typed; };

struct _XtVarArgsListRec { ArgCollection c; };

static void collect_va (va_list *ap, ArgCollection &c)
{
  for (char *name = va_arg (*ap, char *); name; name = va_arg (*ap, char *)) {
    if (!strcmp (name, XtVaTypedArg)) {
      TypedArg t;
      t.name = va_arg (*ap, char *);
      t.type = va_arg (*ap, char *);
      t.value = va_arg (*ap, XtArgVal);
      t.size = va_arg (*ap, int);
      c.typed.append (t);
    } else if (!strcmp (name, XtVaNestedList)) {
      auto *nested = (struct _XtVarArgsListRec *) va_arg (*ap, XtPointer);
      if (nested) { c.args += nested->c.args; c.typed += nested->c.typed; }
    } else {
      c.args.append ({ name, va_arg (*ap, XtArgVal) });
    }
  }
}

/* Resolve typed args against the widget's resource types. */
static void resolve_typed (Widget w, ArgCollection &c, QList<XtArgVal> &to_free)
{
  for (const TypedArg &t : c.typed) {
    const ResDef *d = mq_find_res (w, t.name.constData ());
    if (!d) continue;
    if (t.type == d->rtype) { c.args.append ({ (char *) t.name.constData (), t.value }); continue; }
    if (t.type == XmRString) {
      XtArgVal v = 0;
      if (mq_convert (w, d, (const char *) t.value, &v)) {
        c.args.append ({ (char *) t.name.constData (), v });
        if (!strcmp (d->rtype, XmRXmString)) to_free << v;
      }
    } else {
      c.args.append ({ (char *) t.name.constData (), t.value });
    }
  }
}

static void apply_args (Widget w, ArgList args, Cardinal n, bool initial)
{
  QList<QByteArray> changed;
  bool constraints_changed = false;
  for (Cardinal i = 0; i < n; i++) {
    bool constraint = false;
    const ResDef *d = mq_find_res (w, args[i].name, &constraint);
    if (!d) continue;   /* Xt silently ignores resources the class does not have */
    mq_store (w, d, args[i].value);
    if (constraint) constraints_changed = true;
    changed << QByteArray (args[i].name);
  }
  if (initial || !w->obj) return;
  for (WidgetClass k = w->klass; k; k = k->superclass)
    if (k->apply) { for (const QByteArray &r : changed) k->apply (w, r); break; }
  if (changed.contains (XmNsensitive)) XtSetSensitive (w, mq_getb (w, XmNsensitive));
  if (changed.contains (XmNmappedWhenManaged)) mq_sync_visible (w);
  if ((changed.contains (XmNwidth) || changed.contains (XmNheight)) && !(w->klass->kind & K_SHELL)) mq_apply_size (w);
  if (changed.contains (XmNtranslations)) XtOverrideTranslations (w, (XtTranslations) mq_get (w, XmNtranslations));
  if (constraints_changed && w->parent) mq_relayout (w->parent);
}

static Widget create_widget (const char *name, WidgetClass klass, Widget parent, ArgList args, Cardinal n, bool popup,
                             const QList<TypedArg> &typed = {})
{
  mq_ensure_app ();
  Widget w = new _WidgetRec;
  w->klass = klass;
  w->name = name ? QByteArray (name) : QByteArray ();
  w->parent = parent;
  w->is_popup = popup;
  if (parent) (popup ? parent->popups : parent->children).append (w);
  if (klass->kind & K_SHELL && !parent) s_shells.append (w);

  /* resources: defaults < database < arguments */
  mq_resources_from_db (w, args, n);
  ArgCollection c;
  for (Cardinal i = 0; i < n; i++) c.args.append (args[i]);
  c.typed = typed;
  QList<XtArgVal> to_free;
  resolve_typed (w, c, to_free);
  apply_args (w, c.args.data (), c.args.size (), true);
  for (XtArgVal v : to_free) XmStringFree ((XmString) v);

  /* the Qt side */
  for (WidgetClass k = klass; k && !w->obj; k = k->superclass)
    if (k->create) { w->obj = k->create (w); break; }
  if (!w->obj && (klass->kind & K_WIDGET) && !(klass->kind & K_SHELL) && !mq_is (w, xmMenuShellWidgetClass)) {
    w->obj = mq_canvas_create (w);   /* plain Core/Composite widgets: Xlib drawing surface */
  }
  if (w->obj) {
    w->obj->setProperty ("_mq_widget", QVariant::fromValue ((void *) w));
    if (QWidget *qw = mq_qw (w)) {
      qw->setObjectName (QString::fromLocal8Bit (w->name));
      if (!(klass->kind & K_SHELL)) qw->hide ();
    }
    mq_events_install (w);
  }
  for (WidgetClass k = klass; k; k = k->superclass)
    if (k->apply) { k->apply (w, QByteArray ()); break; }
  if (!mq_getb (w, XmNsensitive)) XtSetSensitive (w, False);
  if (!(klass->kind & K_SHELL)) mq_apply_size (w);
  if (mq_get (w, XmNtranslations)) XtOverrideTranslations (w, (XtTranslations) mq_get (w, XmNtranslations));
  for (WidgetClass k = klass; k; k = k->superclass)
    if (k->initialize) { k->initialize (w); break; }
  return w;
}

static void destroy_rec (Widget w)
{
  w->destroying = true;
  for (Widget c : QList<Widget> (w->children)) destroy_rec (c);
  for (Widget p : QList<Widget> (w->popups)) destroy_rec (p);
  mq_call (w, XmNdestroyCallback, nullptr);
}

static void free_rec (Widget w)
{
  for (Widget c : QList<Widget> (w->children)) free_rec (c);
  for (Widget p : QList<Widget> (w->popups)) free_rec (p);
  if (w->obj) {
    w->obj->setProperty ("_mq_widget", QVariant ());
    if (QWidget *qw = mq_qw (w)) qw->hide ();
    w->obj->deleteLater ();
  }
  for (int i = s_wm_protocols.size () - 1; i >= 0; i--)
    if (s_wm_protocols[i].first == w) s_wm_protocols.removeAt (i);
  s_shells.removeAll (w);
  mq_free_resources (w);
  if (w->ext_free) w->ext_free (w->ext);
  delete w;
}

/* ======================================================================
 * Public API
 * ====================================================================== */
extern "C" {

/* ---------- memory ---------- */
char *XtMalloc (Cardinal size) { void *p = malloc (size ? size : 1); if (!p) abort (); return (char *) p; }
char *XtCalloc (Cardinal num, Cardinal size) { void *p = calloc (num ? num : 1, size ? size : 1); if (!p) abort (); return (char *) p; }
char *XtRealloc (char *ptr, Cardinal size) { void *p = realloc (ptr, size ? size : 1); if (!p) abort (); return (char *) p; }
void XtFree (char *ptr) { free (ptr); }
char *__XtMalloc (unsigned size) { return XtMalloc (size); }
char *__XtCalloc (unsigned num, unsigned size) { return XtCalloc (num, size); }

/* ---------- classes ---------- */
Boolean _XtCheckSubclassFlag (Widget object, _XtXtEnum flag)
{
  if (!object) return False;
  unsigned k = object->klass->kind;
  switch (flag) {
  case 0x02: return (k & K_RECT) != 0;
  case 0x04: return (k & K_WIDGET) != 0;
  case 0x08: return (k & K_COMPOSITE) != 0;
  case 0x10: return (k & K_CONSTRAINT) != 0;
  case 0x20: return (k & K_SHELL) != 0;
  case 0x40: return mq_is (object, wmShellWidgetClass);
  case 0x80: return mq_is (object, topLevelShellWidgetClass);
  default: return False;
  }
}

Boolean _XtIsSubclassOf (Widget object, WidgetClass widget_class, WidgetClass, _XtXtEnum) { return mq_is (object, widget_class); }
Boolean XtIsSubclass (Widget w, WidgetClass c) { return mq_is (w, c); }
Boolean XtIsObject (Widget w) { return w != nullptr; }
WidgetClass XtClass (Widget w) { return w ? w->klass : nullptr; }
WidgetClass XtSuperclass (Widget w) { return w ? w->klass->superclass : nullptr; }
Boolean XtIsManaged (Widget w) { return w && w->managed; }
Boolean XtIsRealized (Widget w) { Widget s = mq_shell_of (w); return s && s->realized; }
Boolean XtIsSensitive (Widget w)
{
  for (Widget x = w; x; x = x->parent) if (!x->sensitive) return False;
  return w != nullptr;
}

/* ---------- creation ---------- */
Widget XtCreateWidget (_Xconst char *name, WidgetClass klass, Widget parent, ArgList args, Cardinal n)
{
  return create_widget (name, klass, parent, args, n, (klass->kind & K_SHELL) != 0);
}

Widget XtCreateManagedWidget (_Xconst char *name, WidgetClass klass, Widget parent, ArgList args, Cardinal n)
{
  Widget w = XtCreateWidget (name, klass, parent, args, n);
  XtManageChild (w);
  return w;
}

Widget XtVaCreateWidget (_Xconst char *name, WidgetClass klass, Widget parent, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, parent);
  collect_va (&ap, c);
  va_end (ap);
  return create_widget (name, klass, parent, c.args.data (), c.args.size (), (klass->kind & K_SHELL) != 0, c.typed);
}

Widget XtVaCreateManagedWidget (_Xconst char *name, WidgetClass klass, Widget parent, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, parent);
  collect_va (&ap, c);
  va_end (ap);
  Widget w = create_widget (name, klass, parent, c.args.data (), c.args.size (), (klass->kind & K_SHELL) != 0, c.typed);
  XtManageChild (w);
  return w;
}

Widget XtCreatePopupShell (_Xconst char *name, WidgetClass klass, Widget parent, ArgList args, Cardinal n)
{
  return create_widget (name, klass, parent, args, n, true);
}

Widget XtVaCreatePopupShell (_Xconst char *name, WidgetClass klass, Widget parent, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, parent);
  collect_va (&ap, c);
  va_end (ap);
  return create_widget (name, klass, parent, c.args.data (), c.args.size (), true, c.typed);
}

Widget XtAppCreateShell (_Xconst char *name, _Xconst char *class_, WidgetClass klass, Display *, ArgList args, Cardinal n)
{
  (void) class_;
  return create_widget (name ? name : mq_app_name (), klass, nullptr, args, n, false);
}

Widget XtVaAppCreateShell (_Xconst char *name, _Xconst char *class_, WidgetClass klass, Display *dpy, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, dpy);
  (void) class_;
  collect_va (&ap, c);
  va_end (ap);
  return create_widget (name ? name : mq_app_name (), klass, nullptr, c.args.data (), c.args.size (), false, c.typed);
}

Widget XtCreateApplicationShell (_Xconst char *name, WidgetClass klass, ArgList args, Cardinal n)
{
  return create_widget (name ? name : mq_app_name (), klass, nullptr, args, n, false);
}

void XtDestroyWidget (Widget w)
{
  if (!w || w->destroying) return;
  if (w->managed && w->parent && w->parent->klass->unplace) w->parent->klass->unplace (w->parent, w);
  destroy_rec (w);
  if (w->parent) {
    w->parent->children.removeAll (w);
    w->parent->popups.removeAll (w);
    if (w->managed) mq_relayout (w->parent);
  }
  free_rec (w);
}

/* ---------- management ---------- */
void XtManageChild (Widget w)
{
  if (!w || w->managed || w->destroying) return;
  w->managed = true;
  Widget p = w->parent;
  if (p && !w->is_popup) {
    for (WidgetClass k = p->klass; k; k = k->superclass)
      if (k->place) { k->place (p, w); break; }
  }
  mq_sync_visible (w);
  if (p && p->realized) w->realized = true;
}

void XtManageChildren (WidgetList children, Cardinal n) { for (Cardinal i = 0; i < n; i++) XtManageChild (children[i]); }

void XtUnmanageChild (Widget w)
{
  if (!w || !w->managed) return;
  w->managed = false;
  Widget p = w->parent;
  if (p && !w->is_popup)
    for (WidgetClass k = p->klass; k; k = k->superclass)
      if (k->unplace) { k->unplace (p, w); break; }
  mq_sync_visible (w);
}

void XtUnmanageChildren (WidgetList children, Cardinal n) { for (Cardinal i = 0; i < n; i++) XtUnmanageChild (children[i]); }

void XtChangeManagedSet (WidgetList unmanage, Cardinal nu, XtDoChangeProc, XtPointer, WidgetList manage, Cardinal nm)
{
  XtUnmanageChildren (unmanage, nu);
  XtManageChildren (manage, nm);
}

void XtSetMappedWhenManaged (Widget w, _XtBoolean mapped)
{
  XtVaSetValues (w, XmNmappedWhenManaged, (XtArgVal) (mapped ? True : False), NULL);
}


static void realize_rec (Widget w)
{
  w->realized = true;
  for (Widget c : w->children) realize_rec (c);
}

void XtRealizeWidget (Widget w)
{
  if (!w) return;
  realize_rec (w);
  if ((w->klass->kind & K_SHELL) && !w->is_popup) {
    QWidget *win = mq_qw (w);
    if (win && mq_getb (w, XmNmappedWhenManaged)) {
      win->adjustSize ();
      shell_apply (w, QByteArray ());
      if (!mq_getb (w, XmNiconic) || !mq_find_res (w, XmNiconic)) win->show ();
      else win->showMinimized ();
      w->popped_up = true;
    }
  }
}

void XtUnrealizeWidget (Widget w)
{
  if (!w) return;
  w->realized = false;
  if (QWidget *qw = mq_qw (w)) if (w->klass->kind & K_SHELL) qw->hide ();
}

void XtPopup (Widget shell, XtGrabKind grab)
{
  if (!shell) return;
  realize_rec (shell);
  if (QWidget *win = mq_qw (shell)) win->setWindowModality (grab == XtGrabExclusive ? Qt::ApplicationModal : Qt::NonModal);
  mq_popup_shell (shell, true);
}

void XtPopupSpringLoaded (Widget shell) { XtPopup (shell, XtGrabExclusive); }
void XtPopdown (Widget shell) { if (shell) mq_popup_shell (shell, false); }

void XtCallbackNone (Widget, XtPointer closure, XtPointer) { XtPopup ((Widget) closure, XtGrabNone); }
void XtCallbackNonexclusive (Widget, XtPointer closure, XtPointer) { XtPopup ((Widget) closure, XtGrabNonexclusive); }
void XtCallbackExclusive (Widget, XtPointer closure, XtPointer) { XtPopup ((Widget) closure, XtGrabExclusive); }
void XtCallbackPopdown (Widget, XtPointer closure, XtPointer)
{
  auto *rec = (XtPopdownID) closure;
  if (rec) XtPopdown (rec->shell_widget);
}

/* ---------- accessors ---------- */
String XtName (Widget w) { return w ? w->name.data () : nullptr; }
Widget XtParent (Widget w) { return w ? w->parent : nullptr; }
Display *XtDisplay (Widget) { return mq_display; }
Display *XtDisplayOfObject (Widget) { return mq_display; }
Screen *XtScreen (Widget) { return DefaultScreenOfDisplay (mq_display); }
Screen *XtScreenOfObject (Widget) { return DefaultScreenOfDisplay (mq_display); }
Window XtWindow (Widget w) { return mq_window_for (w); }
Window XtWindowOfObject (Widget w)
{
  while (w && !mq_qw (w)) w = w->parent;
  return mq_window_for (w);
}
XtAppContext XtWidgetToApplicationContext (Widget) { mq_ensure_app (); return mq_app; }
XtAppContext XtDisplayToApplicationContext (Display *) { mq_ensure_app (); return mq_app; }

Widget XtWindowToWidget (Display *, Window win) { return mq_widget_for_window (win); }

static Widget name_lookup (Widget w, const QByteArray &part, bool any_depth)
{
  QList<Widget> kids = w->children + w->popups;
  for (Widget c : kids)
    if (c->name == part || (part.startsWith ('X') && c->klass->class_name == part)) return c;
  if (any_depth)
    for (Widget c : kids)
      if (Widget r = name_lookup (c, part, true)) return r;
  return nullptr;
}

Widget XtNameToWidget (Widget reference, _Xconst char *names)
{
  if (!reference || !names) return nullptr;
  Widget w = reference;
  QByteArray s (names);
  int i = 0;
  while (i < s.size () && w) {
    bool loose = false;
    while (i < s.size () && (s[i] == '.' || s[i] == '*')) { if (s[i] == '*') loose = true; i++; }
    int j = i;
    while (j < s.size () && s[j] != '.' && s[j] != '*') j++;
    QByteArray part = s.mid (i, j - i);
    if (part.isEmpty ()) break;
    w = name_lookup (w, part, loose);
    i = j;
  }
  return w;
}

void XtSetSensitive (Widget w, _XtBoolean sensitive)
{
  if (!w) return;
  w->sensitive = sensitive;
  if (QWidget *qw = mq_qw (w)) qw->setEnabled (sensitive);
  else if (QAction *a = mq_action (w)) a->setEnabled (sensitive);
  XtArgVal v = sensitive ? True : False;
  if (const ResDef *d = mq_find_res (w, XmNsensitive)) mq_store (w, d, v);
}

void XtMoveWidget (Widget w, _XtPosition x, _XtPosition y)
{
  XtVaSetValues (w, XmNx, (XtArgVal) x, XmNy, (XtArgVal) y, NULL);
  if (QWidget *qw = mq_qw (w)) qw->move (x, y);
}

void XtResizeWidget (Widget w, _XtDimension width, _XtDimension height, _XtDimension)
{
  XtVaSetValues (w, XmNwidth, (XtArgVal) width, XmNheight, (XtArgVal) height, NULL);
}

void XtConfigureWidget (Widget w, _XtPosition x, _XtPosition y, _XtDimension width, _XtDimension height, _XtDimension bw)
{
  XtMoveWidget (w, x, y);
  XtResizeWidget (w, width, height, bw);
}

void XtTranslateCoords (Widget w, _XtPosition x, _XtPosition y, Position *rx, Position *ry)
{
  QWidget *qw = mq_qw (w);
  QPoint p = qw ? qw->mapToGlobal (QPoint (x, y)) : QPoint (x, y);
  if (rx) *rx = p.x ();
  if (ry) *ry = p.y ();
}

XtGeometryResult XtQueryGeometry (Widget w, XtWidgetGeometry *, XtWidgetGeometry *preferred)
{
  QWidget *qw = mq_qw (w);
  if (preferred) {
    preferred->request_mode = CWWidth | CWHeight;
    QSize s = qw ? qw->sizeHint () : QSize ();
    preferred->width = s.width ();
    preferred->height = s.height ();
  }
  return XtGeometryYes;
}

XtGeometryResult XtMakeResizeRequest (Widget w, _XtDimension width, _XtDimension height, Dimension *rw, Dimension *rh)
{
  XtResizeWidget (w, width, height, 0);
  if (rw) *rw = width;
  if (rh) *rh = height;
  return XtGeometryYes;
}

/* ---------- values ---------- */
static void write_out (const ResDef *d, XtArgVal dst, XtArgVal v)
{
  void *p = (void *) dst;
  if (!p) return;
  switch (d->size) {
  case 1: *(unsigned char *) p = (unsigned char) v; break;
  case 2: *(unsigned short *) p = (unsigned short) v; break;
  case 4: *(unsigned int *) p = (unsigned int) v; break;
  default: memcpy (p, &v, qMin ((size_t) d->size, sizeof (XtArgVal))); break;
  }
}

void XtGetValues (Widget w, ArgList args, Cardinal n)
{
  if (!w) return;
  for (Cardinal i = 0; i < n; i++) {
    const ResDef *d = mq_find_res (w, args[i].name);
    QByteArray name = args[i].name;
    XtArgVal v = 0;
    bool handled = false;
    for (WidgetClass k = w->klass; k && !handled; k = k->superclass)
      if (k->fetch) { handled = k->fetch (w, name, &v); break; }
    if (!handled) {
      if (name == XmNchildren) { w->scratch.resize (sizeof (Widget) * (w->children.size () + 1));
        auto *list = (Widget *) w->scratch.data ();
        for (int c = 0; c < w->children.size (); c++) list[c] = w->children[c];
        v = (XtArgVal) list; handled = true; }
      else if (name == XmNnumChildren) { v = w->children.size (); handled = true; }
      else if (name == XmNwidth || name == XmNheight || name == XmNx || name == XmNy) {
        if (QWidget *qw = mq_qw (w)) {
          QWidget *q = qw->isVisible () || !qw->isWindow () ? qw : qw;
          QSize sz = q->isVisible () ? q->size () : q->sizeHint ().expandedTo (q->minimumSize ());
          v = name == XmNwidth ? sz.width () : name == XmNheight ? sz.height () : name == XmNx ? q->x () : q->y ();
          if (!q->isVisible () && (name == XmNwidth || name == XmNheight) && mq_isset (w, name.constData ()))
            v = mq_get (w, name.constData ());
          handled = true;
        }
      }
    }
    if (!d) {
      if (handled && args[i].value) {
        if (name == XmNnumChildren) *(Cardinal *) args[i].value = (Cardinal) v;
        else *(XtArgVal *) args[i].value = v;
      }
      continue;
    }
    if (!handled) v = mq_get (w, args[i].name);
    if (!strcmp (d->rtype, XmRXmString) && v && !handled) v = (XtArgVal) XmStringCopy ((XmString) v);
    write_out (d, args[i].value, v);
  }
}

void XtSetValues (Widget w, ArgList args, Cardinal n)
{
  if (!w) return;
  apply_args (w, args, n, false);
}

void XtVaSetValues (Widget w, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, w);
  collect_va (&ap, c);
  va_end (ap);
  QList<XtArgVal> to_free;
  resolve_typed (w, c, to_free);
  apply_args (w, c.args.data (), c.args.size (), false);
  for (XtArgVal v : to_free) XmStringFree ((XmString) v);
}

void XtVaGetValues (Widget w, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, w);
  collect_va (&ap, c);
  va_end (ap);
  XtGetValues (w, c.args.data (), c.args.size ());
}

XtVarArgsList XtVaCreateArgsList (XtPointer unused, ...)
{
  auto *list = new _XtVarArgsListRec;
  va_list ap;
  va_start (ap, unused);
  collect_va (&ap, list->c);
  va_end (ap);
  return (XtVarArgsList) list;
}

ArgList XtMergeArgLists (ArgList a1, Cardinal n1, ArgList a2, Cardinal n2)
{
  ArgList r = (ArgList) XtMalloc (sizeof (Arg) * (n1 + n2));
  memcpy (r, a1, sizeof (Arg) * n1);
  memcpy (r + n1, a2, sizeof (Arg) * n2);
  return r;
}

void XtGetResourceList (WidgetClass, XtResourceList *list, Cardinal *num) { if (list) *list = nullptr; if (num) *num = 0; }

/* ---------- callbacks ---------- */
void XtAddCallback (Widget w, _Xconst char *name, XtCallbackProc proc, XtPointer closure)
{
  if (w && proc) w->callbacks[name].append ({ proc, closure });
}

void XtAddCallbacks (Widget w, _Xconst char *name, XtCallbackList list)
{
  for (; list && list->callback; list++) XtAddCallback (w, name, list->callback, list->closure);
}

void XtRemoveCallback (Widget w, _Xconst char *name, XtCallbackProc proc, XtPointer closure)
{
  if (!w) return;
  QList<XtCallbackRec> &l = w->callbacks[name];
  for (int i = 0; i < l.size (); i++)
    if (l[i].callback == proc && l[i].closure == closure) { l.removeAt (i); return; }
}

void XtRemoveCallbacks (Widget w, _Xconst char *name, XtCallbackList list)
{
  for (; list && list->callback; list++) XtRemoveCallback (w, name, list->callback, list->closure);
}

void XtRemoveAllCallbacks (Widget w, _Xconst char *name) { if (w) w->callbacks.remove (name); }
void XtCallCallbacks (Widget w, _Xconst char *name, XtPointer call_data) { mq_call (w, name, call_data); }

void XtCallCallbackList (Widget w, XtCallbackList list, XtPointer call_data)
{
  for (; list && list->callback; list++) list->callback (w, list->closure, call_data);
}

XtCallbackStatus XtHasCallbacks (Widget w, _Xconst char *name)
{
  if (!w || !mq_find_res (w, name)) return XtCallbackNoList;
  return mq_has_callbacks (w, name) ? XtCallbackHasSome : XtCallbackHasNone;
}

/* ---------- application start-up ---------- */
void XtToolkitInitialize (void) {}

XtAppContext XtCreateApplicationContext (void) { mq_ensure_app (); return mq_app; }
void XtDestroyApplicationContext (XtAppContext) { fflush (stdout); }
void XtAppSetFallbackResources (XtAppContext app, String *specs) { if (app) app->fallback = specs; }

static char *base_name (const char *argv0)
{
  const char *s = argv0 ? strrchr (argv0, '/') : nullptr;
  return const_cast<char *> (s ? s + 1 : argv0 ? argv0 : "motif");
}

Display *XtOpenDisplay (XtAppContext app, _Xconst char *, _Xconst char *name, _Xconst char *class_,
                        XrmOptionDescRec *options, Cardinal n, int *argc, char **argv)
{
  mq_ensure_app (argc, argv);
  const char *app_name = name ? name : base_name (argc && *argc > 0 ? argv[0] : nullptr);
  for (int i = 1; argc && i + 1 < *argc; i++) if (!strcmp (argv[i], "-name")) app_name = argv[i + 1];
  mq_resources_init (app_name, class_, app ? app->fallback : nullptr);
  mq_parse_command_line (options, n, argc, argv);
  return mq_display;
}

void XtDisplayInitialize (XtAppContext app, Display *, _Xconst char *name, _Xconst char *class_,
                          XrmOptionDescRec *options, Cardinal n, int *argc, char **argv)
{
  XtOpenDisplay (app, nullptr, name, class_, options, n, argc, argv);
}

Widget XtOpenApplication (XtAppContext *app_return, _Xconst char *class_, XrmOptionDescList options, Cardinal n,
                          int *argc, String *argv, String *fallback, WidgetClass klass, ArgList args, Cardinal num_args)
{
  XtAppContext app = XtCreateApplicationContext ();
  app->fallback = fallback;
  XtOpenDisplay (app, nullptr, nullptr, class_, options, n, argc, argv);
  if (app_return) *app_return = app;
  QVector<Arg> all;
  all.append ({ (char *) XtNargc, (XtArgVal) (argc ? *argc : 0) });
  all.append ({ (char *) XtNargv, (XtArgVal) argv });
  for (Cardinal i = 0; i < num_args; i++) all.append (args[i]);
  return create_widget (mq_app_name (), klass ? klass : applicationShellWidgetClass, nullptr, all.data (), all.size (), false);
}

Widget XtVaOpenApplication (XtAppContext *app_return, _Xconst char *class_, XrmOptionDescList options, Cardinal n,
                            int *argc, String *argv, String *fallback, WidgetClass klass, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, klass);
  collect_va (&ap, c);
  va_end (ap);
  return XtOpenApplication (app_return, class_, options, n, argc, argv, fallback, klass, c.args.data (), c.args.size ());
}

Widget XtAppInitialize (XtAppContext *app_return, _Xconst char *class_, XrmOptionDescList options, Cardinal n,
                        int *argc, String *argv, String *fallback, ArgList args, Cardinal num_args)
{
  return XtOpenApplication (app_return, class_, options, n, argc, argv, fallback, applicationShellWidgetClass, args, num_args);
}

Widget XtVaAppInitialize (XtAppContext *app_return, _Xconst char *class_, XrmOptionDescList options, Cardinal n,
                          int *argc, String *argv, String *fallback, ...)
{
  ArgCollection c;
  va_list ap;
  va_start (ap, fallback);
  collect_va (&ap, c);
  va_end (ap);
  return XtOpenApplication (app_return, class_, options, n, argc, argv, fallback, applicationShellWidgetClass,
                            c.args.data (), c.args.size ());
}

Widget XtInitialize (_Xconst char *, _Xconst char *class_, XrmOptionDescRec *options, Cardinal n, int *argc, String *argv)
{
  return XtAppInitialize (nullptr, class_, options, n, argc, argv, nullptr, nullptr, 0);
}

XtLanguageProc XtSetLanguageProc (XtAppContext, XtLanguageProc, XtPointer) { return nullptr; }

/* ---------- errors and warnings ---------- */
static void default_error (String msg) { fprintf (stderr, "Error: %s\n", msg); exit (1); }
static void default_warning (String msg) { fprintf (stderr, "Warning: %s\n", msg); }

void XtAppError (XtAppContext app, _Xconst char *msg)
{
  if (app && app->error_handler) app->error_handler ((String) msg); else default_error ((String) msg);
  exit (1);   /* error handlers must not return */
}
void XtError (_Xconst char *msg) { XtAppError (mq_app, msg); }
void XtAppWarning (XtAppContext app, _Xconst char *msg)
{
  if (app && app->warning_handler) app->warning_handler ((String) msg); else default_warning ((String) msg);
}
void XtWarning (_Xconst char *msg) { XtAppWarning (mq_app, msg); }

static void format_msg (char *buf, size_t n, _Xconst char *def, String *params, Cardinal *num)
{
  const char *p[10] = {};
  for (Cardinal i = 0; num && i < *num && i < 10; i++) p[i] = params[i];
  snprintf (buf, n, def ? def : "", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
}

void XtAppErrorMsg (XtAppContext app, _Xconst char *, _Xconst char *, _Xconst char *, _Xconst char *def, String *params, Cardinal *num)
{
  char buf[1024];
  format_msg (buf, sizeof buf, def, params, num);
  XtAppError (app, buf);
}
void XtErrorMsg (_Xconst char *a, _Xconst char *b, _Xconst char *c, _Xconst char *def, String *params, Cardinal *num)
{
  XtAppErrorMsg (mq_app, a, b, c, def, params, num);
}
void XtAppWarningMsg (XtAppContext app, _Xconst char *, _Xconst char *, _Xconst char *, _Xconst char *def, String *params, Cardinal *num)
{
  char buf[1024];
  format_msg (buf, sizeof buf, def, params, num);
  XtAppWarning (app, buf);
}
void XtWarningMsg (_Xconst char *a, _Xconst char *b, _Xconst char *c, _Xconst char *def, String *params, Cardinal *num)
{
  XtAppWarningMsg (mq_app, a, b, c, def, params, num);
}

XtErrorHandler XtAppSetErrorHandler (XtAppContext app, XtErrorHandler h _X_NORETURN)
{
  mq_ensure_app ();
  XtErrorHandler old = app->error_handler; app->error_handler = h; return old ? old : default_error;
}
XtErrorHandler XtAppSetWarningHandler (XtAppContext app, XtErrorHandler h)
{
  mq_ensure_app ();
  XtErrorHandler old = app->warning_handler; app->warning_handler = h; return old ? old : default_warning;
}
void XtSetErrorHandler (XtErrorHandler h _X_NORETURN) { mq_ensure_app (); mq_app->error_handler = h; }
void XtSetWarningHandler (XtErrorHandler h) { mq_ensure_app (); mq_app->warning_handler = h; }
XtErrorMsgHandler XtAppSetErrorMsgHandler (XtAppContext app, XtErrorMsgHandler h _X_NORETURN)
{
  XtErrorMsgHandler old = app->error_msg_handler; app->error_msg_handler = h; return old;
}
XtErrorMsgHandler XtAppSetWarningMsgHandler (XtAppContext app, XtErrorMsgHandler h)
{
  XtErrorMsgHandler old = app->warning_msg_handler; app->warning_msg_handler = h; return old;
}

void XtAppSetExitFlag (XtAppContext app) { if (app) app->exit_flag = true; QCoreApplication::exit (0); }
Boolean XtAppGetExitFlag (XtAppContext app) { return app && app->exit_flag; }

/* ---------- misc ---------- */
String XtResolvePathname (Display *, _Xconst char *, _Xconst char *filename, _Xconst char *, _Xconst char *, Substitution, Cardinal, XtFilePredicate)
{
  return filename && access (filename, R_OK) == 0 ? XtNewString ((char *) filename) : nullptr;
}

String XtFindFile (_Xconst char *path, Substitution, Cardinal, XtFilePredicate)
{
  return path && access (path, R_OK) == 0 ? XtNewString ((char *) path) : nullptr;
}

void XtSetTypeConverter (_Xconst char *, _Xconst char *, XtTypeConverter, XtConvertArgList, Cardinal, XtCacheType, XtDestructor) {}
void XtAppSetTypeConverter (XtAppContext, _Xconst char *, _Xconst char *, XtTypeConverter, XtConvertArgList, Cardinal, XtCacheType, XtDestructor) {}
void XtAppAddConverter (XtAppContext, _Xconst char *, _Xconst char *, XtConverter, XtConvertArgList, Cardinal) {}
void XtAddConverter (_Xconst char *, _Xconst char *, XtConverter, XtConvertArgList, Cardinal) {}

Boolean XtConvertAndStore (Widget w, _Xconst char *from_type, XrmValue *from, _Xconst char *to_type, XrmValue *to)
{
  if (!from || !to || strcmp (from_type, XtRString)) return False;
  ResDef d { "", false, "", "", to_type, to->size ? to->size : (unsigned) sizeof (XtArgVal), XtRString, 0, nullptr };
  static XtArgVal result;
  if (!mq_convert (w, &d, (const char *) from->addr, &result)) return False;
  if (to->addr) memcpy (to->addr, &result, qMin ((size_t) to->size, sizeof (XtArgVal)));
  else { to->addr = (XPointer) &result; to->size = sizeof (result); }
  return True;
}

void XmAddProtocolCallback (Widget shell, Atom, Atom protocol, XtCallbackProc cb, XtPointer closure)
{
  s_wm_protocols.append ({ shell, { protocol, { cb, closure } } });
}

void XmRemoveProtocolCallback (Widget shell, Atom, Atom protocol, XtCallbackProc cb, XtPointer closure)
{
  for (int i = 0; i < s_wm_protocols.size (); i++) {
    auto &p = s_wm_protocols[i];
    if (p.first == shell && p.second.first == protocol && p.second.second.callback == cb && p.second.second.closure == closure) {
      s_wm_protocols.removeAt (i);
      return;
    }
  }
}

void XmAddProtocols (Widget, Atom, Atom *, Cardinal) {}
void XmRemoveProtocols (Widget, Atom, Atom *, Cardinal) {}
void XmActivateProtocol (Widget, Atom, Atom) {}
void XmDeactivateProtocol (Widget, Atom, Atom) {}
void XmSetProtocolHooks (Widget, Atom, Atom, XtCallbackProc, XtPointer, XtCallbackProc, XtPointer) {}

} /* extern "C" */
