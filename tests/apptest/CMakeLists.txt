# apptest: checks of the program interface (files, threads, IPC, memory protection...)
crtos_app(apptest SOURCES apptest.c STACK 16384 HEAP 262144)

# the same checks from a program that runs in place (/flash0/bin/apptest_xip.app)
crtos_app(apptest_xip SOURCES apptest.c XIP DEST flash0:bin STACK 16384 HEAP 1048576)
