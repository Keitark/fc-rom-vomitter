#!/usr/bin/env python3
"""Repeat read-only page/status loads while recording RVST and heap diagnostics."""
import argparse
import csv
import datetime
import json
from pathlib import Path
import statistics
import threading
import time

import requests
import serial
from serial.tools import list_ports

from serial_status import status_line


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1')
    parser.add_argument('--port')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--normal-count', type=int, default=30)
    parser.add_argument('--burst-count', type=int, default=20)
    args = parser.parse_args()
    if args.normal_count < 1 or args.burst_count < 0:
        parser.error('normal-count must be positive; burst-count nonnegative')
    args.output.mkdir(parents=True, exist_ok=True)
    port = args.port
    if not port:
        ports = [p.device for p in list_ports.comports()
                 if p.vid == 0x303A and p.pid == 0x1001]
        if len(ports) != 1:
            parser.error(f'Expected one ESP32-S3 serial port; found {ports}')
        port = ports[0]
    # No DTR/RTS assertion, reset, upload, demo click, or SDR start.
    link = serial.Serial()
    link.port, link.baudrate, link.timeout = port, 115200, 0.1
    link.dtr = False
    link.rts = False
    link.open()
    stop = threading.Event()
    snapshots = []
    serial_errors = []

    def monitor():
        pending = bytearray()
        next_query = 0
        try:
            with (args.output / 'serial.log').open('w', encoding='utf-8') as log:
                while not stop.is_set():
                    if time.monotonic() >= next_query:
                        link.write(b'RVST\n')
                        next_query = time.monotonic() + 3
                    pending.extend(link.read(4096))
                    while b'\n' in pending:
                        line, _, rest = pending.partition(b'\n')
                        pending[:] = rest
                        text = line.decode('utf-8', 'replace').strip()
                        stamp = utc_now()
                        log.write(stamp + ' ' + text + '\n')
                        log.flush()
                        reply = status_line(text)
                        if reply:
                            fields = dict(token.split('=', 1) for token in
                                          reply[1].split()[1:] if '=' in token)
                            snapshots.append({'utc': stamp, 'type': reply[0].strip(), **fields})
        except Exception as error:
            serial_errors.append(str(error))

    worker = threading.Thread(target=monitor, daemon=True)
    worker.start()
    session = requests.Session()
    session.trust_env = False
    rows = []
    started = utc_now()
    try:
        with (args.output / 'http.csv').open('w', newline='', encoding='utf-8') as log:
            columns = ['utc', 'phase', 'cycle', 'path', 'ok', 'http_status',
                       'elapsed_ms', 'bytes', 'heap_free', 'heap_largest', 'error']
            writer = csv.DictWriter(log, fieldnames=columns)
            writer.writeheader()
            for phase, count, interval in (
                    ('normal', args.normal_count, 5.0), ('rapid', args.burst_count, 1.0)):
                for cycle in range(1, count + 1):
                    cycle_start = time.monotonic()
                    for path in ('/', '/api/status'):
                        row = dict.fromkeys(columns, '')
                        row.update(utc=utc_now(), phase=phase, cycle=cycle, path=path, ok=False)
                        begin = time.monotonic()
                        try:
                            response = session.get(args.url.rstrip('/') + path,
                                                   timeout=(2, 4),
                                                   headers={'Cache-Control': 'no-cache'})
                            row.update(http_status=response.status_code, bytes=len(response.content))
                            response.raise_for_status()
                            if path == '/':
                                if 'ROM Vomitter FC' not in response.text:
                                    raise ValueError('Page title missing')
                            else:
                                state = response.json()
                                if 'mode' not in state:
                                    raise ValueError('Status mode missing')
                                row.update(heap_free=state.get('heap_free', ''),
                                           heap_largest=state.get('heap_largest', ''))
                            row['ok'] = True
                        except Exception as error:
                            row['error'] = str(error)
                        row['elapsed_ms'] = round((time.monotonic() - begin) * 1000, 2)
                        rows.append(row)
                        writer.writerow(row)
                        log.flush()
                        if not row['ok']:
                            print(f"FAIL {phase} {cycle} {path}: {row['error']}", flush=True)
                    if cycle == 1 or cycle % 5 == 0:
                        failures = sum(not r['ok'] for r in rows)
                        print(f'{phase} cycle {cycle}/{count}: {len(rows)} requests, {failures} failures', flush=True)
                    if cycle < count:
                        stop.wait(max(0, interval - (time.monotonic() - cycle_start)))
    finally:
        stop.set()
        worker.join(timeout=2)
        link.close()
        session.close()
        timings = [r['elapsed_ms'] for r in rows if r['ok']]
        free = [int(s['heap_free']) for s in snapshots
                if s.get('type') == 'RVROM' and 'heap_free' in s]
        largest = [int(s['heap_largest']) for s in snapshots
                   if s.get('type') == 'RVROM' and 'heap_largest' in s]
        summary = {
            'started_utc': started, 'finished_utc': utc_now(),
            'url': args.url, 'port': port, 'requests': len(rows),
            'failures': sum(not r['ok'] for r in rows),
            'median_success_ms': statistics.median(timings) if timings else None,
            'max_success_ms': max(timings) if timings else None,
            'serial_heap_free_min': min(free) if free else None,
            'serial_heap_free_max': max(free) if free else None,
            'serial_heap_largest_min': min(largest) if largest else None,
            'serial_errors': serial_errors,
            'phases': {'normal': {'cycles': args.normal_count, 'interval_s': 5},
                       'rapid': {'cycles': args.burst_count, 'interval_s': 1}},
            'scope': 'HTTP page/status loads and no-reset USB queries; no browser rendering or console/RF operations',
        }
        (args.output / 'serial-status.json').write_text(json.dumps(snapshots, indent=2), encoding='utf-8')
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
