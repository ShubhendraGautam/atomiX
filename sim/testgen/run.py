#!/usr/bin/env python3
"""Run reproducible generated programs until a cosim event budget is met."""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sim', required=True)
    parser.add_argument('--target-events', type=int, default=10_000_000)
    parser.add_argument('--program-insns', type=int, default=1_000_000)
    parser.add_argument('--seed', type=lambda n: int(n, 0), default=1)
    parser.add_argument('--ws', type=int, default=0)
    args = parser.parse_args()
    if args.target_events < 1 or args.program_insns < 1:
        parser.error('event and program counts must be positive')

    here = pathlib.Path(__file__).resolve().parent
    generator = here / 'gen.py'
    total = 0
    run = 0
    with tempfile.TemporaryDirectory(prefix='axcosim-') as tmp:
        image = pathlib.Path(tmp) / 'stream.bin'
        while total < args.target_events:
            seed = args.seed + run
            made = subprocess.run([sys.executable, str(generator), '--seed', str(seed),
                                   '--count', str(args.program_insns), '--out', str(image)])
            if made.returncode:
                return made.returncode
            result = subprocess.run([args.sim, '--bin', str(image), '--ws', str(args.ws)],
                                    text=True, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.PIPE)
            if result.returncode:
                sys.stderr.write(result.stderr)
                return result.returncode
            match = re.search(r'\((\d+) events,', result.stderr)
            if not match:
                sys.stderr.write('[testgen] cosim did not report an event count\n')
                return 2
            events = int(match.group(1))
            total += events
            run += 1
            print(f'[testgen] seed={seed} {events} events; total={total}')
    print(f'[testgen] PASS: {total} lock-step events across {run} seeds')


if __name__ == '__main__':
    main()
