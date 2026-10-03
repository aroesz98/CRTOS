# Tests and measurements run on the board (CRTOS_BUILD_TESTS, on by default): the programming
# interface (apptest), the heap (heaptest), programs in place (xiptest), speed (bench), input
# for the graphics server (gfxtap), the network (nettest, for "crtos netbench"), and a driver
# module that checks the display and touch screen. Programs go to /sd/crtos/bin unless they
# say otherwise (DEST flash0:bin).
set(CRTOS_DEFAULT_DEST bin)
crtos_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})

# the display, touch screen and user button (binds to the device tree node "crtos,touchpaint")
crtos_module(touchpaint SOURCES drivers/touchpaint/touchpaint.c ALIASES crtos,touchpaint)
