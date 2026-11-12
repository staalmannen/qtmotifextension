# a 3-page PDF with a nested outline (Introduction > Why, How; Widgets > Managers, Primitives; Custom classes)
objs = {}
def add(n, s): objs[n] = s
pages = [3, 4, 5]
add(1, "<< /Type /Catalog /Pages 2 0 R /Outlines 20 0 R /PageMode /UseOutlines >>")
add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>")
titles = ["Introduction", "Widgets", "Custom classes"]
for i, p in enumerate(pages):
    annots = " /Annots [30 0 R]" if i == 0 else ""
    add(p, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents %d 0 R /Resources << /Font << /F1 9 0 R >> >>%s >>" % (10 + i, annots))
    text = "BT /F1 36 Tf 72 700 Td (%s) Tj ET BT /F1 14 Tf 72 660 Td (Page %d of the outline test.) Tj ET" % (titles[i], i + 1)
    if i == 0: text += " BT /F1 14 Tf 72 620 Td (Go to the custom classes page) Tj ET"
    add(10 + i, "<< /Length %d >>\nstream\n%s\nendstream" % (len(text), text))
# a link on page 1 (around "Go to the custom classes page") to page 3
add(30, "<< /Type /Annot /Subtype /Link /Rect [70 615 300 635] /Border [0 0 1] /Dest [5 0 R /XYZ 0 792 0] >>")
add(9, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
# outline: 20 root; 21 Intro (22 Why, 23 How); 24 Widgets (25 Managers, 26 Primitives); 27 Custom
def item(n, title, parent, page, prev=None, nxt=None, first=None, last=None, count=None):
    s = "<< /Title (%s) /Parent %d 0 R /Dest [%d 0 R /XYZ 0 792 0]" % (title, parent, page)
    if prev: s += " /Prev %d 0 R" % prev
    if nxt: s += " /Next %d 0 R" % nxt
    if first: s += " /First %d 0 R /Last %d 0 R /Count %d" % (first, last, count)
    add(n, s + " >>")
add(20, "<< /Type /Outlines /First 21 0 R /Last 27 0 R /Count 7 >>")
item(21, "Introduction", 20, 3, nxt=24, first=22, last=23, count=2)
item(22, "Why", 21, 3, nxt=23)
item(23, "How", 21, 3, prev=22)
item(24, "Widgets", 20, 4, prev=21, nxt=27, first=25, last=26, count=-2)   # closed
item(25, "Managers", 24, 4, nxt=26)
item(26, "Primitives", 24, 4, prev=25)
item(27, "Custom classes", 20, 5, prev=24)
out = "%PDF-1.4\n"
offsets = {}
for n in sorted(objs):
    offsets[n] = len(out)
    out += "%d 0 obj\n%s\nendobj\n" % (n, objs[n])
xref = len(out)
mx = max(objs) + 1
out += "xref\n0 %d\n0000000000 65535 f \n" % mx
for n in range(1, mx):
    out += "%010d 00000 n \n" % offsets[n] if n in offsets else "0000000000 65535 f \n"
out += "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (mx, xref)
open("nested.pdf", "w").write(out)
