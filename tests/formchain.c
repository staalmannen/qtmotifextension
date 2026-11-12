/* Form attachment chains attached from the bottom (as in xpdf's Find
 * dialog): the form must be tall enough for every child's preferred size. */
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <stdio.h>
#include <stdlib.h>

static void check (Widget top, Widget form1, Widget text)
{
  Dimension h1 = 0, ht = 0;
  XtVaGetValues (form1, XmNheight, &h1, NULL);
  XtVaGetValues (text, XmNheight, &ht, NULL);
  printf ("inner form height %d, text height %d\n", h1, ht);
  exit (h1 >= 15 && ht >= 15 ? 0 : 1);
}

static XtAppContext app;
static Widget g_top, g_form1, g_text;
static void timeout (XtPointer p, XtIntervalId *id) { check (g_top, g_form1, g_text); }

int main (int argc, char **argv)
{
  Widget top = XtVaAppInitialize (&app, "FormChain", NULL, 0, &argc, argv, NULL, NULL);
  Widget form = XmCreateForm (top, "form", NULL, 0);
  Widget ok = XtVaCreateManagedWidget ("Find", xmPushButtonWidgetClass, form,
    XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 4, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 4, NULL);
  XtVaCreateManagedWidget ("Close", xmPushButtonWidgetClass, form,
    XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 4, XmNbottomAttachment, XmATTACH_FORM, XmNbottomOffset, 4, NULL);
  Widget back = XtVaCreateManagedWidget ("backward", xmToggleButtonWidgetClass, form,
    XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 4, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, ok, NULL);
  Widget mc = XtVaCreateManagedWidget ("matchCase", xmToggleButtonWidgetClass, form,
    XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, back, XmNleftOffset, 16,
    XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, ok, NULL);
  XtVaCreateManagedWidget ("wholeWord", xmToggleButtonWidgetClass, form,
    XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, mc, XmNleftOffset, 16, XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 4,
    XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, ok, NULL);
  Widget form1 = XtVaCreateManagedWidget ("form1", xmFormWidgetClass, form,
    XmNtopAttachment, XmATTACH_FORM, XmNtopOffset, 4, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, back,
    XmNleftAttachment, XmATTACH_FORM, XmNleftOffset, 2, XmNrightAttachment, XmATTACH_FORM, XmNrightOffset, 2, NULL);
  Widget label = XtVaCreateManagedWidget ("label", xmLabelWidgetClass, form1,
    XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, NULL);
  Widget text = XtVaCreateManagedWidget ("text", xmTextFieldWidgetClass, form1,
    XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, label,
    XmNrightAttachment, XmATTACH_FORM, NULL);
  XtManageChild (form);
  XtRealizeWidget (top);
  g_top = top; g_form1 = form1; g_text = text;
  XtAppAddTimeOut (app, 300, timeout, NULL);
  XtAppMainLoop (app);
  return 0;
}
