# The system's command-line programs - the shell and its tools - installed in /sd/crtos/bin
# (the shell finds them by name). Every subdirectory with a CMakeLists.txt is one program.
set(CRTOS_DEFAULT_DEST bin)
crtos_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
