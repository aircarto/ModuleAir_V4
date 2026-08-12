Import("env")
# ---------------------------------------------------------------------------
# Pre-build: embed web/app.html (gzipped) into src/web_index.h so the ESP32
# serves the ModuleAir SPA from PROGMEM. Single source of truth = web/app.html.
# (web/index.html is the standalone BLE-Improv page hosted elsewhere — NOT it.)
# src/web_index.h is generated, gitignored, and regenerated on every build.
# ---------------------------------------------------------------------------
import os, gzip

proj = env.get("PROJECT_DIR", os.getcwd())
src_html = os.path.join(proj, "web", "app.html")
out_h    = os.path.join(proj, "src", "web_index.h")

def build_header():
    if not os.path.exists(src_html):
        print("[web_embed] web/app.html introuvable, skip")
        return
    raw = open(src_html, "rb").read()
    gz = gzip.compress(raw, 9)
    # skip regeneration if unchanged (avoids needless rebuilds)
    stamp = "// src=%d gz=%d\n" % (len(raw), len(gz))
    if os.path.exists(out_h):
        first = open(out_h, "r", encoding="utf-8", errors="ignore").readline()
        if first == stamp:
            print("[web_embed] app.html inchange (%d B -> %d B gz)" % (len(raw), len(gz)))
            return
    with open(out_h, "w") as f:
        f.write(stamp)
        f.write("#pragma once\n#include <Arduino.h>\n")
        f.write("// Auto-genere depuis web/app.html par web_embed.py. NE PAS EDITER.\n")
        f.write("const unsigned WEB_INDEX_GZ_LEN = %d;\n" % len(gz))
        f.write("const uint8_t WEB_INDEX_GZ[] PROGMEM = {\n")
        for i in range(0, len(gz), 20):
            f.write("  " + ",".join(str(b) for b in gz[i:i+20]) + ",\n")
        f.write("};\n")
    print("[web_embed] genere src/web_index.h (%d B -> %d B gz)" % (len(raw), len(gz)))

build_header()
