#!/usr/bin/env bash
# Run the riscv-tests ISA suites (p environment) against a simulator.
# Usage: run-riscv-tests.sh [suite-glob ...]   (default: rv32ui rv32mi)
# SIM=<path> overrides the simulator (default: aXsim).
set -u
cd "$(dirname "$0")"
sim=${SIM:-../sim/axsim/axsim}
globs=("${@:-rv32ui rv32mi}")
[[ $# -eq 0 ]] && globs=(rv32ui rv32mi)

# Policy exclusions, not failures:
#   rv32ui-p-ma_data expects HARDWARE misaligned data access support; atomiX
#   (like Spike without --misaligned) traps on misaligned accesses instead —
#   the complementary rv32mi-p-ma_addr test verifies those traps and must pass.
exclude=(rv32ui-p-ma_data)

pass=0 fail=0
failed=()
for suite in "${globs[@]}"; do
  for t in riscv-tests/isa/"$suite"-p-*; do
    [[ $t == *.dump || ! -f $t ]] && continue
    [[ " ${exclude[*]} " == *" $(basename "$t") "* ]] && continue
    if "$sim" --bin "$t" --max 2000000 >/dev/null 2>&1; then
      pass=$((pass + 1))
    else
      fail=$((fail + 1))
      failed+=("$(basename "$t")")
    fi
  done
done

echo "riscv-tests: $pass passed, $fail failed"
((fail > 0)) && printf '  FAIL: %s\n' "${failed[@]}"
exit $((fail > 0))
