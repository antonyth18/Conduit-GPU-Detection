# Cloud development container, 2 vCPU, GPU EMULATED, no calldata

Development run used to test the scripts. **No GPU:** every GPU row is the host
emulation of the kernels and its times mean nothing. **No calldata:** P2/P3
collapse to P1. Only the correctness checks and the machine-independent counts
(batches, edges, model bounds) are meaningful here.
