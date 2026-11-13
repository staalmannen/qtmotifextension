/*
 * Motif on Qt - selections: X selection ownership within the process, the
 * Xt selection functions on top of QClipboard (PRIMARY is Qt's selection
 * clipboard where the platform has one, CLIPBOARD the clipboard), and the
 * XTextProperty helpers.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <X11/Xatom.h>
#include <QMimeData>
#include <cstring>

namespace {
struct Owner {
  Window window = None;
  Widget widget = nullptr;
  XtLoseSelectionProc lose = nullptr;
  XtSelectionDoneProc done = nullptr;
};
}

static QHash<Atom, Owner> &owners ()
{
  static QHash<Atom, Owner> h;
  return h;
}

static QClipboard::Mode clip_mode (Atom selection, bool *ok)
{
  QClipboard *cb = QGuiApplication::clipboard ();
  *ok = true;
  if (selection == XA_PRIMARY) {
    if (cb->supportsSelection ()) return QClipboard::Selection;
    *ok = false;   /* no primary selection on this platform: keep it in-process */
    return QClipboard::Clipboard;
  }
  if (selection == XInternAtom (mq_display, "CLIPBOARD", False)) return QClipboard::Clipboard;
  *ok = false;
  return QClipboard::Clipboard;
}

static QByteArray text_from (Atom type, XtPointer value, unsigned long length, int format)
{
  if (!value || format != 8) return QByteArray ();
  Q_UNUSED (type);
  return QByteArray ((const char *) value, length);
}

/* the request being converted, for XtGetSelectionRequest: the requestor
 * is the owner's own window (Qt's clipboard asks on behalf of everyone) */
static XSelectionRequestEvent s_request;
static bool s_converting = false;

XSelectionRequestEvent *XtGetSelectionRequest (Widget, Atom, XtRequestId)
{
  return s_converting ? &s_request : nullptr;
}

/* the program's text for the selection, asked for through its convert proc */
static bool convert_text (Widget w, Atom selection, XtConvertSelectionProc convert, QString *out)
{
  Atom targets[] = { XInternAtom (mq_display, "UTF8_STRING", False), XA_STRING, XInternAtom (mq_display, "TEXT", False) };
  for (Atom target : targets) {
    Atom sel = selection, tgt = target, type = None;
    XtPointer value = nullptr;
    unsigned long length = 0;
    int format = 8;
    s_request = {};
    s_request.type = SelectionRequest;
    s_request.display = mq_display;
    s_request.owner = s_request.requestor = XtWindow (w);
    s_request.selection = selection;
    s_request.target = target;
    s_request.property = selection;
    s_request.time = CurrentTime;
    s_converting = true;
    bool ok = convert (w, &sel, &tgt, &type, &value, &length, &format);
    s_converting = false;
    if (!ok) continue;
    QByteArray b = text_from (type, value, length, format);
    XtFree ((char *) value);
    *out = target == XA_STRING ? QString::fromLatin1 (b) : QString::fromUtf8 (b);
    return true;
  }
  return false;
}

static void watch_clipboard ()
{
  static bool done = false;
  if (done) return;
  done = true;
  QObject::connect (QGuiApplication::clipboard (), &QClipboard::changed, qApp, [] (QClipboard::Mode mode) {
    /* someone else took the selection: tell the previous owner */
    const QMimeData *m = QGuiApplication::clipboard ()->mimeData (mode);
    if (m && m->hasFormat ("application/x-motif-on-qt-owner")) return;
    Atom sel = mode == QClipboard::Selection ? XA_PRIMARY : XInternAtom (mq_display, "CLIPBOARD", False);
    auto it = owners ().find (sel);
    if (it == owners ().end () || !it->widget) return;
    Owner o = it.value ();
    owners ().erase (it);
    if (o.lose && !R (o.widget)->destroying) o.lose (o.widget, &sel);
  });
}

extern "C" {

int XSetSelectionOwner (Display * a0_, Atom selection, Window owner, Time a3_)
{ MQ_FWD (XSetSelectionOwner, a0_, a0_, selection, owner, a3_);
  Owner &o = owners ()[selection];
  if (o.widget && o.window != owner && o.lose && !R (o.widget)->destroying) {
    Widget w = o.widget;
    XtLoseSelectionProc lose = o.lose;
    o = Owner ();
    lose (w, &selection);
  }
  if (owner == None) owners ().remove (selection);
  else owners ()[selection].window = owner;
  return 1;
}

Window XGetSelectionOwner (Display * a0_, Atom selection)
{ MQ_FWD (XGetSelectionOwner, a0_, a0_, selection);
  auto it = owners ().constFind (selection);
  return it == owners ().constEnd () ? None : it->window;
}

int XConvertSelection (Display * a0_, Atom a1_, Atom a2_, Atom a3_, Window a4_, Time a5_) { MQ_FWD (XConvertSelection, a0_, a0_, a1_, a2_, a3_, a4_, a5_); return 1; }

Boolean XtOwnSelection (Widget w, Atom selection, Time time, XtConvertSelectionProc convert, XtLoseSelectionProc lose, XtSelectionDoneProc done)
{
  if (!w || !convert) return False;
  XSetSelectionOwner (mq_display, selection, XtWindow (w), time);
  Owner &o = owners ()[selection];
  o.window = XtWindow (w);
  o.widget = w;
  o.lose = lose;
  o.done = done;
  QString text;
  bool ok;
  QClipboard::Mode mode = clip_mode (selection, &ok);
  if (ok && convert_text (w, selection, convert, &text)) {
    watch_clipboard ();
    auto *m = new QMimeData;
    m->setText (text);
    m->setData ("application/x-motif-on-qt-owner", QByteArray ());
    QGuiApplication::clipboard ()->setMimeData (m, mode);
  }
  return True;
}

Boolean XtOwnSelectionIncremental (Widget w, Atom selection, Time time, XtConvertSelectionIncrProc, XtLoseSelectionIncrProc,
                                   XtSelectionDoneIncrProc, XtCancelConvertSelectionProc, XtPointer)
{
  MQ_WARN_ONCE ("Motif on Qt: incremental selections are not supported");
  XSetSelectionOwner (mq_display, selection, XtWindow (w), time);
  return True;
}

void XtDisownSelection (Widget w, Atom selection, Time)
{
  auto it = owners ().find (selection);
  if (it == owners ().end () || it->widget != w) return;
  owners ().erase (it);
  bool ok;
  QClipboard::Mode mode = clip_mode (selection, &ok);
  const QMimeData *m = QGuiApplication::clipboard ()->mimeData (mode);
  if (ok && m && m->hasFormat ("application/x-motif-on-qt-owner")) QGuiApplication::clipboard ()->clear (mode);
}

static void deliver (Widget w, Atom selection, Atom target, XtSelectionCallbackProc cb, XtPointer closure)
{
  Atom sel = selection, type = None;
  unsigned long length = 0;
  int format = 8;
  XtPointer value = nullptr;
  Atom targets = XInternAtom (mq_display, "TARGETS", False);
  auto it = owners ().constFind (selection);
  bool ok;
  QClipboard::Mode mode = clip_mode (selection, &ok);
  QString text;
  bool have = false;
  if (it != owners ().constEnd () && it->widget && !ok) {
    /* in-process selection with no system equivalent: nothing to read here;
       the owner's data was offered through its convert proc already */
  } else if (ok) {
    const QMimeData *m = QGuiApplication::clipboard ()->mimeData (mode);
    if (m && m->hasText ()) { text = m->text (); have = true; }
  }
  if (have && target == targets) {
    auto *a = (Atom *) XtMalloc (3 * sizeof (Atom));
    a[0] = XA_STRING; a[1] = XInternAtom (mq_display, "UTF8_STRING", False); a[2] = XInternAtom (mq_display, "TEXT", False);
    value = a; length = 3; format = 32; type = XA_ATOM;
  } else if (have) {
    QByteArray b = target == XA_STRING ? text.toLatin1 () : text.toUtf8 ();
    value = XtMalloc (b.size () + 1);
    memcpy (value, b.constData (), b.size () + 1);
    length = b.size ();
    type = target == XA_STRING ? XA_STRING : XInternAtom (mq_display, "UTF8_STRING", False);
  }
  cb (w, closure, &sel, &type, value, &length, &format);
}

void XtGetSelectionValue (Widget w, Atom selection, Atom target, XtSelectionCallbackProc cb, XtPointer closure, Time)
{
  if (!cb) return;
  /* Xt delivers asynchronously */
  QTimer::singleShot (0, qApp, [=] () { if (!R (w)->destroying) deliver (w, selection, target, cb, closure); });
}

void XtGetSelectionValues (Widget w, Atom selection, Atom *targets, int count, XtSelectionCallbackProc cb, XtPointer *closures, Time t)
{
  for (int i = 0; i < count; i++) XtGetSelectionValue (w, selection, targets[i], cb, closures ? closures[i] : nullptr, t);
}

void XtGetSelectionValueIncremental (Widget w, Atom s, Atom t, XtSelectionCallbackProc cb, XtPointer c, Time tm) { XtGetSelectionValue (w, s, t, cb, c, tm); }
void XtGetSelectionValuesIncremental (Widget w, Atom s, Atom *t, int n, XtSelectionCallbackProc cb, XtPointer *c, Time tm) { XtGetSelectionValues (w, s, t, n, cb, c, tm); }
void XtAppSetSelectionTimeout (XtAppContext, unsigned long) {}
unsigned long XtAppGetSelectionTimeout (XtAppContext) { return 5000; }
void XtSetSelectionTimeout (unsigned long) {}
unsigned long XtGetSelectionTimeout (void) { return 5000; }

void XtGrabButton (Widget, int, Modifiers, _XtBoolean, unsigned int, int, int, Window, Cursor) {}
void XtUngrabButton (Widget, unsigned int, Modifiers) {}

/* ---------- text properties ---------- */
static Status list_to_property (char **list, int count, Atom encoding, XTextProperty *tp)
{
  QByteArray b;
  for (int i = 0; i < count; i++) { if (i) b.append ('\0'); if (list[i]) b.append (list[i]); }
  tp->value = (unsigned char *) malloc (b.size () + 1);
  memcpy (tp->value, b.constData (), b.size ());
  tp->value[b.size ()] = 0;
  tp->nitems = b.size ();
  tp->format = 8;
  tp->encoding = encoding;
  return 1;
}

Status XStringListToTextProperty (char **list, int count, XTextProperty *tp) { return list_to_property (list, count, XA_STRING, tp); }

int XmbTextListToTextProperty (Display * a0_, char **list, int count, XICCEncodingStyle a3_, XTextProperty *tp)
{ MQ_FWD (XmbTextListToTextProperty, a0_, a0_, list, count, a3_, tp);
  list_to_property (list, count, XInternAtom (mq_display, "UTF8_STRING", False), tp);
  return Success;
}

int Xutf8TextListToTextProperty (Display *d, char **list, int count, XICCEncodingStyle style, XTextProperty *tp)
{ MQ_FWD (Xutf8TextListToTextProperty, d, d, list, count, style, tp);
  return XmbTextListToTextProperty (d, list, count, style, tp);
}

Status XTextPropertyToStringList (XTextProperty *tp, char ***list, int *count)
{
  QList<QByteArray> parts = QByteArray ((const char *) tp->value, tp->nitems).split ('\0');
  *count = parts.size ();
  *list = (char **) malloc ((parts.size () + 1) * sizeof (char *));
  for (int i = 0; i < parts.size (); i++) (*list)[i] = strdup (parts[i].constData ());
  (*list)[parts.size ()] = nullptr;
  return 1;
}

int XmbTextPropertyToTextList (Display * a0_, const XTextProperty *tp, char ***list, int *count)
{ MQ_FWD (XmbTextPropertyToTextList, a0_, a0_, tp, list, count);
  XTextPropertyToStringList (const_cast<XTextProperty *> (tp), list, count);
  return Success;
}

int Xutf8TextPropertyToTextList (Display *d, const XTextProperty *tp, char ***list, int *count) { MQ_FWD (Xutf8TextPropertyToTextList, d, d, tp, list, count); return XmbTextPropertyToTextList (d, tp, list, count); }

void XFreeStringList (char **list)
{
  if (!list) return;
  for (char **p = list; *p; p++) free (*p);
  free (list);
}

void XSetTextProperty (Display *d, Window w, XTextProperty *tp, Atom property)
{ MQ_FWD (XSetTextProperty, d, d, w, tp, property);
  XChangeProperty (d, w, property, tp->encoding, tp->format, PropModeReplace, tp->value, tp->nitems);
}

void XSetWMName (Display *d, Window w, XTextProperty *tp)
{ MQ_FWD (XSetWMName, d, d, w, tp);
  XStoreName (d, w, QByteArray ((const char *) tp->value, tp->nitems).constData ());
}

void XSetWMIconName (Display * a0_, Window a1_, XTextProperty * a2_) { MQ_FWD (XSetWMIconName, a0_, a0_, a1_, a2_);}

} /* extern "C" */
