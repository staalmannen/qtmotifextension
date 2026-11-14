/*
 * Motif on Qt - XmMessageBox, XmSelectionBox, XmFileSelectionBox, XmCommand
 * and the dialog convenience functions. The internal children (OK button,
 * list, text ...) are real widgets with Motif's names, so XtNameToWidget,
 * Xm*GetChild and resource files work as with Motif.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <QDir>
#include <cstring>

#define MGRK (K_RECT | K_WIDGET | K_COMPOSITE | K_CONSTRAINT | K_MANAGER)

extern MqClass mq_bb_rec;

static Widget child (Widget box, const char *name)
{
  for (Widget c : R (box)->children) if (R (c)->name == name) return c;
  return nullptr;
}

static Widget list_of (Widget box, const char *name) { return XtNameToWidget (box, QByteArray ("*").append (name).constData ()); }

static QWidget *outer (Widget w)
{
  /* scrolled lists live in their scrolled window */
  if (w && R (w)->parent && mq_class_is (R (w)->parent, "XmScrolledWindow") && R (R (w)->parent)->parent) return mq_qw (R (w)->parent);
  return mq_qw (w);
}

static int margin (Widget w, const char *res) { return mq_isset (w, res) ? mq_geti (w, res) : 10; }

static QGridLayout *fresh_grid (Widget box)
{
  QWidget *qw = mq_qw (box);
  auto *g = qobject_cast<QGridLayout *> (qw->layout ());
  if (!g) {
    delete qw->layout ();
    g = new QGridLayout (qw);
  }
  while (QLayoutItem *it = g->takeAt (0)) delete it;
  int mw = margin (box, XmNmarginWidth), mh = margin (box, XmNmarginHeight);
  g->setContentsMargins (mw, mh, mw, mh);
  g->setSpacing (6);
  for (int i = 0; i < g->rowCount (); i++) g->setRowStretch (i, 0);
  for (int i = 0; i < g->columnCount (); i++) g->setColumnStretch (i, 0);
  return g;
}

static bool shown (Widget c) { return c && R (c)->managed && !R (c)->destroying; }

static void add (QGridLayout *g, Widget c, int row, int col, int rs = 1, int cs = 1, Qt::Alignment a = Qt::Alignment ())
{
  if (!shown (c)) return;
  QWidget *qw = outer (c);
  if (!qw) return;
  if (qw->parentWidget () != g->parentWidget ()) qw->setParent (g->parentWidget ());
  g->addWidget (qw, row, col, rs, cs, a);
  qw->show ();
}

static QHBoxLayout *button_row (Widget box, const QList<Widget> &buttons)
{
  auto *row = new QHBoxLayout;
  row->setSpacing (8);
  bool minimize = mq_find_res (box, XmNminimizeButtons) && mq_getb (box, XmNminimizeButtons);
  int n = 0;
  for (Widget b : buttons) if (shown (b)) n++;
  row->addStretch (1);
  for (Widget b : buttons) {
    if (!shown (b) || !mq_qw (b)) continue;
    QWidget *qw = mq_qw (b);
    if (qw->parentWidget () != mq_qw (box)) qw->setParent (mq_qw (box));
    qw->setSizePolicy (minimize ? QSizePolicy::Fixed : QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->addWidget (qw, minimize ? 0 : 1);
    qw->show ();
    if (--n) row->addStretch (1);
  }
  row->addStretch (1);
  return row;
}

/* activation of one of the standard buttons */
static void box_button (Widget box, const char *cb, int reason, bool unmanage)
{
  XmAnyCallbackStruct cs = { reason, mq_current_event () };
  mq_call (box, cb, &cs);
  if (unmanage && !R (box)->destroying && mq_getb (box, XmNautoUnmanage) && R (box)->parent && mq_class_is (R (box)->parent, "XmDialogShell"))
    XtUnmanageChild (box);
}

static void make_default (Widget box, Widget button)
{
  if (const ResDef *d = mq_find_res (box, XmNdefaultButton)) mq_store (box, d, (XtArgVal) button);
  if (auto *b = button ? mq_q<QPushButton> (button) : nullptr) { b->setAutoDefault (true); b->setDefault (true); }
}

/* ======================================================================
 * XmMessageBox
 * ====================================================================== */
static const char *mb_internal[] = { "Symbol", "Message", "Separator", "OK", "Cancel", "Help" };

static bool is_internal (Widget c, const char *const *names, int n)
{
  for (int i = 0; i < n; i++) if (R (c)->name == names[i]) return true;
  return false;
}

static QPixmap symbol_for (Widget w)
{
  Pixmap p = (Pixmap) mq_get (w, XmNsymbolPixmap);
  if (p && p != XmUNSPECIFIED_PIXMAP) return mq_pixmap_qt (p);
  QStyle::StandardPixmap sp;
  switch (mq_geti (w, XmNdialogType)) {
  case XmDIALOG_ERROR: sp = QStyle::SP_MessageBoxCritical; break;
  case XmDIALOG_WARNING: sp = QStyle::SP_MessageBoxWarning; break;
  case XmDIALOG_QUESTION: sp = QStyle::SP_MessageBoxQuestion; break;
  case XmDIALOG_INFORMATION: sp = QStyle::SP_MessageBoxInformation; break;
  case XmDIALOG_WORKING: sp = QStyle::SP_BrowserReload; break;
  default: return QPixmap ();
  }
  int size = QApplication::style ()->pixelMetric (QStyle::PM_MessageBoxIconSize);
  return QApplication::style ()->standardIcon (sp).pixmap (size, size);
}

static void mb_arrange (Widget w)
{
  if (!mq_qw (w)) return;
  QGridLayout *g = fresh_grid (w);
  Widget sym = child (w, "Symbol"), msg = child (w, "Message");
  add (g, sym, 0, 0, 1, 1, Qt::AlignTop);
  add (g, msg, 0, 1, 1, 1, Qt::AlignVCenter);
  g->setColumnStretch (1, 1);
  int row = 1;
  QList<Widget> buttons;
  buttons << child (w, "OK");
  for (Widget c : R (w)->children) {
    if (is_internal (c, mb_internal, 6) || !shown (c)) continue;
    if (mq_class_is (c, "XmPushButton") || mq_class_is (c, "XmPushButtonGadget")) buttons << c;
    else add (g, c, row++, 1);
  }
  g->setRowStretch (row - 1, 1);
  buttons << child (w, "Cancel") << child (w, "Help");
  add (g, child (w, "Separator"), row++, 0, 1, 2);
  g->addLayout (button_row (w, buttons), row, 0, 1, 2);
}

static void mb_update (Widget w, const QByteArray &res)
{
  bool all = res.isEmpty ();
  if (all || res == XmNmessageString) if (Widget m = child (w, "Message")) {
    XmString s = (XmString) mq_get (w, XmNmessageString);
    XmString e = s ? nullptr : XmStringCreateLocalized ((char *) "");
    XtVaSetValues (m, XmNlabelString, (XtArgVal) (s ? s : e), NULL);
    if (e) XmStringFree (e);
  }
  if (all || res == XmNmessageAlignment) if (Widget m = child (w, "Message"))
    XtVaSetValues (m, XmNalignment, (XtArgVal) mq_geti (w, XmNmessageAlignment), NULL);
  struct { const char *res, *name; } labels[] = { { XmNokLabelString, "OK" }, { XmNcancelLabelString, "Cancel" }, { XmNhelpLabelString, "Help" } };
  for (auto &l : labels)
    if ((all || res == l.res) && mq_isset (w, l.res)) if (Widget b = child (w, l.name)) XtVaSetValues (b, XmNlabelString, mq_get (w, l.res), NULL);
  if (all || res == XmNdialogType || res == XmNsymbolPixmap) if (Widget s = child (w, "Symbol")) {
    QPixmap p = symbol_for (w);
    if (auto *l = mq_q<QLabel> (s)) l->setPixmap (p);
    if (p.isNull ()) XtUnmanageChild (s); else XtManageChild (s);
  }
  if (all || res == XmNdefaultButtonType) {
    int t = mq_geti (w, XmNdefaultButtonType);
    Widget d = t == XmDIALOG_CANCEL_BUTTON ? child (w, "Cancel") : t == XmDIALOG_HELP_BUTTON ? child (w, "Help") : t == XmDIALOG_NONE ? nullptr : child (w, "OK");
    make_default (w, d);
  }
}

static void mb_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (!res.isEmpty () && child (w, "OK")) { mb_update (w, res); mb_arrange (w); }
}

static void mb_initialize (Widget w)
{
  int type = mq_geti (w, XmNdialogType);
  Widget sym = XtVaCreateWidget ("Symbol", xmLabelGadgetClass, w, XmNlabelType, XmPIXMAP, NULL);
  Widget msg = XtVaCreateManagedWidget ("Message", xmLabelGadgetClass, w, XmNalignment, XmALIGNMENT_BEGINNING, NULL);
  Q_UNUSED (sym);
  Q_UNUSED (msg);
  XtVaCreateManagedWidget ("Separator", xmSeparatorGadgetClass, w, NULL);
  struct { const char *name, *cb; int reason; bool unmanage; } bs[] = {
    { "OK", XmNokCallback, XmCR_OK, true }, { "Cancel", XmNcancelCallback, XmCR_CANCEL, true }, { "Help", XmNhelpCallback, XmCR_HELP, false } };
  for (auto &b : bs) {
    Widget pb = XtVaCreateWidget (b.name, xmPushButtonGadgetClass, w, NULL);
    const char *cb = b.cb;
    int reason = b.reason;
    bool un = b.unmanage;
    XtAddCallback (pb, XmNactivateCallback, [] (Widget pbw, XtPointer, XtPointer) {
      Widget box = R (pbw)->parent;
      if (R (pbw)->name == "OK") box_button (box, XmNokCallback, XmCR_OK, true);
      else if (R (pbw)->name == "Cancel") box_button (box, XmNcancelCallback, XmCR_CANCEL, true);
      else box_button (box, XmNhelpCallback, XmCR_HELP, false);
    }, nullptr);
    Q_UNUSED (cb); Q_UNUSED (reason); Q_UNUSED (un);
    /* template dialogs only get the buttons the program asked for */
    bool want = type != XmDIALOG_TEMPLATE || mq_has_callbacks (w, b.cb)
             || mq_isset (w, !strcmp (b.name, "OK") ? XmNokLabelString : !strcmp (b.name, "Cancel") ? XmNcancelLabelString : XmNhelpLabelString);
    if (want) XtManageChild (pb);
  }
  if (Widget c = child (w, "Cancel")) if (const ResDef *d = mq_find_res (w, XmNcancelButton)) mq_store (w, d, (XtArgVal) c);
  mb_update (w, QByteArray ());
  if (type == XmDIALOG_TEMPLATE && !mq_isset (w, XmNmessageString)) XtUnmanageChild (child (w, "Message"));
  if (type == XmDIALOG_TEMPLATE && !R (child (w, "OK"))->managed && !R (child (w, "Cancel"))->managed && !R (child (w, "Help"))->managed)
    XtUnmanageChild (child (w, "Separator"));
  mb_arrange (w);
}

static void box_place (Widget box, Widget) { for (MqClass *k = R (box)->klass; k; k = k->superclass) if (k->relayout) { k->relayout (box); break; } }
static void box_unplace (Widget box, Widget c)
{
  if (QWidget *qw = outer (c)) qw->hide ();
  box_place (box, c);
}

/* ======================================================================
 * XmSelectionBox (and XmCommand, XmFileSelectionBox on top of it)
 * ====================================================================== */
static const char *sb_internal[] = { "Items", "ItemsListSW", "Selection", "Text", "Separator", "OK", "Apply", "Cancel", "Help",
                                     "FilterLabel", "FilterText", "Dir", "DirListSW" };

static int sb_type (Widget w)
{
  int t = mq_geti (w, XmNdialogType);
  if (mq_class_is (w, "XmCommand")) return XmDIALOG_COMMAND;
  if (mq_class_is (w, "XmFileSelectionBox")) return XmDIALOG_FILE_SELECTION;
  if (t == XmDIALOG_PROMPT || t == XmDIALOG_SELECTION || t == XmDIALOG_WORK_AREA || t == XmDIALOG_COMMAND) return t;
  return R (w)->parent && mq_class_is (R (w)->parent, "XmDialogShell") ? XmDIALOG_SELECTION : XmDIALOG_WORK_AREA;
}

static QList<Widget> work_children (Widget w)
{
  QList<Widget> out;
  for (Widget c : R (w)->children) if (shown (c) && !is_internal (c, sb_internal, 13)) out << c;
  return out;
}

static void sb_arrange (Widget w)
{
  if (!mq_qw (w) || !child (w, "Text")) return;
  QGridLayout *g = fresh_grid (w);
  int type = sb_type (w), row = 0;
  int placement = mq_geti (w, XmNchildPlacement);
  QList<Widget> work = work_children (w);
  auto add_work = [&] () { for (Widget c : work) add (g, c, row++, 0, 1, 2); };
  if (type == XmDIALOG_FILE_SELECTION) {
    add (g, child (w, "FilterLabel"), row++, 0, 1, 2);
    add (g, child (w, "FilterText"), row++, 0, 1, 2);
    if (placement == XmPLACE_TOP) add_work ();
    add (g, child (w, "Dir"), row, 0);
    add (g, child (w, "Items"), row++, 1);
    add (g, list_of (w, "DirList"), row, 0);
    add (g, list_of (w, "ItemsList"), row, 1);
    g->setRowStretch (row++, 1);
    g->setColumnStretch (0, 1);
    g->setColumnStretch (1, 1);
  } else {
    if (placement == XmPLACE_TOP) add_work ();
    add (g, child (w, "Items"), row++, 0, 1, 2);
    if (Widget l = list_of (w, "ItemsList")) { add (g, l, row, 0, 1, 2); g->setRowStretch (row++, 1); }
  }
  if (placement == XmPLACE_ABOVE_SELECTION) add_work ();
  add (g, child (w, "Selection"), row++, 0, 1, 2);
  add (g, child (w, "Text"), row++, 0, 1, 2);
  if (placement == XmPLACE_BELOW_SELECTION) add_work ();
  if (type == XmDIALOG_PROMPT && work.isEmpty ()) g->setRowStretch (row, 1);
  add (g, child (w, "Separator"), row++, 0, 1, 2);
  if (child (w, "OK"))
    g->addLayout (button_row (w, { child (w, "OK"), child (w, "Apply"), child (w, "Cancel"), child (w, "Help") }), row, 0, 1, 2);
}

static QString sb_text (Widget w)
{
  Widget t = child (w, "Text");
  char *s = t ? XmTextFieldGetString (t) : nullptr;
  QString r = QString::fromLocal8Bit (s ? s : "");
  XtFree (s);
  return r;
}

static void sb_set_text (Widget w, const QString &s)
{
  if (Widget t = child (w, "Text")) {
    QByteArray b = s.toLocal8Bit ();
    XmTextFieldSetString (t, b.data ());
    XmTextFieldSetInsertionPosition (t, s.size ());
  }
}

static void sb_callback (Widget w, const char *cb, int reason)
{
  XmString value = mq_xmstring_from (sb_text (w));
  if (mq_class_is (w, "XmFileSelectionBox")) {
    XmFileSelectionBoxCallbackStruct cs;
    memset (&cs, 0, sizeof cs);
    cs.reason = reason;
    cs.event = mq_current_event ();
    cs.value = value;
    cs.length = XmStringLength (value);
    cs.mask = (XmString) mq_get (w, XmNdirMask);
    cs.mask_length = XmStringLength (cs.mask);
    cs.dir = (XmString) mq_get (w, XmNdirectory);
    cs.dir_length = XmStringLength (cs.dir);
    cs.pattern = (XmString) mq_get (w, XmNpattern);
    cs.pattern_length = XmStringLength (cs.pattern);
    mq_call (w, cb, &cs);
  } else {
    XmSelectionBoxCallbackStruct cs = { reason, mq_current_event (), value, XmStringLength (value) };
    mq_call (w, cb, &cs);
  }
  XmStringFree (value);
}

static void sb_ok (Widget w)
{
  if (mq_getb (w, XmNmustMatch) && !mq_class_is (w, "XmFileSelectionBox")) {
    Widget l = list_of (w, "ItemsList");
    XmString v = mq_xmstring_from (sb_text (w));
    bool found = l && XmListItemExists (l, v);
    XmStringFree (v);
    if (!found) { sb_callback (w, XmNnoMatchCallback, XmCR_NO_MATCH); return; }
  }
  if (mq_class_is (w, "XmFileSelectionBox") && mq_getb (w, XmNmustMatch)) {
    if (!QFileInfo::exists (sb_text (w))) { sb_callback (w, XmNnoMatchCallback, XmCR_NO_MATCH); return; }
  }
  sb_callback (w, XmNokCallback, XmCR_OK);
  if (!R (w)->destroying && mq_getb (w, XmNautoUnmanage) && R (w)->parent && mq_class_is (R (w)->parent, "XmDialogShell")) XtUnmanageChild (w);
}

void mq_fsb_search (Widget w, XmString mask);

static void sb_button (Widget pb, XtPointer, XtPointer)
{
  Widget w = R (pb)->parent;
  if (R (pb)->name == "OK") sb_ok (w);
  else if (R (pb)->name == "Apply") {
    if (mq_class_is (w, "XmFileSelectionBox")) {
      Widget ft = child (w, "FilterText");
      char *s = ft ? XmTextFieldGetString (ft) : nullptr;
      XmString mask = XmStringCreateLocalized (s ? s : (char *) "*");
      XtFree (s);
      mq_fsb_search (w, mask);
      XmStringFree (mask);
    }
    sb_callback (w, XmNapplyCallback, XmCR_APPLY);
  } else if (R (pb)->name == "Cancel") {
    sb_callback (w, XmNcancelCallback, XmCR_CANCEL);
    if (!R (w)->destroying && mq_getb (w, XmNautoUnmanage) && R (w)->parent && mq_class_is (R (w)->parent, "XmDialogShell")) XtUnmanageChild (w);
  } else if (R (pb)->name == "Help") sb_callback (w, XmNhelpCallback, XmCR_HELP);
}

static void list_to_text (Widget list, XtPointer box_ptr, XtPointer call)
{
  Widget w = (Widget) box_ptr;
  auto *cs = (XmListCallbackStruct *) call;
  if (!cs->item) return;
  sb_set_text (w, mq_xmstring_text (cs->item));
  if (cs->reason == XmCR_DEFAULT_ACTION) {
    if (mq_class_is (w, "XmCommand")) {
      XmCommandCallbackStruct cc = { XmCR_COMMAND_ENTERED, mq_current_event (), cs->item, XmStringLength (cs->item) };
      mq_call (w, XmNcommandEnteredCallback, &cc);
    } else sb_ok (w);
  }
  Q_UNUSED (list);
}

static void sb_update (Widget w, const QByteArray &res)
{
  bool all = res.isEmpty ();
  Widget list = list_of (w, "ItemsList");
  if ((all || res == XmNlistItems || res == XmNlistItemCount) && list && !mq_class_is (w, "XmFileSelectionBox")) {
    XmStringTable items = (XmStringTable) mq_get (w, XmNlistItems);
    if (items || !all) XtVaSetValues (list, XmNitems, (XtArgVal) items, XmNitemCount, mq_get (w, XmNlistItemCount), NULL);
  }
  if ((all || res == XmNlistVisibleItemCount) && list && mq_isset (w, XmNlistVisibleItemCount))
    XtVaSetValues (list, XmNvisibleItemCount, mq_get (w, XmNlistVisibleItemCount), NULL);
  if ((all || res == XmNtextString) && mq_get (w, XmNtextString)) sb_set_text (w, mq_xmstring_text ((XmString) mq_get (w, XmNtextString)));
  if ((all || res == XmNtextColumns) && mq_isset (w, XmNtextColumns))
    if (Widget t = child (w, "Text")) XtVaSetValues (t, XmNcolumns, mq_get (w, XmNtextColumns), NULL);
  struct { const char *res, *name; } labels[] = {
    { XmNlistLabelString, "Items" }, { XmNselectionLabelString, "Selection" }, { XmNokLabelString, "OK" },
    { XmNapplyLabelString, "Apply" }, { XmNcancelLabelString, "Cancel" }, { XmNhelpLabelString, "Help" },
    { XmNfilterLabelString, "FilterLabel" }, { XmNdirListLabelString, "Dir" }, { XmNfileListLabelString, "Items" },
    { XmNpromptString, "Selection" } };
  for (auto &l : labels)
    if ((all || res == l.res) && mq_find_res (w, l.res) && mq_isset (w, l.res) && mq_get (w, l.res))
      if (Widget c = child (w, l.name)) XtVaSetValues (c, XmNlabelString, mq_get (w, l.res), NULL);
}

static bool sb_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  if (res == XmNtextString || res == XmNcommand) { *out = (XtArgVal) mq_xmstring_from (sb_text (w)); return true; }
  Widget list = list_of (w, "ItemsList");
  if (list && (res == XmNlistItems || res == XmNhistoryItems)) { XtVaGetValues (list, XmNitems, out, NULL); return true; }
  if (list && (res == XmNlistItemCount || res == XmNhistoryItemCount)) { int n = 0; XtVaGetValues (list, XmNitemCount, &n, NULL); *out = n; return true; }
  return false;
}

static void sb_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (!res.isEmpty () && child (w, "Text")) { sb_update (w, res); sb_arrange (w); }
}

static Widget labelg (Widget w, const char *name, const char *text)
{
  XmString s = XmStringCreateLocalized ((char *) text);
  Widget l = XtVaCreateManagedWidget (name, xmLabelGadgetClass, w, XmNlabelString, (XtArgVal) s, XmNalignment, XmALIGNMENT_BEGINNING, NULL);
  XmStringFree (s);
  return l;
}

static void sb_initialize (Widget w)
{
  int type = sb_type (w);
  bool fsb = type == XmDIALOG_FILE_SELECTION, cmd = type == XmDIALOG_COMMAND;
  if (fsb) {
    labelg (w, "FilterLabel", "Filter");
    XtManageChild (XmCreateTextField (w, (char *) "FilterText", nullptr, 0));
    labelg (w, "Dir", "Directories");
    Widget dl = XmCreateScrolledList (w, (char *) "DirList", nullptr, 0);
    XtVaSetValues (dl, XmNvisibleItemCount, 8, XmNselectionPolicy, XmBROWSE_SELECT, NULL);
    XtManageChild (dl);
  }
  if (type != XmDIALOG_PROMPT && !cmd) labelg (w, "Items", fsb ? "Files" : "Items");
  if (type != XmDIALOG_PROMPT) {
    Widget l = XmCreateScrolledList (w, (char *) "ItemsList", nullptr, 0);
    XtVaSetValues (l, XmNvisibleItemCount, mq_isset (w, XmNlistVisibleItemCount) ? mq_geti (w, XmNlistVisibleItemCount) : 8,
                   XmNselectionPolicy, XmBROWSE_SELECT, NULL);
    XtAddCallback (l, XmNbrowseSelectionCallback, list_to_text, w);
    XtAddCallback (l, XmNsingleSelectionCallback, list_to_text, w);
    XtAddCallback (l, XmNdefaultActionCallback, list_to_text, w);
    XtManageChild (l);
  }
  labelg (w, "Selection", cmd ? ">" : "Selection");
  Widget text = XtVaCreateManagedWidget ("Text", xmTextFieldWidgetClass, w, NULL);
  if (cmd) {
    XtAddCallback (text, XmNactivateCallback, [] (Widget t, XtPointer, XtPointer) {
      Widget box = R (t)->parent;
      QString s = sb_text (box);
      XmString v = mq_xmstring_from (s);
      XmCommandCallbackStruct cc = { XmCR_COMMAND_ENTERED, mq_current_event (), v, XmStringLength (v) };
      XmCommandAppendValue (box, nullptr);
      XmString hist = mq_xmstring_from (s);
      Widget l = list_of (box, "ItemsList");
      if (l && !s.isEmpty ()) {
        XmListAddItemUnselected (l, hist, 0);
        int max = mq_geti (box, XmNhistoryMaxItems), n = 0;
        XtVaGetValues (l, XmNitemCount, &n, NULL);
        if (max > 0) while (n-- > max) XmListDeletePos (l, 1);
        XmListSetBottomPos (l, 0);
      }
      XmStringFree (hist);
      sb_set_text (box, QString ());
      mq_call (box, XmNcommandEnteredCallback, &cc);
      XmStringFree (v);
    }, nullptr);
    XtAddCallback (text, XmNvalueChangedCallback, [] (Widget t, XtPointer, XtPointer) {
      Widget box = R (t)->parent;
      XmString v = mq_xmstring_from (sb_text (box));
      XmCommandCallbackStruct cc = { XmCR_COMMAND_CHANGED, mq_current_event (), v, XmStringLength (v) };
      mq_call (box, XmNcommandChangedCallback, &cc);
      XmStringFree (v);
    }, nullptr);
  } else {
    XtVaCreateManagedWidget ("Separator", xmSeparatorGadgetClass, w, NULL);
    for (const char *n : { "OK", "Apply", "Cancel", "Help" }) {
      Widget pb = XtVaCreateManagedWidget (n, xmPushButtonGadgetClass, w, NULL);
      XtAddCallback (pb, XmNactivateCallback, sb_button, nullptr);
    }
    if (fsb) {
      XmString f = XmStringCreateLocalized ((char *) "Filter");
      XtVaSetValues (child (w, "Apply"), XmNlabelString, (XtArgVal) f, NULL);
      XmStringFree (f);
    }
    if (type == XmDIALOG_PROMPT || type == XmDIALOG_WORK_AREA) XtUnmanageChild (child (w, "Apply"));
    make_default (w, child (w, "OK"));
    if (const ResDef *d = mq_find_res (w, XmNcancelButton)) mq_store (w, d, (XtArgVal) child (w, "Cancel"));
  }
  sb_update (w, QByteArray ());
  if (cmd) {
    Widget l = list_of (w, "ItemsList");
    XmStringTable h = (XmStringTable) mq_get (w, XmNhistoryItems);
    if (l && h) XtVaSetValues (l, XmNitems, (XtArgVal) h, XmNitemCount, mq_get (w, XmNhistoryItemCount), NULL);
    if (l && mq_isset (w, XmNhistoryVisibleItemCount)) XtVaSetValues (l, XmNvisibleItemCount, mq_get (w, XmNhistoryVisibleItemCount), NULL);
    if (mq_get (w, XmNpromptString)) XtVaSetValues (child (w, "Selection"), XmNlabelString, mq_get (w, XmNpromptString), NULL);
  }
  if (fsb) {
    Widget dl = list_of (w, "DirList");
    XtAddCallback (dl, XmNdefaultActionCallback, [] (Widget, XtPointer box_ptr, XtPointer call) {
      Widget box = (Widget) box_ptr;
      auto *cs = (XmListCallbackStruct *) call;
      QString dir = QDir::cleanPath (mq_xmstring_text (cs->item));
      XmString pat = (XmString) mq_get (box, XmNpattern);
      QString mask = dir + "/" + (pat ? mq_xmstring_text (pat) : QString ("*"));
      XmString m = mq_xmstring_from (mask);
      mq_fsb_search (box, m);
      XmStringFree (m);
    }, w);
    Widget filter = child (w, "FilterText");
    XtAddCallback (filter, XmNactivateCallback, [] (Widget t, XtPointer, XtPointer) {
      Widget box = R (t)->parent;
      char *s = XmTextFieldGetString (t);
      XmString m = XmStringCreateLocalized (s);
      XtFree (s);
      mq_fsb_search (box, m);
      XmStringFree (m);
    }, nullptr);
    XmString mask = (XmString) mq_get (w, XmNdirMask);
    mq_fsb_search (w, mask);
  }
  sb_arrange (w);
}

/* ---------- file selection ---------- */
static QString fsb_dir (Widget w)
{
  XmString d = (XmString) mq_get (w, XmNdirectory);
  QString s = d ? mq_xmstring_text (d) : QString ();
  if (s.isEmpty ()) s = QDir::currentPath ();
  return s;
}

void mq_fsb_search (Widget w, XmString mask_str)
{
  /* split the mask into directory and pattern */
  QString mask = mask_str ? mq_xmstring_text (mask_str) : QString ();
  QString dir, pattern;
  if (mask.isEmpty ()) {
    dir = fsb_dir (w);
    XmString p = (XmString) mq_get (w, XmNpattern);
    pattern = p ? mq_xmstring_text (p) : QString ("*");
  } else {
    if (mask.startsWith ('~')) mask = QDir::homePath () + mask.mid (1);
    if (QFileInfo (mask).isDir ()) { dir = mask; pattern = "*"; }
    else {
      int slash = mask.lastIndexOf ('/');
      dir = slash >= 0 ? mask.left (slash) : fsb_dir (w);
      pattern = slash >= 0 ? mask.mid (slash + 1) : mask;
      if (dir.isEmpty ()) dir = "/";
      if (!QDir::isAbsolutePath (dir)) dir = QDir (fsb_dir (w)).filePath (dir);
    }
    if (pattern.isEmpty ()) pattern = "*";
  }
  dir = QDir::cleanPath (dir);
  if (!dir.endsWith ('/')) dir += '/';
  QDir d (dir);
  bool valid = d.exists () && d.isReadable ();
  /* store directory, pattern, dirMask */
  auto store = [w] (const char *res, const QString &v) {
    if (const ResDef *def = mq_find_res (w, res)) { XmString s = mq_xmstring_from (v); mq_store (w, def, (XtArgVal) s); XmStringFree (s); }
  };
  store (XmNdirectory, dir);
  store (XmNpattern, pattern);
  store (XmNdirMask, dir + pattern);
  if (Widget ft = child (w, "FilterText")) { QByteArray b = (dir + pattern).toLocal8Bit (); XmTextFieldSetString (ft, b.data ()); }
  Widget dl = list_of (w, "DirList"), fl = list_of (w, "ItemsList");
  if (dl) XmListDeleteAllItems (dl);
  if (fl) XmListDeleteAllItems (fl);
  if (valid) {
    QStringList dirs = d.entryList (QDir::Dirs | QDir::NoDot | QDir::Hidden, QDir::Name);
    QStringList show_dirs;
    show_dirs << dir + ".";
    for (const QString &s : dirs) {
      if (s.startsWith ('.') && s != ".." && !mq_getb (w, XmNfileFilterStyle)) { /* Motif lists dot dirs too */ }
      show_dirs << dir + s;
    }
    for (const QString &s : show_dirs) { XmString x = mq_xmstring_from (s); if (dl) XmListAddItemUnselected (dl, x, 0); XmStringFree (x); }
    int types = mq_geti (w, XmNfileTypeMask);
    QDir::Filters f = QDir::NoDotAndDotDot;
    if (types == XmFILE_DIRECTORY) f |= QDir::Dirs;
    else if (types == XmFILE_ANY_TYPE) f |= QDir::Dirs | QDir::Files;
    else f |= QDir::Files;
    QStringList files = d.entryList ({ pattern }, f, QDir::Name);
    for (const QString &s : files) {
      if (s.startsWith ('.') && !pattern.startsWith ('.')) continue;
      XmString x = mq_xmstring_from (dir + s);
      if (fl) XmListAddItemUnselected (fl, x, 0);
      XmStringFree (x);
    }
  }
  if (const ResDef *def = mq_find_res (w, XmNdirectoryValid)) mq_store (w, def, valid);
  if (const ResDef *def = mq_find_res (w, XmNlistUpdated)) mq_store (w, def, True);
  if (child (w, "Text")) sb_set_text (w, dir);
}

static void fsb_apply (Widget w, const QByteArray &res)
{
  sb_apply (w, res);
  if (!child (w, "Text")) return;
  if (res == XmNdirectory || res == XmNpattern) {
    XmString p = (XmString) mq_get (w, XmNpattern);
    QString m = fsb_dir (w) + "/" + (p ? mq_xmstring_text (p) : QString ("*"));
    XmString x = mq_xmstring_from (m);
    mq_fsb_search (w, x);
    XmStringFree (x);
  } else if (res == XmNdirMask) {
    XmString m = XmStringCopy ((XmString) mq_get (w, XmNdirMask));
    mq_fsb_search (w, m);
    XmStringFree (m);
  }
}

static bool fsb_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  if (res == XmNdirSpec) { *out = (XtArgVal) mq_xmstring_from (sb_text (w)); return true; }
  if (res == XmNdirectory || res == XmNpattern || res == XmNdirMask) {
    XmString s = (XmString) mq_get (w, res.constData ());
    *out = (XtArgVal) (s ? XmStringCopy (s) : nullptr);
    return true;
  }
  return sb_fetch (w, res, out);
}

static void sb_relayout (Widget w) { sb_arrange (w); }
static void mb_relayout (Widget w) { mb_arrange (w); }

static QObject *box_create (Widget) { return new QWidget; }

/* ======================================================================
 * Class records
 * ====================================================================== */
MqXtClassRec xmMessageBoxClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmMessageBoxWidgetClass, mq_mb_rec, xmMessageBoxClassRec, "XmMessageBox", &mq_bb_rec, MGRK, box_create, mb_apply, nullptr, box_place, box_unplace, mb_relayout, mb_initialize)
MqXtClassRec xmSelectionBoxClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmSelectionBoxWidgetClass, mq_sb_rec, xmSelectionBoxClassRec, "XmSelectionBox", &mq_bb_rec, MGRK, box_create, sb_apply, sb_fetch, box_place, box_unplace, sb_relayout, sb_initialize)
MqXtClassRec xmFileSelectionBoxClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmFileSelectionBoxWidgetClass, mq_fsb_rec, xmFileSelectionBoxClassRec, "XmFileSelectionBox", &mq_sb_rec, MGRK, box_create, fsb_apply, fsb_fetch, box_place, box_unplace, sb_relayout, sb_initialize)
MqXtClassRec xmCommandClassRec;   /* Motif's name: programs subclass it */
MQ_DEFINE_CLASS_XT (xmCommandWidgetClass, mq_command_rec, xmCommandClassRec, "XmCommand", &mq_sb_rec, MGRK, box_create, sb_apply, sb_fetch, box_place, box_unplace, sb_relayout, sb_initialize)

/* ======================================================================
 * Creation functions
 * ====================================================================== */
static Widget dialog_shell_for (Widget parent, const char *name)
{
  QByteArray sn = QByteArray (name ? name : "") + "_popup";
  return XtVaCreatePopupShell (sn.constData (), xmDialogShellWidgetClass, parent, XmNallowShellResize, True, NULL);
}

static Widget dialog (Widget parent, const char *name, ArgList args, Cardinal n, WidgetClass cls, int type)
{
  Widget shell = dialog_shell_for (parent, name);
  QVector<Arg> a (args, args + n);
  if (type >= 0) { Arg t; XtSetArg (t, XmNdialogType, type); a.prepend (t); }
  return XtCreateWidget (name, cls, shell, a.data (), a.size ());
}

static Widget box (Widget parent, const char *name, ArgList args, Cardinal n, WidgetClass cls, int type)
{
  QVector<Arg> a (args, args + n);
  if (type >= 0) { Arg t; XtSetArg (t, XmNdialogType, type); a.prepend (t); }
  return XtCreateWidget (name, cls, parent, a.data (), a.size ());
}

extern "C" {

Widget XmCreateDialogShell (Widget p, char *name, ArgList a, Cardinal n) { return XtCreatePopupShell (name, xmDialogShellWidgetClass, p, a, n); }

Widget XmCreateMessageBox (Widget p, char *name, ArgList a, Cardinal n) { return box (p, name, a, n, xmMessageBoxWidgetClass, -1); }
Widget XmCreateMessageDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_MESSAGE); }
Widget XmCreateErrorDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_ERROR); }
Widget XmCreateInformationDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_INFORMATION); }
Widget XmCreateQuestionDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_QUESTION); }
Widget XmCreateWarningDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_WARNING); }
Widget XmCreateWorkingDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_WORKING); }
Widget XmCreateTemplateDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmMessageBoxWidgetClass, XmDIALOG_TEMPLATE); }

Widget XmCreateSelectionBox (Widget p, char *name, ArgList a, Cardinal n) { return box (p, name, a, n, xmSelectionBoxWidgetClass, -1); }
Widget XmCreateSelectionDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmSelectionBoxWidgetClass, XmDIALOG_SELECTION); }
Widget XmCreatePromptDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmSelectionBoxWidgetClass, XmDIALOG_PROMPT); }
Widget XmCreateFileSelectionBox (Widget p, char *name, ArgList a, Cardinal n) { return box (p, name, a, n, xmFileSelectionBoxWidgetClass, -1); }
Widget XmCreateFileSelectionDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmFileSelectionBoxWidgetClass, -1); }
Widget XmCreateCommand (Widget p, char *name, ArgList a, Cardinal n) { return box (p, name, a, n, xmCommandWidgetClass, -1); }
Widget XmCreateCommandDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmCommandWidgetClass, -1); }

Widget XmCreateFormDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmFormWidgetClass, -1); }
Widget XmCreateBulletinBoardDialog (Widget p, char *name, ArgList a, Cardinal n) { return dialog (p, name, a, n, xmBulletinBoardWidgetClass, -1); }

Widget XmMessageBoxGetChild (Widget w, unsigned char which)
{
  switch (which) {
  case XmDIALOG_SYMBOL_LABEL: return child (w, "Symbol");
  case XmDIALOG_MESSAGE_LABEL: return child (w, "Message");
  case XmDIALOG_OK_BUTTON: return child (w, "OK");
  case XmDIALOG_CANCEL_BUTTON: return child (w, "Cancel");
  case XmDIALOG_HELP_BUTTON: return child (w, "Help");
  case XmDIALOG_SEPARATOR: return child (w, "Separator");
  case XmDIALOG_DEFAULT_BUTTON: return (Widget) mq_get (w, XmNdefaultButton);
  default: return nullptr;
  }
}

Widget XmSelectionBoxGetChild (Widget w, unsigned char which)
{
  switch (which) {
  case XmDIALOG_LIST: return list_of (w, "ItemsList");
  case XmDIALOG_LIST_LABEL: return child (w, "Items");
  case XmDIALOG_SELECTION_LABEL: return child (w, "Selection");
  case XmDIALOG_TEXT: return child (w, "Text");
  case XmDIALOG_OK_BUTTON: return child (w, "OK");
  case XmDIALOG_APPLY_BUTTON: return child (w, "Apply");
  case XmDIALOG_CANCEL_BUTTON: return child (w, "Cancel");
  case XmDIALOG_HELP_BUTTON: return child (w, "Help");
  case XmDIALOG_SEPARATOR: return child (w, "Separator");
  case XmDIALOG_DEFAULT_BUTTON: return (Widget) mq_get (w, XmNdefaultButton);
  case XmDIALOG_WORK_AREA: { QList<Widget> k = work_children (w); return k.isEmpty () ? nullptr : k.first (); }
  default: return nullptr;
  }
}

Widget XmFileSelectionBoxGetChild (Widget w, unsigned char which)
{
  switch (which) {
  case XmDIALOG_DIR_LIST: return list_of (w, "DirList");
  case XmDIALOG_DIR_LIST_LABEL: return child (w, "Dir");
  case XmDIALOG_FILTER_LABEL: return child (w, "FilterLabel");
  case XmDIALOG_FILTER_TEXT: return child (w, "FilterText");
  default: return XmSelectionBoxGetChild (w, which);
  }
}

Widget XmCommandGetChild (Widget w, unsigned char which)
{
  switch (which) {
  case XmDIALOG_COMMAND_TEXT: return child (w, "Text");
  case XmDIALOG_PROMPT_LABEL: return child (w, "Selection");
  case XmDIALOG_HISTORY_LIST: return list_of (w, "ItemsList");
  case XmDIALOG_WORK_AREA: { QList<Widget> k = work_children (w); return k.isEmpty () ? nullptr : k.first (); }
  default: return nullptr;
  }
}

void XmFileSelectionDoSearch (Widget w, XmString mask) { mq_fsb_search (w, mask); }

void XmCommandSetValue (Widget w, XmString value) { sb_set_text (w, value ? mq_xmstring_text (value) : QString ()); }

void XmCommandAppendValue (Widget w, XmString value)
{
  if (!value) return;
  sb_set_text (w, sb_text (w) + mq_xmstring_text (value));
}

void XmCommandError (Widget w, XmString error)
{
  Widget l = list_of (w, "ItemsList");
  if (l && error) { XmListAddItemUnselected (l, error, 0); XmListSetBottomPos (l, 0); }
}

} /* extern "C" */
