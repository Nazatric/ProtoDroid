# Lua 5.4.8

Upstream: https://github.com/lua/lua/tree/v5.4.8
License: MIT; see LICENSE and the copyright block in lua.h.

Only the core, auxiliary library, base, math, string, table, and UTF-8 sources are vendored. The standalone interpreter/compiler, internal tests, OS, I/O, package-loader, debug, and automatic `linit` sources are intentionally excluded. ProtoDroid opens a restricted library set manually and permits text-only chunks with its own allocator and instruction limits.
