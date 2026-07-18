COSIM := ../cosim
SIM   := $(abspath $(COSIM)/obj_dir/axcosim)

# Phase-1 exit campaign. Override these for a short local check, e.g.
# `make quick` or `make fuzz FUZZ_EVENTS=100000 FUZZ_INSNS=10000`.
FUZZ_EVENTS ?= 10000000
FUZZ_INSNS  ?= 1000000
FUZZ_SEED   ?= 1

quick: $(SIM)
	@mkdir -p build
	python3 gen.py --seed $(FUZZ_SEED) --count 10000 --out build/quick.bin
	$(SIM) --bin build/quick.bin --ws 2

fuzz: $(SIM)
	python3 run.py --sim $(SIM) --target-events $(FUZZ_EVENTS) \
	  --program-insns $(FUZZ_INSNS) --seed $(FUZZ_SEED)

$(SIM):
	$(MAKE) -C $(COSIM) axcosim

clean:
	rm -rf build

.PHONY: quick fuzz clean
