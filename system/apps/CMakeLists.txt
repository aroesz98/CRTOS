# Layer 3: the system's programs with a window - window manager, terminal, files, settings,
# system monitor - installed in /sd/crtos/apps (the programs menu lists them from
# etc/launcher.cfg). Every subdirectory with a CMakeLists.txt is one program.
set(CRTOS_DEFAULT_DEST apps)
crtos_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
