/*
 * Motif on Qt - compound strings (XmString), font lists / render tables,
 * tab lists and parse tables.
 *
 * An XmString is a list of components exactly as Motif models it (tag, text,
 * locale text, separator, tab, direction, rendition begin/end); widgets show
 * the text with separators as line breaks. A font list / render table is a
 * list of renditions, each a tag plus a QFont (and optional colours).
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"
#include <cstring>

QFont mq_font_from_xlfd (const QString &name, bool *ok);
QFont mq_font_for (Font fid);
void mq_draw_text_lines (Drawable d, GC gc, const QFont &f, int x, int y, int width, int align,
                         const QStringList &lines, bool image, int underline_line, int underline_from, int underline_len);

/* ======================================================================
 * Representation
 * ====================================================================== */
namespace {

struct Comp {
  unsigned char type;
  QByteArray data;
  bool operator== (const Comp &o) const { return type == o.type && data == o.data; }
};

struct XmStr {
  QList<Comp> c;
};

struct Rendition {
  QByteArray tag;
  QByteArray font_name;
  XmFontType font_type = XmFONT_IS_FONT;
  QFont font;
  bool has_font = false;
  XFontStruct *fs = nullptr;
  Pixel fg = XmUNSPECIFIED_PIXEL, bg = XmUNSPECIFIED_PIXEL;
  unsigned char underline = XmAS_IS, strike = XmAS_IS;
  XmTabList tabs = nullptr;
};

struct RenderTable {
  QList<Rendition *> r;
  ~RenderTable () { qDeleteAll (r); }
};

struct Tab {
  float value;
  unsigned char units;
  XmOffsetModel model;
  unsigned char alignment;
  QByteArray decimal;
};

struct TabList {
  QList<Tab> tabs;
};

struct ParseMapping {
  QByteArray pattern;
  XmTextType pattern_type = XmCHARSET_TEXT;
  XmString substitute = nullptr;
  XmParseProc invoke = nullptr;
  XtPointer client_data = nullptr;
  XmIncludeStatus include = XmINSERT;
};

struct Context {
  XmStr *s;
  int pos = 0;
  QByteArray tag;
  XmStringDirection dir = XmSTRING_DIRECTION_L_TO_R;
};

}

static XmStr *S (XmString s) { return reinterpret_cast<XmStr *> (s); }
static XmString X (XmStr *s) { return reinterpret_cast<XmString> (s); }
static RenderTable *RT (XmFontList f) { return reinterpret_cast<RenderTable *> (f); }
static XmFontList FL (RenderTable *t) { return reinterpret_cast<XmFontList> (t); }
static Rendition *R (XmRendition r) { return reinterpret_cast<Rendition *> (r); }
static XmRendition RN (Rendition *r) { return reinterpret_cast<XmRendition> (r); }
static TabList *TL (XmTabList t) { return reinterpret_cast<TabList *> (t); }
static Tab *T (XmTab t) { return reinterpret_cast<Tab *> (t); }

static bool is_text (unsigned char t)
{
  return t == XmSTRING_COMPONENT_TEXT || t == XmSTRING_COMPONENT_LOCALE_TEXT || t == XmSTRING_COMPONENT_WIDECHAR_TEXT;
}

static bool is_default_tag (const QByteArray &t)
{
  return t.isEmpty () || t == XmFONTLIST_DEFAULT_TAG || t == _MOTIF_DEFAULT_LOCALE || t == XmSTRING_DEFAULT_CHARSET
      || t == XmSTRING_ISO8859_1;
}

/* Text components hold bytes in the locale encoding (UTF-8 nowadays); old
 * programs sometimes pass Latin-1 literals, so fall back to that. */
static QString decode (const QByteArray &b, unsigned char type = XmSTRING_COMPONENT_TEXT)
{
  if (type == XmSTRING_COMPONENT_WIDECHAR_TEXT) {
    QString out;
    const wchar_t *w = (const wchar_t *) b.constData ();
    for (int i = 0; i < (int) (b.size () / sizeof (wchar_t)); i++) out += QString::fromWCharArray (&w[i], 1);
    return out;
  }
  auto dec = QStringDecoder (QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  QString s = dec.decode (b);
  if (dec.hasError ()) return QString::fromLatin1 (b);
  return s;
}

static XmStr *make () { return new XmStr; }
static void add (XmStr *s, unsigned char t, const QByteArray &d = QByteArray ()) { s->c.append ({ t, d }); }

static XmStr *from_text (const char *text, const char *tag, bool split_lines, unsigned char text_type)
{
  XmStr *s = make ();
  if (tag && text_type == XmSTRING_COMPONENT_TEXT) add (s, XmSTRING_COMPONENT_TAG, tag);
  QByteArray t = text ? QByteArray (text) : QByteArray ();
  if (!split_lines) { add (s, text_type, t); return s; }
  QList<QByteArray> lines = t.split ('\n');
  for (int i = 0; i < lines.size (); i++) {
    if (i) add (s, XmSTRING_COMPONENT_SEPARATOR);
    if (!lines[i].isEmpty () || lines.size () == 1) add (s, text_type, lines[i]);
  }
  return s;
}

/* text of a string: separators become newlines, tabs become tab characters */
QString mq_xmstring_text (XmString xs)
{
  XmStr *s = S (xs);
  if (!s) return QString ();
  QString out;
  for (const Comp &c : s->c) {
    if (is_text (c.type)) out += decode (c.data, c.type);
    else if (c.type == XmSTRING_COMPONENT_SEPARATOR) out += '\n';
    else if (c.type == XmSTRING_COMPONENT_TAB) out += '\t';
  }
  return out;
}

XmString mq_xmstring_from (const QString &text)
{
  QByteArray b = text.toUtf8 ();
  return X (from_text (b.constData (), nullptr, true, XmSTRING_COMPONENT_LOCALE_TEXT));
}

QString mq_mnemonic_text (const QString &text, KeySym mnemonic)
{
  QString out = text;
  out.replace ('&', "&&");
  if (!mnemonic || mnemonic == NoSymbol) return out;
  QChar m (mnemonic < 0x100 ? (ushort) mnemonic : (ushort) (mnemonic & 0xffff));
  int i = out.indexOf (m);
  if (i < 0) i = out.indexOf (m, 0, Qt::CaseInsensitive);
  if (i >= 0) out.insert (i, '&');
  return out;
}

/* lines of text runs, each run carrying the rendition tag in effect */
struct Run { QString text; QByteArray tag; };
static QList<QList<Run>> runs (XmStr *s)
{
  QList<QList<Run>> lines (1);
  QByteArray tag = XmFONTLIST_DEFAULT_TAG;
  QList<QByteArray> stack;
  if (!s) return lines;
  for (const Comp &c : s->c) {
    switch (c.type) {
    case XmSTRING_COMPONENT_TAG: tag = c.data; break;
    case XmSTRING_COMPONENT_RENDITION_BEGIN: stack << tag; tag = c.data; break;
    case XmSTRING_COMPONENT_RENDITION_END: if (!stack.isEmpty ()) tag = stack.takeLast (); break;
    case XmSTRING_COMPONENT_SEPARATOR: lines.append (QList<Run> ()); break;
    case XmSTRING_COMPONENT_TAB: lines.last ().append ({ "\t", tag }); break;
    default:
      if (is_text (c.type)) lines.last ().append ({ decode (c.data, c.type), c.type == XmSTRING_COMPONENT_LOCALE_TEXT ? QByteArray (XmFONTLIST_DEFAULT_TAG) : tag });
    }
  }
  return lines;
}

/* ======================================================================
 * Font lists / render tables
 * ====================================================================== */
static QFont rendition_font (Rendition *r)
{
  if (r->has_font) return r->font;
  if (r->fs) return mq_font_for (r->fs->fid);
  if (!r->font_name.isEmpty ()) {
    bool ok;
    r->font = mq_font_from_xlfd (QString::fromLatin1 (r->font_name), &ok);
    r->has_font = true;
    return r->font;
  }
  return QApplication::font ();
}

static Rendition *find_rendition (RenderTable *t, const QByteArray &tag)
{
  if (!t || t->r.isEmpty ()) return nullptr;
  for (Rendition *r : t->r) if (r->tag == tag) return r;
  if (is_default_tag (tag)) {
    for (Rendition *r : t->r) if (is_default_tag (r->tag)) return r;
    return t->r.first ();
  }
  return nullptr;
}

static QFont font_for_tag (XmFontList fl, const QByteArray &tag)
{
  Rendition *r = find_rendition (RT (fl), tag);
  if (!r && RT (fl) && !RT (fl)->r.isEmpty ()) r = RT (fl)->r.first ();
  return r ? rendition_font (r) : QApplication::font ();
}

QFont mq_fontlist_font (XmFontList fl, bool *ok)
{
  RenderTable *t = RT (fl);
  if (!t || t->r.isEmpty ()) { *ok = false; return QApplication::font (); }
  *ok = true;
  return font_for_tag (fl, XmFONTLIST_DEFAULT_TAG);
}

/* "font[=tag], font[=tag]" or "font1;font2:tag" (font sets) */
XmFontList mq_fontlist_from_string (const char *spec)
{
  auto *t = new RenderTable;
  QString s = QString::fromLatin1 (spec ? spec : "fixed");
  for (QString part : s.split (',', Qt::SkipEmptyParts)) {
    part = part.trimmed ();
    auto *r = new Rendition;
    int colon = part.indexOf (':');
    if (colon >= 0) {   /* font set */
      r->tag = part.mid (colon + 1).trimmed ().toLatin1 ();
      part = part.left (colon).section (';', 0, 0).trimmed ();
      r->font_type = XmFONT_IS_FONTSET;
    } else {
      int eq = part.lastIndexOf ('=');
      if (eq >= 0) { r->tag = part.mid (eq + 1).trimmed ().toLatin1 (); part = part.left (eq).trimmed (); }
    }
    if (r->tag.isEmpty ()) r->tag = XmFONTLIST_DEFAULT_TAG;
    r->font_name = part.toLatin1 ();
    t->r.append (r);
  }
  if (t->r.isEmpty ()) { auto *r = new Rendition; r->tag = XmFONTLIST_DEFAULT_TAG; r->font_name = "fixed"; t->r.append (r); }
  return FL (t);
}

static Rendition *copy_rendition (const Rendition *r)
{
  auto *n = new Rendition (*r);
  return n;
}

/* ======================================================================
 * Measurement
 * ====================================================================== */
static QSize measure (XmFontList fl, XmStr *s, int *baseline)
{
  int w = 0, h = 0, first_ascent = -1;
  for (const QList<Run> &line : runs (s)) {
    int lw = 0, asc = 0, desc = 0;
    QFontMetrics def (font_for_tag (fl, XmFONTLIST_DEFAULT_TAG));
    asc = def.ascent ();
    desc = def.descent ();
    for (const Run &r : line) {
      QFontMetrics fm (font_for_tag (fl, r.tag));
      lw += r.text == "\t" ? fm.horizontalAdvance (' ') * 8 : fm.horizontalAdvance (r.text);
      asc = qMax (asc, fm.ascent ());
      desc = qMax (desc, fm.descent ());
    }
    if (first_ascent < 0) first_ascent = asc;
    w = qMax (w, lw);
    h += asc + desc;
  }
  if (baseline) *baseline = qMax (0, first_ascent);
  return QSize (w, h);
}

extern "C" {

/* ======================================================================
 * Creation
 * ====================================================================== */
XmString XmStringCreate (char *text, XmStringCharSet charset) { return X (from_text (text, charset ? charset : XmFONTLIST_DEFAULT_TAG, false, XmSTRING_COMPONENT_TEXT)); }
XmString XmStringCreateSimple (char *text) { return X (from_text (text, XmFONTLIST_DEFAULT_TAG, false, XmSTRING_COMPONENT_TEXT)); }
XmString XmStringCreateLocalized (String text) { return X (from_text (text, nullptr, true, XmSTRING_COMPONENT_LOCALE_TEXT)); }
XmString XmStringLtoRCreate (char *text, XmStringCharSet charset) { return X (from_text (text, charset ? charset : XmFONTLIST_DEFAULT_TAG, true, XmSTRING_COMPONENT_TEXT)); }
XmString XmStringCreateLtoR (char *text, XmStringCharSet charset) { return XmStringLtoRCreate (text, charset); }

XmString XmStringDirectionCreate (XmStringDirection direction)
{
  XmStr *s = make ();
  add (s, XmSTRING_COMPONENT_DIRECTION, QByteArray (1, (char) direction));
  return X (s);
}

XmString XmStringSeparatorCreate (void)
{
  XmStr *s = make ();
  add (s, XmSTRING_COMPONENT_SEPARATOR);
  return X (s);
}

XmString XmStringSegmentCreate (char *text, XmStringCharSet charset, XmStringDirection direction, Boolean separator)
{
  XmStr *s = make ();
  add (s, XmSTRING_COMPONENT_DIRECTION, QByteArray (1, (char) direction));
  add (s, XmSTRING_COMPONENT_TAG, charset ? charset : XmFONTLIST_DEFAULT_TAG);
  add (s, XmSTRING_COMPONENT_TEXT, text ? text : "");
  if (separator) add (s, XmSTRING_COMPONENT_SEPARATOR);
  return X (s);
}

XmString XmStringComponentCreate (XmStringComponentType tag, unsigned int length, XtPointer value)
{
  XmStr *s = make ();
  add (s, tag, value ? QByteArray ((const char *) value, length) : QByteArray ());
  return X (s);
}

/* ======================================================================
 * Parsing (XmStringParseText / XmStringGenerate)
 * ====================================================================== */
static int char_len (const char *p, const char *end, XmTextType type)
{
  if (type == XmWIDECHAR_TEXT) return sizeof (wchar_t);
  int n = mblen (p, end ? end - p : MB_CUR_MAX);
  return n > 0 ? n : 1;
}

XmString XmStringParseText (XtPointer text, XtPointer *text_end, XmStringTag tag, XmTextType type,
                            XmParseTable table, Cardinal count, XtPointer call_data)
{
  XmStr *s = make ();
  if (!text) return X (s);
  unsigned char ttype = type == XmWIDECHAR_TEXT ? XmSTRING_COMPONENT_WIDECHAR_TEXT
                      : (type == XmMULTIBYTE_TEXT || !tag || !strcmp (tag, _MOTIF_DEFAULT_LOCALE)) ? XmSTRING_COMPONENT_LOCALE_TEXT
                      : XmSTRING_COMPONENT_TEXT;
  if (ttype == XmSTRING_COMPONENT_TEXT) add (s, XmSTRING_COMPONENT_TAG, tag);
  const char *p = (const char *) text;
  const char *end = text_end && *text_end ? (const char *) *text_end : nullptr;
  if (!end) {
    if (type == XmWIDECHAR_TEXT) { const wchar_t *w = (const wchar_t *) text; while (*w) w++; end = (const char *) w; }
    else end = p + strlen (p);
  }
  QByteArray pending;
  auto flush = [&] () { if (!pending.isEmpty ()) { add (s, ttype, pending); pending.clear (); } };
  while (p < end) {
    bool matched = false;
    for (Cardinal i = 0; i < count && table; i++) {
      auto *m = reinterpret_cast<ParseMapping *> (table[i]);
      if (!m || m->pattern.isEmpty ()) continue;
      if (end - p < m->pattern.size () || memcmp (p, m->pattern.constData (), m->pattern.size ())) continue;
      matched = true;
      flush ();
      XmIncludeStatus st = m->include;
      XmString sub = m->substitute;
      XmString invoked = nullptr;
      const char *after = p + m->pattern.size ();
      if (st == XmINVOKE && m->invoke) {
        XtPointer pos = (XtPointer) p;
        st = m->invoke (&pos, (XtPointer) end, type, tag, (XmParseMapping) m, m->pattern.size (), &invoked, call_data);
        after = (const char *) pos;
        if (after == p) after = p + m->pattern.size ();
        sub = invoked;
      }
      if (sub) s->c += S (sub)->c;
      if (invoked) XmStringFree (invoked);
      p = after;
      if (st == XmTERMINATE) {
        if (text_end) *text_end = (XtPointer) p;
        return X (s);
      }
      break;
    }
    if (matched) continue;
    int n = char_len (p, end, type);
    pending.append (p, n);
    p += n;
  }
  flush ();
  if (text_end) *text_end = (XtPointer) p;
  return X (s);
}

XmString XmStringGenerate (XtPointer text, XmStringTag tag, XmTextType type, XmStringTag rendition)
{
  /* the default parse table: newline -> separator, tab -> tab */
  ParseMapping nl, tab;
  nl.pattern = "\n";
  nl.substitute = XmStringSeparatorCreate ();
  tab.pattern = "\t";
  tab.substitute = XmStringComponentCreate (XmSTRING_COMPONENT_TAB, 0, nullptr);
  XmParseMapping table[2] = { (XmParseMapping) &nl, (XmParseMapping) &tab };
  if (!tag) tag = (XmStringTag) (type == XmCHARSET_TEXT ? XmFONTLIST_DEFAULT_TAG : _MOTIF_DEFAULT_LOCALE);
  XmString r = XmStringParseText (text, nullptr, tag, type, type == XmWIDECHAR_TEXT ? nullptr : table, 2, nullptr);
  XmStringFree (nl.substitute);
  XmStringFree (tab.substitute);
  if (rendition) {
    XmString w = XmStringPutRendition (r, rendition);
    XmStringFree (r);
    r = w;
  }
  return r;
}

XmString XmStringPutRendition (XmString string, XmStringTag rendition)
{
  XmStr *s = make ();
  add (s, XmSTRING_COMPONENT_RENDITION_BEGIN, rendition);
  if (string) s->c += S (string)->c;
  add (s, XmSTRING_COMPONENT_RENDITION_END, rendition);
  return X (s);
}

XtPointer XmStringUnparse (XmString string, XmStringTag tag, XmTextType tag_type, XmTextType output_type,
                           XmParseTable table, Cardinal count, XmParseModel model)
{
  QByteArray out;
  XmStr *s = S (string);
  QByteArray cur_tag = XmFONTLIST_DEFAULT_TAG;
  bool seen_text = false;
  if (s) {
    /* index of the last text component, for XmOUTPUT_BETWEEN / BEGINNING / END */
    int last_text = -1, first_text = -1;
    for (int i = 0; i < s->c.size (); i++) if (is_text (s->c[i].type)) { if (first_text < 0) first_text = i; last_text = i; }
    for (int i = 0; i < s->c.size (); i++) {
      const Comp &c = s->c[i];
      if (c.type == XmSTRING_COMPONENT_TAG) { cur_tag = c.data; continue; }
      if (is_text (c.type)) {
        bool match = !tag || (tag_type == XmCHARSET_TEXT ? (cur_tag == tag || is_default_tag (tag)) : c.type != XmSTRING_COMPONENT_TEXT || is_default_tag (tag));
        if (match) {
          if (c.type == XmSTRING_COMPONENT_WIDECHAR_TEXT) out += decode (c.data, c.type).toUtf8 ();
          else out += c.data;
          seen_text = true;
        }
        continue;
      }
      bool want = model == XmOUTPUT_ALL || (model == XmOUTPUT_BETWEEN && i > first_text && i < last_text)
               || (model == XmOUTPUT_BEGINNING && i < last_text) || (model == XmOUTPUT_END && i > first_text)
               || (model == XmOUTPUT_BOTH);
      if (!want) continue;
      for (Cardinal m = 0; m < count && table; m++) {
        auto *pm = reinterpret_cast<ParseMapping *> (table[m]);
        if (pm && pm->substitute && S (pm->substitute)->c.size () == 1 && S (pm->substitute)->c[0] == c) { out += pm->pattern; break; }
      }
    }
  }
  Q_UNUSED (seen_text);
  if (output_type == XmWIDECHAR_TEXT) {
    std::wstring w = decode (out).toStdWString ();
    auto *buf = (wchar_t *) XtMalloc ((w.size () + 1) * sizeof (wchar_t));
    memcpy (buf, w.c_str (), (w.size () + 1) * sizeof (wchar_t));
    return (XtPointer) buf;
  }
  return (XtPointer) XtNewString (out.constData ());
}

/* ======================================================================
 * Contexts
 * ====================================================================== */
Boolean XmStringInitContext (XmStringContext *context, XmString string)
{
  if (!string) { *context = nullptr; return False; }
  auto *c = new Context;
  c->s = S (string);
  c->tag = XmFONTLIST_DEFAULT_TAG;
  *context = reinterpret_cast<XmStringContext> (c);
  return True;
}

void XmStringFreeContext (XmStringContext context) { delete reinterpret_cast<Context *> (context); }

XmStringComponentType XmStringPeekNextTriple (XmStringContext context)
{
  auto *c = reinterpret_cast<Context *> (context);
  if (!c || c->pos >= c->s->c.size ()) return XmSTRING_COMPONENT_END;
  return c->s->c[c->pos].type;
}

XmStringComponentType XmStringGetNextTriple (XmStringContext context, unsigned int *length, XtPointer *value)
{
  auto *c = reinterpret_cast<Context *> (context);
  *length = 0;
  *value = nullptr;
  if (!c || c->pos >= c->s->c.size ()) return XmSTRING_COMPONENT_END;
  const Comp &comp = c->s->c[c->pos++];
  *length = comp.data.size ();
  if (comp.type != XmSTRING_COMPONENT_SEPARATOR && comp.type != XmSTRING_COMPONENT_TAB) {
    char *v = (char *) XtMalloc (comp.data.size () + sizeof (wchar_t));
    memcpy (v, comp.data.constData (), comp.data.size ());
    memset (v + comp.data.size (), 0, sizeof (wchar_t));
    *value = v;
  }
  if (comp.type == XmSTRING_COMPONENT_TAG) c->tag = comp.data;
  if (comp.type == XmSTRING_COMPONENT_DIRECTION && !comp.data.isEmpty ()) c->dir = comp.data[0];
  return comp.type;
}

XmStringComponentType XmStringPeekNextComponent (XmStringContext context) { return XmStringPeekNextTriple (context); }

XmStringComponentType XmStringGetNextComponent (XmStringContext context, char **text, XmStringCharSet *charset,
                                                XmStringDirection *direction, XmStringComponentType *unknown_tag,
                                                unsigned short *unknown_length, unsigned char **unknown_value)
{
  unsigned int len;
  XtPointer v;
  XmStringComponentType t = XmStringGetNextTriple (context, &len, &v);
  switch (t) {
  case XmSTRING_COMPONENT_TEXT: case XmSTRING_COMPONENT_LOCALE_TEXT: *text = (char *) v; break;
  case XmSTRING_COMPONENT_TAG: *charset = (char *) v; break;
  case XmSTRING_COMPONENT_DIRECTION: *direction = v ? *(unsigned char *) v : 0; XtFree ((char *) v); break;
  case XmSTRING_COMPONENT_SEPARATOR: case XmSTRING_COMPONENT_END: break;
  default:
    if (unknown_tag) *unknown_tag = t;
    if (unknown_length) *unknown_length = len;
    if (unknown_value) *unknown_value = (unsigned char *) v; else XtFree ((char *) v);
    return XmSTRING_COMPONENT_UNKNOWN;
  }
  return t;
}

Boolean XmStringGetNextSegment (XmStringContext context, char **text, XmStringCharSet *charset,
                                XmStringDirection *direction, Boolean *separator)
{
  auto *c = reinterpret_cast<Context *> (context);
  if (!c) return False;
  /* skip to the next text component, tracking tag and direction */
  while (c->pos < c->s->c.size () && !is_text (c->s->c[c->pos].type)) {
    const Comp &comp = c->s->c[c->pos++];
    if (comp.type == XmSTRING_COMPONENT_TAG) c->tag = comp.data;
    else if (comp.type == XmSTRING_COMPONENT_DIRECTION && !comp.data.isEmpty ()) c->dir = comp.data[0];
  }
  if (c->pos >= c->s->c.size ()) return False;
  const Comp &comp = c->s->c[c->pos++];
  QByteArray t = comp.type == XmSTRING_COMPONENT_WIDECHAR_TEXT ? decode (comp.data, comp.type).toUtf8 () : comp.data;
  if (text) *text = XtNewString (t.constData ());
  if (charset) *charset = XtNewString (comp.type == XmSTRING_COMPONENT_TEXT ? c->tag.constData () : XmFONTLIST_DEFAULT_TAG);
  if (direction) *direction = c->dir;
  bool sep = false;
  while (c->pos < c->s->c.size () && !is_text (c->s->c[c->pos].type)) {
    const Comp &n = c->s->c[c->pos];
    if (n.type == XmSTRING_COMPONENT_SEPARATOR) { sep = true; c->pos++; break; }
    if (n.type == XmSTRING_COMPONENT_TAG || n.type == XmSTRING_COMPONENT_DIRECTION) break;
    c->pos++;
  }
  if (separator) *separator = sep;
  return True;
}

Boolean XmStringGetLtoR (XmString string, XmStringCharSet charset, char **text)
{
  *text = nullptr;
  XmStr *s = S (string);
  if (!s) return False;
  QByteArray want = charset ? charset : XmFONTLIST_DEFAULT_TAG;
  QByteArray cur = XmFONTLIST_DEFAULT_TAG, out;
  bool found = false;
  for (const Comp &c : s->c) {
    if (c.type == XmSTRING_COMPONENT_TAG) cur = c.data;
    else if (is_text (c.type)) {
      bool match = cur == want || (is_default_tag (want) && (c.type != XmSTRING_COMPONENT_TEXT || is_default_tag (cur)));
      if (match) { out += c.type == XmSTRING_COMPONENT_WIDECHAR_TEXT ? decode (c.data, c.type).toUtf8 () : c.data; found = true; }
    } else if (c.type == XmSTRING_COMPONENT_SEPARATOR && found) out += '\n';
  }
  if (!found) return False;
  *text = XtNewString (out.constData ());
  return True;
}

/* ======================================================================
 * Operations
 * ====================================================================== */
XmString XmStringCopy (XmString string)
{
  if (!string) return nullptr;
  return X (new XmStr (*S (string)));
}

void XmStringFree (XmString string) { delete S (string); }

XmString XmStringConcat (XmString a, XmString b)
{
  XmStr *s = make ();
  if (a) s->c += S (a)->c;
  if (b) s->c += S (b)->c;
  return X (s);
}

XmString XmStringConcatAndFree (XmString a, XmString b)
{
  XmString r = XmStringConcat (a, b);
  XmStringFree (a);
  XmStringFree (b);
  return r;
}

static int comp_bytes (const Comp &c) { return 2 + c.data.size (); }

static XmStr *ncopy (XmStr *src, int n)
{
  XmStr *s = make ();
  if (!src) return s;
  for (const Comp &c : src->c) {
    if (n <= 0) break;
    if (comp_bytes (c) <= n) { s->c.append (c); n -= comp_bytes (c); }
    else if (is_text (c.type) && n > 2) { s->c.append ({ c.type, c.data.left (n - 2) }); n = 0; }
    else break;
  }
  return s;
}

XmString XmStringNCopy (XmString str, int n) { return X (ncopy (S (str), n)); }

XmString XmStringNConcat (XmString first, XmString second, int n)
{
  XmStr *s = make ();
  if (first) s->c += S (first)->c;
  XmStr *tail = ncopy (S (second), n);
  s->c += tail->c;
  delete tail;
  return X (s);
}

Boolean XmStringByteCompare (XmString a, XmString b)
{
  if (!a || !b) return a == b;
  return S (a)->c == S (b)->c;
}

Boolean XmStringCompare (XmString a, XmString b)
{
  if (!a || !b) return (!a || XmStringEmpty (a)) && (!b || XmStringEmpty (b));
  return mq_xmstring_text (a) == mq_xmstring_text (b);
}

int XmStringLength (XmString string)
{
  if (!string) return 0;
  int n = 4;
  for (const Comp &c : S (string)->c) n += comp_bytes (c);
  return n;
}

Boolean XmStringEmpty (XmString string)
{
  if (!string) return True;
  for (const Comp &c : S (string)->c) if (is_text (c.type) && !c.data.isEmpty ()) return False;
  return True;
}

Boolean XmStringIsVoid (XmString string)
{
  if (!string) return True;
  for (const Comp &c : S (string)->c)
    if (is_text (c.type) || c.type == XmSTRING_COMPONENT_SEPARATOR || c.type == XmSTRING_COMPONENT_TAB) return False;
  return True;
}

Boolean XmStringHasSubstring (XmString string, XmString substring)
{
  if (!string || !substring) return False;
  QString sub = mq_xmstring_text (substring);
  if (sub.isEmpty ()) return False;
  for (const Comp &c : S (string)->c)
    if (is_text (c.type) && decode (c.data, c.type).contains (sub)) return True;
  return False;
}

int XmStringLineCount (XmString string) { return string ? runs (S (string)).size () : 0; }

Dimension XmStringBaseline (XmFontList fontlist, XmString string)
{
  int b;
  measure (fontlist, S (string), &b);
  return b;
}

Dimension XmStringWidth (XmFontList fontlist, XmString string) { return measure (fontlist, S (string), nullptr).width (); }
Dimension XmStringHeight (XmFontList fontlist, XmString string) { return measure (fontlist, S (string), nullptr).height (); }

void XmStringExtent (XmFontList fontlist, XmString string, Dimension *width, Dimension *height)
{
  QSize sz = measure (fontlist, S (string), nullptr);
  if (width) *width = sz.width ();
  if (height) *height = sz.height ();
}

static void draw (Display *, Window w, XmFontList fl, XmString string, GC gc, int x, int y, int width, unsigned int align,
                  XRectangle *, bool image, XmString under)
{
  if (!string) return;
  QStringList lines = mq_xmstring_text (string).split ('\n');
  int ul_line = -1, ul_from = 0, ul_len = 0;
  if (under) {
    QString u = mq_xmstring_text (under);
    for (int i = 0; i < lines.size () && !u.isEmpty (); i++) {
      int at = lines[i].indexOf (u);
      if (at >= 0) { ul_line = i; ul_from = at; ul_len = u.size (); break; }
    }
  }
  mq_draw_text_lines (w, gc, font_for_tag (fl, XmFONTLIST_DEFAULT_TAG), x, y, width, align, lines, image, ul_line, ul_from, ul_len);
}

void XmStringDraw (Display *d, Window w, XmFontList fl, XmString s, GC gc, Position x, Position y, Dimension width, unsigned char align, unsigned char, XRectangle *clip)
{
  draw (d, w, fl, s, gc, x, y, width, align, clip, false, nullptr);
}

void XmStringDrawImage (Display *d, Window w, XmFontList fl, XmString s, GC gc, Position x, Position y, Dimension width, unsigned char align, unsigned char, XRectangle *clip)
{
  draw (d, w, fl, s, gc, x, y, width, align, clip, true, nullptr);
}

void XmStringDrawUnderline (Display *d, Window w, XmFontList fl, XmString s, GC gc, Position x, Position y, Dimension width, unsigned char align, unsigned char, XRectangle *clip, XmString under)
{
  draw (d, w, fl, s, gc, x, y, width, align, clip, false, under);
}

/* byte streams: [type][len hi][len lo][data] ... */
unsigned int XmCvtXmStringToByteStream (XmString string, unsigned char **prop_return)
{
  QByteArray b ("\xdf\x80", 2);
  if (string)
    for (const Comp &c : S (string)->c) {
      b.append ((char) c.type);
      b.append ((char) ((c.data.size () >> 8) & 0xff));
      b.append ((char) (c.data.size () & 0xff));
      b.append (c.data);
    }
  if (prop_return) {
    *prop_return = (unsigned char *) XtMalloc (b.size ());
    memcpy (*prop_return, b.constData (), b.size ());
  }
  return b.size ();
}

unsigned int XmStringByteStreamLength (unsigned char *string)
{
  (void) string;
  return 0;   /* the stream is self-delimiting only with its length; callers pass the property length */
}

XmString XmCvtByteStreamToXmString (unsigned char *property)
{
  XmStr *s = make ();
  if (!property || property[0] != 0xdf || property[1] != 0x80) return X (s);
  /* streams created here end with an END marker appended by the clipboard code; parse until 0 type */
  unsigned char *p = property + 2;
  while (*p && *p != XmSTRING_COMPONENT_END) {
    int len = (p[1] << 8) | p[2];
    s->c.append ({ p[0], QByteArray ((const char *) p + 3, len) });
    p += 3 + len;
  }
  return X (s);
}

/* compound text is treated as the locale encoding */
char *XmCvtXmStringToCT (XmString string) { return XtNewString (mq_xmstring_text (string).toUtf8 ().constData ()); }
XmString XmCvtCTToXmString (char *text) { return XmStringCreateLocalized (text); }
char *XmCvtXmStringToUTF8String (XmString string) { return XtNewString (mq_xmstring_text (string).toUtf8 ().constData ()); }

Boolean XmCvtTextToXmString (Display *, XrmValuePtr, Cardinal *, XrmValue *from, XrmValue *to, XtPointer *)
{
  static XmString result;
  result = XmStringCreateLocalized ((char *) from->addr);
  if (to->addr) { if (to->size < sizeof (XmString)) { to->size = sizeof (XmString); return False; } *(XmString *) to->addr = result; }
  else to->addr = (XPointer) &result;
  to->size = sizeof (XmString);
  return True;
}

Boolean XmCvtXmStringToText (Display *, XrmValuePtr, Cardinal *, XrmValue *from, XrmValue *to, XtPointer *)
{
  static char *result;
  result = XmCvtXmStringToCT (*(XmString *) from->addr);
  if (to->addr) { if (to->size < sizeof (char *)) { to->size = sizeof (char *); return False; } *(char **) to->addr = result; }
  else to->addr = (XPointer) &result;
  to->size = sizeof (char *);
  return True;
}

XmStringDirection XmDirectionToStringDirection (XmDirection dir)
{
  if (dir == XmDEFAULT_DIRECTION) return XmSTRING_DIRECTION_DEFAULT;
  return (dir & XmRIGHT_TO_LEFT_MASK) == XmRIGHT_TO_LEFT_MASK && !(dir & XmLEFT_TO_RIGHT_MASK) ? XmSTRING_DIRECTION_R_TO_L : XmSTRING_DIRECTION_L_TO_R;
}

XmDirection XmStringDirectionToDirection (XmStringDirection dir)
{
  if (dir == XmSTRING_DIRECTION_R_TO_L) return XmRIGHT_TO_LEFT;
  if (dir == XmSTRING_DIRECTION_L_TO_R) return XmLEFT_TO_RIGHT;
  return XmDEFAULT_DIRECTION;
}

Boolean XmDirectionMatch (XmDirection d1, XmDirection d2) { return XmDirectionToStringDirection (d1) == XmDirectionToStringDirection (d2); }
Boolean XmDirectionMatchPartial (XmDirection d1, XmDirection d2, XmDirection mask) { return (d1 & mask) == (d2 & mask); }

/* ======================================================================
 * String tables
 * ====================================================================== */
Cardinal XmStringToXmStringTable (XmString string, XmString break_comp, XmStringTable *table)
{
  QList<XmStr *> parts;
  parts << make ();
  unsigned char brk = break_comp && !S (break_comp)->c.isEmpty () ? S (break_comp)->c[0].type : 0xff;
  if (string)
    for (const Comp &c : S (string)->c) {
      if (break_comp && c.type == brk) parts << make ();
      else parts.last ()->c.append (c);
    }
  if (table) {
    *table = (XmStringTable) XtMalloc (parts.size () * sizeof (XmString));
    for (int i = 0; i < parts.size (); i++) (*table)[i] = X (parts[i]);
  } else qDeleteAll (parts);
  return parts.size ();
}

XmString XmStringTableToXmString (XmStringTable table, Cardinal count, XmString break_component)
{
  XmStr *s = make ();
  for (Cardinal i = 0; i < count; i++) {
    if (i && break_component) s->c += S (break_component)->c;
    if (table[i]) s->c += S (table[i])->c;
  }
  return X (s);
}

XtPointer *XmStringTableUnparse (XmStringTable table, Cardinal count, XmStringTag tag, XmTextType tag_type, XmTextType output_type,
                                 XmParseTable parse, Cardinal parse_count, XmParseModel model)
{
  auto **out = (XtPointer *) XtMalloc ((count + 1) * sizeof (XtPointer));
  for (Cardinal i = 0; i < count; i++) out[i] = XmStringUnparse (table[i], tag, tag_type, output_type, parse, parse_count, model);
  out[count] = nullptr;
  return out;
}

XmStringTable XmStringTableParseStringArray (XtPointer *strings, Cardinal count, XmStringTag tag, XmTextType type,
                                             XmParseTable parse, Cardinal parse_count, XtPointer call_data)
{
  auto t = (XmStringTable) XtMalloc (count * sizeof (XmString));
  for (Cardinal i = 0; i < count; i++) t[i] = XmStringParseText (strings[i], nullptr, tag, type, parse, parse_count, call_data);
  return t;
}

/* ======================================================================
 * Parse mappings
 * ====================================================================== */
static void mapping_args (ParseMapping *m, ArgList args, Cardinal n, bool set)
{
  for (Cardinal i = 0; i < n; i++) {
    const char *name = args[i].name;
    if (!strcmp (name, XmNpattern)) {
      if (set) m->pattern = args[i].value ? QByteArray ((const char *) args[i].value) : QByteArray ();
      else *(XtPointer *) args[i].value = (XtPointer) m->pattern.constData ();
    } else if (!strcmp (name, XmNpatternType)) {
      if (set) m->pattern_type = (XmTextType) args[i].value; else *(XmTextType *) args[i].value = m->pattern_type;
    } else if (!strcmp (name, XmNsubstitute)) {
      if (set) { XmStringFree (m->substitute); m->substitute = XmStringCopy ((XmString) args[i].value); }
      else *(XmString *) args[i].value = XmStringCopy (m->substitute);
    } else if (!strcmp (name, XmNinvokeParseProc)) {
      if (set) m->invoke = (XmParseProc) args[i].value; else *(XmParseProc *) args[i].value = m->invoke;
    } else if (!strcmp (name, XmNclientData)) {
      if (set) m->client_data = (XtPointer) args[i].value; else *(XtPointer *) args[i].value = m->client_data;
    } else if (!strcmp (name, XmNincludeStatus)) {
      if (set) m->include = (XmIncludeStatus) args[i].value; else *(XmIncludeStatus *) args[i].value = m->include;
    }
  }
}

XmParseMapping XmParseMappingCreate (ArgList args, Cardinal n)
{
  auto *m = new ParseMapping;
  mapping_args (m, args, n, true);
  return reinterpret_cast<XmParseMapping> (m);
}

void XmParseMappingSetValues (XmParseMapping m, ArgList args, Cardinal n) { mapping_args (reinterpret_cast<ParseMapping *> (m), args, n, true); }
void XmParseMappingGetValues (XmParseMapping m, ArgList args, Cardinal n) { mapping_args (reinterpret_cast<ParseMapping *> (m), args, n, false); }

void XmParseMappingFree (XmParseMapping mapping)
{
  auto *m = reinterpret_cast<ParseMapping *> (mapping);
  if (!m) return;
  XmStringFree (m->substitute);
  delete m;
}

void XmParseTableFree (XmParseTable table, Cardinal count)
{
  for (Cardinal i = 0; i < count; i++) XmParseMappingFree (table[i]);
  XtFree ((char *) table);
}

/* ======================================================================
 * Font lists (Motif 1.x API) on top of render tables
 * ====================================================================== */
XmFontListEntry XmFontListEntryCreate (char *tag, XmFontType type, XtPointer font)
{
  auto *r = new Rendition;
  r->tag = tag ? tag : XmFONTLIST_DEFAULT_TAG;
  r->font_type = type;
  if (type == XmFONT_IS_FONT && font) r->fs = (XFontStruct *) font;
  else if (font) { r->font = mq_font_for ((Font) (quintptr) font); r->has_font = true; }
  return (XmFontListEntry) r;
}

XmFontListEntry XmFontListEntryCreate_r (char *tag, XmFontType type, XtPointer font, Widget) { return XmFontListEntryCreate (tag, type, font); }

XmFontListEntry XmFontListEntryLoad (Display *, char *fontName, XmFontType type, char *tag)
{
  auto *r = new Rendition;
  r->tag = tag ? tag : XmFONTLIST_DEFAULT_TAG;
  r->font_type = type;
  r->font_name = fontName ? fontName : "fixed";
  if (type == XmFONT_IS_FONTSET) r->font_name = r->font_name.split (';').first ().split (',').first ();
  return (XmFontListEntry) r;
}

void XmFontListEntryFree (XmFontListEntry *entry)
{
  if (entry && *entry) { delete reinterpret_cast<Rendition *> (*entry); *entry = nullptr; }
}

XtPointer XmFontListEntryGetFont (XmFontListEntry entry, XmFontType *type)
{
  auto *r = reinterpret_cast<Rendition *> (entry);
  if (!r) return nullptr;
  if (type) *type = XmFONT_IS_FONT;
  if (!r->fs) r->fs = XLoadQueryFont (mq_display, r->font_name.isEmpty () ? "fixed" : r->font_name.constData ());
  return (XtPointer) r->fs;
}

char *XmFontListEntryGetTag (XmFontListEntry entry)
{
  auto *r = reinterpret_cast<Rendition *> (entry);
  return r ? XtNewString (r->tag.constData ()) : nullptr;
}

XmFontList XmFontListAppendEntry (XmFontList old, XmFontListEntry entry)
{
  RenderTable *t = old ? RT (old) : new RenderTable;
  if (entry) t->r.append (copy_rendition (reinterpret_cast<Rendition *> (entry)));
  return FL (t);
}

XmFontList XmFontListRemoveEntry (XmFontList old, XmFontListEntry entry)
{
  RenderTable *t = RT (old);
  auto *e = reinterpret_cast<Rendition *> (entry);
  if (!t || !e) return old;
  for (int i = 0; i < t->r.size (); i++)
    if (t->r[i] == e || (t->r[i]->tag == e->tag && t->r[i]->font_name == e->font_name && t->r[i]->fs == e->fs)) {
      delete t->r.takeAt (i);
      break;
    }
  return old;
}

XmFontList XmFontListCreate (XFontStruct *font, XmStringCharSet charset)
{
  auto *t = new RenderTable;
  auto *r = new Rendition;
  r->tag = charset ? charset : XmFONTLIST_DEFAULT_TAG;
  r->fs = font;
  t->r.append (r);
  return FL (t);
}

XmFontList XmFontListCreate_r (XFontStruct *font, XmStringCharSet charset, Widget) { return XmFontListCreate (font, charset); }
XmFontList XmStringCreateFontList (XFontStruct *font, XmStringCharSet charset) { return XmFontListCreate (font, charset); }
XmFontList XmStringCreateFontList_r (XFontStruct *font, XmStringCharSet charset, Widget) { return XmFontListCreate (font, charset); }

XmFontList XmFontListAdd (XmFontList old, XFontStruct *font, XmStringCharSet charset)
{
  if (!old) return XmFontListCreate (font, charset);
  auto *r = new Rendition;
  r->tag = charset ? charset : XmFONTLIST_DEFAULT_TAG;
  r->fs = font;
  RT (old)->r.append (r);
  return old;
}

void XmFontListFree (XmFontList fontlist) { delete RT (fontlist); }

XmFontList XmFontListCopy (XmFontList fontlist)
{
  if (!fontlist) return nullptr;
  auto *t = new RenderTable;
  for (Rendition *r : RT (fontlist)->r) t->r.append (copy_rendition (r));
  return FL (t);
}

struct FontContext { RenderTable *t; int pos = 0; };

Boolean XmFontListInitFontContext (XmFontContext *context, XmFontList fontlist)
{
  if (!fontlist) return False;
  *context = reinterpret_cast<XmFontContext> (new FontContext { RT (fontlist) });
  return True;
}

XmFontListEntry XmFontListNextEntry (XmFontContext context)
{
  auto *c = reinterpret_cast<FontContext *> (context);
  if (!c || c->pos >= c->t->r.size ()) return nullptr;
  return (XmFontListEntry) c->t->r[c->pos++];
}

Boolean XmFontListGetNextFont (XmFontContext context, XmStringCharSet *charset, XFontStruct **font)
{
  XmFontListEntry e = XmFontListNextEntry (context);
  if (!e) return False;
  if (charset) *charset = XmFontListEntryGetTag (e);
  if (font) *font = (XFontStruct *) XmFontListEntryGetFont (e, nullptr);
  return True;
}

void XmFontListFreeFontContext (XmFontContext context) { delete reinterpret_cast<FontContext *> (context); }

/* ======================================================================
 * Render tables (Motif 2.x API)
 * ====================================================================== */
static void rendition_args (Rendition *r, ArgList args, Cardinal n, bool set)
{
  for (Cardinal i = 0; i < n; i++) {
    const char *name = args[i].name;
    XtArgVal v = args[i].value;
#define GET(type, val) *(type *) v = (type) (val)
    if (!strcmp (name, XmNtag)) { if (set) r->tag = (char *) v; else GET (XmStringTag, r->tag.constData ()); }
    else if (!strcmp (name, XmNfontName)) { if (set) { r->font_name = (char *) v; r->has_font = false; r->fs = nullptr; } else GET (char *, r->font_name.constData ()); }
    else if (!strcmp (name, XmNfontType)) { if (set) r->font_type = (XmFontType) v; else GET (XmFontType, r->font_type); }
    else if (!strcmp (name, XmNfont)) {
      if (set) {
        r->has_font = false;
        if (r->font_type == XmFONT_IS_FONT) r->fs = (XFontStruct *) v;
        else { r->font = mq_font_for ((Font) v); r->has_font = true; }
      } else GET (XtPointer, XmFontListEntryGetFont ((XmFontListEntry) r, nullptr));
    }
    else if (!strcmp (name, XmNrenditionForeground)) { if (set) r->fg = v; else GET (Pixel, r->fg); }
    else if (!strcmp (name, XmNrenditionBackground)) { if (set) r->bg = v; else GET (Pixel, r->bg); }
    else if (!strcmp (name, XmNunderlineType)) { if (set) r->underline = v; else GET (unsigned char, r->underline); }
    else if (!strcmp (name, XmNstrikethruType)) { if (set) r->strike = v; else GET (unsigned char, r->strike); }
    else if (!strcmp (name, XmNtabList)) { if (set) r->tabs = (XmTabList) v; else GET (XmTabList, r->tabs); }
#undef GET
  }
}

XmRendition XmRenditionCreate (Widget, XmStringTag tag, ArgList args, Cardinal n)
{
  auto *r = new Rendition;
  r->tag = tag ? tag : XmFONTLIST_DEFAULT_TAG;
  rendition_args (r, args, n, true);
  return RN (r);
}

void XmRenditionUpdate (XmRendition r, ArgList args, Cardinal n) { if (r) rendition_args (R (r), args, n, true); }
void XmRenditionRetrieve (XmRendition r, ArgList args, Cardinal n) { if (r) rendition_args (R (r), args, n, false); }
void XmRenditionFree (XmRendition r) { delete R (r); }

XmRenderTable XmRenderTableAddRenditions (XmRenderTable old, XmRendition *renditions, Cardinal count, XmMergeMode mode)
{
  RenderTable *t = old ? RT ((XmFontList) old) : new RenderTable;
  for (Cardinal i = 0; i < count; i++) {
    Rendition *n = copy_rendition (R (renditions[i]));
    bool replaced = false;
    for (int j = 0; j < t->r.size (); j++)
      if (t->r[j]->tag == n->tag) {
        if (mode == XmSKIP) { delete n; } else { delete t->r[j]; t->r[j] = n; }
        replaced = true;
        break;
      }
    if (!replaced) t->r.append (n);
  }
  return (XmRenderTable) FL (t);
}

XmRendition XmRenderTableGetRendition (XmRenderTable table, XmStringTag tag)
{
  Rendition *r = find_rendition (RT ((XmFontList) table), tag ? tag : XmFONTLIST_DEFAULT_TAG);
  return r ? RN (copy_rendition (r)) : nullptr;
}

XmRendition *XmRenderTableGetRenditions (XmRenderTable table, XmStringTag *tags, Cardinal count)
{
  if (!table) return nullptr;
  RenderTable *t = RT ((XmFontList) table);
  if (!tags) count = t->r.size ();
  auto *out = (XmRendition *) XtMalloc (qMax<Cardinal> (1, count) * sizeof (XmRendition));
  for (Cardinal i = 0; i < count; i++) {
    Rendition *r = tags ? find_rendition (t, tags[i]) : t->r[i];
    out[i] = r ? RN (copy_rendition (r)) : nullptr;
  }
  return out;
}

int XmRenderTableGetTags (XmRenderTable table, XmStringTag **tag_list)
{
  RenderTable *t = RT ((XmFontList) table);
  int n = t ? t->r.size () : 0;
  *tag_list = (XmStringTag *) XtMalloc (qMax (1, n) * sizeof (XmStringTag));
  for (int i = 0; i < n; i++) (*tag_list)[i] = XtNewString (t->r[i]->tag.constData ());
  return n;
}

void XmRenderTableFree (XmRenderTable table) { XmFontListFree ((XmFontList) table); }

XmRenderTable XmRenderTableCopy (XmRenderTable table, XmStringTag *tags, int count)
{
  if (!table) return nullptr;
  if (!tags) return (XmRenderTable) XmFontListCopy ((XmFontList) table);
  auto *t = new RenderTable;
  for (int i = 0; i < count; i++)
    for (Rendition *r : RT ((XmFontList) table)->r)
      if (r->tag == tags[i]) t->r.append (copy_rendition (r));
  return (XmRenderTable) FL (t);
}

XmRenderTable XmRenderTableRemoveRenditions (XmRenderTable old, XmStringTag *tags, int count)
{
  RenderTable *t = RT ((XmFontList) old);
  if (!t) return old;
  for (int i = 0; i < count; i++)
    for (int j = t->r.size () - 1; j >= 0; j--)
      if (t->r[j]->tag == tags[i]) delete t->r.takeAt (j);
  return old;
}

XmRenderTable XmRenderTableCvtFromProp (Widget, char *prop, unsigned int len)
{
  return (XmRenderTable) mq_fontlist_from_string (QByteArray (prop, len).constData ());
}

unsigned int XmRenderTableCvtToProp (Widget, XmRenderTable table, char **prop_return)
{
  QByteArrayList parts;
  if (table) for (Rendition *r : RT ((XmFontList) table)->r) parts << r->font_name + "=" + r->tag;
  QByteArray b = parts.join (',');
  *prop_return = XtNewString (b.constData ());
  return b.size ();
}

/* ======================================================================
 * Tab lists
 * ====================================================================== */
XmTab XmTabCreate (float value, unsigned char units, XmOffsetModel model, unsigned char alignment, char *decimal)
{
  return reinterpret_cast<XmTab> (new Tab { value, units, model, alignment, decimal ? decimal : "" });
}

void XmTabFree (XmTab tab) { delete T (tab); }
void XmTabSetValue (XmTab tab, float value) { if (tab) T (tab)->value = value; }

float XmTabGetValues (XmTab tab, unsigned char *units, XmOffsetModel *offset, unsigned char *alignment, char **decimal)
{
  Tab *t = T (tab);
  if (units) *units = t->units;
  if (offset) *offset = t->model;
  if (alignment) *alignment = t->alignment;
  if (decimal) *decimal = (char *) t->decimal.constData ();
  return t->value;
}

XmTabList XmTabListInsertTabs (XmTabList old, XmTab *tabs, Cardinal count, int position)
{
  TabList *l = old ? new TabList (*TL (old)) : new TabList;
  int at = position < 0 ? l->tabs.size () : qMin (position, (int) l->tabs.size ());
  for (Cardinal i = 0; i < count; i++) l->tabs.insert (at + i, *T (tabs[i]));
  if (old) delete TL (old);
  return reinterpret_cast<XmTabList> (l);
}

XmTabList XmTabListRemoveTabs (XmTabList old, Cardinal *positions, Cardinal count)
{
  if (!old) return nullptr;
  TabList *l = new TabList (*TL (old));
  QList<Cardinal> pos (positions, positions + count);
  std::sort (pos.begin (), pos.end (), std::greater<Cardinal> ());
  for (Cardinal p : pos) if ((int) p < l->tabs.size ()) l->tabs.removeAt (p);
  delete TL (old);
  return reinterpret_cast<XmTabList> (l);
}

XmTabList XmTabListReplacePositions (XmTabList old, Cardinal *positions, XmTab *tabs, Cardinal count)
{
  if (!old) return nullptr;
  TabList *l = new TabList (*TL (old));
  for (Cardinal i = 0; i < count; i++) if ((int) positions[i] < l->tabs.size ()) l->tabs[positions[i]] = *T (tabs[i]);
  delete TL (old);
  return reinterpret_cast<XmTabList> (l);
}

XmTab XmTabListGetTab (XmTabList list, Cardinal position)
{
  if (!list || (int) position >= TL (list)->tabs.size ()) return nullptr;
  return reinterpret_cast<XmTab> (new Tab (TL (list)->tabs[position]));
}

Cardinal XmTabListTabCount (XmTabList list) { return list ? TL (list)->tabs.size () : 0; }

XmTabList XmTabListCopy (XmTabList list, int offset, Cardinal count)
{
  if (!list) return nullptr;
  TabList *l = new TabList;
  const QList<Tab> &src = TL (list)->tabs;
  if (!count) count = src.size ();
  for (Cardinal i = 0; i < count; i++) {
    int at = offset >= 0 ? offset + (int) i : src.size () - 1 + offset - (int) i;
    if (at >= 0 && at < src.size ()) l->tabs << src[at];
  }
  return reinterpret_cast<XmTabList> (l);
}

void XmTabListFree (XmTabList list) { delete TL (list); }

XmTabList XmStringTableProposeTablist (XmStringTable strings, Cardinal count, Widget, float pad, XmOffsetModel model)
{
  QList<int> widths;
  QFontMetrics fm (QApplication::font ());
  for (Cardinal i = 0; i < count; i++) {
    QStringList cols = mq_xmstring_text (strings[i]).split ('\t');
    for (int c = 0; c < cols.size (); c++) {
      if (widths.size () <= c) widths << 0;
      widths[c] = qMax (widths[c], fm.horizontalAdvance (cols[c]));
    }
  }
  auto *l = new TabList;
  float pos = 0;
  for (int c = 0; c + 1 < widths.size (); c++) {
    float v = widths[c] + pad;
    pos += v;
    l->tabs << Tab { model == XmABSOLUTE ? pos : v, XmPIXELS, model, XmALIGNMENT_BEGINNING, "" };
  }
  return reinterpret_cast<XmTabList> (l);
}

} /* extern "C" */
