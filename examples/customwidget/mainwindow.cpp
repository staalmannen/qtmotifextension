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

#include <QtGui/QApplication>
#include <QtGui/QMenuBar>
#include <QtGui/QMenu>
#include <QtGui/QStatusBar>

#include "mainwindow.h"

#include <qtmotifwidget.h>

#include <Xm/Form.h>
#include <Xm/PushB.h>
#include <Xm/Text.h>

MainWindow::MainWindow()
    : QMainWindow()
{
    QMenu *filemenu = new QMenu(tr("&File"), this);
    filemenu->addAction( tr("&Quit"), qApp, SLOT(quit()) );

    menuBar()->addMenu(filemenu);
    statusBar()->showMessage( tr("This is a QMainWindow with an XmText widget.") );

    customwidget = new QtMotifWidget("form", xmFormWidgetClass, this);

    XmString str;
    Arg args[6];

    str = XmStringCreateLocalized( const_cast<char*>("Push Button (XmPushButton)") );
    XtSetArg( args[0], XmNlabelString, str );
    XtSetArg( args[1], XmNleftAttachment, XmATTACH_FORM );
    XtSetArg( args[2], XmNrightAttachment, XmATTACH_FORM );
    XtSetArg( args[3], XmNbottomAttachment, XmATTACH_FORM );
    Widget button =
	XmCreatePushButton( customwidget->motifWidget(), const_cast<char*>("Push Button"), args, 4 );
    XmStringFree( str );

    XtSetArg( args[0], XmNeditMode, XmMULTI_LINE_EDIT );
    XtSetArg( args[1], XmNleftAttachment, XmATTACH_FORM );
    XtSetArg( args[2], XmNrightAttachment, XmATTACH_FORM );
    XtSetArg( args[3], XmNtopAttachment, XmATTACH_FORM );
    XtSetArg( args[4], XmNbottomAttachment, XmATTACH_WIDGET );
    XtSetArg( args[5], XmNbottomWidget, button );
    Widget texteditor =
	XmCreateScrolledText( customwidget->motifWidget(), const_cast<char*>("Text Editor"), args, 6 );

    XtManageChild( texteditor );
    XtManageChild( button );

    setCentralWidget( customwidget );

    resize( 400, 600 );
}
