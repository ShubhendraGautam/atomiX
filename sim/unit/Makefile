# Unit testbenches: one Verilator build+run per RTL module.
# `make` runs them all; `make run-<module>` runs one.
VERILATOR ?= verilator
VFLAGS    := -Wall --cc -O2

RTL_CORE := ../../rtl/core

MODULES := regfile

test: $(MODULES:%=run-%)

run-%: $(RTL_CORE)/%.sv tb_%.cpp
	$(VERILATOR) $(VFLAGS) --Mdir obj_dir_$* --exe --build \
	  -o tb_$* $< $(CURDIR)/tb_$*.cpp >/dev/null
	./obj_dir_$*/tb_$*

clean:
	rm -rf obj_dir_*

.PHONY: test clean
