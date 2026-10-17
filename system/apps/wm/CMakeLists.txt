# wm: the window manager - task bar, programs menu, window frames
crtos_app(wm SOURCES wm.c STACK 8192 HEAP 262144 LIBS gfx)
# the icon of programs without their own
crtos_icon(application.png application)
# the Apps button's (without it the button shows the word "Apps")
crtos_icon(apps.png wm-apps)
target_compile_options(app_wm_objs PRIVATE -O3 -ffast-math -fno-math-errno)