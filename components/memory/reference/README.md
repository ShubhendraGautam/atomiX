# Reference memory family

This directory owns the complete stock `axmem` implementation and its private
backends: immediate BRAM, delayed external-memory model, and physical x16
SDRAM controller. The `memory.bram`, `memory.delayed`, and `memory.sdram`
manifests select this same source closure with different elaboration and runner
defaults; they are configurations of one compatible reference family, not
three unrelated copies of the controller.

A replacement `memory` component supplies its own `axmem` source list and is
compiled without these reference files. Its only stock integration contract is
the `axmem` module instantiated by the selected `soc_top`.
