# Issue #21: startup crash and `proto_uv.dat`

## Evidence

- Both supplied v0.13 logs initialize direct PanVK successfully on a Mali-G57 (Vulkan 1.3), reach frame 64, and then fault at guest address `0x1000000004` with the same native call stack.
- `f_7c486bf0` reads `[this + 4]`; the fault address is consistent with a null `this`. `proto::FrontEndState::vfunction1_for_IRealTimeTask` obtains that pointer from the global at `0x7d26b474`.
- Writes and nearby function names identify this global as the `proto::SaveGameManager` instance. Its constructor is at `0x10487c30` in the original DLL address space.
- `proto::GameEngine::vfunction10` creates the manager only when the byte at `0x7d0a0f24` is nonzero. The initialization routine at `0x104e1440` clears that byte when its startup object or subsequent validation fails.
- Byte inspection of a temporary DLL copy identifies the startup filename as `proto_uv.dat`. The subsequent routine processes a 1,024-byte structure and compares a 20-byte result. The tracked DLL and generated sources were not modified during analysis.
- Upstream issue [VinTeRuS/proto-re#19](https://github.com/VinTeRuS/proto-re/issues/19) independently reports the same short-delay startup close when `proto_uv.dat` was not copied, and its installation instructions identify it as required user-owned game data.

## Decision

Treat `proto_uv.dat` as a required existing game file. The launcher now accepts only directories containing readable regular files named `prototypeenginef.dll` and `proto_uv.dat`, retains directory priority while skipping incomplete candidates, and displays an explicit missing-file message when no complete candidate exists. The launcher requirement list and README name the file.

Do not fabricate the file, bypass its validation, or add a null-manager guard that merely moves failure into later front-end logic.

## Validation and limits

- Host Java tests cover empty, DLL-only, UV-only, directory-with-filename, complete-primary, and complete-secondary cases.
- Android compilation and exact-commit ARM64 CI validate integration; they do not prove a physical-device launch.
- No directory listing from the affected device was supplied. The evidence identifies the required-file/validation path, but does not directly distinguish an absent file from an unreadable or invalid one. Setup detects absent/unreadable files; it does not attempt to replace or repair game-owned content.
