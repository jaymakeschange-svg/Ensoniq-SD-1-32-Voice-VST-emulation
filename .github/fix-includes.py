# Adds a direct #include <string_view> to MAME sources that use std::string_view without
# including <string_view> or <string>. Newer MSVC STL versions no longer provide it transitively.
import os, re, sys
root = sys.argv[1]
dry = len(sys.argv) > 2 and sys.argv[2] == "--dry"
inc_ok = re.compile(r'^\s*#\s*include\s*<(string_view|string)>', re.M)
first_inc = re.compile(r'^\s*#\s*include\s*[<"][^>"]+[>"][^\n]*\n', re.M)
changed = 0
for sub in ("src/lib", "src/emu", "src/frontend", "src/osd", "src/devices", "src/mame/ensoniq", "src/mame/shared"):
    base = os.path.join(root, sub)
    for dp, _, files in os.walk(base):
        for f in files:
            if not f.endswith((".h", ".hpp", ".ipp", ".cpp")): continue
            p = os.path.join(dp, f)
            try: s = open(p, encoding="utf-8", errors="surrogateescape").read()
            except OSError: continue
            if "std::string_view" not in s or inc_ok.search(s): continue
            m = first_inc.search(s)
            if not m: print("NO-INCLUDE-LINE", p); continue
            changed += 1
            print("patch", os.path.relpath(p, root))
            if not dry:
                s = s[:m.end()] + "#include <string_view>\n" + s[m.end():]
                open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
print("files patched:", changed)
