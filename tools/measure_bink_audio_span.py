#!/usr/bin/env python3
"""Measure the exact Bink audio callback with an identical A/A control.

This is a native host/CI microfixture, not an Android audio-device, underrun, A/V-sync, or FPS result.
Every callback is independently bit-checked by bink_audio_profile.cpp. Raw per-callback timings are retained.
"""
import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import time
from pathlib import Path

LABELS = ('baseline', 'control', 'span')
CONFIGS = (
    ('48000-wrap-direct', 48000, 'wrap', 'direct'),
    ('48000-nowrap-direct', 48000, 'nowrap', 'direct'),
    ('48000-wrap-bin', 48000, 'wrap', 'bin'),
    ('44100-wrap-direct', 44100, 'wrap', 'direct'),
    ('32000-wrap-direct', 32000, 'wrap', 'direct'),
)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def percentile(values, fraction):
    values = sorted(values)
    return values[math.ceil(fraction * len(values)) - 1]


def summarize(row):
    times = row['timings_ns']
    return {'total_ns': row['total_ns'], 'mean_ns': row['mean_ns'],
            'median_ns': statistics.median(times), 'p95_ns': percentile(times, 0.95),
            'p99_ns': percentile(times, 0.99), 'max_ns': max(times)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--span', required=True, type=Path)
    parser.add_argument('--baseline-source', required=True, type=Path)
    parser.add_argument('--span-source', required=True, type=Path)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--iterations', type=int, default=4000)
    parser.add_argument('--rounds', type=int, default=12)
    parser.add_argument('--order-offset', type=int, default=0)
    args = parser.parse_args()
    if args.iterations < 100 or args.rounds < 4:
        raise SystemExit('need at least 100 iterations and four balanced rounds')
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = {'baseline': args.baseline, 'control': args.baseline, 'span': args.span}
    sources = {'baseline': args.baseline_source, 'control': args.baseline_source, 'span': args.span_source}
    binary_hashes = {label: sha(path) for label, path in binaries.items()}
    source_hashes = {label: sha(path) for label, path in sources.items()}
    started = time.perf_counter()
    summary = {
        'schema': 1, 'kind': 'native-production-bink-callback-not-android-device-or-fps-proof',
        'architecture': platform.machine(), 'iterations_per_process': args.iterations,
        'rounds_per_configuration': args.rounds, 'order_offset': args.order_offset,
        'binary_sha256': binary_hashes, 'source_sha256': source_hashes, 'harness_sha256': sha(args.harness),
        'materiality_threshold_ms_cpu_per_second': 0.25, 'configurations': {}, 'all_correct': True,
    }
    all_rows = []
    for config_name, rate, wrap, mix in CONFIGS:
        config_dir = args.output / config_name
        config_dir.mkdir(exist_ok=True)
        rows = []
        for round_index in range(args.rounds):
            shift = (round_index + args.order_offset) % len(LABELS)
            order = LABELS[shift:] + LABELS[:shift]
            for order_index, label in enumerate(order):
                stem = f'round-{round_index:02d}-{label}'
                process = subprocess.run(
                    [str(binaries[label]), str(args.iterations), str(rate), wrap, mix],
                    text=True, capture_output=True)
                (config_dir / f'{stem}.stdout').write_text(process.stdout)
                (config_dir / f'{stem}.stderr').write_text(process.stderr)
                if process.returncode:
                    raise RuntimeError(f'{config_name}/{stem} failed: {process.stderr}')
                row = json.loads(process.stdout)
                row.update({'configuration': config_name, 'round': round_index, 'label': label,
                            'order': order_index, 'binary_sha256': binary_hashes[label],
                            'source_sha256': source_hashes[label],
                            'stderr_bytes': len(process.stderr.encode())})
                if not (row['iterations'] == args.iterations and
                        row['correctness_calls'] == args.iterations + 32 and
                        row['rate'] == rate and row['wrap'] == wrap and row['mix'] == mix and
                        len(row['timings_ns']) == args.iterations and not process.stderr and
                        math.isclose(row['total_ns'], sum(row['timings_ns']), rel_tol=1e-12, abs_tol=1e-6)):
                    raise RuntimeError(f'invalid row {config_name}/{stem}')
                (config_dir / f'{stem}.json').write_text(json.dumps(row, indent=2) + '\n')
                rows.append(row)
                all_rows.append(row)
            selected = {label: next(row for row in rows if row['round'] == round_index and row['label'] == label)
                        for label in LABELS}
            print(json.dumps({'configuration': config_name, 'round': round_index, 'order': list(order),
                              **{label: selected[label]['total_ns'] for label in LABELS}}), flush=True)
        digest = rows[0]['output_fnv1a64']
        if any(row['output_fnv1a64'] != digest for row in rows):
            raise RuntimeError(f'output digest mismatch: {config_name}')
        rounds = []
        for round_index in range(args.rounds):
            selected = {label: next(row for row in rows if row['round'] == round_index and row['label'] == label)
                        for label in LABELS}
            callbacks_per_second = rate / selected['baseline']['callback_frames']
            rounds.append({
                'round': round_index,
                'control_baseline_ratio': selected['control']['total_ns'] / selected['baseline']['total_ns'],
                'span_baseline_ratio': selected['span']['total_ns'] / selected['baseline']['total_ns'],
                'span_control_ratio': selected['span']['total_ns'] / selected['control']['total_ns'],
                'span_saved_ms_cpu_per_second':
                    (selected['baseline']['mean_ns'] - selected['span']['mean_ns']) * callbacks_per_second / 1e6,
            })
        config_summary = {'rate': rate, 'wrap': wrap, 'mix': mix, 'processes': len(rows),
                          'callbacks_per_second': rate / 1024, 'labels': {}, 'output_fnv1a64': digest}
        for label in LABELS:
            selected = [summarize(row) for row in rows if row['label'] == label]
            config_summary['labels'][label] = {
                **{f'{key}_median': statistics.median(item[key] for item in selected) for key in selected[0]},
                'total_ns_min': min(row['total_ns'] for row in selected),
                'total_ns_max': max(row['total_ns'] for row in selected),
            }
        paired = {}
        for name in ('control_baseline_ratio', 'span_baseline_ratio', 'span_control_ratio',
                     'span_saved_ms_cpu_per_second'):
            values = [row[name] for row in rounds]
            paired[name] = {'median': statistics.median(values), 'min': min(values), 'max': max(values)}
        paired['span_wins_over_baseline'] = sum(row['span_baseline_ratio'] < 1 for row in rounds)
        paired['span_wins_over_control'] = sum(row['span_control_ratio'] < 1 for row in rounds)
        paired['meets_absolute_threshold'] = paired['span_saved_ms_cpu_per_second']['median'] >= 0.25
        config_summary['paired'] = paired
        config_summary['rounds'] = rounds
        summary['configurations'][config_name] = config_summary
    summary.update({
        'processes': len(all_rows), 'timed_callbacks': sum(row['iterations'] for row in all_rows),
        'correctness_callbacks': sum(row['correctness_calls'] for row in all_rows),
        'duration_seconds': time.perf_counter() - started,
        'limitations': [
            'One live movie/track and 1,024-frame stereo callback; no Android audio device, contention, underrun, or FPS measurement.',
            'Ring reset, independent reference comparison, and output allocation are outside each timed callback; actual registry/movie locking, bins, mixing, clamping, consumption, and audio-clock update are inside.',
            'Control is an identical fresh-process A/A control. Candidate adoption additionally requires the preregistered absolute CPU/s threshold.',
        ],
    })
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
