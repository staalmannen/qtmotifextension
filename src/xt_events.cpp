/*
 * Motif on Qt - the event side of Xt: main loop, timeouts, work procs,
 * input sources, event handlers, translation tables and actions, and the
 * conversion of Qt events to XEvents.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"

#include <QSocketNotifier>
#include <QTimer>
#include <cstring>

/* ======================================================================
 * Current event (callbacks report the event that caused them)
 * ====================================================================== */
static XEvent *s_current = nullptr;
static XEvent s_last_event;
XEvent *mq_current_event () { return s_current ? s_current : (s_last_event.type ? &s_last_event : nullptr); }
MqEventScope::MqEventScope (XEvent *e) : prev (s_current) { s_current = e; if (e) s_last_event = *e; }
MqEventScope::~MqEventScope () { s_current = prev; }

/* Events recorded while a program runs its own XtAppNextEvent() loop */
static bool s_recording = false;
static QList<XEvent> s_queue;

/* ======================================================================
 * Key symbols
 * ====================================================================== */
static const QHash<int, KeySym> &special_keys ()
{
  static QHash<int, KeySym> h = {
    { Qt::Key_Backspace, XK_BackSpace }, { Qt::Key_Tab, XK_Tab }, { Qt::Key_Backtab, XK_ISO_Left_Tab },
    { Qt::Key_Return, XK_Return }, { Qt::Key_Enter, XK_KP_Enter }, { Qt::Key_Escape, XK_Escape },
    { Qt::Key_Delete, XK_Delete }, { Qt::Key_Insert, XK_Insert }, { Qt::Key_Home, XK_Home }, { Qt::Key_End, XK_End },
    { Qt::Key_Left, XK_Left }, { Qt::Key_Right, XK_Right }, { Qt::Key_Up, XK_Up }, { Qt::Key_Down, XK_Down },
    { Qt::Key_PageUp, XK_Page_Up }, { Qt::Key_PageDown, XK_Page_Down }, { Qt::Key_Shift, XK_Shift_L },
    { Qt::Key_Control, XK_Control_L }, { Qt::Key_Alt, XK_Alt_L }, { Qt::Key_Meta, XK_Meta_L },
    { Qt::Key_CapsLock, XK_Caps_Lock }, { Qt::Key_Space, XK_space }, { Qt::Key_Help, XK_Help },
    { Qt::Key_Menu, XK_Menu }, { Qt::Key_Print, XK_Print }, { Qt::Key_Pause, XK_Pause },
  };
  return h;
}

static KeySym qt_keysym (QKeyEvent *ke)
{
  auto it = special_keys ().constFind (ke->key ());
  if (it != special_keys ().constEnd ()) return it.value ();
  if (ke->key () >= Qt::Key_F1 && ke->key () <= Qt::Key_F35) return XK_F1 + (ke->key () - Qt::Key_F1);
  QString t = ke->text ();
  if (!t.isEmpty () && t[0].unicode () >= 0x20 && !(ke->modifiers () & Qt::ControlModifier)) {
    uint c = t[0].unicode ();
    return c < 0x100 ? c : 0x01000000 | c;
  }
  int k = ke->key ();
  if (k >= Qt::Key_A && k <= Qt::Key_Z) return (ke->modifiers () & Qt::ShiftModifier) ? k : k + 0x20;
  return k < 0x100 ? (KeySym) k : NoSymbol;
}

static unsigned int qt_state (Qt::KeyboardModifiers m, Qt::MouseButtons b)
{
  unsigned s = 0;
  if (m & Qt::ShiftModifier) s |= ShiftMask;
  if (m & Qt::ControlModifier) s |= ControlMask;
  if (m & Qt::AltModifier) s |= Mod1Mask;
  if (m & Qt::MetaModifier) s |= Mod4Mask;
  if (b & Qt::LeftButton) s |= Button1Mask;
  if (b & Qt::MiddleButton) s |= Button2Mask;
  if (b & Qt::RightButton) s |= Button3Mask;
  return s;
}

/* ======================================================================
 * Qt event -> XEvent
 * ====================================================================== */
bool mq_qt_to_xevent (Widget w, QEvent *e, XEvent *xe)
{
  memset (xe, 0, sizeof (*xe));
  QWidget *qw = mq_qw (w);
  Window win = mq_window_for (w);
  xe->xany.display = mq_display;
  xe->xany.window = win;
  xe->xany.send_event = False;
  switch (e->type ()) {
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonDblClick:
  case QEvent::MouseButtonRelease: {
    auto *me = static_cast<QMouseEvent *> (e);
    QPointF p = me->position (), g = me->globalPosition ();
    xe->xbutton.type = e->type () == QEvent::MouseButtonRelease ? ButtonRelease : ButtonPress;
    xe->xbutton.x = (int) p.x ();
    xe->xbutton.y = (int) p.y ();
    xe->xbutton.x_root = (int) g.x ();
    xe->xbutton.y_root = (int) g.y ();
    xe->xbutton.time = me->timestamp ();
    xe->xbutton.button = me->button () == Qt::RightButton ? Button3 : me->button () == Qt::MiddleButton ? Button2 : Button1;
    /* X reports the state *before* the event */
    Qt::MouseButtons before = me->buttons ();
    if (xe->type == ButtonPress) before &= ~me->button (); else before |= me->button ();
    xe->xbutton.state = qt_state (me->modifiers (), before);
    xe->xbutton.root = DefaultRootWindow (mq_display);
    xe->xbutton.same_screen = True;
    return true;
  }
  case QEvent::MouseMove: {
    auto *me = static_cast<QMouseEvent *> (e);
    QPointF p = me->position (), g = me->globalPosition ();
    xe->xmotion.type = MotionNotify;
    xe->xmotion.x = (int) p.x ();
    xe->xmotion.y = (int) p.y ();
    xe->xmotion.x_root = (int) g.x ();
    xe->xmotion.y_root = (int) g.y ();
    xe->xmotion.time = me->timestamp ();
    xe->xmotion.state = qt_state (me->modifiers (), me->buttons ());
    xe->xmotion.root = DefaultRootWindow (mq_display);
    xe->xmotion.same_screen = True;
    return true;
  }
  case MQ_QKeyPress:
  case MQ_QKeyRelease: {
    auto *ke = static_cast<QKeyEvent *> (e);
    xe->xkey.type = e->type () == MQ_QKeyPress ? KeyPress : KeyRelease;
    xe->xkey.keycode = (unsigned int) qt_keysym (ke);   /* this "server" uses keysyms as keycodes */
    xe->xkey.state = qt_state (ke->modifiers (), Qt::NoButton);
    xe->xkey.time = ke->timestamp ();
    xe->xkey.root = DefaultRootWindow (mq_display);
    xe->xkey.same_screen = True;
    if (qw) {
      QPoint p = qw->mapFromGlobal (QCursor::pos ());
      xe->xkey.x = p.x ();
      xe->xkey.y = p.y ();
    }
    return true;
  }
  case QEvent::Enter:
  case QEvent::Leave:
    xe->xcrossing.type = e->type () == QEvent::Enter ? EnterNotify : LeaveNotify;
    if (qw) {
      QPoint p = qw->mapFromGlobal (QCursor::pos ());
      xe->xcrossing.x = p.x ();
      xe->xcrossing.y = p.y ();
    }
    xe->xcrossing.mode = NotifyNormal;
    return true;
  case MQ_QFocusIn:
  case MQ_QFocusOut:
    xe->xfocus.type = e->type () == MQ_QFocusIn ? FocusIn : FocusOut;
    xe->xfocus.mode = NotifyNormal;
    return true;
  case QEvent::Resize:
    xe->xconfigure.type = ConfigureNotify;
    xe->xconfigure.event = win;
    if (qw) {
      xe->xconfigure.x = qw->x ();
      xe->xconfigure.y = qw->y ();
      xe->xconfigure.width = qw->width ();
      xe->xconfigure.height = qw->height ();
    }
    return true;
  case QEvent::Show:
    xe->xmap.type = MapNotify;
    xe->xmap.event = win;
    return true;
  case QEvent::Hide:
    xe->xunmap.type = UnmapNotify;
    xe->xunmap.event = win;
    return true;
  default:
    return false;
  }
}

static EventMask mask_for (XEvent *xe)
{
  switch (xe->type) {
  case ButtonPress: return ButtonPressMask;
  case ButtonRelease: return ButtonReleaseMask;
  case MotionNotify: {
    EventMask m = PointerMotionMask;
    unsigned s = xe->xmotion.state;
    if (s & (Button1Mask | Button2Mask | Button3Mask)) m |= ButtonMotionMask;
    if (s & Button1Mask) m |= Button1MotionMask;
    if (s & Button2Mask) m |= Button2MotionMask;
    if (s & Button3Mask) m |= Button3MotionMask;
    return m;
  }
  case KeyPress: return KeyPressMask;
  case KeyRelease: return KeyReleaseMask;
  case EnterNotify: return EnterWindowMask;
  case LeaveNotify: return LeaveWindowMask;
  case FocusIn: case FocusOut: return FocusChangeMask;
  case ConfigureNotify: case MapNotify: case UnmapNotify: return StructureNotifyMask;
  case Expose: return ExposureMask;
  default: return NoEventMask;
  }
}

void mq_dispatch_handlers (Widget w, XEvent *xe, bool *consumed)
{
  if (!w || R (w)->handlers.isEmpty ()) return;
  EventMask m = mask_for (xe);
  QList<EventHandlerRec> list = R (w)->handlers;
  for (const EventHandlerRec &h : list) {
    if (!(h.mask & m) && !(h.nonmaskable && m == NoEventMask)) continue;
    Boolean cont = True;
    h.proc (w, h.closure, xe, &cont);
    if (R (w)->destroying) { *consumed = true; return; }
    if (!cont) { *consumed = true; return; }
  }
}

/* ======================================================================
 * Bridge: Qt events into Xt
 * ====================================================================== */
bool MqBridge::eventFilter (QObject *o, QEvent *e)
{
  Widget w = widget;
  if (!w || R (w)->destroying || o != R (w)->obj.data ()) return false;
  /* keep core.x/y/width/height current (programs read them) */
  if (e->type () == QEvent::Resize || e->type () == QEvent::Move || e->type () == QEvent::Show) mq_sync_core (w);
  XEvent xe;
  if (!mq_qt_to_xevent (w, e, &xe)) return false;
  MqEventScope scope (&xe);
  if (s_recording) s_queue.append (xe);
  bool consumed = false;
  mq_dispatch_handlers (w, &xe, &consumed);
  if (!consumed && R (w)->translations && !R (w)->destroying) mq_run_translations (w, &xe);
  if (e->type () == QEvent::MouseButtonDblClick && !consumed && !R (w)->destroying) {
    /* X sends press/release/press/release; Qt merges the second press */
    if (R (w)->translations) mq_run_translations (w, &xe);
  }
  return consumed;
}

void mq_events_install (Widget w)
{
  if (!R (w)->obj || R (w)->bridge) return;
  R (w)->bridge = new MqBridge (w);
  R (w)->bridge->setParent (R (w)->obj);
  R (w)->obj->installEventFilter (R (w)->bridge);
}

/* ======================================================================
 * Translations and actions
 * ====================================================================== */
struct ActionCall { QByteArray name; QList<QByteArray> params; };
struct TransRule { int type; unsigned detail; unsigned mods; bool any_detail; QList<ActionCall> actions; };
struct _TranslationData { int mode = 0; /* 0 replace, 1 override, 2 augment */ QList<TransRule> rules; };

static QHash<QByteArray, XtActionProc> &action_table ()
{
  static QHash<QByteArray, XtActionProc> h;
  return h;
}

static QList<QPair<XtActionHookProc, XtPointer>> s_action_hooks;

static bool parse_event (const QByteArray &spec, TransRule &r)
{
  /* "Ctrl Shift<Btn1Down>" "<Key>Return" "<KeyPress>q" "<Motion>" */
  int lt = spec.indexOf ('<'), gt = spec.indexOf ('>');
  if (lt < 0 || gt < lt) return false;
  QByteArray mods = spec.left (lt).trimmed (), ev = spec.mid (lt + 1, gt - lt - 1).trimmed (), detail = spec.mid (gt + 1).trimmed ();
  r.mods = 0;
  for (const QByteArray &m : mods.split (' ')) {
    if (m == "Ctrl" || m == "c") r.mods |= ControlMask;
    else if (m == "Shift" || m == "s") r.mods |= ShiftMask;
    else if (m == "Meta" || m == "Alt" || m == "m" || m == "a" || m == "Mod1") r.mods |= Mod1Mask;
  }
  r.any_detail = true;
  r.detail = 0;
  auto btn = [&] (const char *prefix, int type) -> bool {
    if (!ev.startsWith (prefix)) return false;
    r.type = type;
    QByteArray rest = ev.mid (strlen (prefix));
    return rest.isEmpty ();
  };
  if (ev == "BtnDown" || ev == "ButtonPress" || ev == "BtnPress") r.type = ButtonPress;
  else if (ev == "BtnUp" || ev == "ButtonRelease" || ev == "BtnRelease") r.type = ButtonRelease;
  else if (ev.startsWith ("Btn") && ev.endsWith ("Down")) { r.type = ButtonPress; r.detail = ev.mid (3, 1).toUInt (); r.any_detail = false; }
  else if (ev.startsWith ("Btn") && ev.endsWith ("Up")) { r.type = ButtonRelease; r.detail = ev.mid (3, 1).toUInt (); r.any_detail = false; }
  else if (ev.startsWith ("Btn") && ev.endsWith ("Motion")) { r.type = MotionNotify; r.detail = ev.mid (3, 1).toUInt (); r.any_detail = ev == "BtnMotion"; }
  else if (ev == "Motion" || ev == "MotionNotify" || ev == "PtrMoved" || ev == "MouseMoved") r.type = MotionNotify;
  else if (ev == "Key" || ev == "KeyPress" || ev == "KeyDown") {
    r.type = KeyPress;
    if (!detail.isEmpty ()) { r.detail = XStringToKeysym (detail.constData ()); if (!r.detail) r.detail = (unsigned char) detail[0]; r.any_detail = false; }
  } else if (ev == "KeyUp" || ev == "KeyRelease") {
    r.type = KeyRelease;
    if (!detail.isEmpty ()) { r.detail = XStringToKeysym (detail.constData ()); r.any_detail = false; }
  } else if (ev == "EnterWindow" || ev == "Enter" || ev == "EnterNotify") r.type = EnterNotify;
  else if (ev == "LeaveWindow" || ev == "Leave" || ev == "LeaveNotify") r.type = LeaveNotify;
  else if (ev == "FocusIn") r.type = FocusIn;
  else if (ev == "FocusOut") r.type = FocusOut;
  else if (ev == "Expose") r.type = Expose;
  else if (ev == "Configure" || ev == "ConfigureNotify") r.type = ConfigureNotify;
  else if (ev == "Map" || ev == "MapNotify") r.type = MapNotify;
  else if (ev == "Unmap" || ev == "UnmapNotify") r.type = UnmapNotify;
  else { (void) btn; return false; }
  if (!r.any_detail && r.type == ButtonPress && detail.size ()) { /* "<BtnDown>(2)" click counts are ignored */ }
  return true;
}

static QList<ActionCall> parse_actions (const QByteArray &s)
{
  QList<ActionCall> out;
  int i = 0;
  while (i < s.size ()) {
    while (i < s.size () && (s[i] == ' ' || s[i] == '\t')) i++;
    int open = s.indexOf ('(', i);
    if (open < 0) break;
    ActionCall a;
    a.name = s.mid (i, open - i).trimmed ();
    int close = s.indexOf (')', open);
    if (close < 0) close = s.size ();
    for (QByteArray p : s.mid (open + 1, close - open - 1).split (',')) {
      p = p.trimmed ();
      if (p.startsWith ('"') && p.endsWith ('"') && p.size () >= 2) p = p.mid (1, p.size () - 2);
      if (!p.isEmpty ()) a.params << p;
    }
    if (!a.name.isEmpty ()) out << a;
    i = close + 1;
  }
  return out;
}

static bool rule_matches (const TransRule &r, XEvent *xe)
{
  if (r.type != xe->type) return false;
  switch (xe->type) {
  case ButtonPress:
  case ButtonRelease:
    if (!r.any_detail && r.detail != xe->xbutton.button) return false;
    return (xe->xbutton.state & r.mods) == r.mods;
  case MotionNotify:
    if (!r.any_detail && !(xe->xmotion.state & (Button1Mask << (r.detail - 1)))) return false;
    if (r.any_detail && r.detail == 0 && false) return false;
    return (xe->xmotion.state & r.mods) == r.mods;
  case KeyPress:
  case KeyRelease: {
    if (!r.any_detail) {
      KeySym k = xe->xkey.keycode;
      if (k != r.detail && (k < 0x100 && r.detail < 0x100 ? tolower ((int) k) != tolower ((int) r.detail) : true)) return false;
    }
    return (xe->xkey.state & r.mods) == r.mods;
  }
  default:
    return true;
  }
}

void mq_builtin_action (Widget w, const QByteArray &name, XEvent *xe, String *params, Cardinal *n);

static void call_action (Widget w, const ActionCall &a, XEvent *xe)
{
  QVector<String> params;
  for (const QByteArray &p : a.params) params << const_cast<char *> (p.constData ());
  Cardinal n = params.size ();
  for (auto &h : s_action_hooks) h.first (w, h.second, const_cast<char *> (a.name.constData ()), xe, params.data (), &n);
  XtActionProc proc = action_table ().value (a.name);
  if (proc) proc (w, xe, params.data (), &n);
  else mq_builtin_action (w, a.name, xe, params.data (), &n);
}

void mq_run_translations (Widget w, XEvent *xe)
{
  if (!R (w)->translations) return;
  QList<TransRule> rules = R (w)->translations->rules;
  for (const TransRule &r : rules)
    if (rule_matches (r, xe)) {
      for (const ActionCall &a : r.actions) {
        call_action (w, a, xe);
        if (R (w)->destroying) return;
      }
      return;   /* the first matching production wins */
    }
}

static void enable_input (Widget w, EventMask m)
{
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  if (m & (PointerMotionMask | ButtonMotionMask | Button1MotionMask | Button2MotionMask | Button3MotionMask))
    qw->setMouseTracking (m & PointerMotionMask);
  if ((m & (KeyPressMask | KeyReleaseMask)) && qw->focusPolicy () == Qt::NoFocus) qw->setFocusPolicy (Qt::StrongFocus);
  if (m & (EnterWindowMask | LeaveWindowMask)) qw->setAttribute (Qt::WA_Hover);
}

static EventMask translations_mask (XtTranslations t)
{
  EventMask m = 0;
  for (const TransRule &r : t->rules) {
    XEvent fake;
    memset (&fake, 0, sizeof fake);
    fake.type = r.type;
    if (r.type == MotionNotify) m |= r.any_detail && r.detail == 0 ? PointerMotionMask : ButtonMotionMask;
    else m |= mask_for (&fake);
  }
  return m;
}

/* ======================================================================
 * Timers, work procs, inputs
 * ====================================================================== */
struct TimerRec { QTimer *timer; XtTimerCallbackProc proc; XtPointer closure; };
static QHash<XtIntervalId, TimerRec> s_timers;
static XtIntervalId s_next_timer = 1;

struct WorkRec { XtWorkProc proc; XtPointer closure; };
static QMap<XtWorkProcId, WorkRec> s_work;
static XtWorkProcId s_next_work = 1;
static QTimer *s_work_timer = nullptr;

struct InputRec { QList<QSocketNotifier *> notifiers; XtInputCallbackProc proc; XtPointer closure; int source; };
static QHash<XtInputId, InputRec> s_inputs;
static XtInputId s_next_input = 1;

static void run_work ()
{
  if (s_work.isEmpty ()) { if (s_work_timer) s_work_timer->stop (); return; }
  XtWorkProcId id = s_work.firstKey ();
  WorkRec r = s_work.first ();
  if (r.proc (r.closure)) s_work.remove (id);
  if (s_work.isEmpty () && s_work_timer) s_work_timer->stop ();
}

extern "C" {

void XtAppMainLoop (XtAppContext app)
{
  mq_ensure_app ();
  while (!XtAppGetExitFlag (app)) {
    qApp->exec ();
    if (!app) break;
  }
}

void XtMainLoop (void) { XtAppMainLoop (mq_app); }

void XtAppNextEvent (XtAppContext, XEvent *event)
{
  mq_ensure_app ();
  s_recording = true;
  while (s_queue.isEmpty ()) QCoreApplication::processEvents (QEventLoop::WaitForMoreEvents);
  s_recording = false;
  *event = s_queue.takeFirst ();
}

void XtNextEvent (XEvent *event) { XtAppNextEvent (mq_app, event); }

Boolean XtAppPeekEvent (XtAppContext app, XEvent *event)
{
  XtAppNextEvent (app, event);
  s_queue.prepend (*event);
  return True;
}

Boolean XtDispatchEvent (XEvent *) { return True; }   /* already delivered through Qt */

XtInputMask XtAppPending (XtAppContext)
{
  QCoreApplication::processEvents (QEventLoop::AllEvents, 1);
  return s_queue.isEmpty () ? 0 : XtIMXEvent;
}

Boolean XtPending (void) { return XtAppPending (mq_app) != 0; }

void XtAppProcessEvent (XtAppContext, XtInputMask)
{
  QCoreApplication::processEvents (QEventLoop::WaitForMoreEvents);
  s_queue.clear ();
}

void XtProcessEvent (XtInputMask mask) { XtAppProcessEvent (mq_app, mask); }

Boolean XtAppPeekEventQueue (XtAppContext) { return !s_queue.isEmpty (); }

XtIntervalId XtAppAddTimeOut (XtAppContext, unsigned long interval, XtTimerCallbackProc proc, XtPointer closure)
{
  mq_ensure_app ();
  XtIntervalId id = s_next_timer++;
  auto *t = new QTimer;
  t->setSingleShot (true);
  QObject::connect (t, &QTimer::timeout, t, [id] {
    auto it = s_timers.find (id);
    if (it == s_timers.end ()) return;
    TimerRec r = it.value ();
    s_timers.erase (it);
    r.timer->deleteLater ();
    XtIntervalId copy = id;
    r.proc (r.closure, &copy);
  });
  s_timers.insert (id, { t, proc, closure });
  t->start ((int) interval);
  return id;
}

XtIntervalId XtAddTimeOut (unsigned long interval, XtTimerCallbackProc proc, XtPointer closure)
{
  return XtAppAddTimeOut (mq_app, interval, proc, closure);
}

void XtRemoveTimeOut (XtIntervalId id)
{
  auto it = s_timers.find (id);
  if (it == s_timers.end ()) return;
  it.value ().timer->stop ();
  it.value ().timer->deleteLater ();
  s_timers.erase (it);
}

XtWorkProcId XtAppAddWorkProc (XtAppContext, XtWorkProc proc, XtPointer closure)
{
  mq_ensure_app ();
  XtWorkProcId id = s_next_work++;
  s_work.insert (id, { proc, closure });
  if (!s_work_timer) {
    s_work_timer = new QTimer;
    s_work_timer->setInterval (0);
    QObject::connect (s_work_timer, &QTimer::timeout, s_work_timer, run_work);
  }
  s_work_timer->start ();
  return id;
}

XtWorkProcId XtAddWorkProc (XtWorkProc proc, XtPointer closure) { return XtAppAddWorkProc (mq_app, proc, closure); }
void XtRemoveWorkProc (XtWorkProcId id) { s_work.remove (id); }

XtInputId XtAppAddInput (XtAppContext, int source, XtPointer condition, XtInputCallbackProc proc, XtPointer closure)
{
  mq_ensure_app ();
  XtInputId id = s_next_input++;
  InputRec r { {}, proc, closure, source };
  long cond = (long) condition;
  auto add = [&] (QSocketNotifier::Type type) {
    auto *n = new QSocketNotifier (source, type);
    QObject::connect (n, &QSocketNotifier::activated, n, [id] {
      auto it = s_inputs.find (id);
      if (it == s_inputs.end ()) return;
      InputRec rec = it.value ();
      int src = rec.source;
      XtInputId copy = id;
      rec.proc (rec.closure, &src, &copy);
    });
    r.notifiers << n;
  };
  if (cond & XtInputReadMask) add (QSocketNotifier::Read);
  if (cond & XtInputWriteMask) add (QSocketNotifier::Write);
  if (cond & XtInputExceptMask) add (QSocketNotifier::Exception);
  s_inputs.insert (id, r);
  return id;
}

XtInputId XtAddInput (int source, XtPointer condition, XtInputCallbackProc proc, XtPointer closure)
{
  return XtAppAddInput (mq_app, source, condition, proc, closure);
}

void XtRemoveInput (XtInputId id)
{
  auto it = s_inputs.find (id);
  if (it == s_inputs.end ()) return;
  for (QSocketNotifier *n : it.value ().notifiers) { n->setEnabled (false); n->deleteLater (); }
  s_inputs.erase (it);
}

XtSignalId XtAppAddSignal (XtAppContext, XtSignalCallbackProc, XtPointer) { return 0; }
void XtNoticeSignal (XtSignalId) {}
void XtRemoveSignal (XtSignalId) {}

/* ---------- event handlers ---------- */
void XtAddEventHandler (Widget w, EventMask mask, _XtBoolean nonmaskable, XtEventHandler proc, XtPointer closure)
{
  if (!w) return;
  for (EventHandlerRec &h : R (w)->handlers)
    if (h.proc == proc && h.closure == closure) { h.mask |= mask; enable_input (w, h.mask); return; }
  R (w)->handlers.append ({ mask, (Boolean) nonmaskable, proc, closure });
  enable_input (w, mask);
}

void XtInsertEventHandler (Widget w, EventMask mask, _XtBoolean nonmaskable, XtEventHandler proc, XtPointer closure, XtListPosition pos)
{
  if (!w) return;
  if (pos == XtListHead) { R (w)->handlers.prepend ({ mask, (Boolean) nonmaskable, proc, closure }); enable_input (w, mask); }
  else XtAddEventHandler (w, mask, nonmaskable, proc, closure);
}

void XtRemoveEventHandler (Widget w, EventMask mask, _XtBoolean, XtEventHandler proc, XtPointer closure)
{
  if (!w) return;
  for (int i = 0; i < R (w)->handlers.size (); i++)
    if (R (w)->handlers[i].proc == proc && R (w)->handlers[i].closure == closure) {
      R (w)->handlers[i].mask &= ~mask;
      if (!R (w)->handlers[i].mask) R (w)->handlers.removeAt (i);
      return;
    }
}

void XtAddRawEventHandler (Widget w, EventMask mask, _XtBoolean nonmaskable, XtEventHandler proc, XtPointer closure)
{
  XtAddEventHandler (w, mask, nonmaskable, proc, closure);
}

void XtRemoveRawEventHandler (Widget w, EventMask mask, _XtBoolean nonmaskable, XtEventHandler proc, XtPointer closure)
{
  XtRemoveEventHandler (w, mask, nonmaskable, proc, closure);
}

void XtInsertRawEventHandler (Widget w, EventMask mask, _XtBoolean nonmaskable, XtEventHandler proc, XtPointer closure, XtListPosition pos)
{
  XtInsertEventHandler (w, mask, nonmaskable, proc, closure, pos);
}

EventMask XtBuildEventMask (Widget w)
{
  EventMask m = 0;
  if (w) for (const EventHandlerRec &h : R (w)->handlers) m |= h.mask;
  return m;
}

void XtAddGrab (Widget, _XtBoolean, _XtBoolean) {}
void XtRemoveGrab (Widget) {}
void XtSetKeyboardFocus (Widget, Widget descendant) { if (QWidget *qw = mq_qw (descendant)) qw->setFocus (); }
Time XtLastTimestampProcessed (Display *) { XEvent *e = mq_current_event (); return e && e->type >= KeyPress && e->type <= MotionNotify ? e->xbutton.time : CurrentTime; }
XEvent *XtLastEventProcessed (Display *) { return mq_current_event (); }

/* ---------- translations ---------- */
XtTranslations XtParseTranslationTable (_Xconst char *source)
{
  auto *t = new _TranslationData;
  QByteArray s (source ? source : "");
  s.replace ("\\n", "\n");
  for (QByteArray line : s.split ('\n')) {
    line = line.trimmed ();
    if (line.startsWith ("#override")) { t->mode = 1; line = line.mid (9).trimmed (); }
    else if (line.startsWith ("#augment")) { t->mode = 2; line = line.mid (8).trimmed (); }
    else if (line.startsWith ("#replace")) { t->mode = 0; line = line.mid (8).trimmed (); }
    if (line.isEmpty ()) continue;
    int colon = -1, depth = 0;
    for (int i = 0; i < line.size (); i++) {
      if (line[i] == '<') depth++;
      else if (line[i] == '>') depth--;
      else if (line[i] == ':' && depth == 0) { colon = i; break; }
    }
    if (colon < 0) continue;
    TransRule r;
    /* "<Btn1Down>,<Btn1Up>" sequences: use the first event */
    QByteArray ev = line.left (colon).trimmed ();
    int comma = ev.indexOf (',');
    if (comma > 0) ev = ev.left (comma);
    if (!parse_event (ev, r)) continue;
    r.actions = parse_actions (line.mid (colon + 1));
    t->rules.append (r);
  }
  return t;
}

XtAccelerators XtParseAcceleratorTable (_Xconst char *source) { return (XtAccelerators) XtParseTranslationTable (source); }

static void install (Widget w, XtTranslations t, bool override)
{
  if (!w || !t) return;
  if (!R (w)->translations) R (w)->translations = new _TranslationData;
  if (t->mode == 0 && !override) R (w)->translations->rules = t->rules;
  else if (override || t->mode == 1) R (w)->translations->rules = t->rules + R (w)->translations->rules;
  else R (w)->translations->rules += t->rules;
  enable_input (w, translations_mask (t));
}

void XtOverrideTranslations (Widget w, XtTranslations t) { install (w, t, true); }
void XtAugmentTranslations (Widget w, XtTranslations t) { install (w, t, false); }
void XtUninstallTranslations (Widget w) { if (w && R (w)->translations) R (w)->translations->rules.clear (); }
void XtInstallAccelerators (Widget, Widget) {}
void XtInstallAllAccelerators (Widget, Widget) {}

void XtAppAddActions (XtAppContext, XtActionList actions, Cardinal n)
{
  for (Cardinal i = 0; i < n; i++) action_table ().insert (actions[i].string, actions[i].proc);
}

void XtAddActions (XtActionList actions, Cardinal n) { XtAppAddActions (mq_app, actions, n); }

void XtCallActionProc (Widget w, _Xconst char *action, XEvent *event, String *params, Cardinal n)
{
  ActionCall a;
  a.name = action;
  for (Cardinal i = 0; i < n; i++) a.params << QByteArray (params[i]);
  call_action (w, a, event);
}

XtActionHookId XtAppAddActionHook (XtAppContext, XtActionHookProc proc, XtPointer closure)
{
  s_action_hooks.append ({ proc, closure });
  return (XtActionHookId) (quintptr) s_action_hooks.size ();
}

void XtRemoveActionHook (XtActionHookId) {}
void XtRegisterGrabAction (XtActionProc, _XtBoolean, unsigned int, int, int) {}

} /* extern "C" */

/* keys are not grabbed from other X clients: there are none */
void XtGrabKey (Widget, _XtKeyCode, Modifiers, _XtBoolean, int, int) {}
void XtUngrabKey (Widget, _XtKeyCode, Modifiers) {}

int XtGetMultiClickTime (Display *) { return QApplication::doubleClickInterval (); }

/* as Xt's default translator: Shift (or Lock for letters) picks the second keysym */
void XtTranslateKeycode (Display *dpy, _XtKeyCode keycode, Modifiers modifiers, Modifiers *modifiers_return, KeySym *keysym_return)
{
  KeySym k = XKeycodeToKeysym (dpy, keycode, 0);
  if ((modifiers & ShiftMask) || ((modifiers & LockMask) && k >= 'a' && k <= 'z')) k = XKeycodeToKeysym (dpy, keycode, 1);
  if (modifiers_return) *modifiers_return = ShiftMask | LockMask;
  if (keysym_return) *keysym_return = k;
}

/* the one display lives as long as the program */
void XtCloseDisplay (Display *) {}
