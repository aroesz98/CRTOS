# term: terminal with an on-screen keyboard (runs sh)
# (heap: the clipboard text, up to 64 KB, when pasting or copying)
crtos_app(term SOURCES term.c STACK 8192 HEAP 163840 LIBS gfx ICON icon.png)
