# @NAME@: @TITLE@ - a program with a window (made by "crtos new"); icon.png is its icon in the
# programs menu, on the task bar and in the title bar
crtos_app(@NAME@ SOURCES @NAME@.c STACK 16384 HEAP 65536 LIBS gfx ICON icon.png)
