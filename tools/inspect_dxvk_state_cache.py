#!/usr/bin/env python3
"""Read-only DXVK v8-v18 state-cache inventory and SHA-1 integrity validation."""
import argparse
import collections
import hashlib
import json
import statistics
from pathlib import Path

SUPPORTED_VERSIONS = set(range(8, 19)) - {16}
STAGE_NAMES = (
    (1 << 0, 'vertex'),
    (1 << 1, 'tessellation_control'),
    (1 << 2, 'tessellation_evaluation'),
    (1 << 3, 'geometry'),
    (1 << 4, 'fragment'),
    (1 << 5, 'compute'),
)


def inspect(path):
    data = path.read_bytes()
    report = {
        'schema': 1,
        'validation_scope': 'entry boundaries and SHA-1 only; DXVK validates semantic pipeline fields',
        'file': str(path),
        'file_bytes': len(data),
        'file_sha256': hashlib.sha256(data).hexdigest(),
        'version': None,
        'entries': [],
        'errors': [],
    }
    if len(data) < 12:
        report['errors'].append(f'truncated file header: {len(data)} of 12 bytes')
        return finalize(report)
    if data[:4] != b'DXVK':
        report['errors'].append('invalid DXVK magic')
        return finalize(report)
    version = int.from_bytes(data[4:8], 'little')
    report['version'] = version
    if version not in SUPPORTED_VERSIONS:
        report['errors'].append(f'unsupported cache version: {version}')
        return finalize(report)

    offset = 12
    while offset < len(data):
        entry_offset = offset
        if len(data) - offset < 24:
            report['errors'].append(
                f'truncated entry metadata at offset {offset}: {len(data) - offset} of 24 bytes')
            break
        word = int.from_bytes(data[offset:offset + 4], 'little')
        if version >= 16:
            entry_type = word & 1
            stage_mask = (word >> 1) & 0x1f
            payload_size = word >> 6
        else:
            entry_type = 0
            stage_mask = (word >> 24) & 0xff
            payload_size = word & 0xffffff
        expected_hash = data[offset + 4:offset + 24]
        offset += 24
        if payload_size > len(data) - offset:
            report['errors'].append(
                f'truncated payload at offset {entry_offset}: need {payload_size}, '
                f'have {len(data) - offset}')
            break
        payload = data[offset:offset + payload_size]
        actual_hash = hashlib.sha1(payload).digest()
        report['entries'].append({
            'index': len(report['entries']),
            'offset': entry_offset,
            'entry_type': 'pipeline_library' if entry_type else 'monolithic_pipeline',
            'stage_mask': stage_mask,
            'stages': [name for bit, name in STAGE_NAMES if stage_mask & bit],
            'payload_size': payload_size,
            'expected_sha1': expected_hash.hex(),
            'actual_sha1': actual_hash.hex(),
            'sha1_valid': actual_hash == expected_hash,
        })
        offset += payload_size
    if offset != len(data) and not report['errors']:
        report['errors'].append(f'unconsumed bytes at offset {offset}: {len(data) - offset}')
    return finalize(report)


def finalize(report):
    entries = report['entries']
    sizes = [entry['payload_size'] for entry in entries]
    invalid = sum(not entry['sha1_valid'] for entry in entries)
    if invalid:
        report['errors'].append(f'{invalid} entry SHA-1 mismatch(es)')
    report['summary'] = {
        'entry_count': len(entries),
        'valid_entries': len(entries) - invalid,
        'invalid_entries': invalid,
        'sha1_hash_count': len(entries),
        'hashed_payload_bytes': sum(sizes),
        'payload_size_min': min(sizes) if sizes else 0,
        'payload_size_max': max(sizes) if sizes else 0,
        'payload_size_mean': statistics.mean(sizes) if sizes else 0,
        'payload_size_median': statistics.median(sizes) if sizes else 0,
        'payload_size_counts': dict(sorted(collections.Counter(sizes).items())),
        'entry_type_counts': dict(sorted(collections.Counter(
            entry['entry_type'] for entry in entries).items())),
        'stage_combination_counts': dict(sorted(collections.Counter(
            f"0x{entry['stage_mask']:02x}" for entry in entries).items())),
    }
    report['all_entries_valid'] = not report['errors']
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = inspect(args.cache)
    rendered = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered)
        print(json.dumps({'cache': str(args.cache), 'sha256': report['file_sha256'],
                          'version': report['version'], **report['summary'],
                          'errors': report['errors']}))
    else:
        print(rendered, end='')
    return 0 if report['all_entries_valid'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
