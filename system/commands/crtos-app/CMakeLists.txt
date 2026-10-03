# crtos-app on the board: a program's header (stack, heap), memory and the loader's checks.
# The same source as the PC's crtos-app of the toolchain (toolchain/crtos-app.c).
crtos_app(crtos-app SOURCES ${CRTOS_TOP}/toolchain/crtos-app.c STACK 8192 HEAP 3145728)
