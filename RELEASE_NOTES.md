# PS5 MicroPython v0.2.3 — Standalone release

Native MicroPython homebrew by **YAHBALLON**.

- Real MicroPython v1.26.1 interpreter with console output and demo programs.
- Built-in DualSense Python editor: create, edit, save and run scripts directly on PS5.
- Native DATA / USB0–USB7 script discovery and source loading.
- Native libkernel writing with safe temporary files and atomic rename.
- Up to 64 KiB Python files; persistent /data/PS5MicroPython/scripts folder.
- Bundled hello.py, fibonacci.py and collections.py.

![Real PS5 screenshot](https://raw.githubusercontent.com/Yahballon/PS5_MicroPython/main/screenshots/1.webp)

**Title ID:** PPSA99791 — extract the installer ZIP into /data/homebrew/.

User confirmed the v0.2.3 runtime, editor and script loading on a jailbroken PS5. Embedded MicroPython is not full CPython; no pip, C extensions or live REPL.
