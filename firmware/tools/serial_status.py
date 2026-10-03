#!/usr/bin/env python3
"""Query ROM Vomitter over USB without asserting reset or changing ROM/radio state."""
import argparse
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Defaults to the only ESP32-S3 VID303A/PID1001 port')
    parser.add_argument('--count', type=int, default=3)
    parser.add_argument('--interval', type=float, default=1.0)
    args = parser.parse_args()
    if args.count < 1 or args.interval < 0:
        parser.error('count must be positive and interval nonnegative')
    port = args.port
    if not port:
        candidates = [p.device for p in list_ports.comports()
                      if p.vid == 0x303A and p.pid == 0x1001]
        if len(candidates) != 1:
            parser.error(f'Expected one ESP32-S3 port; found {candidates}. Specify --port.')
        port = candidates[0]
    link = serial.Serial()
    link.port, link.baudrate, link.timeout = port, 115200, 0.2
    link.dtr = False
    link.rts = False
    link.open()
    try:
        for query in range(args.count):
            print(f'Query {query + 1} on {port}', flush=True)
            link.write(b'RVST\n')
            link.flush()
            seen = set()
            pending = bytearray()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and len(seen) < 3:
                pending.extend(link.read(1024))
                while b'\n' in pending:
                    line, _, rest = pending.partition(b'\n')
                    pending[:] = rest
                    text = line.decode('utf-8', 'replace').strip()
                    print(text, flush=True)
                    for prefix in ('RVST ', 'RVAP ', 'RVROM '):
                        if text.startswith(prefix):
                            seen.add(prefix)
            if len(seen) < 3:
                raise SystemExit('Incomplete RVST response. Older firmware has no RVST support; no reset was sent.')
            if query + 1 < args.count:
                time.sleep(args.interval)
    finally:
        link.close()


if __name__ == '__main__':
    main()
