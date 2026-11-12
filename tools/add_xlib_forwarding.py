#!/usr/bin/env python3
"""Insert MQ_FWD guards into the Xlib entry points (idempotent).

Every X* function whose first parameter is a Display* (or Screen*) forwards
to the system libX11 when it is given a display that is not ours: Qt's xcb
platform and libGL call Xlib with real displays, and symbol lookup can bind
those calls to this library.
usage: add_xlib_forwarding.py file.cpp ...
"""
import re, sys

TYPES = set("""int unsigned long short char signed double float void Bool Status Window Drawable GC Atom Pixmap Colormap
Cursor Font Time KeySym KeyCode XID VisualID XPointer Display Screen Visual XEvent XColor XImage XGCValues XFontStruct
XRectangle XPoint XSegment XArc XTextItem XChar2b XSizeHints XWMHints XClassHint XSetWindowAttributes XWindowAttributes
XVisualInfo XTextProperty XrmDatabase XICCEncodingStyle String GContext _Xconst const Region XComposeStatus XKeyEvent
XModifierKeymap XMappingEvent XErrorHandler XIOErrorHandler Boolean""".split())

def split_params(p):
    out, depth, cur = [], 0, ""
    for ch in p:
        if ch == "(": depth += 1
        if ch == ")": depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def name_of(param, i):
    toks = re.findall(r"[A-Za-z_]\w*|\*|\[\]", param)
    if param == "void": return None, param
    if len(toks) >= 2 and re.match(r"^[A-Za-z_]\w*$", toks[-1]) and toks[-1] not in TYPES:
        return toks[-1], param
    n = "a%d_" % i
    return n, param + " " + n

pat = re.compile(r"^((?:[A-Za-z_][\w ]*?[\s\*]+))(X(?!t[A-Z]|m[A-Z])\w+) \(((?:Display|Screen) \*[^{;]*?)\)\s*\{", re.M)

for path in sys.argv[1:]:
    src = open(path).read()
    out, pos, count = [], 0, 0
    for m in pat.finditer(src):
        params = m.group(3)
        if "(*" in params or "..." in params: continue
        body_start = m.end()
        if "MQ_FWD" in src[body_start:body_start + 120]: continue
        ps = split_params(params)
        names, decls = [], []
        for i, p in enumerate(ps):
            n, d = name_of(p, i)
            names.append(n); decls.append(d)
        kind = "MQ_FWD_SCREEN" if ps[0].startswith("Screen") else "MQ_FWD"
        guard = " %s (%s, %s);" % (kind, m.group(2), ", ".join([names[0]] + names))
        out.append(src[pos:m.start(3)])
        out.append(", ".join(decls))
        out.append(src[m.end(3):body_start])
        out.append(guard)
        pos = body_start
        count += 1
    out.append(src[pos:])
    open(path, "w").write("".join(out))
    print(path, count, "guards")
