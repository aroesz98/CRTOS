# Layer 2: system services, installed in /sd/crtos/sbin and started by init (etc/init.cfg).
# Every subdirectory with a CMakeLists.txt is one program.
set(CRTOS_DEFAULT_DEST sbin)
crtos_add_subdirectories(${CMAKE_CURRENT_SOURCE_DIR})
