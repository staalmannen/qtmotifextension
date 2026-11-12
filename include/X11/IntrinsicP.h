/* Motif on Qt compatibility header for <X11/IntrinsicP.h>.
 *
 * Xt's private header exposes the widget records (w->core.width ...). Those
 * do not exist here: widgets are Qt objects. The common accessor macros are
 * provided as functions; code that reads widget structs directly will not
 * compile. */
#ifndef MQ_INTRINSICP_H
#define MQ_INTRINSICP_H
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#ifdef __cplusplus
extern "C" {
#endif
extern Dimension _MqXtWidth (Widget);
extern Dimension _MqXtHeight (Widget);
extern Position _MqXtX (Widget);
extern Position _MqXtY (Widget);
extern Dimension _MqXtBorderWidth (Widget);
extern Boolean _MqXtIsVisible (Widget);
#ifdef __cplusplus
}
#endif

#define XtWidth(w) _MqXtWidth ((Widget) (w))
#define XtHeight(w) _MqXtHeight ((Widget) (w))
#define XtX(w) _MqXtX ((Widget) (w))
#define XtY(w) _MqXtY ((Widget) (w))
#define XtBorderWidth(w) _MqXtBorderWidth ((Widget) (w))
#define XtIsVisible(w) _MqXtIsVisible ((Widget) (w))

#endif
