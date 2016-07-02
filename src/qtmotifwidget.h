/****************************************************************************
** 
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
** 
** This file is part of a Qt Solutions component.
**
** Commercial Usage  
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Solutions Commercial License Agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and Nokia.
** 
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
** 
** In addition, as a special exception, Nokia gives you certain
** additional rights. These rights are described in the Nokia Qt LGPL
** Exception version 1.1, included in the file LGPL_EXCEPTION.txt in this
** package.
** 
** GNU General Public License Usage 
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
** 
** Please note Third Party Software included with Qt Solutions may impose
** additional restrictions and it is the user's responsibility to ensure
** that they have met the licensing requirements of the GPL, LGPL, or Qt
** Solutions Commercial license and the relevant license of the Third
** Party Software they are using.
** 
** If you are unsure which license is appropriate for your use, please
** contact Nokia at qt-info@nokia.com.
** 
****************************************************************************/

#ifndef QMOTIFWIDGET_H
#define QMOTIFWIDGET_H

#include <QtWidgets/qwidget.h>

#include <X11/Intrinsic.h>
#undef Bool
#undef Int

class QtMotifWidgetPrivate;
class QKeyEvent;

class QtMotifWidget : public QWidget
{
    Q_OBJECT
    QtMotifWidgetPrivate *d;

public:
    QtMotifWidget(const char *name, WidgetClass widgetClass, QWidget *parent,
                 ArgList args = NULL, Cardinal argCount = 0,
                 Qt::WindowFlags flags = 0);
    ~QtMotifWidget();

    Widget motifWidget() const;

protected:
    bool event(QEvent *);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);
    bool eventFilter(QObject *object, QEvent *event);
    bool nativeEvent(const QByteArray &eventType, void *message, long *result);

private:
    void realize(Widget);

    friend void qmotif_widget_shell_destroy(Widget w);
    friend void qmotif_widget_shell_realize(Widget, XtValueMask *,
                                            XSetWindowAttributes *);
    friend void qmotif_widget_shell_change_managed(Widget);
    static bool dispatchQEvent(QEvent*, QWidget*);
    friend class QtMotifDialog;
};

#endif // QMOTIFWIDGET_H
