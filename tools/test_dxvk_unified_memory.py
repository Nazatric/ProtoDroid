#!/usr/bin/env python3
"""Compile and exercise the actual patched DXVK memory-type selection function."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile


def function_body(source: str) -> str:
    signature = "void DxvkMemoryAllocator::determineMemoryTypesWithPropertyFlags()"
    start = source.find(signature)
    if start < 0:
        raise SystemExit("DXVK memory-type selection function not found")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                function = source[start:index + 1]
                return function.replace(
                    "DxvkMemoryAllocator::determineMemoryTypesWithPropertyFlags",
                    "Allocator::determineMemoryTypesWithPropertyFlags", 1)
    raise SystemExit("unterminated DXVK memory-type selection function")


def main() -> None:
    source_path = Path(sys.argv[1] if len(sys.argv) > 1 else
                       "/tmp/protodroid-dxvk-test/src/dxvk/dxvk_memory.cpp")
    function = function_body(source_path.read_text())
    harness = r'''
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

using std::uint32_t;
using VkMemoryPropertyFlags = uint32_t;
constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x1;
constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x2;
constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x4;
constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_HOST_CACHED_BIT = 0x8;

struct Allocator {
    struct MemoryType {
        struct Properties {
            VkMemoryPropertyFlags propertyFlags = 0;
        } properties;
    };
    std::size_t m_memTypeCount = 0;
    std::array<MemoryType, 32> m_memTypes{};
    std::array<uint32_t, 256> m_memTypesByPropertyFlags{};
    void determineMemoryTypesWithPropertyFlags();
};

''' + function + r'''

int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void SetTypes(Allocator& allocator, std::initializer_list<VkMemoryPropertyFlags> flags) {
    allocator = Allocator{};
    for (VkMemoryPropertyFlags value : flags)
        allocator.m_memTypes[allocator.m_memTypeCount++].properties.propertyFlags = value;
    allocator.determineMemoryTypesWithPropertyFlags();
}

int main() {
    // Exact type flags from the Adreno 740 / Qualcomm 0.676.1 failure report.
    Allocator adreno;
    SetTypes(adreno, {0x1, 0x1, 0x1, 0xb, 0x7, 0x7, 0xf, 0x21, 0x11});
    const uint32_t hostCoherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const uint32_t selected = adreno.m_memTypesByPropertyFlags[hostCoherent];
    Check(selected == 0x70, "all compatible device-local types must remain candidates");
    // The failing transfer-source buffer accepted types 0 and 6 (mask 0x41). DXVK 2.5.3 selected only type 4,
    // producing an empty intersection and a false out-of-memory error. The fixed selection retains type 6.
    Check((selected & 0x41) == 0x40, "reported Adreno transfer buffer has no usable memory type");
    const uint32_t hostCachedCoherent = hostCoherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    Check(adreno.m_memTypesByPropertyFlags[hostCachedCoherent] == 0x40,
          "cached coherent fallback changed");

    // Preserve the other half of upstream's contract: when real system memory exists, prefer it over host-visible
    // device-local memory for requests that do not explicitly require DEVICE_LOCAL.
    Allocator withSystemMemory;
    SetTypes(withSystemMemory, {0x1, 0x1, 0x1, 0xb, 0x7, 0x7, 0xf, 0x21, 0x11, 0x6});
    Check(withSystemMemory.m_memTypesByPropertyFlags[hostCoherent] == (1u << 9),
          "system memory was not preferred for a non-device-local request");

    if (failures) return 1;
    std::puts("DXVK unified-memory selection tests passed (actual patched function)");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="protodroid-dxvk-memory-test-") as temporary:
        root = Path(temporary)
        cpp = root / "test.cpp"
        executable = root / "test"
        cpp.write_text(harness)
        subprocess.run([
            "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-fno-sanitize-recover=all", str(cpp), "-o", str(executable),
        ], check=True)
        environment = dict(os.environ, UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
        subprocess.run([str(executable)], check=True, env=environment)


if __name__ == "__main__":
    main()
