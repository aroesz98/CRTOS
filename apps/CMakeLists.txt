# Optional programs with a window (CRTOS_BUILD_APPS, on by default; "crtos build --base" leaves
# them out): games, the web browser, demos - and the programs "crtos new" makes. Installed in
# /sd/crtos/apps unless a program says otherwise (DEST); the programs menu lists them from
# etc/launcher.cfg. Every subdirectory with a CMakeLists.txt is one program.
set(CRTOS_DEFAULT_DEST apps)
crtos_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
