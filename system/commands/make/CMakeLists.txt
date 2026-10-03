# make: POSIX make on the board - pdpmake by Ron Yorston (public domain, third_party/pdpmake).
# Commands run through system(), i.e. "sh -c".
set(PDP ${CRTOS_TOP}/third_party/pdpmake)
crtos_app(make
  SOURCES ${PDP}/check.c ${PDP}/input.c ${PDP}/macro.c ${PDP}/main.c ${PDP}/make.c ${PDP}/modtime.c
          ${PDP}/rules.c ${PDP}/target.c ${PDP}/utils.c
  INCLUDES ${PDP}
  STACK 32768 HEAP 524288)
# (its character tables are indexed by char: fine for the ASCII it handles)
target_compile_options(app_make_objs PRIVATE -Wno-char-subscripts)
# checks: sh /sd/crtos/share/make/maketest.sh
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/test/Makefile share/make/Makefile)
crtos_file(${CMAKE_CURRENT_SOURCE_DIR}/test/maketest.sh share/make/maketest.sh)
