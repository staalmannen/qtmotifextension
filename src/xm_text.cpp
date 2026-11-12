/*
 * Motif on Qt - XmText, XmTextField, XmList, XmScale, XmScrollBar,
 * XmComboBox, XmSpinBox and XmSimpleSpinBox.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstring>

#define PRIMK (K_RECT | K_WIDGET | K_PRIMITIVE)
#define MGRK (K_RECT | K_WIDGET | K_COMPOSITE | K_CONSTRAINT | K_MANAGER)

static QByteArray local (const QString &s) { return s.toLocal8Bit (); }
static QString qstr (const char *s) { return s ? QString::fromLocal8Bit (s) : QString (); }

/* ======================================================================
 * XmText / XmTextField
 * ====================================================================== */
struct TextExt {
  QString shadow;          /* text as the program last saw it */
  bool busy = false;       /* inside a change we started */
  int redisplay_off = 0;
};

class MqLineEdit : public QLineEdit {
public:
  Widget w;
  explicit MqLineEdit (Widget wd) : w (wd) {}
  QSize sizeHint () const override
  {
    QSize s = QLineEdit::sizeHint ();
    int cols = mq_geti (w, XmNcolumns);
    if (cols <= 0) cols = 20;
    s.setWidth (fontMetrics ().averageCharWidth () * cols + 2 * mq_geti (w, XmNmarginWidth) + 8);
    return s;
  }
  QSize minimumSizeHint () const override { return QSize (fontMetrics ().averageCharWidth () * 2 + 8, QLineEdit::minimumSizeHint ().height ()); }
};

static void text_activate (Widget w);

class MqTextEdit : public QPlainTextEdit {
public:
  Widget w;
  explicit MqTextEdit (Widget wd) : w (wd) {}
  QSize sizeHint () const override
  {
    int cols = mq_geti (w, XmNcolumns), rows = mq_geti (w, XmNrows);
    if (cols <= 0) cols = 20;
    if (rows <= 0) rows = 1;
    QFontMetrics fm = fontMetrics ();
    int fw = 2 * frameWidth () + 2 * (int) document ()->documentMargin ();
    int sb = verticalScrollBarPolicy () == Qt::ScrollBarAlwaysOn ? verticalScrollBar ()->sizeHint ().width () : 0;
    int hb = horizontalScrollBarPolicy () == Qt::ScrollBarAlwaysOn ? horizontalScrollBar ()->sizeHint ().height () : 0;
    return QSize (fm.averageCharWidth () * cols + fw + sb + 2 * mq_geti (w, XmNmarginWidth), fm.lineSpacing () * rows + fw + hb + 2 * mq_geti (w, XmNmarginHeight));
  }
  QSize minimumSizeHint () const override { return QSize (40, fontMetrics ().lineSpacing () + 2 * frameWidth () + 8); }
protected:
  void keyPressEvent (QKeyEvent *e) override
  {
    if ((e->key () == Qt::Key_Return || e->key () == Qt::Key_Enter) && mq_geti (w, XmNeditMode) == XmSINGLE_LINE_EDIT) {
      text_activate (w);
      return;
    }
    QPlainTextEdit::keyPressEvent (e);
  }
};

static QString text_get (Widget w)
{
  if (auto *l = mq_q<QLineEdit> (w)) return l->text ();
  if (auto *t = mq_q<QPlainTextEdit> (w)) return t->toPlainText ();
  return QString ();
}

static void text_set_raw (Widget w, const QString &s)
{
  TextExt *x = mq_ext<TextExt> (w);
  x->busy = true;
  if (auto *l = mq_q<QLineEdit> (w)) l->setText (s);
  else if (auto *t = mq_q<QPlainTextEdit> (w)) t->setPlainText (s);
  x->shadow = s;
  x->busy = false;
}

static int text_cursor (Widget w)
{
  if (auto *l = mq_q<QLineEdit> (w)) return l->cursorPosition ();
  if (auto *t = mq_q<QPlainTextEdit> (w)) return t->textCursor ().position ();
  return 0;
}

static void text_set_cursor (Widget w, int pos)
{
  if (auto *l = mq_q<QLineEdit> (w)) l->setCursorPosition (pos);
  else if (auto *t = mq_q<QPlainTextEdit> (w)) {
    QTextCursor c = t->textCursor ();
    c.setPosition (qBound (0, pos, (int) t->toPlainText ().size ()));
    t->setTextCursor (c);
  }
}

static void value_changed (Widget w)
{
  XmAnyCallbackStruct cs = { XmCR_VALUE_CHANGED, mq_current_event () };
  mq_call (w, XmNvalueChangedCallback, &cs);
}

/* Run the modify-verify callbacks for replacing [start, end) by text.
 * Returns false if the change was refused; may rewrite text. */
static bool verify (Widget w, int start, int end, QString *text)
{
  if (!mq_has_callbacks (w, XmNmodifyVerifyCallback)) return true;
  QByteArray b = local (*text);
  XmTextBlockRec block;
  block.ptr = b.isEmpty () ? nullptr : XtNewString (b.constData ());
  block.length = b.size ();
  block.format = XmFMT_8_BIT;
  XmTextVerifyCallbackStruct cs;
  memset (&cs, 0, sizeof cs);
  cs.reason = XmCR_MODIFYING_TEXT_VALUE;
  cs.event = mq_current_event ();
  cs.doit = True;
  cs.currInsert = text_cursor (w);
  cs.newInsert = start + text->size ();
  cs.startPos = start;
  cs.endPos = end;
  cs.text = &block;
  mq_call (w, XmNmodifyVerifyCallback, &cs);
  bool ok = cs.doit;
  if (ok) *text = cs.text->ptr ? QString::fromLocal8Bit (cs.text->ptr, cs.text->length) : QString ();
  XtFree (block.ptr == cs.text->ptr ? block.ptr : cs.text->ptr);
  if (block.ptr != cs.text->ptr) XtFree (block.ptr);
  return ok;
}

/* the user changed the text: verify after the fact, revert or adjust */
static void text_changed (Widget w)
{
  TextExt *x = mq_ext<TextExt> (w);
  if (x->busy) return;
  QString now = text_get (w), old = x->shadow;
  if (now == old) return;
  int pre = 0;
  while (pre < now.size () && pre < old.size () && now[pre] == old[pre]) pre++;
  int suf = 0;
  while (suf < now.size () - pre && suf < old.size () - pre && now[now.size () - 1 - suf] == old[old.size () - 1 - suf]) suf++;
  QString inserted = now.mid (pre, now.size () - pre - suf);
  QString want = inserted;
  int cursor = text_cursor (w);
  if (!verify (w, pre, old.size () - suf, &want)) {
    QApplication::beep ();
    text_set_raw (w, old);
    text_set_cursor (w, pre);
    return;
  }
  if (want != inserted) {
    QString fixed = old.left (pre) + want + old.mid (old.size () - suf);
    text_set_raw (w, fixed);
    text_set_cursor (w, pre + want.size ());
  } else {
    x->shadow = now;
    Q_UNUSED (cursor);
  }
  value_changed (w);
}

/* programmatic replace: verify first, like Motif */
static void text_replace (Widget w, int from, int to, const QString &s, bool notify = true)
{
  QString old = text_get (w);
  from = qBound (0, from, (int) old.size ());
  to = qBound (from, to, (int) old.size ());
  QString ins = s;
  if (notify && !verify (w, from, to, &ins)) return;
  int max = mq_geti (w, XmNmaxLength);
  QString now = old.left (from) + ins + old.mid (to);
  if (max > 0 && now.size () > max) now.truncate (max);
  int cursor = text_cursor (w);
  text_set_raw (w, now);
  text_set_cursor (w, cursor <= from ? cursor : cursor + ins.size () - (to - from));
  if (notify) value_changed (w);
}

/* Motif's TextField activate(): run activateCallback, then let the parent
 * manager activate its default button (BulletinBoard dialogs). */
static void text_activate (Widget w)
{
  XmAnyCallbackStruct cs = { XmCR_ACTIVATE, mq_current_event () };
  mq_call (w, XmNactivateCallback, &cs);
  if (w->destroying) return;
  for (Widget p = w->parent; p; p = p->parent) {
    if (!mq_find_res (p, XmNdefaultButton)) { if (p->klass->kind & K_SHELL) break; continue; }
    Widget def = (Widget) mq_get (p, XmNdefaultButton);
    if (!def) continue;
    if (auto *b = mq_q<QAbstractButton> (def)) if (b->isEnabled () && b->isVisible ()) b->click ();
    break;
  }
}

static bool is_multi (Widget w)
{
  return mq_class_is (w, "XmText") && mq_geti (w, XmNeditMode) == XmMULTI_LINE_EDIT;
}

class FocusWatch : public QObject {
public:
  Widget w;
  explicit FocusWatch (Widget wd) : w (wd) {}
  bool eventFilter (QObject *, QEvent *e) override
  {
    if (e->type () == MQ_QFocusIn) {
      XmAnyCallbackStruct cs = { XmCR_FOCUS, mq_current_event () };
      mq_call (w, XmNfocusCallback, &cs);
    } else if (e->type () == MQ_QFocusOut) {
      XmTextVerifyCallbackStruct cs;
      memset (&cs, 0, sizeof cs);
      cs.reason = XmCR_LOSING_FOCUS;
      cs.event = mq_current_event ();
      cs.doit = True;
      cs.currInsert = cs.newInsert = text_cursor (w);
      mq_call (w, XmNlosingFocusCallback, &cs);
    }
    return false;
  }
};

static QObject *text_create (Widget w)
{
  QWidget *qw;
  if (is_multi (w)) {
    auto *t = new MqTextEdit (w);
    t->setLineWrapMode (mq_getb (w, XmNwordWrap) ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    t->setTabChangesFocus (false);
    QObject::connect (t, &QPlainTextEdit::textChanged, t, [w] () { text_changed (w); });
    QObject::connect (t, &QPlainTextEdit::cursorPositionChanged, t, [w] () {
      if (mq_has_callbacks (w, XmNmotionVerifyCallback) && !mq_ext<TextExt> (w)->busy) {
        XmTextVerifyCallbackStruct cs;
        memset (&cs, 0, sizeof cs);
        cs.reason = XmCR_MOVING_INSERT_CURSOR;
        cs.event = mq_current_event ();
        cs.doit = True;
        cs.newInsert = text_cursor (w);
        mq_call (w, XmNmotionVerifyCallback, &cs);
      }
    });
    /* a text inside an application-defined scrolled window scrolls itself */
    bool scrolled = w->parent && mq_class_is (w->parent, "XmScrolledWindow") && mq_geti (w->parent, XmNscrollingPolicy) != XmAUTOMATIC;
    t->setVerticalScrollBarPolicy (scrolled && mq_getb (w, XmNscrollVertical) ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    t->setHorizontalScrollBarPolicy (scrolled && mq_getb (w, XmNscrollHorizontal) && !mq_getb (w, XmNwordWrap) ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    qw = t;
  } else {
    auto *l = new MqLineEdit (w);
    QObject::connect (l, &QLineEdit::textChanged, l, [w] () { text_changed (w); });
    QObject::connect (l, &QLineEdit::returnPressed, l, [w] () { text_activate (w); });
    qw = l;
  }
  auto *fw = new FocusWatch (w);
  fw->setParent (qw);
  qw->installEventFilter (fw);
  return qw;
}

static void text_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  bool all = res.isEmpty ();
  if (all || res == XmNvalue) {
    const char *v = (const char *) mq_get (w, XmNvalue);
    if (!all || v) {
      QString s = qstr (v);
      if (!all) { QString t = s; if (verify (w, 0, text_get (w).size (), &t)) s = t; else return; }
      text_set_raw (w, s);
      if (!all) value_changed (w);
    }
  }
  if (all || res == XmNeditable) {
    bool ed = mq_getb (w, XmNeditable);
    if (auto *l = mq_q<QLineEdit> (w)) l->setReadOnly (!ed);
    if (auto *t = mq_q<QPlainTextEdit> (w)) t->setReadOnly (!ed);
  }
  if (all || res == XmNmaxLength)
    if (auto *l = mq_q<QLineEdit> (w)) { int m = mq_geti (w, XmNmaxLength); if (m > 0 && m < 32767) l->setMaxLength (m); }
  if (all || res == XmNcursorPosition) if (!all || mq_isset (w, XmNcursorPosition)) text_set_cursor (w, mq_geti (w, XmNcursorPosition));
  if (all || res == XmNcursorPositionVisible)
    if (auto *t = mq_q<QPlainTextEdit> (w)) t->setCursorWidth (mq_getb (w, XmNcursorPositionVisible) ? 1 : 0);
  if (res == XmNcolumns || res == XmNrows) if (QWidget *qw = mq_qw (w)) qw->updateGeometry ();
  if (res == XmNwordWrap) if (auto *t = mq_q<QPlainTextEdit> (w)) t->setLineWrapMode (mq_getb (w, XmNwordWrap) ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
  if (all || res == XmNbackground) {
    if (mq_isset (w, XmNbackground) && mq_qw (w)) {
      QPalette p = mq_qw (w)->palette ();
      p.setColor (QPalette::Base, mq_color (mq_get (w, XmNbackground)));
      mq_qw (w)->setPalette (p);
    }
  }
}

static bool text_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  if (res == XmNvalue) { *out = (XtArgVal) XtNewString (local (text_get (w)).constData ()); return true; }
  if (res == XmNcursorPosition) { *out = text_cursor (w); return true; }
  return false;
}

/* ======================================================================
 * XmList
 * ====================================================================== */
struct ListExt {
  QVector<XmString> items;     /* owned */
  QVector<XmString> selected;  /* owned copies, refreshed on change */
  QVector<int> sel_pos;
  bool busy = false;
  ~ListExt () { for (XmString s : items) XmStringFree (s); for (XmString s : selected) XmStringFree (s); }
};

class MqListWidget : public QListWidget {
public:
  Widget w;
  explicit MqListWidget (Widget wd) : w (wd) {}
  QSize sizeHint () const override
  {
    int vis = mq_geti (w, XmNvisibleItemCount);
    if (vis <= 0) vis = 1;
    int rowh = sizeHintForRow (0) > 0 ? sizeHintForRow (0) : fontMetrics ().lineSpacing () + 2;
    int width = sizeHintForColumn (0) > 0 ? sizeHintForColumn (0) : fontMetrics ().averageCharWidth () * 10;
    int sbw = verticalScrollBarPolicy () == Qt::ScrollBarAlwaysOn ? verticalScrollBar ()->sizeHint ().width () : 0;
    return QSize (width + 2 * frameWidth () + sbw + 4 + 2 * mq_geti (w, XmNlistMarginWidth),
                  rowh * vis + 2 * frameWidth () + 2 * mq_geti (w, XmNlistMarginHeight));
  }
  QSize minimumSizeHint () const override { return QSize (30, fontMetrics ().lineSpacing () + 2 * frameWidth ()); }
};

static void list_refresh_selection (Widget w)
{
  ListExt *x = mq_ext<ListExt> (w);
  auto *l = mq_q<QListWidget> (w);
  for (XmString s : x->selected) XmStringFree (s);
  x->selected.clear ();
  x->sel_pos.clear ();
  if (!l) return;
  for (int i = 0; i < l->count (); i++)
    if (l->item (i)->isSelected ()) { x->selected << XmStringCopy (x->items.value (i)); x->sel_pos << i + 1; }
}

static void list_sync (Widget w)
{
  ListExt *x = mq_ext<ListExt> (w);
  auto *l = mq_q<QListWidget> (w);
  if (!l) return;
  x->busy = true;
  l->clear ();
  for (XmString s : x->items) l->addItem (mq_xmstring_text (s));
  x->busy = false;
  l->updateGeometry ();
}

static void list_select_pos (Widget w, int pos, bool on)
{
  auto *l = mq_q<QListWidget> (w);
  ListExt *x = mq_ext<ListExt> (w);
  if (!l || pos < 1 || pos > l->count ()) return;
  x->busy = true;
  int policy = mq_geti (w, XmNselectionPolicy);
  if (on && (policy == XmSINGLE_SELECT || policy == XmBROWSE_SELECT)) l->clearSelection ();
  l->item (pos - 1)->setSelected (on);
  if (on) l->setCurrentRow (pos - 1, QItemSelectionModel::NoUpdate);
  x->busy = false;
  list_refresh_selection (w);
}

static void list_callback (Widget w, int reason, int pos)
{
  ListExt *x = mq_ext<ListExt> (w);
  list_refresh_selection (w);
  XmListCallbackStruct cs;
  memset (&cs, 0, sizeof cs);
  cs.reason = reason;
  cs.event = mq_current_event ();
  XmString item = pos >= 1 && pos <= x->items.size () ? XmStringCopy (x->items[pos - 1]) : nullptr;
  cs.item = item;
  cs.item_length = item ? XmStringLength (item) : 0;
  cs.item_position = pos;
  cs.selected_items = x->selected.data ();
  cs.selected_item_count = x->selected.size ();
  cs.selected_item_positions = x->sel_pos.data ();
  cs.selection_type = XmINITIAL;
  const char *cb = reason == XmCR_SINGLE_SELECT ? XmNsingleSelectionCallback : reason == XmCR_MULTIPLE_SELECT ? XmNmultipleSelectionCallback
                 : reason == XmCR_EXTENDED_SELECT ? XmNextendedSelectionCallback : reason == XmCR_BROWSE_SELECT ? XmNbrowseSelectionCallback
                 : XmNdefaultActionCallback;
  mq_call (w, cb, &cs);
  XmStringFree (item);
}

static int policy_reason (Widget w)
{
  switch (mq_geti (w, XmNselectionPolicy)) {
  case XmSINGLE_SELECT: return XmCR_SINGLE_SELECT;
  case XmMULTIPLE_SELECT: return XmCR_MULTIPLE_SELECT;
  case XmEXTENDED_SELECT: return XmCR_EXTENDED_SELECT;
  default: return XmCR_BROWSE_SELECT;
  }
}

static void list_policy (Widget w)
{
  auto *l = mq_q<QListWidget> (w);
  if (!l) return;
  switch (mq_geti (w, XmNselectionPolicy)) {
  case XmMULTIPLE_SELECT: l->setSelectionMode (QAbstractItemView::MultiSelection); break;
  case XmEXTENDED_SELECT: l->setSelectionMode (QAbstractItemView::ExtendedSelection); break;
  default: l->setSelectionMode (QAbstractItemView::SingleSelection); break;
  }
}

static QObject *list_create (Widget w)
{
  auto *l = new MqListWidget (w);
  bool scrolled = w->parent && mq_class_is (w->parent, "XmScrolledWindow");
  l->setVerticalScrollBarPolicy (scrolled ? (mq_geti (w, XmNscrollBarDisplayPolicy) == XmSTATIC ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAsNeeded) : Qt::ScrollBarAlwaysOff);
  l->setHorizontalScrollBarPolicy (scrolled ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
  l->setUniformItemSizes (true);
  QObject::connect (l, &QListWidget::itemClicked, l, [w, l] (QListWidgetItem *it) {
    if (mq_ext<ListExt> (w)->busy) return;
    list_callback (w, policy_reason (w), l->row (it) + 1);
  });
  QObject::connect (l, &QListWidget::currentRowChanged, l, [w, l] (int row) {
    /* keyboard navigation in browse mode selects */
    if (mq_ext<ListExt> (w)->busy || row < 0 || QApplication::mouseButtons ()) return;
    if (mq_geti (w, XmNselectionPolicy) == XmBROWSE_SELECT && l->hasFocus ()) list_callback (w, XmCR_BROWSE_SELECT, row + 1);
  });
  QObject::connect (l, &QListWidget::itemActivated, l, [w, l] (QListWidgetItem *it) {
    if (mq_ext<ListExt> (w)->busy) return;
    list_callback (w, XmCR_DEFAULT_ACTION, l->row (it) + 1);
  });
  return l;
}

static void list_set_items (Widget w, XmStringTable items, int count)
{
  ListExt *x = mq_ext<ListExt> (w);
  for (XmString s : x->items) XmStringFree (s);
  x->items.clear ();
  for (int i = 0; items && i < count; i++) x->items << XmStringCopy (items[i]);
  list_sync (w);
}

static void list_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  bool all = res.isEmpty ();
  if (all || res == XmNitems || res == XmNitemCount) {
    XmStringTable t = (XmStringTable) mq_get (w, XmNitems);
    int n = mq_geti (w, XmNitemCount);
    if (!all || t) list_set_items (w, t, n);
  }
  if (all || res == XmNselectionPolicy) list_policy (w);
  if (all || res == XmNselectedItems || res == XmNselectedItemCount) {
    XmStringTable t = (XmStringTable) mq_get (w, XmNselectedItems);
    int n = mq_geti (w, XmNselectedItemCount);
    if (t || !all) {
      if (auto *l = mq_q<QListWidget> (w)) { mq_ext<ListExt> (w)->busy = true; l->clearSelection (); mq_ext<ListExt> (w)->busy = false; }
      ListExt *x = mq_ext<ListExt> (w);
      for (int i = 0; t && i < n; i++)
        for (int p = 0; p < x->items.size (); p++)
          if (XmStringCompare (x->items[p], t[i])) list_select_pos (w, p + 1, true);
      list_refresh_selection (w);
    }
  }
  if (res == XmNvisibleItemCount) if (QWidget *qw = mq_qw (w)) qw->updateGeometry ();
  if (res == XmNtopItemPosition) if (auto *l = mq_q<QListWidget> (w)) if (l->item (mq_geti (w, XmNtopItemPosition) - 1))
    l->scrollToItem (l->item (mq_geti (w, XmNtopItemPosition) - 1), QAbstractItemView::PositionAtTop);
}

static bool list_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  ListExt *x = mq_ext<ListExt> (w);
  if (res == XmNitems) { *out = (XtArgVal) (x->items.isEmpty () ? nullptr : x->items.data ()); return true; }
  if (res == XmNitemCount) { *out = x->items.size (); return true; }
  if (res == XmNselectedItems) { list_refresh_selection (w); *out = (XtArgVal) (x->selected.isEmpty () ? nullptr : x->selected.data ()); return true; }
  if (res == XmNselectedItemCount) { list_refresh_selection (w); *out = x->selected.size (); return true; }
  if (res == XmNselectedPositions) { list_refresh_selection (w); *out = (XtArgVal) (x->sel_pos.isEmpty () ? nullptr : x->sel_pos.data ()); return true; }
  if (res == XmNselectedPositionCount) { list_refresh_selection (w); *out = x->sel_pos.size (); return true; }
  if (res == XmNtopItemPosition) {
    auto *l = mq_q<QListWidget> (w);
    QListWidgetItem *it = l ? l->itemAt (2, 2) : nullptr;
    *out = it ? l->row (it) + 1 : 1;
    return true;
  }
  return false;
}

/* ======================================================================
 * XmScale
 * ====================================================================== */
struct ScaleExt { QPointer<QSlider> slider; QPointer<QLabel> title, value; QPointer<QBoxLayout> ticks; };

static int scale_shown (Widget w, int v) { Q_UNUSED (w); return v; }

static QString scale_text (Widget w, int v)
{
  int d = mq_geti (w, XmNdecimalPoints);
  if (d <= 0) return QString::number (v);
  double f = v;
  for (int i = 0; i < d; i++) f /= 10;
  return QString::number (f, 'f', d);
}

static void scale_call (Widget w, const char *cb, int reason, int v)
{
  XmScaleCallbackStruct cs = { reason, mq_current_event (), v };
  mq_call (w, cb, &cs);
}

static QObject *scale_create (Widget w)
{
  auto *box = new QWidget;
  ScaleExt *x = mq_ext<ScaleExt> (w);
  x->slider = new QSlider;
  x->title = new QLabel;
  x->value = new QLabel;
  x->value->setAlignment (Qt::AlignCenter);
  QObject::connect (x->slider, &QSlider::sliderMoved, x->slider, [w] (int v) {
    if (QLabel *l = mq_ext<ScaleExt> (w)->value) l->setText (scale_text (w, v));
    if (const ResDef *d = mq_find_res (w, XmNvalue)) mq_store (w, d, v);
    scale_call (w, XmNdragCallback, XmCR_DRAG, scale_shown (w, v));
  });
  QObject::connect (x->slider, &QSlider::valueChanged, x->slider, [w] (int v) {
    ScaleExt *x = mq_ext<ScaleExt> (w);
    if (x->value) x->value->setText (scale_text (w, v));
    if (const ResDef *d = mq_find_res (w, XmNvalue)) mq_store (w, d, v);
    if (x->slider && !x->slider->isSliderDown () && !x->slider->signalsBlocked ()) scale_call (w, XmNvalueChangedCallback, XmCR_VALUE_CHANGED, v);
  });
  QObject::connect (x->slider, &QSlider::sliderReleased, x->slider, [w] () {
    scale_call (w, XmNvalueChangedCallback, XmCR_VALUE_CHANGED, mq_geti (w, XmNvalue));
  });
  return box;
}

static void scale_layout (Widget w)
{
  ScaleExt *x = mq_ext<ScaleExt> (w);
  QWidget *box = mq_qw (w);
  if (!box || !x->slider) return;
  bool vertical = mq_geti (w, XmNorientation) != XmHORIZONTAL;
  delete box->layout ();
  auto *outer = new QBoxLayout (vertical ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom, box);
  outer->setContentsMargins (0, 0, 0, 0);
  outer->setSpacing (2);
  auto *main = new QBoxLayout (vertical ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
  main->setSpacing (2);
  x->slider->setOrientation (vertical ? Qt::Vertical : Qt::Horizontal);
  if (vertical) x->value->setAlignment (Qt::AlignRight | Qt::AlignVCenter); else x->value->setAlignment (Qt::AlignCenter);
  x->ticks = new QBoxLayout (vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  main->addWidget (x->value);
  main->addWidget (x->slider, 1);
  main->addLayout (x->ticks);
  outer->addLayout (main, 1);
  outer->addWidget (x->title);
  if (vertical) outer->setAlignment (x->title, Qt::AlignBottom);
  int sw = mq_geti (w, XmNscaleWidth), sh = mq_geti (w, XmNscaleHeight);
  if (sw > 0) vertical ? x->slider->setMinimumWidth (sw) : x->slider->setMinimumWidth (sw);
  if (sh > 0) vertical ? x->slider->setMinimumHeight (sh) : x->slider->setMinimumHeight (sh);
  if (vertical && sh <= 0) x->slider->setMinimumHeight (100);
  if (!vertical && sw <= 0) x->slider->setMinimumWidth (100);
  for (Widget c : w->children) if (c->managed && mq_qw (c)) x->ticks->addWidget (mq_qw (c));
}

static void scale_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  ScaleExt *x = mq_ext<ScaleExt> (w);
  if (!x->slider) return;
  bool all = res.isEmpty ();
  if (all || res == XmNorientation || res == XmNscaleWidth || res == XmNscaleHeight) scale_layout (w);
  if (all || res == XmNminimum || res == XmNmaximum || res == XmNvalue || res == XmNprocessingDirection || res == XmNscaleMultiple) {
    QSignalBlocker b (x->slider);
    int mn = mq_geti (w, XmNminimum), mx = mq_geti (w, XmNmaximum);
    x->slider->setRange (mn, qMax (mn, mx));
    int pd = mq_geti (w, XmNprocessingDirection);
    bool vertical = mq_geti (w, XmNorientation) != XmHORIZONTAL;
    x->slider->setInvertedAppearance (vertical ? pd == XmMAX_ON_BOTTOM : pd == XmMAX_ON_LEFT);
    int mult = mq_geti (w, XmNscaleMultiple);
    x->slider->setPageStep (mult > 0 ? mult : qMax (1, (mx - mn) / 10));
    x->slider->setValue (mq_geti (w, XmNvalue));
    x->value->setText (scale_text (w, x->slider->value ()));
  }
  if (all || res == XmNshowValue) x->value->setVisible (mq_geti (w, XmNshowValue) != XmNONE && mq_geti (w, XmNshowValue) != 0);
  if (all || res == XmNtitleString) {
    XmString t = (XmString) mq_get (w, XmNtitleString);
    x->title->setText (t ? mq_xmstring_text (t) : QString ());
    x->title->setVisible (t != nullptr);
  }
  if (res == XmNdecimalPoints) x->value->setText (scale_text (w, x->slider->value ()));
}

static void scale_place (Widget parent, Widget child)
{
  ScaleExt *x = mq_ext<ScaleExt> (parent);
  QWidget *cw = mq_qw (child);
  if (!cw || !x->ticks) return;
  cw->setParent (mq_qw (parent));
  if (x->ticks->indexOf (cw) < 0) x->ticks->addWidget (cw);
  cw->show ();
}

/* ======================================================================
 * XmScrollBar
 * ====================================================================== */
static void sb_call (Widget w, const char *cb, int reason, int v)
{
  XmScrollBarCallbackStruct cs = { reason, mq_current_event (), v, 0 };
  if (strcmp (cb, XmNvalueChangedCallback) && strcmp (cb, XmNdragCallback) && !mq_has_callbacks (w, cb)) {
    cb = XmNvalueChangedCallback;
    cs.reason = XmCR_VALUE_CHANGED;
  }
  mq_call (w, cb, &cs);
}

static QObject *sb_create (Widget w)
{
  auto *s = new QScrollBar;
  QObject::connect (s, &QScrollBar::actionTriggered, s, [w, s] (int action) {
    /* runs before the value changes: defer to report the new value */
    QTimer::singleShot (0, s, [w, s, action] () {
      int v = s->value ();
      if (const ResDef *d = mq_find_res (w, XmNvalue)) mq_store (w, d, v);
      switch (action) {
      case QAbstractSlider::SliderSingleStepAdd: sb_call (w, XmNincrementCallback, XmCR_INCREMENT, v); break;
      case QAbstractSlider::SliderSingleStepSub: sb_call (w, XmNdecrementCallback, XmCR_DECREMENT, v); break;
      case QAbstractSlider::SliderPageStepAdd: sb_call (w, XmNpageIncrementCallback, XmCR_PAGE_INCREMENT, v); break;
      case QAbstractSlider::SliderPageStepSub: sb_call (w, XmNpageDecrementCallback, XmCR_PAGE_DECREMENT, v); break;
      case QAbstractSlider::SliderToMinimum: sb_call (w, XmNtoTopCallback, XmCR_TO_TOP, v); break;
      case QAbstractSlider::SliderToMaximum: sb_call (w, XmNtoBottomCallback, XmCR_TO_BOTTOM, v); break;
      case QAbstractSlider::SliderMove:
        if (s->isSliderDown ()) sb_call (w, XmNdragCallback, XmCR_DRAG, v);
        break;
      default: break;
      }
    });
  });
  QObject::connect (s, &QScrollBar::sliderReleased, s, [w, s] () {
    if (const ResDef *d = mq_find_res (w, XmNvalue)) mq_store (w, d, s->value ());
    sb_call (w, XmNvalueChangedCallback, XmCR_VALUE_CHANGED, s->value ());
  });
  return s;
}

static void sb_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  auto *s = mq_q<QScrollBar> (w);
  if (!s) return;
  QSignalBlocker b (s);
  int mn = mq_geti (w, XmNminimum), mx = mq_geti (w, XmNmaximum), size = mq_geti (w, XmNsliderSize);
  if (size <= 0) size = qMax (1, (mx - mn) / 10);
  s->setOrientation (mq_geti (w, XmNorientation) == XmHORIZONTAL ? Qt::Horizontal : Qt::Vertical);
  s->setRange (mn, qMax (mn, mx - size));
  s->setPageStep (size);
  s->setSingleStep (qMax (1, mq_geti (w, XmNincrement)));
  s->setValue (mq_geti (w, XmNvalue));
  int pd = mq_geti (w, XmNprocessingDirection);
  s->setInvertedAppearance (pd == XmMAX_ON_TOP || pd == XmMAX_ON_LEFT);
}

/* ======================================================================
 * XmComboBox
 * ====================================================================== */
struct ComboExt { QVector<XmString> items; bool busy = false; ~ComboExt () { for (XmString s : items) XmStringFree (s); } };

static bool zero_based (Widget w) { return mq_geti (w, XmNpositionMode) == XmZERO_BASED; }

static void combo_select_cb (Widget w)
{
  auto *c = mq_q<QComboBox> (w);
  if (!c) return;
  XmString item = mq_xmstring_from (c->currentText ());
  XmComboBoxCallbackStruct cs = { XmCR_SELECT, mq_current_event (), item, c->currentIndex () + (zero_based (w) ? 0 : 1) };
  if (const ResDef *d = mq_find_res (w, XmNselectedItem)) mq_store (w, d, (XtArgVal) item);
  if (const ResDef *d = mq_find_res (w, XmNselectedPosition)) mq_store (w, d, c->currentIndex () + (zero_based (w) ? 0 : 1));
  mq_call (w, XmNselectionCallback, &cs);
  XmStringFree (item);
}

static QObject *combo_create (Widget w)
{
  auto *c = new QComboBox;
  c->setEditable (mq_geti (w, XmNcomboBoxType) != XmDROP_DOWN_LIST);
  c->setInsertPolicy (QComboBox::NoInsert);
  QObject::connect (c, &QComboBox::activated, c, [w] (int) { if (!mq_ext<ComboExt> (w)->busy) combo_select_cb (w); });
  if (c->lineEdit ()) QObject::connect (c->lineEdit (), &QLineEdit::returnPressed, c, [w] () { combo_select_cb (w); });
  return c;
}

static void combo_sync (Widget w)
{
  auto *c = mq_q<QComboBox> (w);
  ComboExt *x = mq_ext<ComboExt> (w);
  if (!c) return;
  x->busy = true;
  QString keep = c->currentText ();
  c->clear ();
  for (XmString s : x->items) c->addItem (mq_xmstring_text (s));
  if (c->isEditable ()) c->setEditText (keep);
  x->busy = false;
}

static void combo_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  auto *c = mq_q<QComboBox> (w);
  ComboExt *x = mq_ext<ComboExt> (w);
  if (!c) return;
  bool all = res.isEmpty ();
  if (all || res == XmNitems || res == XmNitemCount) {
    XmStringTable t = (XmStringTable) mq_get (w, XmNitems);
    if (t || !all) {
      for (XmString s : x->items) XmStringFree (s);
      x->items.clear ();
      for (int i = 0; t && i < mq_geti (w, XmNitemCount); i++) x->items << XmStringCopy (t[i]);
      combo_sync (w);
    }
  }
  if (all || res == XmNvisibleItemCount) c->setMaxVisibleItems (qMax (1, mq_geti (w, XmNvisibleItemCount)));
  if (all || res == XmNselectedPosition) {
    int p = mq_geti (w, XmNselectedPosition) - (zero_based (w) ? 0 : 1);
    if (p >= 0 && p < c->count ()) { x->busy = true; c->setCurrentIndex (p); x->busy = false; }
  }
  if (all || res == XmNselectedItem) {
    XmString s = (XmString) mq_get (w, XmNselectedItem);
    if (s) { x->busy = true; QString t = mq_xmstring_text (s); int i = c->findText (t); if (i >= 0) c->setCurrentIndex (i); else if (c->isEditable ()) c->setEditText (t); x->busy = false; }
  }
  if (all || res == XmNcolumns) if (mq_geti (w, XmNcolumns) > 0) c->setMinimumContentsLength (mq_geti (w, XmNcolumns));
}

static bool combo_fetch (Widget w, const QByteArray &res, XtArgVal *out)
{
  ComboExt *x = mq_ext<ComboExt> (w);
  auto *c = mq_q<QComboBox> (w);
  if (res == XmNitems) { *out = (XtArgVal) (x->items.isEmpty () ? nullptr : x->items.data ()); return true; }
  if (res == XmNitemCount) { *out = x->items.size (); return true; }
  if (res == XmNselectedItem && c) { *out = (XtArgVal) mq_xmstring_from (c->currentText ()); return true; }
  if (res == XmNselectedPosition && c) { *out = c->currentIndex () + (zero_based (w) ? 0 : 1); return true; }
  return false;
}

/* ======================================================================
 * XmSpinBox / XmSimpleSpinBox
 * ====================================================================== */
struct SpinExt { QPointer<QToolButton> up, down; QPointer<QHBoxLayout> row; Widget last = nullptr; };

static Widget spin_holder (Widget box, Widget text) { return mq_class_is (box, "XmSimpleSpinBox") ? box : text; }

/* numeric children: position is the value (XmPOSITION_VALUE) or an index
 * into minimum, minimum + increment ... (XmPOSITION_INDEX) */
static bool by_index (Widget holder) { return mq_geti (holder, XmNpositionType) == XmPOSITION_INDEX; }

static QString spin_value_text (Widget holder, int pos)
{
  if (mq_geti (holder, XmNspinBoxChildType) == XmNUMERIC) {
    if (by_index (holder)) pos = mq_geti (holder, XmNminimumValue) + pos * qMax (1, mq_geti (holder, XmNincrementValue));
    int d = mq_geti (holder, XmNdecimalPoints);
    if (d <= 0) return QString::number (pos);
    double f = pos;
    for (int i = 0; i < d; i++) f /= 10;
    return QString::number (f, 'f', d);
  }
  XmStringTable v = (XmStringTable) mq_get (holder, XmNvalues);
  int n = mq_geti (holder, XmNnumValues);
  return v && pos >= 0 && pos < n ? mq_xmstring_text (v[pos]) : QString ();
}

static void spin_show (Widget box, Widget text)
{
  Widget h = spin_holder (box, text);
  if (!text) return;
  /* Motif keeps the position inside the child's range */
  int pos = mq_geti (h, XmNposition);
  bool numeric = mq_geti (h, XmNspinBoxChildType) == XmNUMERIC;
  int mn = numeric ? mq_geti (h, XmNminimumValue) : 0;
  int mx = numeric ? mq_geti (h, XmNmaximumValue) : qMax (0, mq_geti (h, XmNnumValues) - 1);
  if (numeric && by_index (h)) { mx = (mx - mn) / qMax (1, mq_geti (h, XmNincrementValue)); mn = 0; }
  if (pos < mn || pos > mx) {
    pos = qBound (mn, pos, qMax (mn, mx));
    if (const ResDef *d = mq_find_res (h, XmNposition)) mq_store (h, d, pos);
  }
  QString s = spin_value_text (h, pos);
  if (auto *l = mq_q<QLineEdit> (text)) { QSignalBlocker b (l); l->setText (s); }
  if (mq_class_is (text, "XmTextField") || mq_class_is (text, "XmText")) mq_ext<TextExt> (text)->shadow = s;
}

static Widget spin_text (Widget box)
{
  SpinExt *x = mq_ext<SpinExt> (box);
  if (x->last && x->last->parent == box && x->last->managed) return x->last;
  for (Widget c : box->children) if (c->managed && mq_class_is (c, "XmTextField")) return c;
  return nullptr;
}

static void spin_step (Widget box, int dir)
{
  Widget text = spin_text (box);
  if (!text) return;
  Widget h = spin_holder (box, text);
  int pos = mq_geti (h, XmNposition);
  bool numeric = mq_geti (h, XmNspinBoxChildType) == XmNUMERIC;
  int mn = numeric ? mq_geti (h, XmNminimumValue) : 0;
  int mx = numeric ? mq_geti (h, XmNmaximumValue) : qMax (0, mq_geti (h, XmNnumValues) - 1);
  int inc = numeric ? qMax (1, mq_geti (h, XmNincrementValue)) : 1;
  if (numeric && by_index (h)) { mx = (mx - mn) / inc; mn = 0; inc = 1; }
  int np = pos + dir * inc;
  bool crossed = false;
  if (np > mx) { if (mq_getb (h, XmNwrap)) { np = mn; crossed = true; } else np = mx; }
  if (np < mn) { if (mq_getb (h, XmNwrap)) { np = mx; crossed = true; } else np = mn; }
  XmString value = mq_xmstring_from (spin_value_text (h, np));
  XmSpinBoxCallbackStruct cs = { dir > 0 ? XmCR_SPIN_NEXT : XmCR_SPIN_PRIOR, mq_current_event (), text, True, np, value, crossed };
  mq_call (box, XmNmodifyVerifyCallback, &cs);
  if (cs.doit) {
    if (const ResDef *d = mq_find_res (h, XmNposition)) mq_store (h, d, cs.position);
    spin_show (box, text);
    cs.reason = XmCR_OK;
    mq_call (box, XmNvalueChangedCallback, &cs);
  }
  XmStringFree (value);
}

static QObject *spin_create (Widget w)
{
  auto *box = new QWidget;
  SpinExt *x = mq_ext<SpinExt> (w);
  auto *outer = new QHBoxLayout (box);
  outer->setContentsMargins (mq_geti (w, XmNmarginWidth), mq_geti (w, XmNmarginHeight), mq_geti (w, XmNmarginWidth), mq_geti (w, XmNmarginHeight));
  outer->setSpacing (0);
  x->row = new QHBoxLayout;
  x->row->setSpacing (2);
  outer->addLayout (x->row, 1);
  auto *arrows = new QVBoxLayout;
  arrows->setSpacing (0);
  x->up = new QToolButton;
  x->up->setArrowType (Qt::UpArrow);
  x->down = new QToolButton;
  x->down->setArrowType (Qt::DownArrow);
  for (QToolButton *b : { x->up.data (), x->down.data () }) {
    b->setAutoRepeat (true);
    b->setAutoRepeatDelay (mq_geti (w, XmNinitialDelay));
    b->setAutoRepeatInterval (qMax (20, mq_geti (w, XmNrepeatDelay)));
    b->setFixedSize (16, 12);
    b->setFocusPolicy (Qt::NoFocus);
  }
  arrows->addWidget (x->up);
  arrows->addWidget (x->down);
  outer->addLayout (arrows);
  QObject::connect (x->up, &QToolButton::clicked, box, [w] () { spin_step (w, 1); });
  QObject::connect (x->down, &QToolButton::clicked, box, [w] () { spin_step (w, -1); });
  return box;
}

class SpinKeys : public QObject {
public:
  Widget box, text;
  SpinKeys (Widget b, Widget t) : box (b), text (t) {}
  bool eventFilter (QObject *, QEvent *e) override
  {
    if (e->type () == MQ_QFocusIn) mq_ext<SpinExt> (box)->last = text;
    if (e->type () == MQ_QKeyPress) {
      auto *k = static_cast<QKeyEvent *> (e);
      if (k->key () == Qt::Key_Up) { mq_ext<SpinExt> (box)->last = text; spin_step (box, 1); return true; }
      if (k->key () == Qt::Key_Down) { mq_ext<SpinExt> (box)->last = text; spin_step (box, -1); return true; }
    }
    return false;
  }
};

static void spin_place (Widget parent, Widget child)
{
  SpinExt *x = mq_ext<SpinExt> (parent);
  QWidget *cw = mq_qw (child);
  if (!cw || !x->row) return;
  cw->setParent (mq_qw (parent));
  if (x->row->indexOf (cw) < 0) x->row->addWidget (cw, 1);
  cw->show ();
  if (mq_class_is (child, "XmTextField")) {
    auto *k = new SpinKeys (parent, child);
    k->setParent (cw);
    cw->installEventFilter (k);
    Widget h = spin_holder (parent, child);
    if (!mq_getb (h, XmNeditable) || mq_geti (h, XmNspinBoxChildType) == XmSTRING)
      if (auto *l = mq_q<QLineEdit> (child)) l->setReadOnly (!mq_getb (h, XmNeditable) || mq_class_is (parent, "XmSpinBox"));
    spin_show (parent, child);
  }
}

static void spin_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (mq_class_is (w, "XmSimpleSpinBox") && !res.isEmpty ()) {
    Widget t = spin_text (w);
    if (t) spin_show (w, t);
  }
}

static void spin_relayout (Widget w)
{
  /* a child's constraint (position, values ...) changed */
  for (Widget c : w->children) if (mq_class_is (c, "XmTextField")) spin_show (w, c);
}

static void sspin_initialize (Widget w)
{
  Widget t = XtVaCreateManagedWidget ("Text", xmTextFieldWidgetClass, w, XmNcolumns, mq_geti (w, XmNcolumns), NULL);
  if (const ResDef *d = mq_find_res (w, XmNtextField)) mq_store (w, d, (XtArgVal) t);
  if (auto *l = mq_q<QLineEdit> (t)) l->setReadOnly (!mq_getb (w, XmNeditable));
  spin_show (w, t);
}

/* ======================================================================
 * Class records
 * ====================================================================== */
extern WidgetClassRec mq_primitive_rec, mq_manager_rec;

MQ_DEFINE_CLASS (xmTextWidgetClass, mq_text_rec, "XmText", &mq_primitive_rec, PRIMK, text_create, text_apply, text_fetch, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmTextFieldWidgetClass, mq_textfield_rec, "XmTextField", &mq_primitive_rec, PRIMK, text_create, text_apply, text_fetch, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmListWidgetClass, mq_list_rec, "XmList", &mq_primitive_rec, PRIMK, list_create, list_apply, list_fetch, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmScrollBarWidgetClass, mq_scrollbar_rec, "XmScrollBar", &mq_primitive_rec, PRIMK, sb_create, sb_apply, nullptr, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmScaleWidgetClass, mq_scale_rec, "XmScale", &mq_manager_rec, MGRK, scale_create, scale_apply, nullptr, scale_place, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmComboBoxWidgetClass, mq_combo_rec, "XmComboBox", &mq_manager_rec, MGRK, combo_create, combo_apply, combo_fetch, nullptr, nullptr, nullptr, nullptr)
MQ_DEFINE_CLASS (xmSpinBoxWidgetClass, mq_spin_rec, "XmSpinBox", &mq_manager_rec, MGRK, spin_create, spin_apply, nullptr, spin_place, nullptr, spin_relayout, nullptr)
MQ_DEFINE_CLASS (xmSimpleSpinBoxWidgetClass, mq_sspin_rec, "XmSimpleSpinBox", &mq_spin_rec, MGRK, spin_create, spin_apply, nullptr, spin_place, nullptr, spin_relayout, sspin_initialize)

/* ======================================================================
 * Public functions
 * ====================================================================== */
static int last_pos (Widget w) { return text_get (w).size (); }

extern "C" {

/* ---------- text ---------- */
Widget XmCreateText (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmTextWidgetClass, p, a, n); }
Widget XmCreateTextField (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmTextFieldWidgetClass, p, a, n); }

Widget XmCreateScrolledText (Widget parent, char *name, ArgList args, Cardinal n)
{
  QByteArray swname = QByteArray (name ? name : "") + "SW";
  QVector<Arg> sa (args, args + n);
  Arg extra[3];
  XtSetArg (extra[0], XmNscrollingPolicy, XmAPPLICATION_DEFINED);
  XtSetArg (extra[1], XmNvisualPolicy, XmVARIABLE);
  XtSetArg (extra[2], XmNscrollBarDisplayPolicy, XmSTATIC);
  sa.prepend (extra[2]); sa.prepend (extra[1]); sa.prepend (extra[0]);
  Widget sw = XtCreateManagedWidget (swname.constData (), xmScrolledWindowWidgetClass, parent, sa.data (), sa.size ());
  QVector<Arg> ta (args, args + n);
  Arg mode;
  XtSetArg (mode, XmNeditMode, XmMULTI_LINE_EDIT);
  bool has_mode = false;
  for (const Arg &a : ta) if (!strcmp (a.name, XmNeditMode)) has_mode = true;
  if (!has_mode) ta.prepend (mode);
  return XtCreateWidget (name, xmTextWidgetClass, sw, ta.data (), ta.size ());
}

char *XmTextGetString (Widget w) { return XtNewString (local (text_get (w)).constData ()); }
char *XmTextFieldGetString (Widget w) { return XmTextGetString (w); }
wchar_t *XmTextGetStringWcs (Widget w)
{
  std::wstring s = text_get (w).toStdWString ();
  auto *r = (wchar_t *) XtMalloc ((s.size () + 1) * sizeof (wchar_t));
  memcpy (r, s.c_str (), (s.size () + 1) * sizeof (wchar_t));
  return r;
}
wchar_t *XmTextFieldGetStringWcs (Widget w) { return XmTextGetStringWcs (w); }

int XmTextGetSubstring (Widget w, XmTextPosition start, int num, int size, char *buf)
{
  QByteArray b = local (text_get (w).mid (start, num));
  if (b.size () >= size) { memcpy (buf, b.constData (), size - 1); buf[size - 1] = 0; return XmCOPY_TRUNCATED; }
  memcpy (buf, b.constData (), b.size () + 1);
  return start + num > last_pos (w) ? XmCOPY_TRUNCATED : XmCOPY_SUCCEEDED;
}
int XmTextFieldGetSubstring (Widget w, XmTextPosition s, int n, int size, char *buf) { return XmTextGetSubstring (w, s, n, size, buf); }
int XmTextGetSubstringWcs (Widget w, XmTextPosition start, int num, int size, wchar_t *buf)
{
  std::wstring s = text_get (w).mid (start, num).toStdWString ();
  int n = qMin ((int) s.size (), size - 1);
  memcpy (buf, s.c_str (), n * sizeof (wchar_t));
  buf[n] = 0;
  return n < (int) s.size () ? XmCOPY_TRUNCATED : XmCOPY_SUCCEEDED;
}
int XmTextFieldGetSubstringWcs (Widget w, XmTextPosition s, int n, int size, wchar_t *buf) { return XmTextGetSubstringWcs (w, s, n, size, buf); }

XmTextPosition XmTextGetLastPosition (Widget w) { return last_pos (w); }
XmTextPosition XmTextFieldGetLastPosition (Widget w) { return last_pos (w); }

void XmTextSetString (Widget w, char *value) { if (w) text_replace (w, 0, last_pos (w), qstr (value)); }
void XmTextFieldSetString (Widget w, char *value) { XmTextSetString (w, value); }
void XmTextSetStringWcs (Widget w, wchar_t *v) { if (w) text_replace (w, 0, last_pos (w), v ? QString::fromWCharArray (v) : QString ()); }
void XmTextFieldSetStringWcs (Widget w, wchar_t *v) { XmTextSetStringWcs (w, v); }
void XmTextReplace (Widget w, XmTextPosition from, XmTextPosition to, char *value) { if (w) text_replace (w, from, to, qstr (value)); }
void XmTextFieldReplace (Widget w, XmTextPosition from, XmTextPosition to, char *value) { XmTextReplace (w, from, to, value); }
void XmTextReplaceWcs (Widget w, XmTextPosition from, XmTextPosition to, wchar_t *v) { if (w) text_replace (w, from, to, v ? QString::fromWCharArray (v) : QString ()); }
void XmTextFieldReplaceWcs (Widget w, XmTextPosition from, XmTextPosition to, wchar_t *v) { XmTextReplaceWcs (w, from, to, v); }
void XmTextInsert (Widget w, XmTextPosition pos, char *value) { XmTextReplace (w, pos, pos, value); }
void XmTextFieldInsert (Widget w, XmTextPosition pos, char *value) { XmTextReplace (w, pos, pos, value); }
void XmTextInsertWcs (Widget w, XmTextPosition pos, wchar_t *v) { XmTextReplaceWcs (w, pos, pos, v); }
void XmTextFieldInsertWcs (Widget w, XmTextPosition pos, wchar_t *v) { XmTextReplaceWcs (w, pos, pos, v); }

void XmTextSetAddMode (Widget, Boolean) {}
void XmTextFieldSetAddMode (Widget, Boolean) {}
Boolean XmTextGetAddMode (Widget) { return False; }
Boolean XmTextFieldGetAddMode (Widget) { return False; }
Boolean XmTextGetEditable (Widget w) { return mq_getb (w, XmNeditable); }
Boolean XmTextFieldGetEditable (Widget w) { return mq_getb (w, XmNeditable); }
void XmTextSetEditable (Widget w, Boolean e) { XtVaSetValues (w, XmNeditable, (XtArgVal) e, NULL); }
void XmTextFieldSetEditable (Widget w, Boolean e) { XmTextSetEditable (w, e); }
int XmTextGetMaxLength (Widget w) { return mq_geti (w, XmNmaxLength); }
int XmTextFieldGetMaxLength (Widget w) { return mq_geti (w, XmNmaxLength); }
void XmTextSetMaxLength (Widget w, int m) { XtVaSetValues (w, XmNmaxLength, (XtArgVal) m, NULL); }
void XmTextFieldSetMaxLength (Widget w, int m) { XmTextSetMaxLength (w, m); }

XmTextPosition XmTextGetTopCharacter (Widget w)
{
  if (auto *t = mq_q<QPlainTextEdit> (w)) return t->cursorForPosition (QPoint (2, 2)).block ().position ();
  return 0;
}

void XmTextSetTopCharacter (Widget w, XmTextPosition top)
{
  if (auto *t = mq_q<QPlainTextEdit> (w)) {
    QTextBlock b = t->document ()->findBlock (top);
    t->verticalScrollBar ()->setValue (b.blockNumber ());
  }
}

XmTextPosition XmTextGetCursorPosition (Widget w) { return text_cursor (w); }
XmTextPosition XmTextFieldGetCursorPosition (Widget w) { return text_cursor (w); }
XmTextPosition XmTextGetInsertionPosition (Widget w) { return text_cursor (w); }
XmTextPosition XmTextFieldGetInsertionPosition (Widget w) { return text_cursor (w); }
void XmTextSetInsertionPosition (Widget w, XmTextPosition p) { text_set_cursor (w, p); }
void XmTextFieldSetInsertionPosition (Widget w, XmTextPosition p) { text_set_cursor (w, p); }
void XmTextSetCursorPosition (Widget w, XmTextPosition p) { text_set_cursor (w, p); }
void XmTextFieldSetCursorPosition (Widget w, XmTextPosition p) { text_set_cursor (w, p); }

Boolean XmTextGetSelectionPosition (Widget w, XmTextPosition *left, XmTextPosition *right)
{
  int a = 0, b = 0;
  if (auto *l = mq_q<QLineEdit> (w)) { if (!l->hasSelectedText ()) return False; a = l->selectionStart (); b = a + l->selectedText ().size (); }
  else if (auto *t = mq_q<QPlainTextEdit> (w)) { QTextCursor c = t->textCursor (); if (!c.hasSelection ()) return False; a = c.selectionStart (); b = c.selectionEnd (); }
  else return False;
  if (left) *left = a;
  if (right) *right = b;
  return True;
}
Boolean XmTextFieldGetSelectionPosition (Widget w, XmTextPosition *l, XmTextPosition *r) { return XmTextGetSelectionPosition (w, l, r); }

char *XmTextGetSelection (Widget w)
{
  XmTextPosition a, b;
  if (!XmTextGetSelectionPosition (w, &a, &b)) return nullptr;
  return XtNewString (local (text_get (w).mid (a, b - a)).constData ());
}
char *XmTextFieldGetSelection (Widget w) { return XmTextGetSelection (w); }
wchar_t *XmTextGetSelectionWcs (Widget w)
{
  XmTextPosition a, b;
  if (!XmTextGetSelectionPosition (w, &a, &b)) return nullptr;
  std::wstring s = text_get (w).mid (a, b - a).toStdWString ();
  auto *r = (wchar_t *) XtMalloc ((s.size () + 1) * sizeof (wchar_t));
  memcpy (r, s.c_str (), (s.size () + 1) * sizeof (wchar_t));
  return r;
}
wchar_t *XmTextFieldGetSelectionWcs (Widget w) { return XmTextGetSelectionWcs (w); }

void XmTextSetSelection (Widget w, XmTextPosition first, XmTextPosition last, Time)
{
  if (auto *l = mq_q<QLineEdit> (w)) l->setSelection (first, last - first);
  else if (auto *t = mq_q<QPlainTextEdit> (w)) {
    QTextCursor c = t->textCursor ();
    int n = t->toPlainText ().size ();
    c.setPosition (qBound (0, (int) first, n));
    c.setPosition (qBound (0, (int) last, n), QTextCursor::KeepAnchor);
    t->setTextCursor (c);
  }
}
void XmTextFieldSetSelection (Widget w, XmTextPosition f, XmTextPosition l, Time t) { XmTextSetSelection (w, f, l, t); }

void XmTextClearSelection (Widget w, Time)
{
  if (auto *l = mq_q<QLineEdit> (w)) l->deselect ();
  else if (auto *t = mq_q<QPlainTextEdit> (w)) { QTextCursor c = t->textCursor (); c.clearSelection (); t->setTextCursor (c); }
}
void XmTextFieldClearSelection (Widget w, Time t) { XmTextClearSelection (w, t); }

Boolean XmTextRemove (Widget w)
{
  XmTextPosition a, b;
  if (!mq_getb (w, XmNeditable) || !XmTextGetSelectionPosition (w, &a, &b)) return False;
  text_replace (w, a, b, QString ());
  return True;
}
Boolean XmTextFieldRemove (Widget w) { return XmTextRemove (w); }

Boolean XmTextCopy (Widget w, Time)
{
  char *s = XmTextGetSelection (w);
  if (!s) return False;
  QGuiApplication::clipboard ()->setText (qstr (s));
  XtFree (s);
  return True;
}
Boolean XmTextFieldCopy (Widget w, Time t) { return XmTextCopy (w, t); }
Boolean XmTextCopyLink (Widget w, Time t) { return XmTextCopy (w, t); }
Boolean XmTextFieldCopyLink (Widget w, Time t) { return XmTextCopy (w, t); }
Boolean XmTextCut (Widget w, Time t) { return XmTextCopy (w, t) && XmTextRemove (w); }
Boolean XmTextFieldCut (Widget w, Time t) { return XmTextCut (w, t); }

Boolean XmTextPaste (Widget w)
{
  if (!mq_getb (w, XmNeditable)) return False;
  QString s = QGuiApplication::clipboard ()->text ();
  XmTextPosition a, b;
  if (!XmTextGetSelectionPosition (w, &a, &b)) a = b = text_cursor (w);
  text_replace (w, a, b, s);
  text_set_cursor (w, a + s.size ());
  return True;
}
Boolean XmTextFieldPaste (Widget w) { return XmTextPaste (w); }
Boolean XmTextPasteLink (Widget w) { return XmTextPaste (w); }
Boolean XmTextFieldPasteLink (Widget w) { return XmTextPaste (w); }

XmTextPosition XmTextXYToPos (Widget w, Position x, Position y)
{
  if (auto *t = mq_q<QPlainTextEdit> (w)) return t->cursorForPosition (QPoint (x, y)).position ();
  if (auto *l = mq_q<QLineEdit> (w)) return l->cursorPositionAt (QPoint (x, y));
  return 0;
}
XmTextPosition XmTextFieldXYToPos (Widget w, Position x, Position y) { return XmTextXYToPos (w, x, y); }

Boolean XmTextPosToXY (Widget w, XmTextPosition pos, Position *x, Position *y)
{
  QRect r;
  if (auto *t = mq_q<QPlainTextEdit> (w)) {
    QTextCursor c (t->document ());
    c.setPosition (qBound (0, (int) pos, t->document ()->characterCount () - 1));
    r = t->cursorRect (c);
  } else if (auto *l = mq_q<QLineEdit> (w)) {
    int keep = l->cursorPosition ();
    l->setCursorPosition (pos);
    r = l->inputMethodQuery (Qt::ImCursorRectangle).toRect ();
    l->setCursorPosition (keep);
  } else return False;
  if (x) *x = r.x ();
  if (y) *y = r.bottom ();
  return True;
}
Boolean XmTextFieldPosToXY (Widget w, XmTextPosition p, Position *x, Position *y) { return XmTextPosToXY (w, p, x, y); }

void XmTextShowPosition (Widget w, XmTextPosition pos)
{
  if (auto *t = mq_q<QPlainTextEdit> (w)) {
    QTextCursor keep = t->textCursor ();
    QTextCursor c (t->document ());
    c.setPosition (qBound (0, (int) pos, t->document ()->characterCount () - 1));
    t->setTextCursor (c);
    t->ensureCursorVisible ();
    t->setTextCursor (keep);
  } else if (auto *l = mq_q<QLineEdit> (w)) l->setCursorPosition (pos);
}
void XmTextFieldShowPosition (Widget w, XmTextPosition p) { XmTextShowPosition (w, p); }

void XmTextScroll (Widget w, int n)
{
  if (auto *t = mq_q<QPlainTextEdit> (w)) t->verticalScrollBar ()->setValue (t->verticalScrollBar ()->value () + n);
}

int XmTextGetBaseline (Widget w) { QWidget *q = mq_qw (w); return q ? q->fontMetrics ().ascent () + 4 : 0; }
int XmTextFieldGetBaseline (Widget w) { return XmTextGetBaseline (w); }
int XmTextGetCenterline (Widget w) { QWidget *q = mq_qw (w); return q ? q->height () / 2 : 0; }
void XmTextDisableRedisplay (Widget w) { if (QWidget *q = mq_qw (w)) q->setUpdatesEnabled (false); }
void XmTextEnableRedisplay (Widget w) { if (QWidget *q = mq_qw (w)) q->setUpdatesEnabled (true); }

void XmTextSetHighlight (Widget w, XmTextPosition left, XmTextPosition right, XmHighlightMode mode)
{
  auto *t = mq_q<QPlainTextEdit> (w);
  if (!t) return;
  QList<QTextEdit::ExtraSelection> sels = t->extraSelections ();
  QTextEdit::ExtraSelection s;
  s.cursor = QTextCursor (t->document ());
  int n = t->document ()->characterCount () - 1;
  s.cursor.setPosition (qBound (0, (int) left, n));
  s.cursor.setPosition (qBound (0, (int) right, n), QTextCursor::KeepAnchor);
  if (mode == XmHIGHLIGHT_SELECTED) { s.format.setBackground (t->palette ().highlight ()); s.format.setForeground (t->palette ().highlightedText ()); }
  else if (mode == XmHIGHLIGHT_SECONDARY_SELECTED) s.format.setFontUnderline (true);
  else { /* normal: remove highlights in the range */
    for (int i = sels.size () - 1; i >= 0; i--)
      if (sels[i].cursor.selectionStart () >= left && sels[i].cursor.selectionEnd () <= right) sels.removeAt (i);
    t->setExtraSelections (sels);
    return;
  }
  sels << s;
  t->setExtraSelections (sels);
}
void XmTextFieldSetHighlight (Widget w, XmTextPosition l, XmTextPosition r, XmHighlightMode m) { XmTextSetHighlight (w, l, r, m); }

Boolean XmTextFindString (Widget w, XmTextPosition start, char *str, XmTextDirection dir, XmTextPosition *pos)
{
  QString t = text_get (w), s = qstr (str);
  int i = dir == XmTEXT_FORWARD ? t.indexOf (s, start) : t.lastIndexOf (s, qMax (0, (int) start - (int) s.size ()));
  if (i < 0) return False;
  if (pos) *pos = i;
  return True;
}

Boolean XmTextFindStringWcs (Widget w, XmTextPosition start, wchar_t *str, XmTextDirection dir, XmTextPosition *pos)
{
  QByteArray b = local (QString::fromWCharArray (str));
  return XmTextFindString (w, start, b.data (), dir, pos);
}

XmTextSource XmTextGetSource (Widget w) { return reinterpret_cast<XmTextSource> (w); }
void XmTextSetSource (Widget w, XmTextSource source, XmTextPosition top, XmTextPosition cursor)
{
  /* sharing a source between widgets: copy the text over */
  Widget from = reinterpret_cast<Widget> (source);
  if (from && from != w) text_set_raw (w, text_get (from));
  XmTextSetTopCharacter (w, top);
  text_set_cursor (w, cursor);
}

/* ---------- list ---------- */
Widget XmCreateList (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmListWidgetClass, p, a, n); }

Widget XmCreateScrolledList (Widget parent, char *name, ArgList args, Cardinal n)
{
  QByteArray swname = QByteArray (name ? name : "") + "SW";
  QVector<Arg> sa (args, args + n);
  Arg extra[2];
  XtSetArg (extra[0], XmNscrollingPolicy, XmAPPLICATION_DEFINED);
  XtSetArg (extra[1], XmNvisualPolicy, XmVARIABLE);
  sa.prepend (extra[1]); sa.prepend (extra[0]);
  Widget sw = XtCreateManagedWidget (swname.constData (), xmScrolledWindowWidgetClass, parent, sa.data (), sa.size ());
  return XtCreateWidget (name, xmListWidgetClass, sw, args, n);
}

static void add_items (Widget w, XmString *items, int count, int pos, bool select)
{
  ListExt *x = mq_ext<ListExt> (w);
  int at = pos <= 0 || pos > x->items.size () ? x->items.size () : pos - 1;
  for (int i = 0; i < count; i++) x->items.insert (at + i, XmStringCopy (items[i]));
  auto *l = mq_q<QListWidget> (w);
  if (l) {
    x->busy = true;
    for (int i = 0; i < count; i++) l->insertItem (at + i, mq_xmstring_text (items[i]));
    x->busy = false;
    l->updateGeometry ();
  }
  if (select) {
    /* XmListAddItem selects the new item if it matches a selected item */
    for (int i = 0; i < count; i++)
      for (XmString s : x->selected) if (XmStringCompare (s, items[i])) list_select_pos (w, at + i + 1, true);
  }
}

void XmListAddItem (Widget w, XmString item, int pos) { add_items (w, &item, 1, pos, true); }
void XmListAddItems (Widget w, XmString *items, int count, int pos) { add_items (w, items, count, pos, true); }
void XmListAddItemsUnselected (Widget w, XmString *items, int count, int pos) { add_items (w, items, count, pos, false); }
void XmListAddItemUnselected (Widget w, XmString item, int pos) { add_items (w, &item, 1, pos, false); }

void XmListDeletePos (Widget w, int pos)
{
  ListExt *x = mq_ext<ListExt> (w);
  if (pos == 0) pos = x->items.size ();
  if (pos < 1 || pos > x->items.size ()) return;
  XmStringFree (x->items.takeAt (pos - 1));
  if (auto *l = mq_q<QListWidget> (w)) { x->busy = true; delete l->takeItem (pos - 1); x->busy = false; }
  list_refresh_selection (w);
}

void XmListDeleteItem (Widget w, XmString item)
{
  int p = XmListItemPos (w, item);
  if (p > 0) XmListDeletePos (w, p);
  else XtWarning ("XmListDeleteItem: item not found");
}

void XmListDeleteItems (Widget w, XmString *items, int count) { for (int i = 0; i < count; i++) XmListDeleteItem (w, items[i]); }

void XmListDeletePositions (Widget w, int *positions, int count)
{
  QList<int> p (positions, positions + count);
  std::sort (p.begin (), p.end (), std::greater<int> ());
  for (int pos : p) XmListDeletePos (w, pos);
}

void XmListDeleteItemsPos (Widget w, int count, int pos) { for (int i = 0; i < count; i++) XmListDeletePos (w, pos); }

void XmListDeleteAllItems (Widget w)
{
  ListExt *x = mq_ext<ListExt> (w);
  for (XmString s : x->items) XmStringFree (s);
  x->items.clear ();
  list_sync (w);
  list_refresh_selection (w);
}

void XmListReplaceItemsPos (Widget w, XmString *items, int count, int pos)
{
  ListExt *x = mq_ext<ListExt> (w);
  auto *l = mq_q<QListWidget> (w);
  for (int i = 0; i < count; i++) {
    int at = pos - 1 + i;
    if (at < 0 || at >= x->items.size ()) break;
    XmStringFree (x->items[at]);
    x->items[at] = XmStringCopy (items[i]);
    if (l) l->item (at)->setText (mq_xmstring_text (items[i]));
  }
  list_refresh_selection (w);
}
void XmListReplaceItemsPosUnselected (Widget w, XmString *items, int count, int pos)
{
  XmListReplaceItemsPos (w, items, count, pos);
  for (int i = 0; i < count; i++) list_select_pos (w, pos + i, false);
}

void XmListReplaceItems (Widget w, XmString *old_items, int count, XmString *new_items)
{
  ListExt *x = mq_ext<ListExt> (w);
  for (int i = 0; i < count; i++)
    for (int p = 0; p < x->items.size (); p++)
      if (XmStringCompare (x->items[p], old_items[i])) XmListReplaceItemsPos (w, &new_items[i], 1, p + 1);
}
void XmListReplaceItemsUnselected (Widget w, XmString *o, int count, XmString *n) { XmListReplaceItems (w, o, count, n); }
void XmListReplacePositions (Widget w, int *positions, XmString *items, int count)
{
  for (int i = 0; i < count; i++) XmListReplaceItemsPos (w, &items[i], 1, positions[i]);
}

void XmListSelectPos (Widget w, int pos, Boolean notify)
{
  ListExt *x = mq_ext<ListExt> (w);
  if (pos == 0) pos = x->items.size ();
  int policy = mq_geti (w, XmNselectionPolicy);
  auto *l = mq_q<QListWidget> (w);
  bool on = !(policy == XmMULTIPLE_SELECT && l && l->item (pos - 1) && l->item (pos - 1)->isSelected ());
  list_select_pos (w, pos, on);
  if (notify) list_callback (w, policy_reason (w), pos);
}

void XmListSelectItem (Widget w, XmString item, Boolean notify)
{
  int p = XmListItemPos (w, item);
  if (p > 0) XmListSelectPos (w, p, notify);
}

void XmListDeselectPos (Widget w, int pos)
{
  if (pos == 0) pos = mq_ext<ListExt> (w)->items.size ();
  list_select_pos (w, pos, false);
}

void XmListDeselectItem (Widget w, XmString item)
{
  int p = XmListItemPos (w, item);
  if (p > 0) list_select_pos (w, p, false);
}

void XmListDeselectAllItems (Widget w)
{
  if (auto *l = mq_q<QListWidget> (w)) { mq_ext<ListExt> (w)->busy = true; l->clearSelection (); mq_ext<ListExt> (w)->busy = false; }
  list_refresh_selection (w);
}

void XmListSetPos (Widget w, int pos)
{
  auto *l = mq_q<QListWidget> (w);
  if (pos == 0) pos = mq_ext<ListExt> (w)->items.size ();
  if (l && l->item (pos - 1)) l->scrollToItem (l->item (pos - 1), QAbstractItemView::PositionAtTop);
}

void XmListSetBottomPos (Widget w, int pos)
{
  auto *l = mq_q<QListWidget> (w);
  if (pos == 0) pos = mq_ext<ListExt> (w)->items.size ();
  if (l && l->item (pos - 1)) l->scrollToItem (l->item (pos - 1), QAbstractItemView::PositionAtBottom);
}

void XmListSetItem (Widget w, XmString item) { int p = XmListItemPos (w, item); if (p > 0) XmListSetPos (w, p); }
void XmListSetBottomItem (Widget w, XmString item) { int p = XmListItemPos (w, item); if (p > 0) XmListSetBottomPos (w, p); }
void XmListSetAddMode (Widget, Boolean) {}

Boolean XmListItemExists (Widget w, XmString item) { return XmListItemPos (w, item) > 0; }

int XmListItemPos (Widget w, XmString item)
{
  ListExt *x = mq_ext<ListExt> (w);
  for (int i = 0; i < x->items.size (); i++) if (XmStringCompare (x->items[i], item)) return i + 1;
  return 0;
}

int XmListGetKbdItemPos (Widget w) { auto *l = mq_q<QListWidget> (w); return l ? l->currentRow () + 1 : 0; }

Boolean XmListSetKbdItemPos (Widget w, int pos)
{
  auto *l = mq_q<QListWidget> (w);
  if (!l || pos < 0 || pos > l->count ()) return False;
  if (pos == 0) pos = l->count ();
  mq_ext<ListExt> (w)->busy = true;
  l->setCurrentRow (pos - 1, QItemSelectionModel::NoUpdate);
  mq_ext<ListExt> (w)->busy = false;
  return True;
}

int XmListYToPos (Widget w, Position y)
{
  auto *l = mq_q<QListWidget> (w);
  if (!l) return 0;
  QListWidgetItem *it = l->itemAt (2, y);
  return it ? l->row (it) + 1 : l->count () + 1;
}

Boolean XmListPosToBounds (Widget w, int pos, Position *x, Position *y, Dimension *width, Dimension *height)
{
  auto *l = mq_q<QListWidget> (w);
  if (!l || !l->item (pos - 1)) return False;
  QRect r = l->visualItemRect (l->item (pos - 1));
  QPoint off = l->viewport ()->mapTo (l, QPoint (0, 0));
  if (x) *x = r.x () + off.x ();
  if (y) *y = r.y () + off.y ();
  if (width) *width = r.width ();
  if (height) *height = r.height ();
  return True;
}

Boolean XmListGetMatchPos (Widget w, XmString item, int **pos_list, int *count)
{
  ListExt *x = mq_ext<ListExt> (w);
  QList<int> p;
  for (int i = 0; i < x->items.size (); i++) if (XmStringCompare (x->items[i], item)) p << i + 1;
  *count = p.size ();
  if (p.isEmpty ()) { *pos_list = nullptr; return False; }
  *pos_list = (int *) XtMalloc (p.size () * sizeof (int));
  for (int i = 0; i < p.size (); i++) (*pos_list)[i] = p[i];
  return True;
}

Boolean XmListGetSelectedPos (Widget w, int **pos_list, int *count)
{
  list_refresh_selection (w);
  ListExt *x = mq_ext<ListExt> (w);
  *count = x->sel_pos.size ();
  if (x->sel_pos.isEmpty ()) { *pos_list = nullptr; return False; }
  *pos_list = (int *) XtMalloc (x->sel_pos.size () * sizeof (int));
  memcpy (*pos_list, x->sel_pos.data (), x->sel_pos.size () * sizeof (int));
  return True;
}

void XmListSetHorizPos (Widget w, int pos) { if (auto *l = mq_q<QListWidget> (w)) l->horizontalScrollBar ()->setValue (pos); }
void XmListUpdateSelectedList (Widget w) { list_refresh_selection (w); }
Boolean XmListPosSelected (Widget w, int pos)
{
  auto *l = mq_q<QListWidget> (w);
  return l && l->item (pos - 1) && l->item (pos - 1)->isSelected ();
}

/* ---------- scale / scroll bar ---------- */
Widget XmCreateScale (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmScaleWidgetClass, p, a, n); }
Widget XmCreateScrollBar (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmScrollBarWidgetClass, p, a, n); }

void XmScaleGetValue (Widget w, int *value) { if (value) *value = mq_geti (w, XmNvalue); }
void XmScaleSetValue (Widget w, int value) { XtVaSetValues (w, XmNvalue, (XtArgVal) value, NULL); }
void XmScaleSetTicks (Widget, int, Cardinal, Cardinal, Dimension, Dimension, Dimension) {}

void XmScrollBarGetValues (Widget w, int *value, int *slider, int *inc, int *page)
{
  if (value) *value = mq_geti (w, XmNvalue);
  if (slider) *slider = mq_geti (w, XmNsliderSize);
  if (inc) *inc = mq_geti (w, XmNincrement);
  if (page) *page = mq_geti (w, XmNpageIncrement);
}

void XmScrollBarSetValues (Widget w, int value, int slider, int inc, int page, Boolean notify)
{
  XtVaSetValues (w, XmNvalue, (XtArgVal) value, NULL);
  if (slider > 0) XtVaSetValues (w, XmNsliderSize, (XtArgVal) slider, NULL);
  if (inc > 0) XtVaSetValues (w, XmNincrement, (XtArgVal) inc, NULL);
  if (page > 0) XtVaSetValues (w, XmNpageIncrement, (XtArgVal) page, NULL);
  if (notify) sb_call (w, XmNvalueChangedCallback, XmCR_VALUE_CHANGED, value);
}

/* ---------- combo box ---------- */
Widget XmCreateComboBox (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmComboBoxWidgetClass, p, a, n); }

static Widget combo_typed (Widget p, char *name, ArgList a, Cardinal n, unsigned char type)
{
  QVector<Arg> args (a, a + n);
  Arg t;
  XtSetArg (t, XmNcomboBoxType, type);
  args.prepend (t);
  return XtCreateWidget (name, xmComboBoxWidgetClass, p, args.data (), args.size ());
}
Widget XmCreateDropDownComboBox (Widget p, char *name, ArgList a, Cardinal n) { return combo_typed (p, name, a, n, XmDROP_DOWN_COMBO_BOX); }
Widget XmCreateDropDownList (Widget p, char *name, ArgList a, Cardinal n) { return combo_typed (p, name, a, n, XmDROP_DOWN_LIST); }

void XmComboBoxAddItem (Widget w, XmString item, int pos, Boolean unique)
{
  ComboExt *x = mq_ext<ComboExt> (w);
  if (unique) for (XmString s : x->items) if (XmStringCompare (s, item)) return;
  int at = pos <= 0 || pos > x->items.size () ? x->items.size () : pos - 1;
  x->items.insert (at, XmStringCopy (item));
  if (auto *c = mq_q<QComboBox> (w)) { x->busy = true; c->insertItem (at, mq_xmstring_text (item)); x->busy = false; }
}

void XmComboBoxDeletePos (Widget w, int pos)
{
  ComboExt *x = mq_ext<ComboExt> (w);
  if (pos == 0) pos = x->items.size ();
  if (pos < 1 || pos > x->items.size ()) return;
  XmStringFree (x->items.takeAt (pos - 1));
  if (auto *c = mq_q<QComboBox> (w)) { x->busy = true; c->removeItem (pos - 1); x->busy = false; }
}

void XmComboBoxSelectItem (Widget w, XmString item)
{
  auto *c = mq_q<QComboBox> (w);
  if (!c) return;
  int i = c->findText (mq_xmstring_text (item));
  if (i < 0) return;
  mq_ext<ComboExt> (w)->busy = true;
  c->setCurrentIndex (i);
  mq_ext<ComboExt> (w)->busy = false;
}

void XmComboBoxSetItem (Widget w, XmString item) { XmComboBoxSelectItem (w, item); }
void XmComboBoxUpdate (Widget w) { combo_sync (w); }
XmString XmCombinationBoxGetValue (Widget w) { auto *c = mq_q<QComboBox> (w); return c ? mq_xmstring_from (c->currentText ()) : nullptr; }

/* ---------- spin boxes ---------- */
Widget XmCreateSpinBox (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmSpinBoxWidgetClass, p, a, n); }
Widget XmCreateSimpleSpinBox (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmSimpleSpinBoxWidgetClass, p, a, n); }

int XmSpinBoxValidatePosition (Widget text, int *position)
{
  Widget box = text ? text->parent : nullptr;
  if (!box) return XmVALID_VALUE;
  Widget h = spin_holder (box, text);
  int mn = mq_geti (h, XmNminimumValue), mx = mq_geti (h, XmNmaximumValue);
  bool ok;
  int d = mq_geti (h, XmNdecimalPoints);
  double v = text_get (text).toDouble (&ok);
  if (!ok) { if (position) *position = mq_geti (h, XmNposition); return XmCURRENT_VALUE; }
  for (int i = 0; i < d; i++) v *= 10;
  int p = qRound (v);
  if (p < mn) { if (position) *position = mn; return XmMINIMUM_VALUE; }
  if (p > mx) { if (position) *position = mx; return XmMAXIMUM_VALUE; }
  int inc = qMax (1, mq_geti (h, XmNincrementValue));
  if ((p - mn) % inc) { if (position) *position = p - (p - mn) % inc; return XmINCREMENT_VALUE; }
  if (position) *position = p;
  return XmVALID_VALUE;
}

void XmSimpleSpinBoxAddItem (Widget w, XmString item, int pos)
{
  XmStringTable v = (XmStringTable) mq_get (w, XmNvalues);
  int n = mq_geti (w, XmNnumValues);
  QVector<XmString> list (v, v + (v ? n : 0));
  int at = pos <= 0 || pos > n ? n : pos - 1;
  list.insert (at, item);
  XtVaSetValues (w, XmNvalues, (XtArgVal) list.data (), XmNnumValues, (XtArgVal) list.size (), NULL);
}

void XmSimpleSpinBoxDeletePos (Widget w, int pos)
{
  XmStringTable v = (XmStringTable) mq_get (w, XmNvalues);
  int n = mq_geti (w, XmNnumValues);
  if (!v || n <= 0) return;
  if (pos == 0) pos = n;
  QVector<XmString> list (v, v + n);
  if (pos >= 1 && pos <= n) list.removeAt (pos - 1);
  XtVaSetValues (w, XmNvalues, (XtArgVal) list.data (), XmNnumValues, (XtArgVal) list.size (), NULL);
}

void XmSimpleSpinBoxSetItem (Widget w, XmString item)
{
  XmStringTable v = (XmStringTable) mq_get (w, XmNvalues);
  int n = mq_geti (w, XmNnumValues);
  for (int i = 0; v && i < n; i++) if (XmStringCompare (v[i], item)) { XtVaSetValues (w, XmNposition, (XtArgVal) i, NULL); return; }
}

} /* extern "C" */

/* ======================================================================
 * XmDropDown (Motif 2.3 "combination box 2"): a label, a text field, an
 * arrow button and a popup list, all real child widgets with Motif's names;
 * the list's resources (items, itemCount ...) and the label's labelString
 * are set and read through the drop down.
 * ====================================================================== */
#include <Xm/DropDown.h>

static Widget dd_child (Widget w, const char *name)
{
  for (Widget c : w->children) if (c->name == name) return c;
  return nullptr;
}

static Widget dd_list (Widget w)
{
  for (Widget p : w->popups)
    if (p->name == "popupShell") return XtNameToWidget (p, "*list");
  return nullptr;
}

static void dd_popdown (Widget w, bool take_text)
{
  Widget shell = nullptr;
  for (Widget p : w->popups) if (p->name == "popupShell") shell = p;
  if (!shell) return;
  if (take_text) {
    Widget list = dd_list (w), text = dd_child (w, "text");
    XmStringTable sel = nullptr;
    int n = 0;
    XtVaGetValues (list, XmNselectedItems, &sel, XmNselectedItemCount, &n, NULL);
    if (n > 0 && text && !mq_getb (w, XmNcustomizedCombinationBox)) {
      QByteArray b = mq_xmstring_text (sel[0]).toLocal8Bit ();
      XmTextFieldSetString (text, b.data ());
    }
  }
  XtPopdown (shell);
  if (take_text) {
    XmAnyCallbackStruct cs = { XmCR_UPDATE_TEXT, mq_current_event () };
    mq_call (w, XmNupdateTextCallback, &cs);
  }
}

static void dd_popup (Widget w)
{
  Widget shell = nullptr;
  for (Widget p : w->popups) if (p->name == "popupShell") shell = p;
  Widget text = dd_child (w, "text");
  if (!shell || !text || !mq_qw (shell) || !mq_qw (text)) return;
  QWidget *win = mq_qw (shell), *tq = mq_qw (text);
  XmAnyCallbackStruct cs = { XmCR_UPDATE_SHELL, mq_current_event () };
  mq_call (w, XmNupdateShellCallback, &cs);
  /* select the item matching the text */
  Widget list = dd_list (w);
  char *s = XmTextFieldGetString (text);
  XmString x = XmStringCreateLocalized (s);
  XtFree (s);
  XmListDeselectAllItems (list);
  int pos = XmListItemPos (list, x);
  XmStringFree (x);
  if (pos > 0) { XmListSelectPos (list, pos, False); XmListSetPos (list, pos); }
  win->setWindowFlags (Qt::Popup);
  QWidget *arrow = dd_child (w, "arrow") ? mq_qw (dd_child (w, "arrow")) : nullptr;
  int width = tq->width () + (arrow ? arrow->width () : 0);
  win->setMinimumWidth (width);
  win->move (tq->mapToGlobal (QPoint (0, tq->height ())));
  XtPopup (shell, XtGrabExclusive);
}

static QObject *dd_create (Widget)
{
  auto *box = new QWidget;
  auto *l = new QHBoxLayout (box);
  l->setContentsMargins (0, 0, 0, 0);
  l->setSpacing (2);
  return box;
}

static void dd_place (Widget parent, Widget child)
{
  QWidget *cw = mq_qw (child), *box = mq_qw (parent);
  if (!cw || !box) return;
  auto *l = static_cast<QHBoxLayout *> (box->layout ());
  cw->setParent (box);
  if (l->indexOf (cw) < 0) {
    /* label, text, arrow */
    int at = child->name == "label" ? 0 : child->name == "arrow" ? l->count () : (l->count () && dd_child (parent, "label") && dd_child (parent, "label")->managed ? 1 : 0);
    l->insertWidget (at, cw, child->name == "text" ? 1 : 0);
  }
  cw->show ();
}

static void dd_initialize (Widget w)
{
  Widget label = XtVaCreateWidget ("label", xmLabelWidgetClass, w, NULL);
  if (mq_getb (w, XmNshowLabel)) XtManageChild (label);
  Widget text = XtVaCreateManagedWidget ("text", xmTextFieldWidgetClass, w, XmNeditable, mq_get (w, XmNeditable), NULL);
  Widget arrow = XtVaCreateManagedWidget ("arrow", xmArrowButtonWidgetClass, w, XmNarrowDirection, XmARROW_DOWN, NULL);
  XtAddCallback (arrow, XmNactivateCallback, [] (Widget a, XtPointer, XtPointer) { dd_popup (a->parent); }, nullptr);
  XtAddCallback (text, XmNactivateCallback, [] (Widget t, XtPointer, XtPointer) {
    XmAnyCallbackStruct cs = { XmCR_VERIFY_TEXT, mq_current_event () };
    mq_call (t->parent, XmNverifyTextCallback, &cs);
  }, nullptr);
  Widget shell = XtVaCreatePopupShell ("popupShell", overrideShellWidgetClass, w, NULL);
  Widget list = XmCreateScrolledList (shell, (char *) "list", nullptr, 0);
  XtVaSetValues (list, XmNvisibleItemCount, mq_geti (w, XmNvisibleItemCount), XmNselectionPolicy, XmBROWSE_SELECT, NULL);
  XtManageChild (list);
  auto pick = [] (Widget l, XtPointer dd, XtPointer) { dd_popdown ((Widget) dd, true); };
  XtAddCallback (list, XmNdefaultActionCallback, pick, w);
  XtAddCallback (list, XmNbrowseSelectionCallback, [] (Widget l, XtPointer dd, XtPointer) {
    if (!(QApplication::mouseButtons () & Qt::LeftButton)) dd_popdown ((Widget) dd, true);
  }, w);
  if (const ResDef *d = mq_find_res (w, XmNpopupShellWidget)) mq_store (w, d, (XtArgVal) shell);
  Q_UNUSED (label);
}

static void dd_apply (Widget w, const QByteArray &res)
{
  mq_apply_common (w, res);
  if (res == XmNshowLabel) if (Widget l = dd_child (w, "label")) { if (mq_getb (w, XmNshowLabel)) XtManageChild (l); else XtUnmanageChild (l); }
  if (res == XmNeditable) if (Widget t = dd_child (w, "text")) XtVaSetValues (t, XmNeditable, mq_get (w, XmNeditable), NULL);
  if (res == XmNvisibleItemCount) if (Widget l = dd_list (w)) XtVaSetValues (l, XmNvisibleItemCount, mq_get (w, XmNvisibleItemCount), NULL);
}

static Widget dd_owner (Widget w, const char *name)
{
  if (!strcmp (name, XmNlabelString) || !strcmp (name, XmNalignment)) return dd_child (w, "label");
  if (!strcmp (name, XmNvalue) || !strcmp (name, XmNcolumns) || !strcmp (name, XmNmaxLength)) return dd_child (w, "text");
  return dd_list (w);   /* items, itemCount, selectedItems, ... */
}

static void dd_set_other (Widget w, const char *name, XtArgVal v)
{
  Widget target = dd_owner (w, name);
  Arg a;
  a.name = (String) name;
  a.value = v;
  if (target && mq_find_res (target, name)) XtSetValues (target, &a, 1);
}

static bool dd_get_other (Widget w, const char *name, XtArgVal dest)
{
  Widget target = dd_owner (w, name);
  if (!target || !mq_find_res (target, name)) return false;
  Arg a;
  a.name = (String) name;
  a.value = dest;
  XtGetValues (target, &a, 1);
  return true;
}

MQ_DEFINE_CLASS (xmDropDownWidgetClass, mq_dropdown_rec, "XmDropDown", &mq_manager_rec, MGRK, dd_create, dd_apply, nullptr, dd_place, nullptr, nullptr, dd_initialize)
static struct DropDownHooks {
  DropDownHooks () { mq_dropdown_rec.set_other = dd_set_other; mq_dropdown_rec.get_other = dd_get_other; }
} s_dropdown_hooks;

extern "C" {
Widget XmCreateDropDown (Widget p, char *name, ArgList a, Cardinal n) { return XtCreateWidget (name, xmDropDownWidgetClass, p, a, n); }
Widget XmCreateCombinationBox2 (Widget p, char *name, ArgList a, Cardinal n) { return XmCreateDropDown (p, name, a, n); }
String XmDropDownGetValue (Widget w) { Widget t = dd_child (w, "text"); return t ? XmTextFieldGetString (t) : nullptr; }
Widget XmDropDownGetLabel (Widget w) { return dd_child (w, "label"); }
Widget XmDropDownGetArrow (Widget w) { return dd_child (w, "arrow"); }
Widget XmDropDownGetText (Widget w) { return dd_child (w, "text"); }
Widget XmDropDownGetList (Widget w) { return dd_list (w); }
Widget XmDropDownGetChild (Widget w, int which)
{
  switch (which) {
  case XmDROPDOWN_LABEL: return dd_child (w, "label");
  case XmDROPDOWN_TEXT: return dd_child (w, "text");
  case XmDROPDOWN_ARROW_BUTTON: return dd_child (w, "arrow");
  case XmDROPDOWN_LIST: return dd_list (w);
  default: return nullptr;
  }
}
}
