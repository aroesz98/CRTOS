# The base system: what every build of the whole system contains (user space, layers 2 and 3).
#
#   lib/        the libraries of programs: libcrtos (the system calls, POSIX, threads, sockets),
#               libgfx (windows, drawing, widgets), TFTLIB
#   services/   system services (layer 2), installed in /sd/crtos/sbin, started by init
#   commands/   command-line programs, installed in /sd/crtos/bin: the shell and its tools
#   apps/       the system's programs with a window, installed in /sd/crtos/apps: the window
#               manager, terminal, files, settings, system monitor
#
# Optional programs (games, the web browser, demos) are in apps/ at the top, tests in tests/,
# examples in examples/.
add_subdirectory(lib)
add_subdirectory(services)
add_subdirectory(commands)
add_subdirectory(apps)
