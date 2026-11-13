/*
 * Motif on Qt - primitives: XmPrimitive/XmGadget/XmManager behaviour shared
 * by all widgets, XmLabel, XmPushButton, XmToggleButton, XmCascadeButton,
 * XmArrowButton, XmSeparator, XmDrawnButton, their gadget twins, and the
 * canvas used by Core widgets and drawing areas for Xlib drawing.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstring>

/* ======================================================================
 * Shared helpers
 * ====================================================================== */
bool mq_class_is (Widget w, const char *class_name)
{
  for (MqClass *k = w ? R (w)->klass : nullptr; k; k = k->superclass)
    if (!strcmp (k->class_name, class_name)) return true;
  return false;
}

static bool is_button (Widget w)
{
  return mq_class_is (w, "XmPushButton") || mq_class_is (w, "XmPushButtonGadget") || mq_class_is (w, "XmToggleButton")
      || mq_class_is (w, "XmToggleButtonGadget") || mq_class_is (w, "XmCascadeButton") || mq_class_is (w, "XmCascadeButtonGadget")
      || mq_class_is (w, "XmArrowButton") || mq_class_is (w, "XmDrawnButton");
}

static bool is_text (Widget w)
{
  return mq_class_is (w, "XmText") || mq_class_is (w, "XmTextField") || mq_class_is (w, "XmList");
}

QFont mq_widget_font (Widget w, const char *which)
{
  bool ok;
  for (const char *r : { XmNrenderTable, XmNfontList })
    if (mq_isset (w, r) && mq_get (w, r)) {
      QFont f = mq_fontlist_font ((XmFontList) mq_get (w, r), &ok);
      if (ok) return f;
    }
  if (!which) which = is_text (w) ? XmNtextFontList : is_button (w) ? XmNbuttonFontList : XmNlabelFontList;
  QByteArray rt = QByteArray (which).replace ("FontList", "RenderTable");
  for (Widget a = R (w)->parent; a; a = R (a)->parent)
    for (const char *r : { rt.constData (), which, XmNdefaultFontList, "defaultRenderTable" })
      if (mq_find_res (a, r) && mq_isset (a, r) && mq_get (a, r)) {
        QFont f = mq_fontlist_font ((XmFontList) mq_get (a, r), &ok);
        if (ok) return f;
      }
  return QApplication::font ();
}

/* Tool tips (Motif 2.2+): shown only when the shell's toolTipEnable is set;
 * the shell's TipShell (see XmToolTipGetLabel) gets popup/popdown callbacks. */
static Widget tip_shell (Widget shell)
{
  for (Widget p : shell ? R (shell)->popups : QList<Widget> ()) if (R (p)->name == "TipShell") return p;
  return nullptr;
}

class TipFilter : public QObject {
public:
  QPointer<QWidget> showing;
  bool eventFilter (QObject *o, QEvent *e) override
  {
    if (e->type () != QEvent::ToolTip || !o->isWidgetType ()) return false;
    Widget w = mq_from_qobject (o);
    Widget shell = mq_shell_of (w);
    while (shell && R (shell)->parent && !mq_find_res (shell, XmNtoolTipEnable)) shell = mq_shell_of (R (shell)->parent);
    if (!shell) return false;
    if (!mq_getb (shell, XmNtoolTipEnable)) return true;   /* swallowed: tips are off */
    Widget tip = tip_shell (shell);
    if (tip) {
      mq_call (tip, XmNpopupCallback, nullptr);
      QTimer::singleShot (qMax (100, mq_geti (shell, XmNtoolTipPostDuration)), qApp, [tip] () {
        if (!R (tip)->destroying) mq_call (tip, XmNpopdownCallback, nullptr);
      });
    }
    return false;
  }
};

static void mq_tooltips_install ()
{
  static TipFilter *f = nullptr;
  if (f || !qApp) return;
  f = new TipFilter;
  f->setParent (qApp);
  qApp->installEventFilter (f);
}

extern "C" Widget XmToolTipGetLabel (Widget w)
{
  Widget shell = mq_shell_of (w);
  if (!shell) return nullptr;
  Widget tip = tip_shell (shell);
  if (!tip) {
    tip = XtCreatePopupShell ("TipShell", overrideShellWidgetClass, shell, nullptr, 0);
    XtVaCreateManagedWidget ("TipLabel", xmLabelWidgetClass, tip, NULL);
  }
  return R (tip)->children.isEmpty () ? nullptr : R (tip)->children.first ();
}

void mq_apply_common (Widget w, const QByteArray &res)
{
  bool all = res.isEmpty ();
  QWidget *qw = mq_qw (w);
  QAction *a = mq_action (w);
  if (qw && (all || res == XmNbackground || res == XmNforeground)) {
    bool bg = mq_isset (w, XmNbackground), fg = mq_isset (w, XmNforeground) && mq_find_res (w, XmNforeground);
    if (bg || fg) {
      QPalette pal = qw->palette ();
      if (bg) {
        QColor c = mq_color (mq_get (w, XmNbackground));
        for (auto role : { QPalette::Window, QPalette::Button }) pal.setColor (role, c);
        if (!is_text (w)) pal.setColor (QPalette::Base, c);
        qw->setAutoFillBackground (true);
      }
      if (fg) {
        QColor c = mq_color (mq_get (w, XmNforeground));
        for (auto role : { QPalette::WindowText, QPalette::ButtonText, QPalette::Text }) pal.setColor (role, c);
      }
      qw->setPalette (pal);
    }
  }
  if (all || res == XmNfontList || res == XmNrenderTable) {
    bool own = (mq_isset (w, XmNfontList) && mq_get (w, XmNfontList)) || (mq_isset (w, XmNrenderTable) && mq_get (w, XmNrenderTable));
    if (own || all) {
      QFont f = mq_widget_font (w);
      if (qw && (own || f != QApplication::font ())) qw->setFont (f);
      else if (a && own) a->setFont (f);
    }
  }
  if (all || res == "toolTipString") mq_tooltips_install ();
  if (all || res == "toolTipString") {
    XmString tip = (XmString) mq_get (w, "toolTipString");
    if (tip || !all) {
      QString t = tip ? mq_xmstring_text (tip) : QString ();
      if (qw) qw->setToolTip (t);
      else if (a) a->setToolTip (t);
    }
  }
  if (qw && (all || res == XmNtraversalOn) && mq_find_res (w, XmNtraversalOn) && mq_isset (w, XmNtraversalOn) && !mq_getb (w, XmNtraversalOn))
    qw->setFocusPolicy (Qt::NoFocus);
}

QString mq_label_text (Widget w)
{
  XmString s = (XmString) mq_get (w, XmNlabelString);
  if (s) return mq_xmstring_text (s);
  return QString::fromLocal8Bit (R (w)->name);
}

/* "Ctrl<Key>q", "Shift Ctrl<Key>s", "Meta<Key>x", "<Key>F1", "Alt<Key>osfDelete" */
QKeySequence mq_accelerator_keys (const char *accel)
{
  if (!accel || !*accel) return QKeySequence ();
  QByteArray s (accel);
  int lt = s.indexOf ('<'), gt = s.indexOf ('>');
  if (lt < 0 || gt < lt) return QKeySequence ();
  QByteArray mods = s.left (lt), key = s.mid (gt + 1).trimmed ();
  int m = 0;
  if (mods.contains ("Ctrl") || mods.contains ("Control")) m |= Qt::CTRL;
  if (mods.contains ("Shift")) m |= Qt::SHIFT;
  if (mods.contains ("Meta") || mods.contains ("Alt") || mods.contains ("Mod1")) m |= Qt::ALT;
  if (key.startsWith ("osf")) key = key.mid (3);
  static const QHash<QByteArray, int> special = {
    { "Return", Qt::Key_Return }, { "Delete", Qt::Key_Delete }, { "BackSpace", Qt::Key_Backspace }, { "Escape", Qt::Key_Escape },
    { "Tab", Qt::Key_Tab }, { "Insert", Qt::Key_Insert }, { "Home", Qt::Key_Home }, { "End", Qt::Key_End },
    { "Left", Qt::Key_Left }, { "Right", Qt::Key_Right }, { "Up", Qt::Key_Up }, { "Down", Qt::Key_Down },
    { "Prior", Qt::Key_PageUp }, { "Next", Qt::Key_PageDown }, { "PageUp", Qt::Key_PageUp }, { "PageDown", Qt::Key_PageDown },
    { "Help", Qt::Key_F1 }, { "Cancel", Qt::Key_Escape }, { "Undo", Qt::Key_Z | Qt::CTRL }, { "space", Qt::Key_Space },
    { "Copy", Qt::Key_Copy }, { "Cut", Qt::Key_Cut }, { "Paste", Qt::Key_Paste },
  };
  int k = 0;
  if (special.contains (key)) k = special.value (key);
  else if (key.size () > 1 && key[0] == 'F' && key.mid (1).toInt () > 0) k = Qt::Key_F1 + key.mid (1).toInt () - 1;
  else if (key.size () == 1) k = QChar (key[0]).toUpper ().unicode ();
  else {
    KeySym ks = XStringToKeysym (key.constData ());
    if (ks >= 0x20 && ks < 0x7f) k = QChar ((ushort) ks).toUpper ().unicode ();
  }
  if (!k) return QKeySequence ();
  return QKeySequence (k | m);
}

/* Activation of a RowColumn entry: the RowColumn's entryCallback, if any,
 * replaces the entry's own activate/valueChanged callbacks. */
void mq_option_menu_follow (Widget pulldown);   /* xm_managers.cpp */

static void entry_activate (Widget w, const char *cb, XtPointer cs)
{
  Widget rc = R (w)->parent;
  if (rc && mq_class_is (rc, "XmRowColumn")) {
    int type = mq_menu_type (rc);
    if (type == XmMENU_PULLDOWN || type == XmMENU_POPUP) {
      XtVaSetValues (rc, XmNmenuHistory, (XtArgVal) w, NULL);
      mq_option_menu_follow (rc);   /* option menus show their pulldown's history */
    }
    if (mq_has_callbacks (rc, XmNentryCallback)) {
      XmRowColumnCallbackStruct rcs;
      memset (&rcs, 0, sizeof rcs);
      rcs.reason = XmCR_ACTIVATE;
      rcs.event = mq_current_event ();
      rcs.widget = w;
      QList<XtCallbackRec> own = R (w)->callbacks.value (cb);
      rcs.data = own.isEmpty () ? nullptr : (char *) own.first ().closure;
      rcs.callbackstruct = (char *) cs;
      mq_call (rc, XmNentryCallback, &rcs);
      return;
    }
  }
  mq_call (w, cb, cs);
}



/* ======================================================================
 * Canvas: Core widgets, drawing areas and drawn buttons draw with Xlib into
 * a backing image that is painted here; showing, resizing and XClearArea
 * produce Expose events.
 * ====================================================================== */
QSize mq_foreign_size_hint (Widget w);
void mq_foreign_resize (Widget w);
void mq_foreign_expose (Widget w, XEvent *xe, const QRect &area);

class MqCanvas : public QWidget {
public:
  Widget w;
  bool exposed = false;
  bool resize_pending = false;   /* a program class's resize method, due from the event loop */
  explicit MqCanvas (Widget wd) : w (wd) { setAttribute (Qt::WA_OpaquePaintEvent); }
  QSize sizeHint () const override
  {
    if (R (w)->klass->foreign) return mq_foreign_size_hint (w);   /* the class's own idea */
    int width = mq_isset (w, XmNwidth) ? mq_geti (w, XmNwidth) : 0, height = mq_isset (w, XmNheight) ? mq_geti (w, XmNheight) : 0;
    QSize s = layout () ? layout ()->sizeHint () : QSize (width ? width : 100, height ? height : 100);
    return s.expandedTo (QSize (width, height));
  }
protected:
  void resizeEvent (QResizeEvent *e) override
  {
    Window win = mq_window_for (w);
    if (win) mq_window_resize (win, e->size ().width (), e->size ().height ());
    exposed = false;
    if (R (w)->klass->foreign) {
      w->core.width = e->size ().width ();
      w->core.height = e->size ().height ();
      /* Qt's layouts can pass through transient sizes (a hidden window laid
       * out before all its parts are managed). On X a resize method only sees
       * the size the widget ends up with: run it from the event loop, once,
       * with the current size. (XtConfigureWidget calls it directly.) */
      if (XtIsRealized (w) && !resize_pending) {
        resize_pending = true;
        QPointer<MqCanvas> self (this);
        QTimer::singleShot (0, this, [self] {
          if (!self || R (self->w)->destroying) return;
          self->resize_pending = false;
          self->w->core.width = self->width ();
          self->w->core.height = self->height ();
          mq_foreign_resize (self->w);
        });
      }
    }
    if ((isVisible () || XtIsRealized (w)) && mq_has_callbacks (w, XmNresizeCallback)) {
      XmDrawingAreaCallbackStruct cs = { XmCR_RESIZE, mq_current_event (), win };
      mq_call (w, XmNresizeCallback, &cs);
    }
    update ();
  }
  void paintEvent (QPaintEvent *pe) override
  {
    Window win = mq_window_for (w);
    QImage *img = mq_window_image (win, true);
    QVariant pending = property ("_mq_expose");
    if (!exposed || pending.isValid ()) {
      QRect r = exposed ? pending.value<QRegion> ().boundingRect () : rect ();
      exposed = true;
      setProperty ("_mq_expose", QVariant ());
      XEvent xe;
      memset (&xe, 0, sizeof xe);
      xe.xexpose.type = Expose;
      xe.xexpose.display = mq_display;
      xe.xexpose.window = win;
      xe.xexpose.x = r.x ();
      xe.xexpose.y = r.y ();
      xe.xexpose.width = r.width ();
      xe.xexpose.height = r.height ();
      MqEventScope scope (&xe);
      bool consumed = false;
      mq_dispatch_handlers (w, &xe, &consumed);
      if (R (w)->translations) mq_run_translations (w, &xe);
      if (mq_has_callbacks (w, XmNexposeCallback)) {
        XmDrawingAreaCallbackStruct cs = { XmCR_EXPOSE, &xe, win };
        mq_call (w, XmNexposeCallback, &cs);
      }
      mq_foreign_expose (w, &xe, r);
      img = mq_window_image (win, true);
    }
    QPainter p (this);
    if (img) p.drawImage (pe->rect (), *img, pe->rect ());
    else p.fillRect (pe->rect (), palette ().color (QPalette::Window));
    paint_extra (p);
  }
  virtual void paint_extra (QPainter &) {}
  void input (QEvent *e)
  {
    if (R (w)->translations || !mq_has_callbacks (w, XmNinputCallback)) return;
    XEvent xe;
    if (!mq_qt_to_xevent (w, e, &xe)) return;
    MqEventScope scope (&xe);
    XmDrawingAreaCallbackStruct cs = { XmCR_INPUT, &xe, mq_window_for (w) };
    mq_call (w, XmNinputCallback, &cs);
  }
  void mousePressEvent (QMouseEvent *e) override { input (e); }
  void mouseReleaseEvent (QMouseEvent *e) override { input (e); }
  void keyPressEvent (QKeyEvent *e) override { input (e); }
  void keyReleaseEvent (QKeyEvent *e) override { input (e); }
};

QObject *mq_canvas_create (Widget w)
{
  auto *c = new MqCanvas (w);
  if (mq_has_callbacks (w, XmNinputCallback) || mq_class_is (w, "XmDrawingArea")) c->setFocusPolicy (Qt::ClickFocus);
  return c;
}

/* ======================================================================
 * XmLabel and the label part of every button
 * ====================================================================== */
static Qt::Alignment label_alignment (Widget w)
{
  switch (mq_geti (w, XmNalignment)) {
  case XmALIGNMENT_BEGINNING: return Qt::AlignLeft | Qt::AlignVCenter;
  case XmALIGNMENT_END: return Qt::AlignRight | Qt::AlignVCenter;
  default: return Qt::AlignHCenter | Qt::AlignVCenter;
  }
}

static QPixmap label_pixmap (Widget w)
{
  Pixmap p = (Pixmap) mq_get (w, XmNlabelPixmap);
  if (!p || p == XmUNSPECIFIED_PIXMAP) return QPixmap ();
  return mq_pixmap_qt (p);
}

static void update_label (Widget w)
{
  QString text = mq_label_text (w);
  int type = mq_geti (w, XmNlabelType);
  QPixmap pix = type != XmSTRING ? label_pixmap (w) : QPixmap ();
  bool show_text = type != XmPIXMAP || pix.isNull ();
  KeySym mn = (KeySym) mq_get (w, XmNmnemonic);
  if (mn == XK_VoidSymbol) mn = 0;
  if (QAction *a = mq_action (w)) {
    QString t = mq_mnemonic_text (show_text ? text : QString (), mn);
    XmString at = (XmString) mq_get (w, XmNacceleratorText);
    QKeySequence keys = mq_accelerator_keys ((const char *) mq_get (w, XmNaccelerator));
    if (at) t += '\t' + mq_xmstring_text (at);
    else if (!keys.isEmpty ()) t += '\t' + keys.toString (QKeySequence::NativeText);
    a->setText (t);
    a->setIcon (pix.isNull () ? QIcon () : QIcon (pix));
    a->setShortcut (keys);
    a->setShortcutContext (Qt::ApplicationShortcut);
    return;
  }
  if (auto *l = mq_q<QLabel> (w)) {
    if (!show_text) l->setPixmap (pix);
    else l->setText (text);
    return;
  }
  if (auto *b = mq_q<QAbstractButton> (w)) {
    bool option_button = mq_class_is (w, "XmCascadeButton") && R (w)->parent && mq_menu_type (R (w)->parent) == XmMENU_OPTION;
    QString t = text;
    t.replace ('&', "&&");
    if (option_button) t = mq_mnemonic_text (text, mn);
    b->setText (show_text ? t : QString ());
    b->setIcon (pix.isNull () ? QIcon () : QIcon (pix));
    if (!pix.isNull ()) b->setIconSize (pix.size ());
  }
}

static void label_geometry (Widget w)
{
  QWidget *qw = mq_qw (w);
  if (!qw) return;
  int mw = mq_geti (w, XmNmarginWidth), mh = mq_geti (w, XmNmarginHeight);
  int st = mq_find_res (w, XmNshadowThickness) ? mq_geti (w, XmNshadowThickness) : 0;
  int ht = mq_find_res (w, XmNhighlightThickness) ? mq_geti (w, XmNhighlightThickness) : 0;
  if (auto *l = qobject_cast<QLabel *> (qw)) {
    l->setAlignment (label_alignment (w));
    int extra = st + ht;
    l->setContentsMargins (mw + extra + mq_geti (w, XmNmarginLeft), mh + extra + mq_geti (w, XmNmarginTop),
                           mw + extra + mq_geti (w, XmNmarginRight), mh + extra + mq_geti (w, XmNmarginBottom));
  }
}

static QObject *label_create (Widget w)
{
  if (mq_in_menu (w)) {
    auto *a = new QAction (nullptr);
    a->setEnabled (false);   /* menu titles */
    return a;
  }
  auto *l = new QLabel;
  l->setTextFormat (Qt::PlainText);
  return l;
}

static void label_apply (Widget w, const QByteArray &res)
{
  bool all = res.isEmpty ();
  mq_apply_common (w, res);
  if (all || res == XmNlabelString || res == XmNlabelType || res == XmNlabelPixmap || res == XmNmnemonic
      || res == XmNacceleratorText || res == XmNaccelerator)
    update_label (w);
  if (all || res == XmNalignment || res.startsWith ("margin") || res == XmNshadowThickness) label_geometry (w);
  if (mq_action (w) && R (w)->parent && (all || res == XmNlabelString)) { /* menu text changed: nothing else to do */ }
  if (!(R (w)->klass->kind & K_GADGET)) {   /* gadgets keep these in a cache record */
    XmString label = (XmString) mq_get (w, XmNlabelString);
    XmString made = label ? nullptr : XmStringCreateLocalized (R (w)->name.data ());
    XmRenderTable font = nullptr;
    for (const char *r : { XmNrenderTable, XmNfontList })
      if (!font && mq_isset (w, r)) font = (XmRenderTable) mq_get (w, r);
    if (!font) font = XmeGetDefaultRenderTable (w, is_button (w) ? XmBUTTON_FONTLIST : XmLABEL_FONTLIST);
    mq_sync_label_record (w, label ? label : made, (unsigned char) mq_geti (w, XmNlabelType), font,
                          (unsigned char) mq_geti (w, XmNalignment), (Pixmap) mq_get (w, XmNlabelPixmap),
                          (Pixmap) mq_get (w, XmNlabelInsensitivePixmap), (Dimension) mq_geti (w, XmNmarginWidth),
                          (Dimension) mq_geti (w, XmNmarginHeight), mq_getb (w, XmNrecomputeSize));
    if (made) XmStringFree (made);
  }
}

/* Motif's default render tables: the nearest ancestor's labelFontList /
 * buttonFontList / textFontList, else the "fixed" font, as Motif does */
extern "C" XmFontList XmeGetDefaultRenderTable (Widget w, unsigned char type)
{
  const char *which = type == XmBUTTON_FONTLIST ? XmNbuttonFontList : type == XmTEXT_FONTLIST ? XmNtextFontList : XmNlabelFontList;
  QByteArray rt = QByteArray (which).replace ("FontList", "RenderTable");
  for (Widget a = w ? R (w)->parent : nullptr; a; a = R (a)->parent)
    for (const char *r : { rt.constData (), which, XmNdefaultFontList })
      if (mq_find_res (a, r) && mq_isset (a, r) && mq_get (a, r)) return (XmFontList) mq_get (a, r);
  static XmFontList fallback = nullptr;
  if (!fallback) {
    mq_ensure_app ();
    XmFontListEntry e = XmFontListEntryLoad (mq_display, (char *) "fixed", XmFONT_IS_FONT, (char *) XmFONTLIST_DEFAULT_TAG);
    fallback = XmFontListAppendEntry (nullptr, e);
    XmFontListEntryFree (&e);
  }
  return fallback;
}

static bool label_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  /* Motif's Label exports these as copies the caller frees (NEdit does) */
  if (res == XmNaccelerator || res == XmNmnemonicCharSet) {
    const char *v = (const char *) mq_get (w, res.constData ());
    *out = (XtArgVal) (v ? XtNewString (v) : nullptr);
    return true;
  }
  if (res == XmNlabelString) {
    XmString s = (XmString) mq_get (w, XmNlabelString);
    *out = (XtArgVal) (s ? XmStringCopy (s) : XmStringCreateLocalized (R (w)->name.data ()));
    return true;
  }
  return false;
}

/* ======================================================================
 * XmPushButton
 * ====================================================================== */
static void push_call (Widget w, const char *cb, int reason, int clicks = 1)
{
  XmPushButtonCallbackStruct cs = { reason, mq_current_event (), clicks };
  if (!strcmp (cb, XmNactivateCallback)) entry_activate (w, cb, &cs);
  else mq_call (w, cb, &cs);
}

class MqPushButton : public QPushButton {
public:
  Widget w;
  explicit MqPushButton (Widget wd) : w (wd) { setAutoDefault (false); }
protected:
  void mouseDoubleClickEvent (QMouseEvent *e) override
  {
    QPushButton::mouseDoubleClickEvent (e);
    if (mq_getb (w, XmNmultiClick) == XmMULTICLICK_KEEP && e->button () == Qt::LeftButton) push_call (w, XmNactivateCallback, XmCR_ACTIVATE, 2);
  }
};

static QObject *push_create (Widget w)
{
  if (mq_in_menu (w)) {
    auto *a = new QAction (nullptr);
    QObject::connect (a, &QAction::triggered, a, [w] () {
      push_call (w, XmNarmCallback, XmCR_ARM);
      push_call (w, XmNactivateCallback, XmCR_ACTIVATE);
      push_call (w, XmNdisarmCallback, XmCR_DISARM);
    });
    return a;
  }
  auto *b = new MqPushButton (w);
  QObject::connect (b, &QPushButton::pressed, b, [w] () { push_call (w, XmNarmCallback, XmCR_ARM); });
  QObject::connect (b, &QPushButton::released, b, [w] () { push_call (w, XmNdisarmCallback, XmCR_DISARM); });
  QObject::connect (b, &QPushButton::clicked, b, [w] () { push_call (w, XmNactivateCallback, XmCR_ACTIVATE); });
  return b;
}

static void push_apply (Widget w, const QByteArray &res)
{
  label_apply (w, res);
  if (auto *b = mq_q<QPushButton> (w)) {
    if (res.isEmpty () || res == XmNshowAsDefault) b->setDefault (mq_geti (w, XmNshowAsDefault) > 0);
    if (res.isEmpty () || res == XmNshadowThickness) b->setFlat (mq_isset (w, XmNshadowThickness) && mq_geti (w, XmNshadowThickness) == 0);
  }
}

/* ======================================================================
 * XmToggleButton
 * ====================================================================== */
static bool radio_parent (Widget w)
{
  Widget p = R (w)->parent;
  return p && mq_class_is (p, "XmRowColumn") && mq_getb (p, XmNradioBehavior);
}

static bool toggle_is_radio (Widget w)
{
  int t = mq_geti (w, XmNindicatorType);
  if (t == XmONE_OF_MANY || t == XmONE_OF_MANY_ROUND || t == XmONE_OF_MANY_DIAMOND) return true;
  if (t == XmN_OF_MANY && mq_isset (w, XmNindicatorType)) return false;
  return radio_parent (w);
}

static void toggle_show (Widget w, bool on)
{
  if (QAction *a = mq_action (w)) { QSignalBlocker b (a); a->setChecked (on); }
  else if (auto *b = mq_q<QAbstractButton> (w)) { QSignalBlocker bl (b); b->setChecked (on); }
}

static void toggle_call (Widget w, bool on)
{
  XmToggleButtonCallbackStruct cs = { XmCR_VALUE_CHANGED, mq_current_event (), on ? XmSET : XmUNSET };
  entry_activate (w, XmNvalueChangedCallback, &cs);
}

void mq_toggle_set (Widget w, bool on, bool notify)
{
  if (const ResDef *d = mq_find_res (w, XmNset)) mq_store (w, d, on ? XmSET : XmUNSET);
  toggle_show (w, on);
  if (on && radio_parent (w))
    for (Widget s : R (R (w)->parent)->children)
      if (s != w && (mq_class_is (s, "XmToggleButton") || mq_class_is (s, "XmToggleButtonGadget")) && mq_geti (s, XmNset)) {
        mq_store (s, mq_find_res (s, XmNset), XmUNSET);
        toggle_show (s, false);
        if (notify) toggle_call (s, false);
      }
  if (notify) toggle_call (w, on);
}

static void toggle_user (Widget w, bool on)
{
  if (!on && radio_parent (w) && mq_getb (R (w)->parent, XmNradioAlwaysOne)) { toggle_show (w, true); return; }
  mq_call (w, XmNarmCallback, nullptr);
  mq_toggle_set (w, on, true);
  XmToggleButtonCallbackStruct cs = { XmCR_DISARM, mq_current_event (), on };
  mq_call (w, XmNdisarmCallback, &cs);
}

static QObject *toggle_create (Widget w)
{
  if (mq_in_menu (w)) {
    auto *a = new QAction (nullptr);
    a->setCheckable (true);
    QObject::connect (a, &QAction::triggered, a, [w] (bool on) { toggle_user (w, on); });
    return a;
  }
  QAbstractButton *b = toggle_is_radio (w) ? (QAbstractButton *) new QRadioButton : (QAbstractButton *) new QCheckBox;
  b->setAutoExclusive (false);
  QObject::connect (b, &QAbstractButton::clicked, b, [w] (bool on) { toggle_user (w, on); });
  return b;
}

static void toggle_apply (Widget w, const QByteArray &res)
{
  label_apply (w, res);
  if (res.isEmpty () || res == XmNset) toggle_show (w, mq_geti (w, XmNset) != XmUNSET);
  if (res == XmNset && radio_parent (w) && mq_geti (w, XmNset)) mq_toggle_set (w, true, false);
}

/* ======================================================================
 * XmCascadeButton
 * ====================================================================== */
static void cascade_attach (Widget w)
{
  Widget sub = (Widget) mq_get (w, XmNsubMenuId);
  QMenu *menu = sub ? mq_q<QMenu> (sub) : nullptr;
  if (menu) menu->setProperty ("_mq_cascade", QVariant::fromValue ((void *) w));
  if (QAction *a = mq_action (w)) a->setMenu (menu);
  else if (auto *b = mq_q<QPushButton> (w)) b->setMenu (menu);
}

static QObject *cascade_create (Widget w)
{
  if (mq_in_menu (w)) {
    auto *a = new QAction (nullptr);
    QObject::connect (a, &QAction::triggered, a, [w] () {
      XmAnyCallbackStruct cs = { XmCR_ACTIVATE, mq_current_event () };
      entry_activate (w, XmNactivateCallback, &cs);
    });
    return a;
  }
  auto *b = new QPushButton;
  b->setAutoDefault (false);
  QObject::connect (b, &QPushButton::clicked, b, [w] () {
    XmAnyCallbackStruct cs = { XmCR_ACTIVATE, mq_current_event () };
    mq_call (w, XmNactivateCallback, &cs);
  });
  return b;
}

static void cascade_apply (Widget w, const QByteArray &res)
{
  label_apply (w, res);
  if (res.isEmpty () || res == XmNsubMenuId) cascade_attach (w);
}

/* ======================================================================
 * XmArrowButton
 * ====================================================================== */
static void arrow_call (Widget w, const char *cb, int reason)
{
  XmArrowButtonCallbackStruct cs = { reason, mq_current_event (), 1 };
  if (!strcmp (cb, XmNactivateCallback)) entry_activate (w, cb, &cs);
  else mq_call (w, cb, &cs);
}

static QObject *arrow_create (Widget w)
{
  auto *b = new QToolButton;
  b->setAutoRaise (false);
  QObject::connect (b, &QToolButton::pressed, b, [w] () { arrow_call (w, XmNarmCallback, XmCR_ARM); });
  QObject::connect (b, &QToolButton::released, b, [w] () { arrow_call (w, XmNdisarmCallback, XmCR_DISARM); });
  QObject::connect (b, &QToolButton::clicked, b, [w] () { arrow_call (w, XmNactivateCallback, XmCR_ACTIVATE); });
  return b;
}

static void arrow_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  auto *b = mq_q<QToolButton> (w);
  if (!b) return;
  if (res.isEmpty () || res == XmNarrowDirection) {
    switch (mq_geti (w, XmNarrowDirection)) {
    case XmARROW_UP: b->setArrowType (Qt::UpArrow); break;
    case XmARROW_DOWN: b->setArrowType (Qt::DownArrow); break;
    case XmARROW_LEFT: b->setArrowType (Qt::LeftArrow); break;
    default: b->setArrowType (Qt::RightArrow); break;
    }
  }
}

/* ======================================================================
 * XmSeparator
 * ====================================================================== */
static QObject *separator_create (Widget w)
{
  if (mq_in_menu (w)) {
    auto *a = new QAction (nullptr);
    a->setSeparator (true);
    return a;
  }
  return new QFrame;
}

static void separator_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  auto *f = mq_q<QFrame> (w);
  if (!f) return;
  int type = mq_geti (w, XmNseparatorType);
  bool vertical = mq_geti (w, XmNorientation) == XmVERTICAL;
  if (type == XmNO_LINE) f->setFrameShape (QFrame::NoFrame);
  else f->setFrameShape (vertical ? QFrame::VLine : QFrame::HLine);
  f->setFrameShadow (type == XmSHADOW_ETCHED_OUT || type == XmSHADOW_ETCHED_OUT_DASH ? QFrame::Raised
                     : type == XmSINGLE_LINE || type == XmDOUBLE_LINE || type == XmSINGLE_DASHED_LINE || type == XmDOUBLE_DASHED_LINE ? QFrame::Plain
                     : QFrame::Sunken);
  f->setLineWidth (type == XmDOUBLE_LINE || type == XmDOUBLE_DASHED_LINE ? 2 : 1);
  int margin = mq_geti (w, XmNmargin);
  if (vertical) f->setContentsMargins (0, margin, 0, margin);
  else f->setContentsMargins (margin, 0, margin, 0);
  f->setSizePolicy (vertical ? QSizePolicy::Fixed : QSizePolicy::Expanding, vertical ? QSizePolicy::Expanding : QSizePolicy::Fixed);
}

/* ======================================================================
 * XmDrawnButton: a push button whose face the program draws
 * ====================================================================== */
class MqDrawnButton : public MqCanvas {
public:
  bool down = false;
  explicit MqDrawnButton (Widget wd) : MqCanvas (wd) { setFocusPolicy (Qt::StrongFocus); }
  void call (const char *cb, int reason, int clicks = 1)
  {
    XmDrawnButtonCallbackStruct cs = { reason, mq_current_event (), mq_window_for (w), clicks };
    if (!strcmp (cb, XmNactivateCallback)) entry_activate (w, cb, &cs);
    else mq_call (w, cb, &cs);
  }
protected:
  void paint_extra (QPainter &p) override
  {
    int st = mq_geti (w, XmNshadowThickness);
    if (st <= 0) return;
    int type = mq_geti (w, XmNshadowType);
    bool in = (type == XmSHADOW_IN || type == XmSHADOW_ETCHED_IN) != (down && mq_getb (w, XmNpushButtonEnabled));
    qDrawShadePanel (&p, rect (), palette (), in, st);
  }
  void mousePressEvent (QMouseEvent *e) override
  {
    if (e->button () != Qt::LeftButton) return;
    down = true;
    update ();
    call (XmNarmCallback, XmCR_ARM);
  }
  void mouseReleaseEvent (QMouseEvent *e) override
  {
    if (e->button () != Qt::LeftButton || !down) return;
    down = false;
    update ();
    if (rect ().contains (e->position ().toPoint ())) call (XmNactivateCallback, XmCR_ACTIVATE);
    call (XmNdisarmCallback, XmCR_DISARM);
  }
  void keyPressEvent (QKeyEvent *e) override
  {
    if (e->key () == Qt::Key_Space || e->key () == Qt::Key_Return) call (XmNactivateCallback, XmCR_ACTIVATE);
    else MqCanvas::keyPressEvent (e);
  }
};

static QObject *drawn_create (Widget w) { return new MqDrawnButton (w); }

static void drawn_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (auto *c = mq_q<QWidget> (w)) c->update ();
}

/* ======================================================================
 * Class records
 * ====================================================================== */
#define PRIMK (K_RECT | K_WIDGET | K_PRIMITIVE)
#define GADGETK (K_RECT | K_GADGET)
XmPrimitiveClassRec xmPrimitiveClassRec;
XmGadgetClassRec xmGadgetClassRec;

static void primitive_apply (Widget w, const QByteArray &res) { mq_apply_common (w, res); }

MQ_DEFINE_CLASS_XT (xmPrimitiveWidgetClass, mq_primitive_rec, xmPrimitiveClassRec, "XmPrimitive", &core_rec, PRIMK, nullptr, primitive_apply, nullptr, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS_XT (xmGadgetClass, mq_gadget_rec, xmGadgetClassRec, "XmGadget", &rectobj_rec, GADGETK, nullptr, primitive_apply, nullptr, nullptr, nullptr, nullptr, nullptr)

MqXtClassRec xmLabelClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmLabelWidgetClass, mq_label_rec, xmLabelClassRec, "XmLabel", &mq_primitive_rec, PRIMK, label_create, label_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmPushButtonClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmPushButtonWidgetClass, mq_push_rec, xmPushButtonClassRec, "XmPushButton", &mq_label_rec, PRIMK, push_create, push_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmToggleButtonClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmToggleButtonWidgetClass, mq_toggle_rec, xmToggleButtonClassRec, "XmToggleButton", &mq_label_rec, PRIMK, toggle_create, toggle_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmCascadeButtonClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmCascadeButtonWidgetClass, mq_cascade_rec, xmCascadeButtonClassRec, "XmCascadeButton", &mq_label_rec, PRIMK, cascade_create, cascade_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmDrawnButtonClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmDrawnButtonWidgetClass, mq_drawn_rec, xmDrawnButtonClassRec, "XmDrawnButton", &mq_label_rec, PRIMK, drawn_create, drawn_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmArrowButtonClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmArrowButtonWidgetClass, mq_arrow_rec, xmArrowButtonClassRec, "XmArrowButton", &mq_primitive_rec, PRIMK, arrow_create, arrow_apply, nullptr, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmSeparatorClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmSeparatorWidgetClass, mq_separator_rec, xmSeparatorClassRec, "XmSeparator", &mq_primitive_rec, PRIMK, separator_create, separator_apply, nullptr, nullptr, nullptr, nullptr, nullptr)

/* Gadgets: windowless in Motif, ordinary Qt widgets here */
MqXtClassRec xmLabelGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmLabelGadgetClass, mq_label_gadget_rec, xmLabelGadgetClassRec, "XmLabelGadget", &mq_gadget_rec, GADGETK, label_create, label_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmPushButtonGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmPushButtonGadgetClass, mq_push_gadget_rec, xmPushButtonGadgetClassRec, "XmPushButtonGadget", &mq_label_gadget_rec, GADGETK, push_create, push_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmToggleButtonGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmToggleButtonGadgetClass, mq_toggle_gadget_rec, xmToggleButtonGadgetClassRec, "XmToggleButtonGadget", &mq_label_gadget_rec, GADGETK, toggle_create, toggle_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmCascadeButtonGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmCascadeButtonGadgetClass, mq_cascade_gadget_rec, xmCascadeButtonGadgetClassRec, "XmCascadeButtonGadget", &mq_label_gadget_rec, GADGETK, cascade_create, cascade_apply, label_fetch, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmArrowButtonGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmArrowButtonGadgetClass, mq_arrow_gadget_rec, xmArrowButtonGadgetClassRec, "XmArrowButtonGadget", &mq_gadget_rec, GADGETK, arrow_create, arrow_apply, nullptr, nullptr, nullptr, nullptr, nullptr)
MqXtClassRec xmSeparatorGadgetClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmSeparatorGadgetClass, mq_separator_gadget_rec, xmSeparatorGadgetClassRec, "XmSeparatorGadget", &mq_gadget_rec, GADGETK, separator_create, separator_apply, nullptr, nullptr, nullptr, nullptr, nullptr)

/* ======================================================================
 * Public functions
 * ====================================================================== */
extern "C" {

Boolean XmToggleButtonGetState (Widget w) { return w && mq_geti (w, XmNset) != XmUNSET; }
Boolean XmToggleButtonGadgetGetState (Widget w) { return XmToggleButtonGetState (w); }
void XmToggleButtonSetState (Widget w, Boolean state, Boolean notify) { if (w) mq_toggle_set (w, state, notify); }
void XmToggleButtonGadgetSetState (Widget w, Boolean state, Boolean notify) { XmToggleButtonSetState (w, state, notify); }
Boolean XmToggleButtonSetValue (Widget w, XmToggleButtonState state, Boolean notify)
{
  if (!w) return False;
  if (state == XmINDETERMINATE) {
    mq_store (w, mq_find_res (w, XmNset), XmINDETERMINATE);
    if (auto *c = mq_q<QCheckBox> (w)) { QSignalBlocker b (c); c->setTristate (true); c->setCheckState (Qt::PartiallyChecked); }
    return True;
  }
  mq_toggle_set (w, state == XmSET, notify);
  return True;
}
Boolean XmToggleButtonGadgetSetValue (Widget w, XmToggleButtonState state, Boolean notify) { return XmToggleButtonSetValue (w, state, notify); }

void XmCascadeButtonHighlight (Widget, Boolean) {}
void XmCascadeButtonGadgetHighlight (Widget, Boolean) {}

} /* extern "C" */
