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
    parser.add_argument('--max-cycles-per-insn', type=int, default=8,
                        help='conservative watchdog budget before wait states')
    args = parser.parse_args()
    if (args.target_events < 1 or args.program_insns < 1 or
            args.max_cycles_per_insn < 1):
        parser.error('event and program counts must be positive')

    here = pathlib.Path(__file__).resolve().parent
    generator = here / 'gen.py'
    total = 0
    run = 0
    # RV32M instructions serialize for 32 cycles, unlike the original RV32I
    # generator's roughly two-cycles-per-instruction profile. Keep a bounded
    # watchdog, but scale it with program length so a valid M-heavy program
    # cannot be mistaken for a hung core. Each configured data-bus wait state
    # gets two further cycles of headroom per instruction.
    max_cycles = (args.program_insns *
                  (args.max_cycles_per_insn + 2 * args.ws) + 10_000)
    with tempfile.TemporaryDirectory(prefix='axcosim-') as tmp:
        image = pathlib.Path(tmp) / 'stream.bin'
        while total < args.target_events:
            seed = args.seed + run
            made = subprocess.run([sys.executable, str(generator), '--seed', str(seed),
                                   '--count', str(args.program_insns), '--out', str(image)])
            if made.returncode:
                return made.returncode
            result = subprocess.run([args.sim, '--bin', str(image), '--ws', str(args.ws),
                                     '--max', str(max_cycles)],
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
