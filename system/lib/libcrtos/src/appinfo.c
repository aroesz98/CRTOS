/*
 * appinfo.c - the default program header: a 16 KB stack for the main thread and a 64 KB
 * heap. The link asks for __crtos_app_info (crtos.specs: -u), so this member of libcrtos
 * comes in only when the program has no header of its own (CRTOS_APP() in crtos.h, or
 * STACK/HEAP of crtos_app() in CMake).
 */
#include <crtos.h>

CRTOS_APP(16384u, 65536u);
