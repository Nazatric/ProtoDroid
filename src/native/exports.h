// Entry points that other libraries resolve out of libmain.so by name.
//
// The Android build compiles with -fvisibility=hidden (item 4: direct calls instead of PLT stubs, and a .dynsym of a
// few hundred bytes instead of megabytes), and src/native/libmain.map lists exactly the names that must stay in
// .dynsym. A version script can only keep symbols the compiler exported: hidden visibility wins over the script, and a
// name listed as `global` that the compiler hid simply disappears from .dynsym without a link error. That is how
// protodroid_device_api_level was missing from the first real ARM64 build of this branch.
//
// So every definition named in the map carries PROTODROID_EXPORT. SDL_main does not need it here: SDL's own
// SDL_main.h declares it with SDLMAIN_DECLSPEC, which is __attribute__((visibility("default"))) on Android.
// tools/test_perf_program.py links a stub against libmain.map with the same flags to prove the pair works.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define PROTODROID_EXPORT __attribute__((visibility("default")))
#else
#define PROTODROID_EXPORT
#endif
