# Respiratory monitor foundation

Educational simulated data only. This application is not a medical device;
its values and behavior do not define clinical thresholds.

## Structure

- `include/respmon/types.hpp`: sample records and scoped state/fault enums.
- `include/respmon/config.hpp`: fixed capacities and simulation parameters.
- `include/respmon/ring_buffer.hpp`: fixed-capacity, oldest-first ring buffer.
- `include/respmon/filter.hpp`, `src/filter.cpp`: moving average filter.
- `include/respmon/sensor_simulator.hpp`, `src/sensor_simulator.cpp`: bounded ramps.
- `include/respmon/state_machine.hpp`, `src/state_machine.cpp`: debounced monitoring states.
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

The independent state machine consumes filtered samples, not simulator labels.
See the transition rules below. Frozen-value detection, configurable elapsed-time
timeout detection, respiratory metrics, execution-health counters and separate
telemetry remain future work. Missing acquisitions clear filter history and
supply explicit timeout events; no placeholder enters the filter.

Runtime domain code uses fixed storage, bounded execution, no dynamic allocation,
exceptions or RTTI. Containers and domain objects have a single owner and are
not thread-safe. The main loop samples every 500 ms against absolute deadlines,
skipping missed periods; console output remains synchronous in this foundation.
No timing or clinical performance is claimed.

## Monitoring states and transitions

`MonitorConfig` is a copied configuration object supplied to `StateMachine`.
Defaults below are arbitrary educational simulation settings, not clinically
validated thresholds. All comparisons are inclusive and classify upper excursions
only; pressure is integer Pa and airflow is integer mL/min.

| Setting | Default |
|---|---|
| Warning pressure / airflow | 180 Pa / 70 mL/min |
| Fault pressure / airflow | 350 Pa / 140 mL/min |
| Consecutive abnormal events for WARNING | 3 |
| Consecutive fault-level events for FAULT | 3 |
| Consecutive normal samples for initialisation/recovery | 4 |

Each `update()` or `on_timeout()` is exactly one acquisition event. Callers must
supply each acquisition once; sequence freshness/deduplication is not implemented.
An invalid sample is fault-level regardless of numeric fields. A timeout is also
fault-level. For valid input, either fault threshold is fault-level; otherwise
either warning threshold is warning-level. Normal means valid and strictly below
both warning thresholds. Pressure has reason priority if both fault thresholds
are exceeded. Low excursions are not classified by this upper-threshold demo.

| Current state | Condition | Result |
|---|---|---|
| INITIALISING | Four consecutive normal samples | MONITORING |
| INITIALISING or MONITORING | Three consecutive abnormal events | WARNING |
| Any state | Three consecutive fault-level events | FAULT |
| WARNING or FAULT | Four consecutive normal samples | MONITORING; reason cleared |
| FAULT | Warning-level input or incomplete recovery | Remain FAULT; retain reason |
| Any state | No rule satisfied | Retain state |

Fault-level events also count as abnormal. If warning and fault entry qualify
on the same event, FAULT wins. A normal sample resets both abnormal entry counters;
warning-only input resets the fault counter; any abnormal event resets recovery.
Fault-level events with different causes can form one consecutive streak. The
most recent event supplies the active reason once the fault streak qualifies.
While the streak remains qualified, a different fault-level cause updates the
reason without changing state. Recovery retains the last reason until complete.
Counters saturate at their configured limits instead of overflowing.

`StateMachineResult` returns `state`, `transitioned`, `fault_reason`, and
`reason_changed`. `transitioned` means only a change of state. The application
prints a `STATE` event when state or active reason changes, including previous
and current state and a named reason. Per-sample `SIM` telemetry contains readings
and validity without repeating the monitoring state.

Reasons are `none`, `pressure_threshold`, `airflow_threshold`, `invalid_sample`,
`sensor_timeout`, and `invalid_configuration`. Before fault confirmation there
is no active fault reason; WARNING has reason `none`. Invalid configuration
(warning threshold >= fault threshold or any count < 2) produces an immediate,
latched FAULT on evaluation, with reason `invalid_configuration`; it cannot
recover without constructing a machine with valid settings. `reset()` returns
to INITIALISING and clears all counters/reasons, preserving configuration.
The default configuration is checked at compile time.

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
raw and filtered pressure and airflow with explicit units. Separate `STATE`
events report named states and reasons only when they change. Missing acquisitions log `available=0` without measurement values. For example, the second input is
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
inputs, and complete filtered-pipeline cycles. A separate state-machine suite checks
threshold equality and channel independence, interrupted streaks, latched faults,
recovery, reason changes, custom/invalid configuration, and reset. Checks remain active in
release builds. Build and test every milestone before creating a local commit;
do not push to a remote.
