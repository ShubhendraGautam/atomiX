BUILD := build/riscv-formal/cores/axcore
CHECKS := insn_add_ch0 insn_beq_ch0 insn_lw_ch0 insn_sw_ch0

.PHONY: check generate list full clean

check: generate

	python3 run_checks.py $(CHECKS)

generate:

	python3 run.py

list: generate

	find $(BUILD)/checks -maxdepth 1 -name '*.sby' -printf '%f\n' | sort

full: generate

	python3 run_checks.py --all

clean:

	rm -rf build
