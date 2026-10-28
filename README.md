# Respiratory monitor foundation

Educational simulated data only. This application is not a medical device;
its values and behavior do not define clinical thresholds.

## Structure

- `include/respmon/types.hpp`: sample records and scoped state/fault enums.
- `include/respmon/config.hpp`: fixed capacities and simulation parameters.
- `include/respmon/ring_buffer.hpp`: fixed-capacity, oldest-first ring buffer.
- `include/respmon/filter.hpp`, `src/filter.cpp`: moving average filter.
- `include/respmon/sensor_simulator.hpp`, `src/sensor_simulator.cpp`: bounded ramps.
- `include/respmon/state_machine.hpp`, `src/state_machine.cpp`: validity states.
- `src/main.cpp`: Zephyr clock, scheduling and console output.
- `tests/`: standalone host tests of the RTOS-independent domain.

## Data and behavior

All domain types are in `respmon`. Pressure is integer pascals (Pa), signed
airflow is integer millilitres per minute (mL/min), and timestamps are integer
milliseconds from a monotonic clock. No physiological interpretation is implied.
Default samples are invalid until explicitly marked valid.

The simulator repeats pressure 100–109 Pa and airflow 40–44 mL/min with a bounded
index. It accepts caller-supplied timestamps without accessing Zephyr. The filter
averages up to four valid samples, including partial startup windows, using
64-bit sums and integer division toward zero. Invalid input clears history and
produces invalid output with the input timestamp.

The state machine starts in `initializing` (0), enters `monitoring` (1) on valid
processed input, and enters `sensor_fault` (2) on invalid input. Fault reasons
are `none` (0) and `invalid_sample` (1). Recovery is immediate at this stage.
Freshness checks, fault injection, alarm persistence/hysteresis, respiratory
metrics, execution-health counters and separate telemetry are future work.

Runtime domain code uses fixed storage, bounded execution, no dynamic allocation,
exceptions or RTTI. Containers and domain objects have a single owner and are
not thread-safe. The main loop samples every 500 ms against absolute deadlines,
skipping missed periods; console output remains synchronous in this foundation.
No timing or clinical performance is claimed.

## Build and run

From the repository, with the existing Zephyr workspace and SDK installed:

```sh
source ~/zephyrproject/.venv312/bin/activate
west build -p always -b qemu_cortex_m3 .
west build -t run
```

Expected startup includes `Respiratory monitor starting (simulated educational data)`
followed by `SIM` records with explicit units and numeric state/fault codes.
Exit QEMU with Ctrl-A then X. Build outputs are ignored; do not commit them.
The Zephyr source checkout must remain unchanged.
Toolchain capability caches default to the ignored `.cache/zephyr` directory.
The optional external compiler cache is disabled by default to keep generated
files in the repository. Both settings can be overridden through CMake.

## Host tests

With CMake 3.28+ and a GCC/Clang C++17 compiler:

```sh
cmake -S tests -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Tests cover ring-buffer overwrite and bounds, simulator wrap/reset, partial and
full filter windows, negative rounding, invalid-input reset, extreme integer
inputs, and state fault/recovery/reset transitions. Checks remain active in
release builds. Build and test every milestone before creating a local commit;
do not push to a remote.
