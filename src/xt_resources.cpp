/*
 * Motif on Qt - resources: definitions, typed storage, string converters,
 * the Xrm resource database (with Xt's precedence rules), command line
 * options and XtGetApplicationResources.
 * Licensed under the LGPL 2.1 or later; see LICENSE.LGPL.
 */
#include "private.h"

#include <QFile>
#include <QDir>
#include <QColor>
#include <QRegularExpression>
#include <type_traits>
#include <cstring>
#include <cstdlib>

/* ======================================================================
 * Resource definitions
 * ====================================================================== */
#define RES(cls, constraint, name, rclass, rtype, size, dtype, dv, ds) \
  { cls, constraint, name, rclass, rtype, (unsigned) (size), dtype, (XtArgVal) (dv), ds },
static const ResDef motif_defs[] = {
#include "generated/resources.inc"
};
#undef RES

/* Xt's own classes are not in the Motif sources; these are from the Xt spec. */
static const ResDef xt_defs[] = {
  { "Object", false, XtNdestroyCallback, XtCCallback, XtRCallback, sizeof (XtCallbackList), XtRImmediate, 0, nullptr },
  { "RectObj", false, XtNx, XtCPosition, XtRPosition, sizeof (Position), XtRImmediate, 0, nullptr },
  { "RectObj", false, XtNy, XtCPosition, XtRPosition, sizeof (Position), XtRImmediate, 0, nullptr },
  { "RectObj", false, XtNwidth, XtCWidth, XtRDimension, sizeof (Dimension), XtRImmediate, 0, nullptr },
  { "RectObj", false, XtNheight, XtCHeight, XtRDimension, sizeof (Dimension), XtRImmediate, 0, nullptr },
  { "RectObj", false, XtNborderWidth, XtCBorderWidth, XtRDimension, sizeof (Dimension), XtRImmediate, 1, nullptr },
  { "RectObj", false, XtNsensitive, XtCSensitive, XtRBoolean, sizeof (Boolean), XtRImmediate, True, nullptr },
  { "RectObj", false, XtNancestorSensitive, XtCSensitive, XtRBoolean, sizeof (Boolean), XtRImmediate, True, nullptr },
  { "Core", false, XtNmappedWhenManaged, XtCMappedWhenManaged, XtRBoolean, sizeof (Boolean), XtRImmediate, True, nullptr },
  { "Core", false, XtNbackground, XtCBackground, XtRPixel, sizeof (Pixel), XmRCallProc, 0, nullptr },
  { "Core", false, XtNbackgroundPixmap, XtCPixmap, XtRPixmap, sizeof (Pixmap), XtRImmediate, XtUnspecifiedPixmap, nullptr },
  { "Core", false, XtNborderColor, XtCBorderColor, XtRPixel, sizeof (Pixel), XtRImmediate, 0, nullptr },
  { "Core", false, XtNborderPixmap, XtCPixmap, XtRPixmap, sizeof (Pixmap), XtRImmediate, XtUnspecifiedPixmap, nullptr },
  { "Core", false, XtNcolormap, XtCColormap, XtRColormap, sizeof (Colormap), XtRImmediate, 0, nullptr },
  { "Core", false, XtNdepth, XtCDepth, XtRInt, sizeof (int), XtRImmediate, 24, nullptr },
  { "Core", false, XtNscreen, XtCScreen, XtRScreen, sizeof (Screen *), XtRImmediate, 0, nullptr },
  { "Core", false, XtNtranslations, XtCTranslations, XtRTranslationTable, sizeof (XtTranslations), XtRImmediate, 0, nullptr },
  { "Core", false, XtNaccelerators, XtCAccelerators, XtRAcceleratorTable, sizeof (XtTranslations), XtRImmediate, 0, nullptr },
  { "Core", false, XtNinitialResourcesPersistent, XtCInitialResourcesPersistent, XtRBoolean, sizeof (Boolean), XtRImmediate, True, nullptr },
  { "Composite", false, XtNchildren, XtCReadOnly, XtRWidgetList, sizeof (WidgetList), XtRImmediate, 0, nullptr },
  { "Composite", false, XtNnumChildren, XtCReadOnly, XtRCardinal, sizeof (Cardinal), XtRImmediate, 0, nullptr },
  { "Composite", false, XtNinsertPosition, XtCInsertPosition, XtRFunction, sizeof (XtOrderProc), XtRImmediate, 0, nullptr },
  { "Shell", false, XtNallowShellResize, XtCAllowShellResize, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "Shell", false, XtNgeometry, XtCGeometry, XtRString, sizeof (String), XtRImmediate, 0, nullptr },
  { "Shell", false, XtNoverrideRedirect, XtCOverrideRedirect, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "Shell", false, XtNsaveUnder, XtCSaveUnder, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "Shell", false, XtNpopupCallback, XtCCallback, XtRCallback, sizeof (XtCallbackList), XtRImmediate, 0, nullptr },
  { "Shell", false, XtNpopdownCallback, XtCCallback, XtRCallback, sizeof (XtCallbackList), XtRImmediate, 0, nullptr },
  { "Shell", false, XtNvisual, XtCVisual, XtRVisual, sizeof (Visual *), XtRImmediate, 0, nullptr },
  { "WMShell", false, XtNtitle, XtCTitle, XtRString, sizeof (String), XtRImmediate, 0, nullptr },
  { "WMShell", false, XtNtitleEncoding, XtCTitleEncoding, XtRAtom, sizeof (Atom), XtRImmediate, 0, nullptr },
  { "WMShell", false, XtNtransient, XtCTransient, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "WMShell", false, XtNwaitForWm, XtCWaitForWm, XtRBoolean, sizeof (Boolean), XtRImmediate, True, nullptr },
  { "WMShell", false, XtNminWidth, XtCMinWidth, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNminHeight, XtCMinHeight, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNmaxWidth, XtCMaxWidth, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNmaxHeight, XtCMaxHeight, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNwidthInc, XtCWidthInc, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNheightInc, XtCHeightInc, XtRInt, sizeof (int), XtRImmediate, -1, nullptr },
  { "WMShell", false, XtNiconPixmap, XtCIconPixmap, XtRBitmap, sizeof (Pixmap), XtRImmediate, 0, nullptr },
  { "WMShell", false, XtNinput, XtCInput, XtRBool, sizeof (Bool), XtRImmediate, False, nullptr },
  { "WMShell", false, XtNwindowGroup, XtCWindowGroup, XtRWindow, sizeof (Window), XtRImmediate, 0, nullptr },
  { "WMShell", false, XtNurgency, XtCUrgency, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "TransientShell", false, XtNtransientFor, XtCTransientFor, XtRWidget, sizeof (Widget), XtRImmediate, 0, nullptr },
  { "TopLevelShell", false, XtNiconName, XtCIconName, XtRString, sizeof (String), XtRImmediate, 0, nullptr },
  { "TopLevelShell", false, XtNiconic, XtCIconic, XtRBoolean, sizeof (Boolean), XtRImmediate, False, nullptr },
  { "TopLevelShell", false, XtNiconNameEncoding, XtCIconNameEncoding, XtRAtom, sizeof (Atom), XtRImmediate, 0, nullptr },
  { "ApplicationShell", false, XtNargc, XtCArgc, XtRInt, sizeof (int), XtRImmediate, 0, nullptr },
  { "ApplicationShell", false, XtNargv, XtCArgv, XtRStringArray, sizeof (String *), XtRImmediate, 0, nullptr },
  /* Motif 2.3 tool tips are a VendorShell/Primitive/Gadget resource */
  { "XmPrimitive", false, "toolTipString", "ToolTipString", XmRXmString, sizeof (XmString), XtRImmediate, 0, nullptr },
  { "XmGadget", false, "toolTipString", "ToolTipString", XmRXmString, sizeof (XmString), XtRImmediate, 0, nullptr },
  { "XmManager", false, "toolTipString", "ToolTipString", XmRXmString, sizeof (XmString), XtRImmediate, 0, nullptr },
};

static QHash<QByteArray, QList<const ResDef *>> &defs_by_class (bool constraint)
{
  static QHash<QByteArray, QList<const ResDef *>> normal, cons;
  static bool init = false;
  if (!init) {
    init = true;
    for (const ResDef &d : motif_defs) (d.constraint ? cons : normal)[d.cls].append (&d);
    for (const ResDef &d : xt_defs) (d.constraint ? cons : normal)[d.cls].append (&d);
  }
  return constraint ? cons : normal;
}

static QList<const ResDef *> *resolve (MqClass *k, bool constraint)
{
  QList<const ResDef *> *&cache = constraint ? k->resolved_constraints : k->resolved;
  if (cache) return cache;
  cache = new QList<const ResDef *>;
  QSet<QByteArray> seen;
  for (MqClass *c = k; c; c = c->superclass) {
    const char *key = c->res_class ? c->res_class : c->class_name;
    for (const ResDef *d : defs_by_class (constraint).value (key))
      if (!seen.contains (d->name)) { seen.insert (d->name); cache->append (d); }
  }
  return cache;
}

const ResDef *mq_find_res (Widget w, const char *name, bool *constraint)
{
  if (!w || !name) return nullptr;
  for (const ResDef *d : *resolve (R (w)->klass, false))
    if (!strcmp (d->name, name)) { if (constraint) *constraint = false; return d; }
  if (R (w)->parent && (R (R (w)->parent)->klass->kind & K_CONSTRAINT))
    for (const ResDef *d : *resolve (R (R (w)->parent)->klass, true))
      if (!strcmp (d->name, name)) { if (constraint) *constraint = true; return d; }
  return nullptr;
}

/* ======================================================================
 * Typed storage
 * ====================================================================== */
enum Cat { C_RAW, C_XMSTRING, C_STRING, C_CALLBACK, C_TABLE };

static Cat category (const ResDef *d)
{
  const char *t = d->rtype;
  if (!strcmp (t, XmRXmString)) return C_XMSTRING;
  if (!strcmp (t, XmRString) && d->size == sizeof (String)) return C_STRING;
  if (!strcmp (t, XmRCallback)) return C_CALLBACK;
  if (!strcmp (t, XmRXmStringTable)) return C_TABLE;
  return C_RAW;
}

static void free_value (const ResDef *d, XtArgVal v)
{
  if (!v) return;
  switch (category (d)) {
  case C_XMSTRING: XmStringFree ((XmString) v); break;
  case C_STRING: XtFree ((char *) v); break;
  default: break;
  }
}

void mq_store (Widget w, const ResDef *d, XtArgVal v)
{
  QByteArray key = d->name;
  switch (category (d)) {
  case C_CALLBACK:
    for (XtCallbackList cl = (XtCallbackList) v; cl && cl->callback; cl++)
      R (w)->callbacks[key].append (*cl);
    return;
  case C_XMSTRING:
    v = v ? (XtArgVal) XmStringCopy ((XmString) v) : 0;
    break;
  case C_STRING:
    v = v ? (XtArgVal) XtNewString ((char *) v) : 0;
    break;
  default:
    break;
  }
  auto it = R (w)->res.find (key);
  if (it != R (w)->res.end ()) free_value (d, it.value ());
  R (w)->res.insert (key, v);
}

void mq_free_resources (Widget w)
{
  for (auto it = R (w)->res.begin (); it != R (w)->res.end (); ++it)
    if (const ResDef *d = mq_find_res (w, it.key ().constData ())) free_value (d, it.value ());
  R (w)->res.clear ();
}

bool mq_isset (Widget w, const char *name) { return w && R (w)->res.contains (name); }

/* Motif computes some defaults at run time (XmRCallProc); use the Qt palette */
static XtArgVal dynamic_default (Widget w, const ResDef *d)
{
  QPalette pal = QApplication::palette ();
  QByteArray n = d->name;
  if (!strcmp (d->rtype, XmRPixel)) {
    QColor bg = pal.color (QPalette::Button);
    if (n == XmNbackground) return mq_pixel (bg);
    if (n == XmNforeground || n == XmNhighlightColor || n == XmNborderColor) return mq_pixel (pal.color (QPalette::ButtonText));
    if (n == XmNtopShadowColor) return mq_pixel (bg.lighter (140));
    if (n == XmNbottomShadowColor) return mq_pixel (bg.darker (160));
    if (n == XmNselectColor || n == XmNarmColor || n == XmNtroughColor) return mq_pixel (bg.darker (120));
    return mq_pixel (bg);
  }
  if (n == XmNshadowThickness) return mq_kind (w, K_SHELL) ? 0 : 2;
  if (n == XmNhighlightThickness) return 0;
  return d->dimm;
}

static XtArgVal default_value (Widget w, const ResDef *d)
{
  if (d->dtype && !strcmp (d->dtype, XmRCallProc) && !d->dstr) return dynamic_default (w, d);
  if (d->dstr) {
    XtArgVal v = 0;
    if (mq_convert (w, d, d->dstr, &v)) {
      mq_store (w, d, v);   /* cache so it is converted (and freed) once */
      if (category (d) == C_XMSTRING) XmStringFree ((XmString) v);
      else if (category (d) == C_STRING) XtFree ((char *) v);
      return R (w)->res.value (d->name);
    }
    return 0;
  }
  return d->dimm;
}

XtArgVal mq_get (Widget w, const char *name)
{
  if (!w) return 0;
  auto it = R (w)->res.constFind (name);
  if (it != R (w)->res.constEnd ()) return it.value ();
  const ResDef *d = mq_find_res (w, name);
  return d ? default_value (w, d) : 0;
}

/* Raw values are stored as XtArgVal; narrow them the way Xt would. */
static long narrow (const ResDef *d, XtArgVal v)
{
  if (!d) return (long) v;
  switch (d->size) {
  case 1: return !strcmp (d->rtype, XmRBoolean) ? (long) (unsigned char) v : (long) (unsigned char) v;
  case 2: return strstr (d->rtype, "Position") ? (long) (short) v : (long) (unsigned short) v;
  case 4: return (long) (int) v;
  default: return (long) v;
  }
}

int mq_geti (Widget w, const char *name) { return (int) narrow (mq_find_res (w, name), mq_get (w, name)); }
bool mq_getb (Widget w, const char *name) { return mq_geti (w, name) != 0; }

QString mq_gets (Widget w, const char *name)
{
  const ResDef *d = mq_find_res (w, name);
  XtArgVal v = mq_get (w, name);
  if (!d || !v) return QString ();
  if (category (d) == C_XMSTRING) return mq_xmstring_text ((XmString) v);
  if (category (d) == C_STRING) return QString::fromLocal8Bit ((const char *) v);
  return QString ();
}

/* ======================================================================
 * Enum names
 * ====================================================================== */
template <typename T> static void add_enum (QHash<QByteArray, int> &h, const char *n, T v)
{
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) h.insert (n, (int) v);
}

static QHash<QByteArray, int> &enum_table ()
{
  static QHash<QByteArray, int> h;
  if (h.isEmpty ()) {
#define ENUM(x) add_enum (h, #x, x);
#include "generated/enums.inc"
#undef ENUM
    h.insert ("True", 1); h.insert ("False", 0);
  }
  return h;
}

int mq_enum_value (const char *name, bool *ok)
{
  QHash<QByteArray, int> &h = enum_table ();
  QByteArray s = QByteArray (name).trimmed ();
  bool num = false;
  int v = s.toInt (&num, 0);
  if (num) { *ok = true; return v; }
  QByteArray u = s.toUpper ();
  if (u.startsWith ("XM")) u = u.mid (2);
  auto it = h.constFind ("Xm" + u);
  if (it != h.constEnd ()) { *ok = true; return it.value (); }
  /* short forms: "form" -> XmATTACH_FORM, "column" -> XmPACK_COLUMN ... */
  int found = 0, value = 0;
  for (auto e = h.constBegin (); e != h.constEnd (); ++e)
    if (e.key ().endsWith ("_" + u)) { found++; value = e.value (); }
  *ok = found == 1;
  return value;
}

/* ======================================================================
 * Converters (string -> resource value)
 * ====================================================================== */
static QStringList split_table (const QString &s)
{
  /* comma separated, backslash escapes a comma */
  QStringList out;
  QString cur;
  for (int i = 0; i < s.size (); i++) {
    if (s[i] == '\\' && i + 1 < s.size ()) cur += s[++i];
    else if (s[i] == ',') { out << cur.trimmed (); cur.clear (); }
    else cur += s[i];
  }
  if (!cur.trimmed ().isEmpty () || !out.isEmpty ()) out << cur.trimmed ();
  return out;
}

bool mq_convert (Widget w, const ResDef *d, const char *str, XtArgVal *out)
{
  const char *t = d->rtype;
  QByteArray s = QByteArray (str);
  if (!strcmp (t, XmRXmString)) { *out = (XtArgVal) mq_xmstring_from (QString::fromLocal8Bit (s)); return true; }
  if (!strcmp (t, XmRString)) { *out = (XtArgVal) XtNewString (s.constData ()); return true; }
  if (!strcmp (t, XmRXmStringTable)) {
    QStringList parts = split_table (QString::fromLocal8Bit (s));
    auto *table = (XmString *) XtMalloc (sizeof (XmString) * (parts.size () + 1));
    for (int i = 0; i < parts.size (); i++) table[i] = mq_xmstring_from (parts[i]);
    table[parts.size ()] = nullptr;
    *out = (XtArgVal) table;
    return true;
  }
  QByteArray lower = s.trimmed ().toLower ();
  if (!strcmp (t, XmRBoolean) || !strcmp (t, XtRBool) || !strcmp (t, "Boolean")) {
    *out = lower == "true" || lower == "yes" || lower == "on" || lower == "1";
    return true;
  }
  if (!strcmp (t, XmRPixel)) {
    XColor c;
    if (XParseColor (mq_display, 0, s.trimmed ().constData (), &c)) {
      *out = (XtArgVal) mq_pixel (QColor (c.red >> 8, c.green >> 8, c.blue >> 8));
      return true;
    }
    return false;
  }
  if (!strcmp (t, XmRFontList) || !strcmp (t, XmRRenderTable) || !strcmp (t, "ButtonRenderTable") ||
      !strcmp (t, "LabelRenderTable") || !strcmp (t, "TextRenderTable") || !strcmp (t, XmRFontStruct)) {
    *out = (XtArgVal) mq_fontlist_from_string (s.constData ());
    return true;
  }
  if (!strcmp (t, XmRWidget) || !strcmp (t, XmRWindow)) {
    Widget ref = w && R (w)->parent ? XtNameToWidget (R (w)->parent, s.trimmed ().constData ()) : nullptr;
    *out = (XtArgVal) ref;
    return ref != nullptr;
  }
  if (!strcmp (t, XmRKeySym) || !strcmp (t, "Mnemonic")) {
    KeySym k = XStringToKeysym (s.trimmed ().constData ());
    if (k == NoSymbol && !s.isEmpty ()) k = (unsigned char) s.trimmed ()[0];
    *out = (XtArgVal) k;
    return true;
  }
  if (!strcmp (t, XmRTranslationTable) || !strcmp (t, XtRTranslationTable)) {
    *out = (XtArgVal) XtParseTranslationTable (s.constData ());
    return true;
  }
  if (!strcmp (t, XmRPixmap) || !strcmp (t, XmRBitmap) || !strcmp (t, "DynamicPixmap") || !strcmp (t, "PrimForegroundPixmap") ||
      !strcmp (t, "ManForegroundPixmap") || !strcmp (t, "GadgetPixmap"))
    return false;
  /* numbers, then enum names */
  bool ok = false;
  long n = s.trimmed ().toLong (&ok, 0);
  if (ok) { *out = (XtArgVal) n; return true; }
  if (!strcmp (t, XmRFloat)) { *out = 0; return false; }
  int e = mq_enum_value (s.constData (), &ok);
  if (ok) { *out = (XtArgVal) e; return true; }
  return false;
}

/* ======================================================================
 * The resource database
 * ====================================================================== */
struct DbComp { bool loose; QByteArray name; };
struct DbEntry { QList<DbComp> comps; QByteArray value; };

struct _XrmHashBucketRec {
  QList<DbEntry> entries;
};
static XrmDatabase s_app_db = nullptr;

static XrmDatabase app_db ()
{
  if (!s_app_db) s_app_db = new _XrmHashBucketRec;
  return s_app_db;
}

static void db_put (XrmDatabase db, const QByteArray &spec, const QByteArray &value)
{
  DbEntry e;
  bool loose = false;
  QByteArray cur;
  auto flush = [&] {
    if (!cur.isEmpty ()) { e.comps.append ({ loose, cur }); cur.clear (); loose = false; }
  };
  for (char c : spec.trimmed ()) {
    if (c == '*') { flush (); loose = true; }
    else if (c == '.') { flush (); }
    else if (c != ' ' && c != '\t') cur += c;
  }
  flush ();
  if (e.comps.isEmpty ()) return;
  e.value = value;
  for (DbEntry &old : db->entries) {
    if (old.comps.size () != e.comps.size ()) continue;
    bool same = true;
    for (int i = 0; i < e.comps.size () && same; i++)
      same = old.comps[i].loose == e.comps[i].loose && old.comps[i].name == e.comps[i].name;
    if (same) { old.value = value; return; }
  }
  db->entries.append (e);
}

static void db_put_line (XrmDatabase db, const QByteArray &line)
{
  QByteArray l = line;
  int colon = l.indexOf (':');
  if (colon <= 0 || l.trimmed ().startsWith ('!') || l.trimmed ().startsWith ('#')) return;
  QByteArray value = l.mid (colon + 1);
  while (!value.isEmpty () && (value[0] == ' ' || value[0] == '\t')) value.remove (0, 1);
  /* \n escapes as in X resource files */
  value.replace ("\\n", "\n");
  db_put (db, l.left (colon), value);
}

static void db_put_file_text (XrmDatabase db, const QByteArray &text)
{
  QByteArray joined = text;
  joined.replace ("\\\n", "");   /* line continuations */
  for (const QByteArray &line : joined.split ('\n')) db_put_line (db, line);
}

void mq_db_add_line (const char *line) { db_put_line (app_db (), line); }

/* Score of one level: skipped 0, '?' 2, class 4, name 6; +1 for a tight binding. */
typedef QVector<int> Score;

static bool better (const Score &a, const Score &b)
{
  for (int i = 0; i < a.size () && i < b.size (); i++)
    if (a[i] != b[i]) return a[i] > b[i];
  return false;
}

static bool match (const QList<DbComp> &comps, int j, const QList<QByteArray> &names, const QList<QByteArray> &classes,
                   int i, Score &cur, Score &best, bool &found)
{
  if (j == comps.size ()) {
    if (i == names.size () && (!found || better (cur, best))) { best = cur; found = true; }
    return found;
  }
  if (i >= names.size ()) return found;
  const DbComp &c = comps[j];
  int v = c.name == names[i] ? 6 : c.name == classes[i] ? 4 : c.name == "?" ? 2 : 0;
  if (v) {
    cur[i] = v + (c.loose ? 0 : 1);
    match (comps, j + 1, names, classes, i + 1, cur, best, found);
    cur[i] = 0;
  }
  if (c.loose && i < names.size () - 1) {   /* '*' may skip levels, never the resource itself */
    cur[i] = 0;
    match (comps, j, names, classes, i + 1, cur, best, found);
  }
  return found;
}

static const QByteArray *db_query (XrmDatabase db, const QList<QByteArray> &names, const QList<QByteArray> &classes)
{
  if (!db) return nullptr;
  const QByteArray *result = nullptr;
  Score best_overall;
  const QByteArray &rn = names.last (), &rc = classes.last ();
  for (const DbEntry &e : db->entries) {
    const QByteArray &last = e.comps.last ().name;
    if (last != rn && last != rc && last != "?") continue;
    Score cur (names.size (), 0), best;
    bool found = false;
    match (e.comps, 0, names, classes, 0, cur, best, found);
    if (found && (!result || better (best, best_overall) || best == best_overall)) {
      result = &e.value;
      best_overall = best;
    }
  }
  return result;
}

static QByteArray s_app_name = "motif", s_app_class = "Motif";

static void widget_path (Widget w, QList<QByteArray> &names, QList<QByteArray> &classes)
{
  QList<Widget> chain;
  for (Widget x = w; x; x = R (x)->parent) chain.prepend (x);
  for (Widget x : chain) {
    names << R (x)->name;
    if (!R (x)->parent && (R (x)->klass->kind & K_SHELL)) classes << s_app_class;
    else classes << QByteArray (R (x)->klass->class_name);
  }
}

const char *mq_lookup_db (Widget w, const char *name, const char *rclass)
{
  QList<QByteArray> names, classes;
  widget_path (w, names, classes);
  names << QByteArray (name);
  classes << QByteArray (rclass);
  const QByteArray *v = db_query (app_db (), names, classes);
  return v ? v->constData () : nullptr;
}

/* Fill a new widget from the database; args win over database entries. */
void mq_resources_from_db (Widget w, ArgList args, Cardinal n)
{
  QSet<QByteArray> given;
  for (Cardinal i = 0; i < n; i++) given.insert (args[i].name);
  QList<const ResDef *> defs = *resolve (R (w)->klass, false);
  if (R (w)->parent && (R (R (w)->parent)->klass->kind & K_CONSTRAINT)) defs += *resolve (R (R (w)->parent)->klass, true);
  QSet<QByteArray> lasts;
  for (const DbEntry &e : app_db ()->entries) lasts.insert (e.comps.last ().name);
  if (lasts.isEmpty ()) return;
  for (const ResDef *d : defs) {
    if (given.contains (d->name)) continue;
    if (!lasts.contains (d->name) && !lasts.contains (d->rclass) && !lasts.contains ("?")) continue;
    if (const char *s = mq_lookup_db (w, d->name, d->rclass)) {
      XtArgVal v = 0;
      if (mq_convert (w, d, s, &v)) {
        mq_store (w, d, v);
        if (category (d) == C_XMSTRING) XmStringFree ((XmString) v);
        else if (category (d) == C_STRING) XtFree ((char *) v);
      }
    }
  }
}

static bool load_file (XrmDatabase db, const QString &path)
{
  QFile f (path);
  if (!f.open (QIODevice::ReadOnly)) return false;
  db_put_file_text (db, f.readAll ());
  return true;
}

void mq_resources_init (const char *app_name, const char *app_class, String *fallback)
{
  if (app_name) s_app_name = app_name;
  if (app_class) s_app_class = app_class;
  XrmDatabase db = app_db ();
  /* app-defaults file, else the fallback resources (Xt's rule) */
  QStringList dirs;
  if (const char *d = getenv ("XAPPLRESDIR")) dirs << QString::fromLocal8Bit (d);
  dirs << QDir::homePath () << QStringLiteral ("/usr/share/X11/app-defaults") << QStringLiteral ("/etc/X11/app-defaults");
  bool found = false;
  for (const QString &d : dirs)
    if (load_file (db, d + "/" + QString::fromLocal8Bit (s_app_class))) { found = true; break; }
  if (!found)
    for (String *f = fallback; f && *f; f++) db_put_line (db, *f);
  load_file (db, QDir::homePath () + "/.Xdefaults");
  if (const char *env = getenv ("XENVIRONMENT")) load_file (db, QString::fromLocal8Bit (env));
}

const char *mq_app_name () { return s_app_name.constData (); }
const char *mq_app_class () { return s_app_class.constData (); }

/* ======================================================================
 * Command line options
 * ====================================================================== */
static XrmOptionDescRec std_options[] = {
  { (char *) "-bg", (char *) "*background", XrmoptionSepArg, nullptr },
  { (char *) "-background", (char *) "*background", XrmoptionSepArg, nullptr },
  { (char *) "-fg", (char *) "*foreground", XrmoptionSepArg, nullptr },
  { (char *) "-foreground", (char *) "*foreground", XrmoptionSepArg, nullptr },
  { (char *) "-fn", (char *) "*font", XrmoptionSepArg, nullptr },
  { (char *) "-font", (char *) "*font", XrmoptionSepArg, nullptr },
  { (char *) "-title", (char *) ".title", XrmoptionSepArg, nullptr },
  { (char *) "-geometry", (char *) ".geometry", XrmoptionSepArg, nullptr },
  { (char *) "-iconic", (char *) ".iconic", XrmoptionNoArg, (XPointer) "on" },
  { (char *) "-xrm", nullptr, XrmoptionResArg, nullptr },
  { (char *) "-display", nullptr, XrmoptionSkipArg, nullptr },
  { (char *) "-name", nullptr, XrmoptionSkipArg, nullptr },
  { (char *) "-synchronous", nullptr, XrmoptionSkipArg, nullptr },
  { (char *) "-rv", nullptr, XrmoptionSkipNArgs, nullptr },
  { (char *) "-reverse", nullptr, XrmoptionSkipNArgs, nullptr },
};

static void parse_options (XrmDatabase db, XrmOptionDescRec *opts, Cardinal n, int *argc, char **argv, const char *app_name)
{
  if (!argc || !argv) return;
  int out = 1;
  for (int i = 1; i < *argc; i++) {
    const char *a = argv[i];
    XrmOptionDescRec *hit = nullptr;
    auto find = [&] (XrmOptionDescRec *table, Cardinal count) {
      for (Cardinal k = 0; k < count && !hit; k++) {
        size_t len = strlen (table[k].option);
        if (table[k].argKind == XrmoptionStickyArg ? !strncmp (a, table[k].option, len) : !strcmp (a, table[k].option))
          hit = &table[k];
      }
    };
    find (opts, n);
    find (std_options, XtNumber (std_options));
    if (!hit) { argv[out++] = argv[i]; continue; }
    QByteArray spec = hit->specifier ? QByteArray (hit->specifier) : QByteArray ();
    if (spec.startsWith ('.')) spec.prepend (app_name);
    switch (hit->argKind) {
    case XrmoptionNoArg: db_put (db, spec, hit->value ? (const char *) hit->value : ""); break;
    case XrmoptionIsArg: db_put (db, spec, a); break;
    case XrmoptionStickyArg: db_put (db, spec, a + strlen (hit->option)); break;
    case XrmoptionSepArg: if (i + 1 < *argc) db_put (db, spec, argv[++i]); break;
    case XrmoptionResArg: if (i + 1 < *argc) db_put_line (db, argv[++i]); break;
    case XrmoptionSkipArg: i++; break;
    case XrmoptionSkipLine: i = *argc; break;
    default: break;
    }
  }
  argv[out] = nullptr;
  *argc = out;
}

void mq_parse_command_line (XrmOptionDescRec *opts, Cardinal n, int *argc, char **argv)
{
  parse_options (app_db (), opts, n, argc, argv, s_app_name.constData ());
}

/* ======================================================================
 * Public Xrm / application resource API
 * ====================================================================== */
static XtArgVal resource_default (Widget w, XtResource *r)
{
  if (!strcmp (r->default_type, XtRImmediate)) return (XtArgVal) r->default_addr;
  if (!strcmp (r->default_type, XtRString) && r->default_addr) {
    ResDef d { "", false, r->resource_name, r->resource_class, r->resource_type, r->resource_size, XtRString, 0, nullptr };
    XtArgVal v = 0;
    if (mq_convert (w, &d, (const char *) r->default_addr, &v)) return v;
    return 0;
  }
  if (r->default_addr && strcmp (r->default_type, XtRCallProc)) {
    XtArgVal v = 0;
    memcpy (&v, r->default_addr, qMin ((size_t) r->resource_size, sizeof (XtArgVal)));
    return v;
  }
  return 0;
}

static void write_sized (void *dst, unsigned size, XtArgVal v)
{
  switch (size) {
  case 1: *(char *) dst = (char) v; break;
  case 2: *(short *) dst = (short) v; break;
  case 4: *(int *) dst = (int) v; break;
  default: memcpy (dst, &v, qMin ((size_t) size, sizeof (XtArgVal))); break;
  }
}

static void get_resources (Widget w, const QList<QByteArray> &prefix_names, const QList<QByteArray> &prefix_classes,
                           XtPointer base, XtResourceList resources, Cardinal num, ArgList args, Cardinal num_args)
{
  for (Cardinal i = 0; i < num; i++) {
    XtResource *r = &resources[i];
    void *dst = (char *) base + r->resource_offset;
    bool done = false;
    for (Cardinal a = 0; a < num_args && !done; a++)
      if (!strcmp (args[a].name, r->resource_name)) {
        if (r->resource_size > sizeof (XtArgVal)) memcpy (dst, (void *) args[a].value, r->resource_size);
        else write_sized (dst, r->resource_size, args[a].value);
        done = true;
      }
    if (done) continue;
    QList<QByteArray> names = prefix_names, classes = prefix_classes;
    names << r->resource_name;
    classes << r->resource_class;
    if (const QByteArray *s = db_query (app_db (), names, classes)) {
      ResDef d { "", false, r->resource_name, r->resource_class, r->resource_type, r->resource_size, XtRString, 0, nullptr };
      XtArgVal v = 0;
      if (mq_convert (w, &d, s->constData (), &v)) { write_sized (dst, r->resource_size, v); continue; }
    }
    if (!strcmp (r->default_type, XtRImmediate) || !strcmp (r->default_type, XtRString) || !r->default_addr) {
      write_sized (dst, r->resource_size, resource_default (w, r));
    } else if (strcmp (r->default_type, XtRCallProc)) {
      memcpy (dst, r->default_addr, r->resource_size);
    }
  }
}

extern "C" {

void XtGetApplicationResources (Widget w, XtPointer base, XtResourceList resources, Cardinal num, ArgList args, Cardinal num_args)
{
  QList<QByteArray> names, classes;
  if (w) widget_path (w, names, classes);
  else { names << s_app_name; classes << s_app_class; }
  get_resources (w, names, classes, base, resources, num, args, num_args);
}

void XtVaGetApplicationResources (Widget w, XtPointer base, XtResourceList resources, Cardinal num, ...)
{
  QVector<Arg> args;
  va_list ap;
  va_start (ap, num);
  for (char *n = va_arg (ap, char *); n; n = va_arg (ap, char *)) args.append ({ n, va_arg (ap, XtArgVal) });
  va_end (ap);
  XtGetApplicationResources (w, base, resources, num, args.data (), args.size ());
}

void XtGetSubresources (Widget w, XtPointer base, _Xconst char *name, _Xconst char *class_, XtResourceList resources,
                        Cardinal num, ArgList args, Cardinal num_args)
{
  QList<QByteArray> names, classes;
  if (w) widget_path (w, names, classes);
  names << QByteArray (name);
  classes << QByteArray (class_);
  get_resources (w, names, classes, base, resources, num, args, num_args);
}

void XtVaGetSubresources (Widget w, XtPointer base, _Xconst char *name, _Xconst char *class_, XtResourceList resources, Cardinal num, ...)
{
  QVector<Arg> args;
  va_list ap;
  va_start (ap, num);
  for (char *n = va_arg (ap, char *); n; n = va_arg (ap, char *)) args.append ({ n, va_arg (ap, XtArgVal) });
  va_end (ap);
  XtGetSubresources (w, base, name, class_, resources, num, args.data (), args.size ());
}

void XtGetApplicationNameAndClass (Display *, String *name, String *class_)
{
  if (name) *name = s_app_name.data ();
  if (class_) *class_ = s_app_class.data ();
}

XrmDatabase XtDatabase (Display *) { return app_db (); }
XrmDatabase XtScreenDatabase (Screen *) { return app_db (); }

char *XGetDefault (Display * a0_, _Xconst char *program, _Xconst char *option)
{ MQ_FWD (XGetDefault, a0_, a0_, program, option);
  QList<QByteArray> names { QByteArray (program), QByteArray (option) }, classes { s_app_class, QByteArray (option) };
  const QByteArray *v = db_query (app_db (), names, classes);
  return v ? const_cast<char *> (v->constData ()) : nullptr;
}

void XrmInitialize (void) {}

XrmDatabase XrmGetStringDatabase (_Xconst char *data)
{
  auto *db = new _XrmHashBucketRec;
  if (data) db_put_file_text (db, data);
  return db;
}

XrmDatabase XrmGetFileDatabase (_Xconst char *filename)
{
  auto *db = new _XrmHashBucketRec;
  if (!load_file (db, QString::fromLocal8Bit (filename))) { delete db; return nullptr; }
  return db;
}

void XrmPutLineResource (XrmDatabase *db, _Xconst char *line)
{
  if (!*db) *db = new _XrmHashBucketRec;
  db_put_line (*db, line);
}

void XrmPutStringResource (XrmDatabase *db, _Xconst char *specifier, _Xconst char *str)
{
  if (!*db) *db = new _XrmHashBucketRec;
  db_put (*db, specifier, str);
}

/* the database holds strings: other types (NEdit puts a Visual) have no
 * meaning without an X server and are left out */
void XrmPutResource (XrmDatabase *db, _Xconst char *specifier, _Xconst char *type, XrmValue *value)
{
  if (!value || !value->addr || !type || strcmp (type, XtRString)) return;
  XrmPutStringResource (db, specifier, (const char *) value->addr);
}

void XtDisplayStringConversionWarning (Display *, _Xconst char *from, _Xconst char *to_type)
{
  fprintf (stderr, "Warning: Cannot convert string \"%s\" to type %s\n", from ? from : "", to_type ? to_type : "?");
}

static QHash<QByteArray, QByteArray> s_value_cache;

Bool XrmGetResource (XrmDatabase db, _Xconst char *str_name, _Xconst char *str_class, char **str_type, XrmValue *value)
{
  QList<QByteArray> names = QByteArray (str_name).split ('.'), classes = QByteArray (str_class ? str_class : str_name).split ('.');
  while (classes.size () < names.size ()) classes << names[classes.size ()];
  const QByteArray *v = db_query (db, names, classes);
  if (!v) return False;
  if (str_type) *str_type = (char *) "String";
  if (value) { value->addr = (XPointer) v->constData (); value->size = v->size () + 1; }
  return True;
}

void XrmMergeDatabases (XrmDatabase source, XrmDatabase *target)
{
  if (!source) return;
  if (!*target) { *target = source; return; }
  for (const DbEntry &e : source->entries) (*target)->entries.append (e);
  delete source;
}

void XrmCombineDatabase (XrmDatabase source, XrmDatabase *target, Bool)
{
  XrmMergeDatabases (source, target);
}

void XrmDestroyDatabase (XrmDatabase db) { if (db && db != s_app_db) delete db; }
void XrmSetDatabase (Display * a0_, XrmDatabase db) { MQ_FWD (XrmSetDatabase, a0_, a0_, db); if (db && db != s_app_db) { XrmMergeDatabases (db, &s_app_db); } }
XrmDatabase XrmGetDatabase (Display * a0_) { MQ_FWD (XrmGetDatabase, a0_, a0_); return app_db (); }
char *XResourceManagerString (Display * a0_) { MQ_FWD (XResourceManagerString, a0_, a0_); return nullptr; }

void XrmParseCommand (XrmDatabase *db, XrmOptionDescList table, int count, _Xconst char *name, int *argc, char **argv)
{
  if (!*db) *db = new _XrmHashBucketRec;
  parse_options (*db, table, count, argc, argv, name ? name : s_app_name.constData ());
}

/* Quarks */
static QHash<QByteArray, XrmQuark> &quarks ()
{
  static QHash<QByteArray, XrmQuark> q;
  return q;
}
static QList<QByteArray> &quark_names ()
{
  static QList<QByteArray> n { QByteArray () };
  return n;
}

XrmQuark XrmStringToQuark (_Xconst char *s)
{
  if (!s) return 0;
  auto it = quarks ().constFind (s);
  if (it != quarks ().constEnd ()) return it.value ();
  XrmQuark q = quark_names ().size ();
  quark_names () << QByteArray (s);
  quarks ().insert (s, q);
  return q;
}

XrmQuark XrmPermStringToQuark (_Xconst char *s) { return XrmStringToQuark (s); }
XrmString XrmQuarkToString (XrmQuark q) { return q > 0 && q < quark_names ().size () ? quark_names ()[q].data () : nullptr; }
XrmQuark XrmUniqueQuark (void) { quark_names () << QByteArray (); return quark_names ().size () - 1; }

} /* extern "C" */
