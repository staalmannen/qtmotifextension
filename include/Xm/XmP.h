/* Motif on Qt compatibility header for <Xm/XmP.h>: the public Motif API, the
 * Xt accessor macros and the Xme functions programs commonly call. Widget
 * records are not available (see X11/IntrinsicP.h). */
#ifndef MQ_XMP_H
#define MQ_XMP_H
#include <Xm/Xm.h>
#include <X11/IntrinsicP.h>

#ifdef __cplusplus
extern "C" {
#endif
extern void XmeWarning (Widget w, char *message);
extern void XmeDrawShadows (Display *d, Drawable dr, GC top_gc, GC bottom_gc, Position x, Position y,
                            Dimension width, Dimension height, Dimension shadow_thick, unsigned int shadow_type);
extern void XmeClearBorder (Display *d, Window w, Position x, Position y, Dimension width, Dimension height, Dimension shadow_thick);
extern void XmeDrawSeparator (Display *d, Drawable dr, GC top_gc, GC bottom_gc, GC separator_gc, Position x, Position y,
                              Dimension width, Dimension height, Dimension shadow_thick, Dimension margin,
                              unsigned char orientation, unsigned char separator_type);
extern void XmeDrawHighlight (Display *d, Drawable dr, GC gc, Position x, Position y, Dimension width, Dimension height,
                              Dimension highlight_thick);
#ifdef __cplusplus
}
#endif

#define XmeWarning(w, m) XmeWarning ((Widget) (w), (char *) (m))
#endif
