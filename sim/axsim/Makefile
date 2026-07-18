CXX      := $(or $(shell command -v g++ 2>/dev/null),$(shell command -v clang++ 2>/dev/null))
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra

SRC := src/main.cpp src/cpu.cpp
HDR := src/bus.h src/cpu.h

axsim: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

# Hand-assembled UART smoke test (stopgap until the RISC-V GCC toolchain
# is installed; then directed tests become real .S sources).
smoke: axsim
	@mkdir -p build
	python3 ../../tests/directed/words2bin.py ../../tests/directed/smoke_ok.words build/smoke_ok.bin
	./axsim --bin build/smoke_ok.bin | tee build/smoke.out
	@grep -qx "OK" build/smoke.out && echo "SMOKE PASS" || (echo "SMOKE FAIL"; exit 1)

clean:
	rm -rf axsim build

.PHONY: smoke clean
