#!/usr/bin/env python3
"""Build and measure exact-source D3D9 bridge micro-fixtures on the current host.

This is not an Android-device benchmark. It is intended for the native ARM64 CI runner as well as local hosts.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HOST_PREFIX = r'''
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
[[noreturn]] void Fatal(const char* fmt, ...) { std::fprintf(stderr, "fatal: %s\n", fmt); std::abort(); }
'''
HOST_SUFFIX = r'''
namespace {
struct Object { uint64_t a = 0, b = 0; };
void Fill(Object& o, uint64_t i) { o.a = 0x9e3779b97f4a7c15ull ^ i; o.b = i * 0x100000001b3ull + 7; }
uint64_t CheckTable() {
    constexpr uint32_t n = 40000;
    HostTable table; std::vector<Object> objects(n); uint64_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        Fill(objects[i], i);
        if (table.Append(&objects[i]) != i) return 0;
    }
    for (uint32_t i = 0; i < n; ++i) {
        const Object* p = static_cast<const Object*>(table.Get(i));
        if (p != &objects[i] || p->a != (0x9e3779b97f4a7c15ull ^ i) || p->b != i * 0x100000001b3ull + 7) return 0;
        sum += p->b;
    }
    return sum;
}
}
int main(int argc, char** argv) {
    const uint64_t check = CheckTable();
    if (!check) { std::fprintf(stderr, "HostTable correctness failed\n"); return 1; }
    if (argc == 2 && !std::strcmp(argv[1], "--check")) {
        std::printf("{\"check_sum\":%" PRIu64 "}\n", check); return 0;
    }
    const uint64_t calls = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 20000000;
    constexpr uint32_t n = 16384;
    HostTable table; std::vector<Object> objects(n);
    for (uint32_t i = 0; i < n; ++i) { Fill(objects[i], i); table.Append(&objects[i]); }
    uint32_t random = 0x12345678; uint64_t sum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < calls; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        sum += static_cast<const Object*>(table.Get(random & (n - 1)))->b;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"calls\":%" PRIu64 ",\"elapsed_ns\":%lld,\"sum\":%" PRIu64 ",\"check_sum\":%" PRIu64 "}\n",
                calls, (long long)ns, sum, check);
    return 0;
}
'''
ANISO_PREFIX = r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
int g_configured = 1;
int g_setting_calls = 0;
int SettingInt(const char*, int def) { ++g_setting_calls; return g_configured == std::numeric_limits<int>::min() ? def : g_configured; }
void Log(const char*, ...) {}
'''
ANISO_MAIN = r'''
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" int ReadAnisotropy();
extern int g_configured;
extern int g_setting_calls;
void InitBridgeConfig();
int main(int argc, char** argv) {
    const bool check = argc == 3 && !std::strcmp(argv[1], "--check");
    if (check) g_configured = std::atoi(argv[2]);
    InitBridgeConfig();
    if (check) {
        const int value = ReadAnisotropy(); const int calls = g_setting_calls;
        std::printf("{\"configured\":%d,\"value\":%d,\"setting_calls\":%d}\n", g_configured, value, calls);
        return 0;
    }
    const uint64_t calls = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 20000000;
    uint64_t sum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < calls; ++i) sum += uint32_t(ReadAnisotropy());
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"calls\":%llu,\"elapsed_ns\":%lld,\"sum\":%llu,\"setting_calls\":%d}\n",
                (unsigned long long)calls, (long long)ns, (unsigned long long)sum, g_setting_calls);
    return g_setting_calls < 1;
}
'''

def run(command, **kwargs):
    return subprocess.run(command, text=True, capture_output=True, **kwargs)

def extract_host(source):
    start = source.index('class HostTable {')
    end = source.index('\n};\nHostTable g_hosts;', start) + 3
    return source[start:end]

def extract_aniso(source):
    if 'static int g_max_anisotropy' in source:
        start = source.index('#ifdef __ANDROID__\nstatic int g_max_anisotropy')
        end = source.index('\n\nM(IDirect3DDevice9, SetSamplerState)', start)
        return source[start:end], True
    start = source.index('int MaxAnisotropy() {')
    end = source.index('\n}\n\nM(IDirect3DDevice9, SetSamplerState)', start) + 2
    return source[start:end], False

def compile_host(source, output, name):
    path = output / f'{name}.cpp'
    path.write_text(HOST_PREFIX + source + HOST_SUFFIX)
    binary = output / name
    result = run(['g++', '-O2', '-std=c++20', '-pthread', str(path), '-o', str(binary)])
    if result.returncode:
        raise RuntimeError(result.stderr)
    check = run([str(binary), '--check'])
    if check.returncode:
        raise RuntimeError(check.stdout + check.stderr)
    return binary, json.loads(check.stdout)

def compile_aniso(source, initialized, output, name, android):
    impl = output / f'{name}-impl.cpp'
    impl.write_text(ANISO_PREFIX + '\n' + source + '\nextern "C" __attribute__((noinline)) int ReadAnisotropy() { return MaxAnisotropy(); }\n' + ('void InitBridgeConfig() { InitMaxAnisotropy(); }\n' if initialized else 'void InitBridgeConfig() {}\n'))
    binary = output / name
    flags = ['-DINIT_ANISOTROPY'] if initialized else []
    if android:
        flags.append('-D__ANDROID__')
    main_object = output / 'aniso-main.o'
    result = run(['g++', '-O2', '-std=c++20', *flags, str(impl), str(main_object), '-o', str(binary)])
    if result.returncode:
        raise RuntimeError(result.stderr)
    return binary

def benchmark(binaries, output, calls, rounds, cwd=None):
    variants = ('baseline', 'a-a', 'candidate')
    orders = list(itertools.permutations(variants))
    rows = []
    with open(output / 'timing.jsonl', 'w') as raw:
        for round_index in range(rounds):
            for variant in orders[round_index % len(orders)]:
                process = run([str(binaries[variant]), str(calls)], cwd=cwd)
                row = {'round': round_index, 'variant': variant, 'returncode': process.returncode, 'stderr': process.stderr}
                if process.stdout.strip():
                    row.update(json.loads(process.stdout))
                rows.append(row)
                raw.write(json.dumps(row) + '\n')
                raw.flush()
                if process.returncode:
                    raise RuntimeError(row)
    if len({row['sum'] for row in rows}) != 1:
        raise RuntimeError('output checksum differs')
    by = {(row['round'], row['variant']): row['elapsed_ns'] for row in rows}
    result = {'calls_per_process': calls, 'rounds': rounds, 'processes': len(rows), 'sum': rows[0]['sum'], 'variants': {}}
    for variant in variants:
        values = [row['elapsed_ns'] for row in rows if row['variant'] == variant]
        result['variants'][variant] = {'median_ns': statistics.median(values), 'min_ns': min(values), 'max_ns': max(values)}
    for variant in ('a-a', 'candidate'):
        ratios = [by[index, variant] / by[index, 'baseline'] for index in range(rounds)]
        result[f'{variant}_paired'] = {'median_ratio': statistics.median(ratios), 'wins': sum(x < 1 for x in ratios), 'losses': sum(x > 1 for x in ratios), 'ratios': ratios}
    (output / 'timing-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('part', choices=('host-table', 'anisotropy'))
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, default=ROOT / 'src/native/d3d9.cpp')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--calls', type=int, default=20000000)
    parser.add_argument('--rounds', type=int, default=24)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    before_bytes = args.before.read_bytes()
    after_bytes = args.after.read_bytes()
    metadata = {'part': args.part, 'machine': platform.machine(), 'before_sha256': hashlib.sha256(before_bytes).hexdigest(), 'after_sha256': hashlib.sha256(after_bytes).hexdigest(), 'physical_android_device': False, 'protocol': 'balanced baseline/A-A/candidate; anisotropy uses one shared main object and separate implementation objects'}
    if args.part == 'host-table':
        before = extract_host(before_bytes.decode())
        after = extract_host(after_bytes.decode())
        binaries = {}
        checks = {}
        for name, source in [('baseline', before), ('a-a', before), ('candidate', after)]:
            binaries[name], checks[name] = compile_host(source, args.output, name)
        if len({json.dumps(value, sort_keys=True) for value in checks.values()}) != 1:
            raise RuntimeError('HostTable checks differ')
        metadata['correctness'] = checks
    else:
        before, before_init = extract_aniso(before_bytes.decode())
        after, after_init = extract_aniso(after_bytes.decode())
        binaries = {}
        main_source = args.output / 'aniso-main.cpp'
        main_source.write_text(ANISO_MAIN)
        main_object = args.output / 'aniso-main.o'
        built = run(['g++', '-O2', '-std=c++20', '-c', str(main_source), '-o', str(main_object)])
        if built.returncode:
            raise RuntimeError(built.stderr)
        for name, source, initialized in [('baseline', before, before_init), ('a-a', before, before_init), ('candidate', after, after_init)]:
            binaries[name] = compile_aniso(source, initialized, args.output, name, True)
        correctness = []
        for configured in (-2147483648, -10, 0, 1, 2, 16, 17, 100):
            values = {}
            for variant, binary in binaries.items():
                process = run([str(binary), '--check', str(configured)])
                if process.returncode:
                    raise RuntimeError(process.stdout + process.stderr)
                values[variant] = json.loads(process.stdout)
            expected = 1 if configured == -2147483648 else max(1, min(16, configured))
            if len({json.dumps(value, sort_keys=True) for value in values.values()}) != 1 or values['candidate']['value'] != expected or values['candidate']['setting_calls'] != 1:
                raise RuntimeError({'configured': configured, 'values': values})
            correctness.append({'configured': configured, 'expected': expected, 'variants': values})
        (args.output / 'correctness.json').write_text(json.dumps(correctness, indent=2) + '\n')
        metadata['correctness_processes'] = len(correctness) * 3
    metadata['timing'] = benchmark(binaries, args.output, args.calls, args.rounds)
    metadata['interpretation'] = 'Native host microfixture only; not physical Android FPS, frame-time, driver, or GPU evidence.'
    (args.output / 'result.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({'part': args.part, 'machine': metadata['machine'], 'candidate': metadata['timing']['candidate_paired'], 'a_a': metadata['timing']['a-a_paired'], 'result': str(args.output / 'result.json')}))

if __name__ == '__main__':
    main()
