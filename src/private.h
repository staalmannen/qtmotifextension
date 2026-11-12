/*
 * Motif on Qt - the Xt/Motif C API implemented with Qt widgets.
 * Internal header shared by the implementation files.
 *
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 *
 * Design in one paragraph: a Widget is a C++ record owning one Qt object -
 * a QWidget, or a QAction for buttons that live in menus. Resources are
 * stored per widget with the exact types/sizes/defaults Motif declares
 * (src/generated/resources.inc is extracted from the Motif sources), and a
 * resource database implements the Xrm matching rules for app-defaults and
 * fallback resources. Each widget class supplies a handful of hooks: build
 * the Qt object, react to resource changes, report dynamic values, and lay
 * out managed children. Xlib drawing goes to per-window backing images.
 */
#ifndef MOTIFQT_PRIVATE_H
#define MOTIFQT_PRIVATE_H

/* Qt first: the X11 headers #define names (None, Bool, Status, KeyPress,
 * Unsorted ...) that break Qt headers parsed after them. Every Qt header the
 * implementation needs is therefore included here, before X11. */
#include <QtWidgets>

/* Qt names that X11 macros (KeyPress, FocusIn ...) would clobber */
static constexpr QEvent::Type MQ_QKeyPress = QEvent::KeyPress;
static constexpr QEvent::Type MQ_QKeyRelease = QEvent::KeyRelease;
static constexpr QEvent::Type MQ_QFocusIn = QEvent::FocusIn;
static constexpr QEvent::Type MQ_QFocusOut = QEvent::FocusOut;
static constexpr QEvent::Type MQ_QExpose = QEvent::Expose;
inline QCursor mq_qcursor (int shape) { return QCursor (static_cast<Qt::CursorShape> (shape)); }

#include <Xm/XmAll.h>
/* the Motif 2.3 extension widgets (not in XmAll.h) */
#include <Xm/SlideC.h>
#include <Xm/DropDown.h>
#include <Xm/ButtonBox.h>
#include <Xm/ColorS.h>
#include <Xm/FontS.h>
#include <Xm/XmP.h>
#include <Xm/Ext18List.h>
#include <Xm/Outline.h>
#include <Xm/Tree.h>
#include <Xm/IconBox.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <X11/IntrinsicP.h>
#include <X11/ShellP.h>
#include <Xm/ManagerP.h>
#include <Xm/PrimitiveP.h>
#include <Xm/GadgetP.h>

/* IntrinsicP.h and XmP.h define macros for names this library implements as
 * functions (as libXt itself does, they are undefined here). */
#undef XtDisplay
#undef XtScreen
#undef XtWindow
#undef XtClass
#undef XtSuperclass
#undef XtIsRealized
#undef XtParent
#undef XtIsRectObj
#undef XtIsWidget
#undef XtIsComposite
#undef XtIsConstraint
#undef XtIsShell
#undef XtIsWMShell
#undef XtIsTopLevelShell
#undef XtIsManaged
#undef XtIsSensitive
#undef XtX
#undef XtY
#undef XtWidth
#undef XtHeight
#undef XtBorderWidth
#undef XtBackground
#undef XtCheckSubclass
#undef XmeWarning

#include <functional>

/* ---------------------------------------------------------------- classes */
enum ClassKind {
  K_OBJECT = 0, K_RECT = 1 << 0, K_WIDGET = 1 << 1, K_COMPOSITE = 1 << 2, K_CONSTRAINT = 1 << 3,
  K_SHELL = 1 << 4, K_PRIMITIVE = 1 << 5, K_MANAGER = 1 << 6, K_GADGET = 1 << 7, K_MENU_ITEM = 1 << 8
};

struct ResDef {
  const char *cls;          /* Motif class the resource belongs to */
  bool constraint;          /* constraint resource (lives on the child) */
  const char *name, *rclass, *rtype;
  unsigned size;
  const char *dtype;
  XtArgVal dimm;            /* immediate default */
  const char *dstr;         /* string default (converted on demand) */
};

typedef QObject *(*CreateProc) (Widget w);
typedef void (*ApplyProc) (Widget w, const QByteArray &res);   /* empty res = apply everything */
typedef bool (*FetchProc) (Widget w, const QByteArray &res, XtArgVal *out);
typedef void (*ChildProc) (Widget parent, Widget child);
typedef void (*WidgetProc) (Widget w);

/* How a widget class is implemented. Every class also has a genuine Xt class
 * record (xt), which is what WidgetClass values point to: programs compare,
 * subclass and read those. Classes a program defines itself get an MqClass
 * built for them (foreign = true; see xt_foreign.cpp). */
struct MqClass {
  const char *class_name;   /* e.g. "XmPushButton"; also the key into the resource tables */
  MqClass *superclass;
  unsigned kind;
  CreateProc create;
  ApplyProc apply;
  FetchProc fetch;
  ChildProc place;          /* child became managed */
  ChildProc unplace;        /* child became unmanaged / destroyed */
  WidgetProc relayout;      /* constraints of a child changed */
  WidgetProc initialize;    /* after create + resources: build internal children */
  WidgetClass xt;           /* the Xt class record */
  const char *res_class;    /* resource table to use if it differs from class_name */
  QList<const ResDef *> *resolved;            /* cache: all normal resources incl. superclasses */
  QList<const ResDef *> *resolved_constraints;
  /* resources the class does not declare, forwarded to internal children
     (DropDown's items ...); set before initialize they arrive afterwards */
  void (*set_other) (Widget w, const char *name, XtArgVal value);
  bool (*get_other) (Widget w, const char *name, XtArgVal dest);   /* writes *dest like XtGetValues */
  bool foreign;
};

MqClass *mq_class (WidgetClass c);      /* nullptr for unknown, never for a valid class */
void mq_register_class (MqClass *c, Cardinal widget_size);

/* An Xt class record for classes with no real Motif type here: CoreClassPart
 * plus room for the parts that follow it in Motif (programs only read those
 * through the real types of the classes they subclass). */
struct MqXtClassRec { CoreClassPart core_class; char parts[512]; };

struct MqClassRegistrar {
  MqClassRegistrar (MqClass *c, Cardinal widget_size) { mq_register_class (c, widget_size); }
};

/* Instance sizes for the Xt parts of our widgets, by kind */
Cardinal mq_instance_size (unsigned kind);

#define MQ_DEFINE_CLASS_XT(var, rec, xtrec, name, super, kind, create, apply, fetch, place, unplace, relayout, init) \
  MqClass rec = { name, super, kind, create, apply, fetch, place, unplace, relayout, init, (WidgetClass) &xtrec, \
                  nullptr, nullptr, nullptr, nullptr, nullptr, false }; \
  WidgetClass var = (WidgetClass) &xtrec; \
  static MqClassRegistrar rec##_registrar (&rec, mq_instance_size (kind));
#define MQ_DEFINE_CLASS(var, rec, name, super, kind, create, apply, fetch, place, unplace, relayout, init) \
  MqXtClassRec rec##_xt; \
  MQ_DEFINE_CLASS_XT (var, rec, rec##_xt, name, super, kind, create, apply, fetch, place, unplace, relayout, init)

/* ---------------------------------------------------------------- widgets */
class MqBridge;

struct EventHandlerRec { EventMask mask; Boolean nonmaskable; XtEventHandler proc; XtPointer closure; };

/* Our data for a widget. It sits just before the widget's Xt instance record
 * (CorePart first, then the class's parts), which is what a Widget points to;
 * R(w) gets from one to the other. */
struct MqRec {
  MqClass *klass = nullptr;
  QByteArray name;
  Widget parent = nullptr;
  QPointer<QObject> obj;        /* QWidget, or QAction for menu entries */
  QPointer<QWidget> inner;      /* the interesting part of a composite Qt widget */
  QPointer<QWidget> content;    /* managers: where children's QWidgets live */
  QHash<QByteArray, XtArgVal> res;                  /* resources explicitly set (owned copies) */
  QHash<QByteArray, QList<XtCallbackRec>> callbacks;
  QList<Widget> children;       /* normal children, creation order */
  QList<Widget> popups;         /* popup children (shells) */
  bool managed = false, realized = false, destroying = false, sensitive = true, is_popup = false;
  bool popped_up = false;
  QList<EventHandlerRec> handlers;
  struct _TranslationData *translations = nullptr;
  MqBridge *bridge = nullptr;
  Window window = 0;            /* X window id used by Xlib drawing */
  void *ext = nullptr;          /* class-specific C++ data (owned via ext_free) */
  void (*ext_free) (void *) = nullptr;
  QByteArray scratch;
  QList<QPair<QByteArray, XtArgVal>> deferred;   /* set_other args given at creation */
  QVector<Widget> xt_children, xt_popups;        /* arrays behind composite.children / core.popup_list */
  QHash<QByteArray, QVector<XtCallbackRec>> xt_callback_arrays;   /* XtCallbackList fields of foreign widgets */
};

enum { MQ_HEADER = (sizeof (MqRec) + 31) & ~31 };
inline MqRec *R (Widget w) { return reinterpret_cast<MqRec *> (reinterpret_cast<char *> (w) - MQ_HEADER); }
Widget mq_alloc_widget (MqClass *klass);
void mq_free_widget (Widget w);
void mq_sync_core (Widget w);           /* core.x/y/width/height ... from Qt and our state */
void mq_sync_children (Widget w);       /* composite.children, core.popup_list */

class MqBridge : public QObject {
public:
  explicit MqBridge (Widget w) : widget (w) {}
  Widget widget;
  bool eventFilter (QObject *o, QEvent *e) override;
};

/* ---------------------------------------------------------------- core helpers */
extern XtAppContext mq_app;
extern Display *mq_display;
void mq_ensure_app (int *argc = nullptr, char **argv = nullptr);

QWidget *mq_qw (Widget w);                     /* the QWidget (nullptr for menu actions) */
QAction *mq_action (Widget w);                 /* the QAction for menu entries */
QWidget *mq_content (Widget w);                /* where children go */
template <class T> T *mq_q (Widget w) { return w ? qobject_cast<T *> (R (w)->obj.data ()) : nullptr; }
template <class T> T *mq_inner (Widget w) { return w ? qobject_cast<T *> (R (w)->inner.data ()) : nullptr; }
Widget mq_from_qobject (QObject *o);
bool mq_is (Widget w, WidgetClass c);          /* XtIsSubclass */
bool mq_kind (Widget w, unsigned kind);
Widget mq_shell_of (Widget w);
bool mq_in_menu (Widget w);                    /* parent is a pulldown/popup/menubar row column */
unsigned char mq_menu_type (Widget w);         /* XmNrowColumnType of a RowColumn */

/* resources */
const ResDef *mq_find_res (Widget w, const char *name, bool *constraint = nullptr);
XtArgVal mq_get (Widget w, const char *name);  /* stored value or default (raw, not copied) */
int mq_geti (Widget w, const char *name);
bool mq_getb (Widget w, const char *name);
QString mq_gets (Widget w, const char *name);  /* XmString or String resource as text */
bool mq_isset (Widget w, const char *name);
void mq_set (Widget w, const char *name, XtArgVal v);          /* store + apply, like XtSetValues */
void mq_store (Widget w, const ResDef *def, XtArgVal v);
bool mq_convert (Widget w, const ResDef *def, const char *str, XtArgVal *out);
const char *mq_lookup_db (Widget w, const char *name, const char *rclass);
void mq_db_add_line (const char *line);
int mq_enum_value (const char *name, bool *ok);

/* callbacks */
void mq_call (Widget w, const char *callback, XtPointer call_data);
bool mq_has_callbacks (Widget w, const char *callback);

/* geometry / visibility */
void mq_place_default (Widget parent, Widget child);
void mq_unplace_default (Widget parent, Widget child);
void mq_sync_visible (Widget w);
void mq_apply_size (Widget w);
void mq_popup_shell (Widget shell, bool up);
void mq_relayout (Widget manager);

/* XmString */
QString mq_xmstring_text (XmString s);
XmString mq_xmstring_from (const QString &text);
QString mq_mnemonic_text (const QString &text, KeySym mnemonic);   /* "File" + 'F' -> "&File" */
QFont mq_fontlist_font (XmFontList fl, bool *ok);
XmFontList mq_fontlist_from_string (const char *spec);

/* events */
bool mq_qt_to_xevent (Widget w, QEvent *e, XEvent *xe);
void mq_dispatch_handlers (Widget w, XEvent *xe, bool *consumed);
void mq_run_translations (Widget w, XEvent *xe);
XEvent *mq_current_event ();                   /* event that triggered the running callback */
struct MqEventScope { XEvent *prev; MqEventScope (XEvent *e); ~MqEventScope (); };

/* Xlib backing store for drawing areas */
Window mq_window_for (Widget w);
Widget mq_widget_for_window (Window win);
class QImage *mq_window_image (Window win, bool create = true);
void mq_window_resize (Window win, int w, int h);
void mq_window_flush (Window win);
unsigned long mq_pixel (const QColor &c);
QColor mq_color (unsigned long pixel);
QPixmap mq_pixmap_qt (Pixmap p);

/* menus */
void mq_rowcolumn_rebuild_menu (Widget menu);
void mq_option_menu_sync (Widget option);

/* class records the implementation files share */
extern MqClass object_rec, rectobj_rec, core_rec, composite_rec, constraint_rec;
extern MqClass mq_shell_rec, mq_toplevel_shell_rec, mq_application_shell_rec, mq_dialog_shell_rec,
  mq_menu_shell_rec, mq_transient_shell_rec;

/* Forwarding to the system libX11: Qt's xcb platform and libGL call Xlib with
 * real displays, and those calls can bind to this library. Each Xlib entry
 * point starts with MQ_FWD, which hands calls for displays that are not ours
 * to the next definition of the function (dlsym RTLD_NEXT). */
void *mq_real_symbol (const char *name);
extern Screen *mq_screen_ptr;
#define MQ_FWD(fn, dpy, ...) \
  do { if ((dpy) && (Display *) (dpy) != mq_display) { \
    static auto real_ = (decltype (&fn)) mq_real_symbol (#fn); \
    if (real_) return real_ (__VA_ARGS__); } } while (0)
#define MQ_FWD_SCREEN(fn, scr, ...) \
  do { if ((scr) && (scr) != mq_screen_ptr) { \
    static auto real_ = (decltype (&fn)) mq_real_symbol (#fn); \
    if (real_) return real_ (__VA_ARGS__); } } while (0)

/* Xlib side (xlib.cpp) */
QFont mq_font_from_xlfd (const QString &name, bool *ok);
QFont mq_font_for (Font fid);
Pixmap mq_pixmap_from_image (const QImage &img);

/* widget-layer helpers (xm_*.cpp) */
QFont mq_widget_font (Widget w, const char *which = nullptr);   /* which: XmNbuttonFontList etc. */
void mq_apply_common (Widget w, const QByteArray &res);         /* colours, font, tool tip, traversal */
QString mq_label_text (Widget w);                               /* labelString, default: widget name */
QKeySequence mq_accelerator_keys (const char *accel);           /* "Ctrl<Key>q" -> Ctrl+Q */
XmString mq_label_xmstring_default (Widget w);
bool mq_class_is (Widget w, const char *class_name);
QObject *mq_canvas_create (Widget w);
void mq_toggle_set (Widget w, bool on, bool notify);

#define MQ_WARN_ONCE(...) do { static bool once = false; if (!once) { once = true; qWarning (__VA_ARGS__); } } while (0)

/* Per-widget C++ extension data */
template <class T> T *mq_ext (Widget w)
{
  MqRec *r = R (w);
  if (!r->ext) { r->ext = new T; r->ext_free = [] (void *p) { delete static_cast<T *> (p); }; }
  return static_cast<T *> (r->ext);
}

#endif
