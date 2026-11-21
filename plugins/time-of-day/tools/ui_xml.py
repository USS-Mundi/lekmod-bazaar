"""Writes mod/TimeOfDay.xml from tools/TimeOfDay.xml.in, expanding @9(texture,corner) into the attributes of a
nine-slice Grid over a 32x32 texture (Civ V's Grid takes each piece's size and where it starts in the texture)."""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
T = 32


def nine(tex, c):
    c = int(c)
    e = T - c
    parts = dict(UL=(c, c, 0, 0), UC=(1, c, c, 0), UR=(c, c, e, 0), L=(c, 1, 0, c), C=(1, 1, c, c), R=(c, 1, e, c),
                 LL=(c, c, 0, e), LC=(1, c, c, e), LR=(c, c, e, e))
    attrs = ['Texture="%s.dds"' % tex]
    for k, (w, h, x, y) in parts.items():
        attrs.append('%sSize="%d,%d" %sTexStart="%d,%d"' % (k, w, h, k, x, y))
    return " ".join(attrs)


src = open(os.path.join(HERE, "TimeOfDay.xml.in"), encoding="utf-8").read()
out = re.sub(r"@9\((\w+),(\d+)\)", lambda m: nine(m.group(1), m.group(2)), src)
open(os.path.join(HERE, "..", "mod", "TimeOfDay.xml"), "w", encoding="utf-8", newline="\r\n").write(out)
print("wrote mod/TimeOfDay.xml")
