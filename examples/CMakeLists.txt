# Examples (CRTOS_BUILD_EXAMPLES, on by default): the smallest command-line program, a
# program with a window, and a driver module that another one depends on. The RTOS example
# (rtos/) is not built here: it is the application of the RTOS build ("crtos build --rtos").
set(CRTOS_DEFAULT_DEST bin)
add_subdirectory(hello)
set(CRTOS_DEFAULT_DEST apps)
add_subdirectory(hello_world)

# a driver module (binds to "crtos,hello") and one that uses its exported function
crtos_module(hello SOURCES drivers/hello/hello.c ALIASES crtos,hello)
crtos_module(hello_dep SOURCES drivers/hello/hello_dep.c)
