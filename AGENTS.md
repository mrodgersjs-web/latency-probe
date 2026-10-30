# latency-probe — agent guide

Header-only C++17, standard library only. Do not add dependencies.

## Proven commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/probe_tests        # the smoke: all cases self-check, exit 0
./build/probe_bench        # prints BENCH line; compare to fixtures/bench-baseline.json
cmake -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  && cmake --build build-san && ./build-san/probe_tests
```

## Constraints

- `-Wall -Wextra -Werror` is the floor; warnings are errors on every job.
- The histogram contract is: constant memory, quantiles are upper bounds within
  the bucket's relative width, never underestimate. Tests enforce both.
- If you change bucket math, regenerate fixtures/bench-baseline.json in the
  same commit and say why the numbers moved.
- CI builds Release (tests + bench) and Debug+ASan/UBSan on gcc and clang.
