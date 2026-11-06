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
        if (failures < 32)
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

uint32_t ModelMask(const std::array<VkMemoryPropertyFlags, 4>& types, uint32_t request) {
    uint32_t system = 0;
    uint32_t device = 0;
    for (uint32_t index = 0; index < types.size(); ++index) {
        const uint32_t properties = types[index];
        if ((properties & request) != request)
            continue;
        if (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            device |= 1u << index;
        else
            system |= 1u << index;
    }
    return system ? system : device;
}

int main() {
    const uint32_t hostCoherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const uint32_t hostCachedCoherent = hostCoherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    // Exact memory-type flags shared by the 0.9 Adreno 740 report and the newer Adreno 710 (#18) and
    // Adreno 750 (#17) reports. Each stopped on the same 90,808-byte transfer-source allocation.
    for (const char* report : {"Adreno 740 / 0.676.1", "Adreno 710 / 0.800.58", "Adreno 750 / 0.762.1"}) {
        Allocator adreno;
        SetTypes(adreno, {0x1, 0x1, 0x1, 0xb, 0x7, 0x7, 0xf, 0x21, 0x11});
        const uint32_t selected = adreno.m_memTypesByPropertyFlags[hostCoherent];
        Check(selected == 0x70, "all compatible device-local types must remain candidates");
        // The failing buffer accepted types 0 and 6 (mask 0x41). DXVK 2.5.3 selected only type 4, producing an empty
        // intersection and a false out-of-memory error. The fixed selection retains type 6.
        Check((selected & 0x41) == 0x40, "reported Adreno transfer buffer has no usable memory type");
        Check(adreno.m_memTypesByPropertyFlags[hostCachedCoherent] == 0x40,
              "cached coherent fallback changed");
        std::printf("validated Qualcomm unified-memory report: %s\n", report);
    }

    // Exact Mali-G720 MC7 49.1.0 heap from the historical diagnostic run. All four types are device-local,
    // so the allocator must retain compatible device-local types rather than waiting for a system-memory type.
    Allocator mali;
    SetTypes(mali, {0x7, 0xb, 0x11, 0x21});
    Check(mali.m_memTypesByPropertyFlags[hostCoherent] == 0x1,
          "Mali host-coherent requests must select type 0");
    Check(mali.m_memTypesByPropertyFlags[VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT] == 0x3,
          "Mali host-visible mask must retain types 0 and 1 (logged buffer mask 0x3)");
    Check(mali.m_memTypesByPropertyFlags[VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT] == 0xf,
          "Mali device-local requests must retain all four types");
    Check(mali.m_memTypesByPropertyFlags[hostCachedCoherent] == 0x1,
          "Mali cached-coherent fallback must use the host-coherent type");
    std::puts("validated Mali-G720 device-local-only memory heap");

    // Preserve the other half of upstream's contract: when real system memory exists, prefer it over host-visible
    // device-local memory for requests that do not explicitly require DEVICE_LOCAL.
    Allocator withSystemMemory;
    SetTypes(withSystemMemory, {0x1, 0x1, 0x1, 0xb, 0x7, 0x7, 0xf, 0x21, 0x11, 0x6});
    Check(withSystemMemory.m_memTypesByPropertyFlags[hostCoherent] == (1u << 9),
          "system memory was not preferred for a non-device-local request");

    // Keep the pristine-upstream negative control concise; named fixtures must pass before the exhaustive sweep.
    if (failures) return 1;

    // Exhaust all four-type combinations over the 16 lookup-table bits plus lazy/protected real-heap variants.
    // The independent model checks system-memory preference, device-local fallback, and cached/coherent fallback.
    constexpr std::array<VkMemoryPropertyFlags, 20> propertyValues = {
        0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9,
        0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x20, 0x21,
    };
    uint64_t configurations = 0;
    uint64_t comparisons = 0;
    uint64_t maskDigest = 14695981039346656037ull;
    for (uint32_t a : propertyValues) for (uint32_t b : propertyValues)
    for (uint32_t c : propertyValues) for (uint32_t d : propertyValues) {
        const std::array<VkMemoryPropertyFlags, 4> types = {a, b, c, d};
        Allocator allocator;
        SetTypes(allocator, {a, b, c, d});
        ++configurations;
        for (uint32_t request = 0; request < 16; ++request) {
            uint32_t expected = ModelMask(types, request);
            if (request == hostCachedCoherent && !expected)
                expected = ModelMask(types, hostCoherent);
            const uint32_t selected = allocator.m_memTypesByPropertyFlags[request];
            Check(selected == expected, "exhaustive memory-type mask mismatch");
            for (uint32_t byte = 0; byte < 4; ++byte) {
                maskDigest ^= (selected >> (8 * byte)) & 0xff;
                maskDigest *= 1099511628211ull;
            }
            ++comparisons;
        }
    }
    std::printf("validated %llu four-type configurations and %llu exact mask lookups\n",
                static_cast<unsigned long long>(configurations),
                static_cast<unsigned long long>(comparisons));
    std::printf("mask_fnv1a64=%016llx\n", static_cast<unsigned long long>(maskDigest));

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
