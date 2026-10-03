# sh: the shell, and its checks (sh /sd/crtos/share/sh/shtest.sh)
crtos_app(sh SOURCES sh.c STACK 16384 HEAP 65536)
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/shtest.sh share/sh/shtest.sh)
