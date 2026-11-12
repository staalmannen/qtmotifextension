/*
 * Motif on Qt - scripted input for headless testing.
 *
 * MOTIFQT_SCRIPT="wait 500; key Ctrl+N; click quit; ..." runs these steps
 * one after another (starting once the event loop runs):
 *   wait <ms>                      pause
 *   key [<widget>] <keys>          key presses (QKeySequence syntax: n, Ctrl+Q, PgDown)
 *   click <widget> [1|2|3] [x y]   press and release a mouse button
 *   type <widget> <text>           type text
 *   shot <file.png>                save the top-level windows
 *   dump                           print the widget tree
 *   quit                           exit(0)
 * <widget> is a Motif widget name (the first match, searching all windows),
 * optionally a path such as toolBar.quit.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstdio>

void mq_dump_widgets ();
void mq_save_windows (const QString &file);

static QWidget *find_widget (const QString &spec)
{
  QStringList path = spec.split ('.');
  QList<QWidget *> roots;
  for (QWidget *t : QApplication::topLevelWidgets ()) if (t->isVisible ()) roots << t;
  QList<QWidget *> scope = roots;
  QWidget *found = nullptr;
  for (const QString &part : path) {
    found = nullptr;
    for (QWidget *r : scope) {
      if (r->objectName () == part) { found = r; break; }
      QList<QWidget *> kids = r->findChildren<QWidget *> (part);
      for (QWidget *k : kids) if (k->isVisible ()) { found = k; break; }
      if (found) break;
    }
    if (!found) return nullptr;
    scope = { found };
  }
  return found;
}

static QWidget *focus_target ()
{
  if (QWidget *f = QApplication::focusWidget ()) return f;
  if (QWidget *a = QApplication::activeWindow ()) return a;
  for (QWidget *t : QApplication::topLevelWidgets ()) if (t->isVisible () && !qobject_cast<QMenu *> (t)) return t;
  return nullptr;
}

static void send_keys (QWidget *target, const QKeySequence &seq)
{
  for (int i = 0; i < seq.count (); i++) {
    QKeyCombination kc = seq[i];
    QString text;
    int k = kc.key ();
    if (k >= 0x20 && k < 0x7f && !(kc.keyboardModifiers () & (Qt::ControlModifier | Qt::AltModifier))) {
      QChar c (k);
      text = (kc.keyboardModifiers () & Qt::ShiftModifier) ? c.toUpper () : c.toLower ();
    }
    QWidget *t = QApplication::activePopupWidget () ? QApplication::activePopupWidget () : target;
    QKeyEvent press (MQ_QKeyPress, k, kc.keyboardModifiers (), text);
    QKeyEvent release (MQ_QKeyRelease, k, kc.keyboardModifiers (), text);
    QApplication::sendEvent (t, &press);
    QApplication::sendEvent (t, &release);
  }
}

static void run_step (QStringList &steps)
{
  while (!steps.isEmpty ()) {
    QString step = steps.takeFirst ().trimmed ();
    if (step.isEmpty ()) continue;
    QStringList a = step.split (' ', Qt::SkipEmptyParts);
    QString cmd = a.takeFirst ();
    if (cmd == "wait") {
      QTimer::singleShot (a.value (0).toInt (), qApp, [steps] () mutable { run_step (steps); });
      return;
    } else if (cmd == "key") {
      QWidget *t = a.size () > 1 ? find_widget (a.takeFirst ()) : focus_target ();
      if (!t) { fprintf (stderr, "script: no widget for: %s\n", qPrintable (step)); continue; }
      QKeySequence seq (a.join (' '), QKeySequence::PortableText);
      if (seq.isEmpty ()) seq = QKeySequence (a.join (' '));
      /* a key the application binds as a shortcut goes through Qt's shortcut map */
      bool handled = false;
      {
        /* menu accelerators live on actions that may not be children of the window */
        for (QWidget *top : QApplication::allWidgets ())
          for (QAction *act : top->actions ())
            if (!handled && act->shortcut () == seq && act->isEnabled ()) { act->trigger (); handled = true; }
      }
      if (!handled) send_keys (t, seq);
    } else if (cmd == "click") {
      QWidget *t = find_widget (a.value (0));
      if (!t) { fprintf (stderr, "script: no widget for: %s\n", qPrintable (step)); continue; }
      int b = a.size () > 1 ? a[1].toInt () : 1;
      Qt::MouseButton button = b == 3 ? Qt::RightButton : b == 2 ? Qt::MiddleButton : Qt::LeftButton;
      QPointF pos = a.size () > 3 ? QPointF (a[2].toInt (), a[3].toInt ()) : QPointF (t->width () / 2.0, t->height () / 2.0);
      QPointF global = t->mapToGlobal (pos);
      QMouseEvent press (QEvent::MouseButtonPress, pos, global, button, button, Qt::NoModifier);
      QMouseEvent release (QEvent::MouseButtonRelease, pos, global, button, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent (t, &press);
      QApplication::sendEvent (t, &release);
    } else if (cmd == "type") {
      QWidget *t = find_widget (a.value (0));
      if (!t) { fprintf (stderr, "script: no widget for: %s\n", qPrintable (step)); continue; }
      t->setFocus ();
      QString text = step.section (' ', 2);
      for (QChar c : text) {
        QKeyEvent press (MQ_QKeyPress, c.toUpper ().unicode (), Qt::NoModifier, QString (c));
        QKeyEvent release (MQ_QKeyRelease, c.toUpper ().unicode (), Qt::NoModifier, QString (c));
        QApplication::sendEvent (t, &press);
        QApplication::sendEvent (t, &release);
      }
    } else if (cmd == "shot") {
      mq_save_windows (a.value (0));
    } else if (cmd == "dump") {
      mq_dump_widgets ();
    } else if (cmd == "quit") {
      fflush (stdout);
      ::exit (0);
    } else {
      fprintf (stderr, "script: unknown step: %s\n", qPrintable (step));
    }
    /* let the program react before the next step */
    QTimer::singleShot (50, qApp, [steps] () mutable { run_step (steps); });
    return;
  }
}

void mq_script_start ()
{
  QString script = qEnvironmentVariable ("MOTIFQT_SCRIPT");
  if (script.isEmpty ()) return;
  QStringList steps = script.split (';');
  QTimer::singleShot (0, qApp, [steps] () mutable { run_step (steps); });
}
