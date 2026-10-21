#!/usr/bin/env python3
"""Fast offline validation, before cross-compiling."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
meta = json.loads((root / "app.json").read_text())
assert meta["titleId"] == "PPSA99791"
assert meta["version"] == "0.2.3"
assert meta["contentVersion"] == "01.000.006"
assert meta["releaseTag"] == "v0.2.3"
assert re.fullmatch(r"UP[0-9]{4}-PPSA[0-9]{5}_00-[A-Z0-9]{16}", meta["contentId"])
for name in ("hello.py", "fibonacci.py", "collections.py"):
    src = (root / "assets/scripts" / name).read_text()
    compile(src, name, "exec")
    assert src.strip()
config = (root / "native/mpconfigport.h").read_text()
assert "MICROPY_ENABLE_COMPILER (1)" in config
assert "MICROPY_ENABLE_GC (1)" in config
ui = (root / "native/main.cpp").read_text()
for token in ("mp_embed_init(", "mp_embed_exec_str(", "mp_embed_deinit(",
              "ps5_mp_console_write", "/data/PS5MicroPython/scripts"):
    assert token in ui, token
assert "kVisibleScripts=8" in ui
assert "i<first+kVisibleScripts" in ui
assert "ADD YOUR OWN SCRIPTS" in ui
assert "Place your .py files here:" in ui
assert "Transfer via FTP or a file manager." in ui
assert "USB: /mnt/usb0/scripts  |  TRIANGLE rescan" in ui
assert "ui.bordered_rect({80,726,543,233}" in ui
assert "by YAHBALLON   v0.2.3" in ui
assert '#include "editor.hpp"' in ui
assert "start_new()" in ui and "start_edit(" in ui and "save_editor()" in ui
assert "pyedit::save_script(" in ui
assert "#define PS5_MICROPYTHON_NATIVE_STORAGE 1" in ui
assert "next_filename(" in ui
assert "report.filenames_seen" in ui
assert "SAVE " in ui
storage=(root / "native/editor.hpp").read_text()
for op in ("sceKernelOpen", "sceKernelWrite", "sceKernelFsync", "sceKernelRename", "sceKernelUnlink"):
    assert op in storage, op
assert "PYTHON CODE EDITOR" in ui
assert "DISCARD UNSAVED CHANGES?" in ui
assert '#include "scripts_scan.hpp"' in ui
assert "pyscan::scan_roots(pyscan::roots())" in ui
assert "scanStatus=result.status()" in ui
assert "#define PS5_MICROPYTHON_NATIVE_SCAN 1" in ui
assert "sceKernel" in (root / "native/scripts_scan.hpp").read_text()
assert "pyscan::read_script(script.path,kMaxSource)" in ui
assert "scanDetails" in ui
assert "scan.log" in ui
assert 'if(stat(path.c_str(),&st)==0 && S_ISREG' not in ui
print("Host-side source, script scan and metadata checks passed")
