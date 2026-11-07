Follow docs/CODING_RULES.md and docs/CODING_RULES_PROMPT.txt for all new and modified C code, tests and examples.

The library is driven by the host GPS receive event. Do not add a private receiver thread, timer or wire-format cast of GpsTime_t.
Keep calendar arithmetic and NCR reconstruction deterministic and testable with injected clocks.
System clock adjustment is opt-in through bAllowClockStep and is disabled by default.
Preserve separate source HOLDOVER, message freshness and last LOCKED age limits.
Capture packet receive contexts before queues/reassembly; retain per-packet time quality.

Target Ubuntu Linux with C11, pthread and 64-bit time_t. Keep the source list Linux-only.
Run make test example or sh ./build_ubuntu.sh test on Ubuntu. Use make asan, make ubsan and make tsan where the toolchain supports them.
Do not reintroduce Windows adapters or Windows build artifacts into this Linux project.
Document which operating systems and sanitizers were actually tested. Do not claim hardware/RLE/IV/AES validation from unit tests.
