"""Writes a Lua file into a C header as one string constant. Usage: embed_lua.py <in.lua> <out.h> <name>"""
import sys

src, out, name = sys.argv[1:4]
text = open(src, encoding="utf-8").read()
esc = lambda l: l.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t")
body = "".join('\t"%s\\n"\r\n' % esc(l) for l in text.split("\n"))
open(out, "w", newline="").write("#pragma once\r\nstatic const char %s[] =\r\n%s\t;\r\n" % (name, body))
