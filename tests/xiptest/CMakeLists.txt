# xiptest: checks of a program that runs in place (built XIP onto /flash0/bin)
crtos_app(xiptest SOURCES xiptest.cpp XIP DEST flash0:bin STACK 16384 HEAP 262144)
