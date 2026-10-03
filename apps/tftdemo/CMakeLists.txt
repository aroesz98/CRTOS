# tftdemo: TFTLIB clock demo
crtos_app(tftdemo SOURCES tftdemo.cpp tftdemo_win.c STACK 8192 HEAP 65536 LIBS tftlib gfx ICON icon.png)
target_compile_options(app_tftdemo_objs PRIVATE -O3 -ffast-math -fno-math-errno)