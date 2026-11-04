#!/usr/bin/env python3
"""Measure exact DXVK SHA-1 padding candidates with A/A control; not Android shader timing."""
import argparse
import hashlib
import itertools
import json
import platform
import shutil
import statistics
import subprocess
from pathlib import Path

MAIN = r'''
#include "sha1_util.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const size_t size = std::strtoull(argv[1], nullptr, 0);
    const uint64_t iterations = std::strtoull(argv[2], nullptr, 0);
    if (!size || !iterations) return 2;
    std::vector<unsigned char> data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<unsigned char>(i * 131 + size * 17 + 7 * 37);
    uint64_t checksum = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i < 100; ++i)
        checksum ^= dxvk::Sha1Hash::compute(data.data(), data.size()).dword(i % 5);
    const auto start = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < iterations; ++i) {
        const auto hash = dxvk::Sha1Hash::compute(data.data(), data.size());
        checksum = ((checksum << 1) | (checksum >> 63)) ^ hash.dword(i % 5);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("{\"size\":%zu,\"iterations\":%llu,\"elapsed_ns\":%lld,\"checksum\":\"%016llx\"}\n",
                size, (unsigned long long)iterations, (long long)elapsed, (unsigned long long)checksum);
    return 0;
}
'''


def run(command):
    return subprocess.run([str(item) for item in command], text=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-c', type=Path, required=True)
    parser.add_argument('--after-c', type=Path, required=True)
    parser.add_argument('--sha1-header', type=Path, required=True)
    parser.add_argument('--util-cpp', type=Path, required=True)
    parser.add_argument('--util-header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--order-offset', type=int, default=0)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('--rounds must be positive')
    args.output.mkdir(parents=True, exist_ok=False)
    include = args.output / 'include'
    include.mkdir()
    shutil.copy2(args.sha1_header, include / 'sha1.h')
    shutil.copy2(args.util_header, include / 'sha1_util.h')
    before_c = args.output / 'before-sha1.c'
    after_c = args.output / 'after-sha1.c'
    util_cpp = args.output / 'sha1_util.cpp'
    shutil.copy2(args.before_c, before_c)
    shutil.copy2(args.after_c, after_c)
    shutil.copy2(args.util_cpp, util_cpp)
    shutil.copy2(__file__, args.output / 'measure_sha1_pad.py')
    main_cpp = args.output / 'main.cpp'
    main_cpp.write_text(MAIN)

    build_commands = {}
    def build(name, command):
        result = run(command)
        build_commands[name] = [str(item) for item in command]
        (args.output / f'{name}.stdout').write_text(result.stdout)
        (args.output / f'{name}.stderr').write_text(result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: {result.stderr}')

    common = ['-O2', '-g', '-Wall', '-Wextra', '-Werror', f'-I{include}']
    build('before-c-build', ['gcc', '-std=c99', *common, '-c', before_c, '-o', args.output / 'before-sha1.o'])
    build('after-c-build', ['gcc', '-std=c99', *common, '-c', after_c, '-o', args.output / 'after-sha1.o'])
    build('util-build', ['g++', '-std=c++17', *common, '-c', util_cpp, '-o', args.output / 'sha1_util.o'])
    build('main-build', ['g++', '-std=c++17', *common, '-c', main_cpp, '-o', args.output / 'main.o'])
    build('baseline-link', ['g++', args.output / 'main.o', args.output / 'sha1_util.o',
                            args.output / 'before-sha1.o', '-o', args.output / 'baseline'])
    build('candidate-link', ['g++', args.output / 'main.o', args.output / 'sha1_util.o',
                             args.output / 'after-sha1.o', '-o', args.output / 'candidate'])
    shutil.copy2(args.output / 'baseline', args.output / 'a-a')
    variants = {name: args.output / name for name in ('baseline', 'a-a', 'candidate')}

    cases = [(20, 400000), (48, 166667), (56, 142857), (64, 125000),
             (128, 62500), (256, 31250), (1024, 7812), (4096, 1953)]
    orders = list(itertools.permutations(tuple(variants)))
    offset = args.order_offset % len(orders)
    orders = orders[offset:] + orders[:offset]
    summaries = {}
    for size, iterations in cases:
        rows = []
        with open(args.output / f'timing-{size}.jsonl', 'w') as raw:
            for round_index in range(args.rounds):
                for variant in orders[round_index % len(orders)]:
                    process = run([variants[variant], size, iterations])
                    row = {'round': round_index, 'variant': variant, 'returncode': process.returncode,
                           'stdout': process.stdout.strip(), 'stderr': process.stderr}
                    if process.stdout.strip():
                        row.update(json.loads(process.stdout))
                    rows.append(row)
                    raw.write(json.dumps(row) + '\n')
                    raw.flush()
                    if process.returncode:
                        raise RuntimeError(row)
        if len({row['checksum'] for row in rows}) != 1:
            raise RuntimeError(f'checksum mismatch at {size}')
        by = {(row['round'], row['variant']): row['elapsed_ns'] for row in rows}
        summary = {'size': size, 'iterations': iterations, 'variants': {}}
        for variant in variants:
            values = [by[r, variant] for r in range(args.rounds)]
            summary['variants'][variant] = {'median_ns': statistics.median(values),
                                            'min_ns': min(values), 'max_ns': max(values)}
        for left, right, key in [('candidate', 'baseline', 'candidate_paired'),
                                 ('a-a', 'baseline', 'a_a_paired')]:
            ratios = [by[r, left] / by[r, right] for r in range(args.rounds)]
            summary[key] = {'median_ratio': statistics.median(ratios),
                            'wins': sum(x < 1 for x in ratios), 'losses': sum(x > 1 for x in ratios),
                            'ratios': ratios}
        summary['baseline_ns_per_hash'] = summary['variants']['baseline']['median_ns'] / iterations
        summary['candidate_ns_per_hash'] = summary['variants']['candidate']['median_ns'] / iterations
        summary['candidate_delta_ns_per_hash'] = (
            summary['candidate_ns_per_hash'] - summary['baseline_ns_per_hash'])
        summaries[str(size)] = summary
        (args.output / f'summary-{size}.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps({'size': size, 'candidate_ratio': summary['candidate_paired']['median_ratio'],
                          'wins': summary['candidate_paired']['wins'],
                          'losses': summary['candidate_paired']['losses'],
                          'a_a_ratio': summary['a_a_paired']['median_ratio'],
                          'delta_ns_per_hash': summary['candidate_delta_ns_per_hash']}))

    source_paths = {'before_c': args.before_c, 'after_c': args.after_c,
                    'sha1_header': args.sha1_header, 'util_cpp': args.util_cpp,
                    'util_header': args.util_header, 'benchmark_main': main_cpp}
    result = {
        'schema': 1,
        'architecture': platform.machine(),
        'source_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in source_paths.items()},
        'benchmark_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'binary_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in variants.items()},
        'build_commands': build_commands,
        'rounds': args.rounds,
        'order_offset': args.order_offset,
        'cases': summaries,
        'physical_android_device': False,
        'kind': 'isolated-sha1-host-microfixture-not-shader-compilation-or-fps',
    }
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
