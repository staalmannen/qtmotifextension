/*
 * Motif on Qt - the real sizes of Motif's widget records.
 *
 * Programs see Motif's genuine private headers: they subclass our widgets
 * (Xlt's BubbleButton is an XmPushButton) and read records through macros
 * such as Lab_Font. Every widget is therefore allocated with the size of
 * its class's real instance record, so the parts the program's compiler
 * lays out are inside the block. Compiled as C: these are C headers.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include <string.h>
#include <Xm/XmP.h>
#include <Xm/PrimitiveP.h>
#include <Xm/GadgetP.h>
#include <Xm/ManagerP.h>
#include <Xm/LabelP.h>
#include <Xm/PushBP.h>
#include <Xm/ToggleBP.h>
#include <Xm/CascadeBP.h>
#include <Xm/DrawnBP.h>
#include <Xm/ArrowBP.h>
#include <Xm/SeparatorP.h>
#include <Xm/LabelGP.h>
#include <Xm/PushBGP.h>
#include <Xm/ToggleBGP.h>
#include <Xm/CascadeBGP.h>
#include <Xm/ArrowBGP.h>
#include <Xm/SeparatoGP.h>
#include <Xm/MessageBP.h>
#include <Xm/SelectioBP.h>
#include <Xm/FileSBP.h>
#include <Xm/CommandP.h>
#include <Xm/BulletinBP.h>
#include <Xm/FormP.h>
#include <Xm/RowColumnP.h>
#include <Xm/FrameP.h>
#include <Xm/DrawingAP.h>
#include <Xm/ScrolledWP.h>
#include <Xm/MainWP.h>
#include <Xm/PanedWP.h>
#include <Xm/DisplayP.h>
#include <Xm/ScreenP.h>
#include <Xm/TextP.h>
#include <Xm/ListP.h>
#include <Xm/ScrollBarP.h>
#include <Xm/ScaleP.h>
#include <Xm/ComboBoxP.h>
#include <Xm/SpinBP.h>
#include <Xm/SSpinBP.h>
#include <Xm/DropDownP.h>
#include <Xm/DialogSP.h>
#include <Xm/MenuShellP.h>

#define REC(name, type) { name, sizeof (type) }
static const struct { const char *name; unsigned size; } records[] = {
  REC ("XmPrimitive", XmPrimitiveRec), REC ("XmGadget", XmGadgetRec), REC ("XmManager", XmManagerRec),
  REC ("XmLabel", XmLabelRec), REC ("XmPushButton", XmPushButtonRec), REC ("XmToggleButton", XmToggleButtonRec),
  REC ("XmCascadeButton", XmCascadeButtonRec), REC ("XmDrawnButton", XmDrawnButtonRec),
  REC ("XmArrowButton", XmArrowButtonRec), REC ("XmSeparator", XmSeparatorRec),
  REC ("XmLabelGadget", XmLabelGadgetRec), REC ("XmPushButtonGadget", XmPushButtonGadgetRec),
  REC ("XmToggleButtonGadget", XmToggleButtonGadgetRec), REC ("XmCascadeButtonGadget", XmCascadeButtonGadgetRec),
  REC ("XmArrowButtonGadget", XmArrowButtonGadgetRec), REC ("XmSeparatorGadget", XmSeparatorGadgetRec),
  REC ("XmMessageBox", XmMessageBoxRec), REC ("XmSelectionBox", XmSelectionBoxRec),
  REC ("XmFileSelectionBox", XmFileSelectionBoxRec), REC ("XmCommand", XmCommandRec),
  REC ("XmBulletinBoard", XmBulletinBoardRec), REC ("XmForm", XmFormRec), REC ("XmRowColumn", XmRowColumnRec),
  REC ("XmFrame", XmFrameRec), REC ("XmDrawingArea", XmDrawingAreaRec), REC ("XmScrolledWindow", XmScrolledWindowRec),
  REC ("XmMainWindow", XmMainWindowRec), REC ("XmPanedWindow", XmPanedWindowRec), REC ("XmDisplay", XmDisplayRec),
  REC ("XmScreen", XmScreenRec), REC ("XmText", XmTextRec), REC ("XmList", XmListRec),
  REC ("XmScrollBar", XmScrollBarRec), REC ("XmScale", XmScaleRec), REC ("XmComboBox", XmComboBoxRec),
  REC ("XmSpinBox", XmSpinBoxRec), REC ("XmSimpleSpinBox", XmSimpleSpinBoxRec), REC ("XmDropDown", XmDropDownRec),
  REC ("XmDialogShell", XmDialogShellRec), REC ("XmMenuShell", XmMenuShellRec),
};

/* the size of the class's real Motif instance record, 0 if not a Motif class */
unsigned mq_motif_record_size (const char *class_name)
{
  unsigned i;
  if (!class_name) return 0;
  for (i = 0; i < sizeof (records) / sizeof (records[0]); i++)
    if (!strcmp (records[i].name, class_name)) return records[i].size;
  return 0;
}

/* Keep the Label part of a label/button's record current: programs read it
 * through LabelP.h (Lab_Font, Lab_IsText ...), subclasses build on it. The
 * label string is held as an XmString copy (our _XmString is an XmString). */
void mq_sync_label_record (Widget w, XmString label, unsigned char label_type, XmRenderTable font,
                           unsigned char alignment, Pixmap pixmap, Pixmap pixmap_insen,
                           Dimension margin_width, Dimension margin_height, Boolean recompute_size)
{
  XmLabelPart *l = &((XmLabelWidget) w)->label;
  if (l->_label) XmStringFree ((XmString) l->_label);
  l->_label = label ? (_XmString) XmStringCopy (label) : NULL;
  l->label_type = label_type;
  l->font = font;
  l->alignment = alignment;
  l->pixmap = pixmap;
  l->pixmap_insen = pixmap_insen;
  l->margin_width = margin_width;
  l->margin_height = margin_height;
  l->recompute_size = recompute_size;
}

void mq_free_label_record (Widget w)
{
  XmLabelPart *l = &((XmLabelWidget) w)->label;
  if (l->_label) XmStringFree ((XmString) l->_label);
  l->_label = NULL;
}
