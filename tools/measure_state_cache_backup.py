#!/usr/bin/env python3
"""Balanced exact-source state-cache backup microfixture; host timing is not Android FPS."""
import argparse
import hashlib
import itertools
import json
import statistics
import shutil
import subprocess
from pathlib import Path

MAIN = r'''
#include "state_cache.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <vector>
namespace fs = std::filesystem;
#ifdef TRACKED_BACKUP
using BackupState = native::StateCacheBackupState;
#endif
bool RunBackup(const std::string& source, const std::string& target,
#ifdef TRACKED_BACKUP
               BackupState& state
#else
               int& state
#endif
) {
#ifdef TRACKED_BACKUP
    return native::BackupStateCache(source, target, &state);
#else
    (void)state;
    return native::BackupStateCache(source, target);
#endif
}
ino_t GetInode(const fs::path& path) { struct stat st; return stat(path.c_str(), &st) == 0 ? st.st_ino : 0; }
uint64_t Digest(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : data) { hash ^= c; hash *= 0x100000001b3ull; }
    return hash;
}
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    const bool changed = std::strcmp(argv[3], "changed") == 0;
    if (!changed && std::strcmp(argv[3], "unchanged") != 0) return 2;
    const size_t bytes = std::strtoull(argv[1], nullptr, 0);
    const int iterations = std::atoi(argv[2]);
    if (bytes < 1024 || iterations < 1) return 2;
    char pattern[] = "/tmp/protodroid-cache-bench-XXXXXX";
    if (!mkdtemp(pattern)) return 3;
    fs::path root = pattern, source = root / "private" / native::kStateCacheFilename;
    fs::path target = root / "shared" / native::kStateCacheFilename;
    fs::create_directories(source.parent_path()); fs::create_directories(target.parent_path());
    if (argc == 5) {
        // The optional fixture is always copied to private temporary storage; the supplied original stays read-only.
        std::error_code ec;
        fs::copy_file(argv[4], source, fs::copy_options::overwrite_existing, ec);
        if (ec || fs::file_size(source, ec) != bytes || ec) return 4;
    } else {
        std::ofstream file(source, std::ios::binary | std::ios::trunc);
        unsigned char header[12] = {0}; std::memcpy(header, "DXVK", 4); header[4] = 18;
        file.write(reinterpret_cast<const char*>(header), sizeof(header));
        std::vector<char> block(16384);
        for (size_t i = 0; i < block.size(); ++i) block[i] = char((i * 131 + 17) & 255);
        size_t remaining = bytes - sizeof(header), offset = 0;
        while (remaining) {
            const size_t count = std::min(remaining, block.size());
            for (size_t i = 0; i < count; ++i) block[i] = char(((offset + i) * 131 + 17) & 255);
            file.write(block.data(), static_cast<std::streamsize>(count));
            remaining -= count; offset += count;
        }
        file.close(); if (!file.good()) return 4;
    }
#ifdef TRACKED_BACKUP
    BackupState state;
#else
    int state = 0;
#endif
    if (!RunBackup(source.string(), target.string(), state)) return 5;
    const ino_t initial_inode = GetInode(target);
    int64_t elapsed = 0;
    uint64_t successes = 0;
    if (changed) {
        for (int i = 0; i < iterations; ++i) {
            {
                std::ofstream append(source, std::ios::binary | std::ios::app);
                append << 'x'; append.close(); if (!append.good()) return 6;
            }
            const auto start = std::chrono::steady_clock::now();
            successes += RunBackup(source.string(), target.string(), state) ? 1 : 0;
            elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        }
    } else {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) successes += RunBackup(source.string(), target.string(), state) ? 1 : 0;
        elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    }
    const ino_t final_inode = GetInode(target);
    const uint64_t digest = Digest(target);
    std::error_code ec; fs::remove_all(root, ec);
#ifdef TRACKED_BACKUP
    const bool shape_ok = changed ? initial_inode != final_inode : initial_inode == final_inode;
#else
    const bool shape_ok = true;  // Baseline copy semantics are covered by the differential tests, not inode numerology.
#endif
    std::printf("{\"bytes\":%zu,\"iterations\":%d,\"elapsed_ns\":%lld,\"successes\":%llu,\"digest\":\"%016llx\",\"initial_inode\":%llu,\"final_inode\":%llu,\"tracked\":%s}\n",
                bytes, iterations, (long long)elapsed, (unsigned long long)successes,
                (unsigned long long)digest, (unsigned long long)initial_inode,
                (unsigned long long)final_inode,
#ifdef TRACKED_BACKUP
                "true");
#else
                "false");
#endif
    return successes != uint64_t(iterations) || !shape_ok;
}
'''

def run(command, **kwargs):
    return subprocess.run(command, text=True, capture_output=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before-cpp', type=Path, required=True)
    parser.add_argument('--before-header', type=Path, required=True)
    parser.add_argument('--after-cpp', type=Path, required=True)
    parser.add_argument('--after-header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--order-offset', type=int, default=0)
    parser.add_argument('--mode', choices=('unchanged', 'changed'), default='unchanged')
    parser.add_argument('--fixture', type=Path,
                        help='optional real cache copied read-only into each temporary source; replaces synthetic sizes')
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('--rounds must be positive')
    if args.fixture and (not args.fixture.is_file() or args.fixture.stat().st_size < 1024):
        parser.error('--fixture must be a readable cache file of at least 1024 bytes')
    args.output.mkdir(parents=True, exist_ok=False)

    def copy_source(label, cpp, header):
        directory = args.output / f'{label}-source'
        directory.mkdir()
        shutil.copy2(cpp, directory / 'state_cache.cpp')
        shutil.copy2(header, directory / 'state_cache.h')
        return directory

    before = copy_source('before', args.before_cpp, args.before_header)
    after = copy_source('after', args.after_cpp, args.after_header)
    shutil.copy2(__file__, args.output / 'measure_state_cache_backup.py')
    fixture = None
    fixture_sha256 = None
    if args.fixture:
        fixture_sha256 = hashlib.sha256(args.fixture.read_bytes()).hexdigest()
        fixture = args.output / 'fixture.dxvk-cache'
        shutil.copy2(args.fixture, fixture)
        if hashlib.sha256(fixture.read_bytes()).hexdigest() != fixture_sha256:
            raise RuntimeError('fixture copy changed bytes')
    source = args.output / 'main.cpp'
    source.write_text(MAIN)
    variants = {}
    build_commands = {}
    for name in ('baseline', 'a-a', 'candidate'):
        tracked = name == 'candidate'
        source_dir = after if tracked else before
        binary = args.output / name
        flags = ['-DTRACKED_BACKUP'] if tracked else []
        command = ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
                   f'-I{source_dir}', str(source_dir / 'state_cache.cpp'), str(source), '-o', str(binary)]
        result = run(command)
        build_commands[name] = command
        (args.output / f'{name}-build.stdout').write_text(result.stdout)
        (args.output / f'{name}-build.stderr').write_text(result.stderr)
        if result.returncode:
            raise RuntimeError(result.stderr)
        variants[name] = binary

    cases = ([(fixture.stat().st_size, 51)] if fixture else
             [(4096, 101), (65536, 51), (1048576, 21), (4194304, 9)])
    orders = list(itertools.permutations(('baseline', 'a-a', 'candidate')))
    offset = args.order_offset % len(orders)
    orders = orders[offset:] + orders[:offset]
    all_summaries = {}
    for size, iterations in cases:
        rows = []
        with open(args.output / f'timing-{size}.jsonl', 'w') as raw:
            for round_index in range(args.rounds):
                for variant in orders[round_index % len(orders)]:
                    command = [str(variants[variant]), str(size), str(iterations), args.mode]
                    if fixture:
                        command.append(str(fixture))
                    process = run(command)
                    row = {'round': round_index, 'variant': variant, 'returncode': process.returncode,
                           'stdout': process.stdout.strip(), 'stderr': process.stderr}
                    if process.stdout.strip():
                        row.update(json.loads(process.stdout))
                    rows.append(row)
                    raw.write(json.dumps(row) + '\n')
                    raw.flush()
                    if process.returncode:
                        raise RuntimeError(row)
        if len({row['digest'] for row in rows}) != 1:
            raise RuntimeError(f'digest mismatch at {size}')
        for row in rows:
            expected_tracked = row['variant'] == 'candidate'
            if row['tracked'] != expected_tracked or row['successes'] != iterations:
                raise RuntimeError(row)
            if expected_tracked and args.mode == 'unchanged' and row['initial_inode'] != row['final_inode']:
                raise RuntimeError(row)
            if expected_tracked and args.mode == 'changed' and row['initial_inode'] == row['final_inode']:
                raise RuntimeError(row)
        by = {(row['round'], row['variant']): row['elapsed_ns'] for row in rows}
        summary = {'bytes': size, 'iterations': iterations, 'mode': args.mode,
                   'processes': len(rows), 'variants': {}}
        if fixture:
            summary['fixture_sha256'] = fixture_sha256
        for variant in ('baseline', 'a-a', 'candidate'):
            values = [by[r, variant] for r in range(args.rounds)]
            summary['variants'][variant] = {
                'median_ns': statistics.median(values), 'min_ns': min(values), 'max_ns': max(values)}
        for left, right, key in [('candidate', 'baseline', 'candidate_paired'),
                                 ('a-a', 'baseline', 'a_a_paired')]:
            ratios = [by[r, left] / by[r, right] for r in range(args.rounds)]
            summary[key] = {'median_ratio': statistics.median(ratios),
                            'wins': sum(x < 1 for x in ratios), 'losses': sum(x > 1 for x in ratios),
                            'ratios': ratios}
        summary['candidate_ns_per_backup'] = summary['variants']['candidate']['median_ns'] / iterations
        summary['baseline_ns_per_backup'] = summary['variants']['baseline']['median_ns'] / iterations
        summary['candidate_delta_ns_per_backup'] = (
            summary['candidate_ns_per_backup'] - summary['baseline_ns_per_backup'])
        all_summaries[str(size)] = summary
        (args.output / f'summary-{size}.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps({'size': size, 'candidate_ratio': summary['candidate_paired']['median_ratio'],
                          'candidate_wins': summary['candidate_paired']['wins'],
                          'candidate_losses': summary['candidate_paired']['losses'],
                          'a_a_ratio': summary['a_a_paired']['median_ratio'],
                          'delta_ns_per_backup': summary['candidate_delta_ns_per_backup']}))

    source_paths = {'before_cpp': args.before_cpp, 'before_header': args.before_header,
                    'after_cpp': args.after_cpp, 'after_header': args.after_header}
    result = {
        'schema': 1,
        'source_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in source_paths.items()},
        'benchmark_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'fixture': ({'source_path': str(args.fixture), 'copied_path': str(fixture),
                     'bytes': fixture.stat().st_size, 'sha256': fixture_sha256} if fixture else None),
        'binary_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in variants.items()},
        'build_commands': build_commands,
        'rounds_per_size': args.rounds,
        'mode': args.mode,
        'order_offset': args.order_offset,
        'cases': all_summaries,
        'physical_android_device': False,
        'filesystem': 'host /tmp filesystem; warm microfixture; not Android shared-storage FUSE',
    }
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
