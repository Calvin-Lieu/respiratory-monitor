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

The simulator accepts caller-supplied monotonic timestamps without accessing
Zephyr. It advances by acquisition-attempt count through the repeating schedule
below. The filter averages up to four valid samples, including partial startup windows, using
64-bit sums and integer division toward zero. Invalid input clears history and
produces invalid output with the input timestamp.

`RingBuffer<T, N>` provides `push`, `size`, compile-time `capacity`, `empty`,
`full`, `clear`, and checked `get(index, output)` access. A push into a full
buffer overwrites exactly the oldest entry and keeps size at N. Index zero is
the oldest retained sample; invalid indices return false and leave output
unchanged. Capacity must be positive. Storage is inline; element types must
support default construction and copying without allocation or exceptions.

`Filter::average()` reads the retained window without adding a sample. When
empty (including after reset), it returns zeros and `valid=false` without
division. Otherwise its timestamp is that of the newest retained sample.
Compile-time window bounds ensure 64-bit sums can hold even extreme 32-bit
inputs. Both pressure and airflow use the same window.

The state machine starts in `initializing` (0), enters `monitoring` (1) on valid
processed input, and enters `sensor_fault` (2) on invalid input. Fault reasons
are `none` (0), `invalid_sample` (1), and `sensor_timeout` (2). Missing acquisitions
clear filter history and call the independent timeout event on the state machine.
No placeholder enters the filter. Recovery is immediate on the next valid input.
Elevated/critical classification, frozen-value detection, configurable timeout
durations, alarm persistence/hysteresis, respiratory metrics, execution-health
counters and separate telemetry are future work. Scenario labels are never used
to select monitoring states.

Runtime domain code uses fixed storage, bounded execution, no dynamic allocation,
exceptions or RTTI. Containers and domain objects have a single owner and are
not thread-safe. The main loop samples every 500 ms against absolute deadlines,
skipping missed periods; console output remains synchronous in this foundation.
No timing or clinical performance is claimed.

## Deterministic scenarios

Every scenario lasts eight acquisition attempts (`config::samples_per_scenario`).
At the configured 500 ms period, a cycle is nominally 32 seconds of simulated
time. QEMU may execute simulated time faster than wall time. Every `read()` call
advances the schedule even if no measurement is available; supplied timestamps
and scheduler delays do not change the count-based schedule. No randomness,
heap storage, sleeping or monitoring decisions occur inside the simulator.

The following are arbitrary synthetic integer values expressed in Pa and mL/min
for exercising software. They are not physiological ranges or clinical thresholds.

| Attempts (zero-based) | Scenario | Pressure (Pa) | Airflow (mL/min) | Result |
|---|---|---|---|---|
| 0–7 | normal | 100–107 | 40–44 | Valid ramp |
| 8–15 | elevated | 200–207 | 80–84 | Valid ramp |
| 16–23 | critical | 400–407 | 160–164 | Valid ramp |
| 24–31 | invalid | 100–107 | 40–44 | Present, validity false |
| 32–39 | out_of_range | -1000 | 1000000 | Present, validity false |
| 40–47 | frozen | 100 | 40 | Valid, identical values every attempt |
| 48–55 | timeout | — | — | No sample available |
| 56–63 | recovery | 100–107 | 40–44 | Valid ramp; monitor recovers |

Normal/elevated/critical/recovery ramps use within-scenario index modulo ten for
pressure and modulo five for airflow. Frozen means constant measurement values;
its timestamps and sequence numbers still advance. The synthetic output envelope
for valid samples is 100–407 Pa and 40–164 mL/min. Out-of-range inputs deliberately
violate this simulator envelope and carry `valid=false`; this does not implement
a general-purpose monitor range validator.

`SensorSample` records the caller timestamp, validity and a uint32 sequence number.
Sequence starts at zero, advances on every acquisition attempt, and wraps modulo
2^32. Timeout attempts consume sequence numbers, so recovery begins at sequence
56 rather than 48. The counter continues across scenario cycles; `reset()`
restarts both the schedule and sequence. `ProcessedSample` preserves the newest
input sequence and timestamp, including invalid input metadata. Empty filter
output has zero metadata and `valid=false`.

`SimulatedReading` carries scenario metadata separately from the measurement.
When `available=false`, its default sample is not a measurement and must be
ignored. The application logs acquisition time and availability instead. Invalid
samples log `filtered_valid=0`; their zero filtered values are not measurements.

## Build and run

From the repository, with the existing Zephyr workspace and SDK installed:

```sh
source ~/zephyrproject/.venv312/bin/activate
west build -p always -b qemu_cortex_m3 .
west build -t run
```

Expected startup includes `Respiratory monitor starting (simulated educational data)`
followed by `SIM` records showing the active scenario, sequence number, validity,
raw and filtered pressure and airflow with explicit units, and numeric state/fault
codes. Missing acquisitions log `available=0` without measurement values. For example, the second input is
101 Pa and 41 mL/min; its two-sample averages are 100 Pa and 40 mL/min due to
integer truncation. Later samples show full-window smoothing and overwrite.
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

Tests cover ring-buffer capacity/state, repeated overwrite, checked bounds,
capacity one, three complete simulator cycles, scenario boundaries, deterministic
replay/reset, missing-sample sequence gaps, frozen values, pipeline fault/recovery,
empty/partial/full filter windows, comparison
against independent sums over many wraps, negative rounding, invalid-input reset, extreme integer
inputs, and state fault/recovery/reset transitions. Checks remain active in
release builds. Build and test every milestone before creating a local commit;
do not push to a remote.
