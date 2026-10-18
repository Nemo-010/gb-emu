/* Tiny logging shim so the core builds for both host and wasm without pulling in
 * stdio in the hot path. */
#ifndef GB_LOG_H
#define GB_LOG_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define GB_LOG(...) emscripten_log(EM_LOG_CONSOLE, __VA_ARGS__)
#else
#include <stdio.h>
#define GB_LOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

#endif
