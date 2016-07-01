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
#include <QtCore/QEvent>

#include "mainwindow.h"
#include "dialog.h"

#include <Xm/MainW.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushB.h>
#include <Xm/PushBG.h>
#include <Xm/SeparatoG.h>
#include <Xm/Text.h>
#include <Xm/MessageB.h>
#include <Xm/Form.h>
#include <Xm/LabelG.h>


static void motifDialogCallback( Widget, XtPointer client_data, XtPointer )
{
    MainWindow *mw = (MainWindow *) client_data;
    mw->showMotifDialog();
}

static void qtDialogCallback( Widget, XtPointer client_data, XtPointer )
{
    MainWindow *mw = (MainWindow *) client_data;
    mw->showQtDialog();
}

static void quitCallback( Widget, XtPointer client_data, XtPointer )
{
    MainWindow *mw = (MainWindow *) client_data;
    mw->close();
}


MainWindow::MainWindow()
    : QtMotifWidget("mainwindow", xmMainWindowWidgetClass, 0)
{
    Widget menubar = XmCreateMenuBar( motifWidget(), const_cast<char*>("menubar"), NULL, 0 );
    Widget filemenu = XmCreatePulldownMenu( menubar, const_cast<char*>("filemenu"), NULL, 0 );
    Widget item;

    item = XtVaCreateManagedWidget( "Motif Dialog...",
				    xmPushButtonGadgetClass, filemenu,
				    XmNmnemonic, 'C',
				    NULL );
    XtAddCallback( item, XmNactivateCallback, motifDialogCallback, this );

    item = XtVaCreateManagedWidget( "Qt Dialog...",
				    xmPushButtonGadgetClass, filemenu,
				    XmNmnemonic, 'Q',
				    NULL );
    XtAddCallback( item, XmNactivateCallback, qtDialogCallback, this );

    item = XtVaCreateManagedWidget( "sep",
				    xmSeparatorGadgetClass, filemenu,
				    NULL );

    item = XtVaCreateManagedWidget( "Exit",
				    xmPushButtonGadgetClass, filemenu,
				    XmNmnemonic, 'x',
				    NULL );
    XtAddCallback( item, XmNactivateCallback, quitCallback, this );

    XmString str = XmStringCreateLocalized( const_cast<char*>("File") );
    item = XtVaCreateManagedWidget( "File",
				    xmCascadeButtonWidgetClass, menubar,
				    XmNlabelString, str,
				    XmNmnemonic, 'F',
				    XmNsubMenuId, filemenu,
				    NULL );
    XmStringFree( str );

    Arg args[2];
    XtSetArg( args[0], XmNeditMode, XmMULTI_LINE_EDIT );
    Widget texteditor =
	XmCreateScrolledText( motifWidget(), const_cast<char*>("texteditor"),
			      args, 1 );

    XtManageChild( menubar );
    XtManageChild( texteditor );

    // pick a nice default size
    XtVaSetValues( motifWidget(),
 		   XmNwidth, 400,
 		   XmNheight, 300,
 		   NULL );

    setWindowTitle(tr("QtMotif Dialog Example"));

}

void MainWindow::showMotifDialog()
{
    QtMotifDialog dialog(this);
    dialog.setWindowTitle(tr("Custom Motif Dialog"));

    Widget form = XmCreateForm( dialog.shell(), const_cast<char*>("custom motif dialog"), NULL, 0 );

    XmString str;
    Arg args[9];

    str = XmStringCreateLocalized( const_cast<char*>("Close") );
    XtSetArg( args[0], XmNlabelString, str );
    XtSetArg( args[1], XmNshowAsDefault, True );
    XtSetArg( args[2], XmNleftAttachment, XmATTACH_POSITION );
    XtSetArg( args[3], XmNleftPosition, 40 );
    XtSetArg( args[4], XmNrightAttachment, XmATTACH_POSITION );
    XtSetArg( args[5], XmNrightPosition, 60 );
    XtSetArg( args[7], XmNbottomAttachment, XmATTACH_FORM );
    XtSetArg( args[6], XmNtopOffset, 10 );
    XtSetArg( args[8], XmNbottomOffset, 10 );
    Widget button = XmCreatePushButton( form, const_cast<char*>("Close"), args, 9 );
    XmStringFree( str );

    str = XmStringCreateLocalized( const_cast<char*>("This is a custom Motif-based dialog using\nQtMotifDialog with a QWidget-based parent.") );

    XtSetArg( args[0], XmNlabelString, str );
    XtSetArg( args[1], XmNleftAttachment, XmATTACH_FORM );
    XtSetArg( args[2], XmNrightAttachment, XmATTACH_FORM );
    XtSetArg( args[3], XmNtopAttachment, XmATTACH_FORM );
    XtSetArg( args[4], XmNbottomAttachment, XmATTACH_WIDGET );
    XtSetArg( args[5], XmNbottomWidget, button );
    XtSetArg( args[6], XmNtopOffset, 10 );
    XtSetArg( args[7], XmNbottomOffset, 10 );
    Widget label = XmCreateLabelGadget( form, const_cast<char*>("label"), args, 8 );
    XmStringFree( str );

    XtManageChild( button );
    XtManageChild( label );
    XtManageChild( form );

    XtAddCallback( button, XmNactivateCallback,
		   (XtCallbackProc) QtMotifDialog::acceptCallback, &dialog );

    dialog.exec();
}

void MainWindow::showQtDialog()
{
    // custom Qt-based dialog using a Motif-based parent
    CustomDialog customdialog(motifWidget());
    customdialog.exec();
}
