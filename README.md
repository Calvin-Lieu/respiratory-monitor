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
- `include/respmon/sensor_health.hpp`, `src/sensor_health.cpp`: pre-filter integrity checks.
- `src/main.cpp`: startup banner and pipeline launch.
- `src/pipeline.cpp`, `include/respmon/pipeline*.hpp`: Zephyr threads, fixed queues,
  reporting snapshots, counters and scheduling configuration.
- `tests/`: standalone host tests of the RTOS-independent domain.
- `tests/ztest/`: Zephyr ztest application for the same domain logic.

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
See the transition rules below. Respiratory metrics remain future work. Monitoring waits for queued samples with a bounded timeout;
missing acquisitions never insert placeholders into the sample queue.

Runtime domain code uses fixed storage, bounded execution, no dynamic allocation,
exceptions or RTTI. Containers and domain objects have a single owner and are
not thread-safe. Domain objects are owned by their respective pipeline threads.
Only the reporter prints after startup. No timing or clinical performance is claimed.

## Zephyr threaded pipeline

```text
acquisition (simulator owner) -- SensorSample queue [4] --> monitoring
          |                                            (filter + state owner)
          +----------- diagnostic snapshots -----------------+
                                    |                        |
                                    v                        v
                               report queue [16] --> reporting --> console
```

All queues and thread objects/stacks are statically allocated. `SensorSample`
and diagnostic records are checked for trivial byte-copyability at compile time
using the GCC/Clang intrinsic supported by Zephyr's minimal C++ library. Messages
contain values, not pointers to producer-owned mutable data.

| Thread | Preemptible priority | Requested stack bytes | Behavior |
|---|---|---|---|
| acquisition | 2 | 1024 | Read every 500 ms using absolute `k_sleep` deadlines |
| monitoring | 3 | 2048 | Receive, filter, evaluate; no console calls |
| reporting | 5 | 2048 | Consume reports, log events and summaries |
| main | default (0) | 1024 | Start threads once and return |

Lower numbers take priority. Stack sizes are configured reservations. Stack
sentinels, stack initialization and Zephyr assertions are enabled. Acquisition
skips missed periods instead of bursting catch-up samples. It never waits for
queue capacity: `k_msgq_put(..., K_NO_WAIT)` rejects the newest sample when the
four-slot sample queue is full, preserving the FIFO backlog and incrementing its
owned `dropped` counter. Invalid but present samples are enqueued normally.

Monitoring uses a 750 ms receive timeout after each sample (500 ms plus scheduling
slack). After the first timeout, it waits 500 ms per overdue event. Each timeout
clears the filter, resets the frozen run, and advances fault debounce. Three
consecutive fault-level events are needed for a timeout FAULT; scheduling and
tick rounding affect the exact time. Monitoring validates each queued sample
against `k_uptime_get()` before filtering, so a backed-up but present sample can
be rejected as stale. FIFO backlogs are processed, not purged.

Acquisition owns `attempts`, `enqueued`, `dropped` (queue-full) and `missing`
(simulator absence). Monitoring owns `total`, `valid`, `invalid`, `timeouts` and
`gaps`: `total` counts delivered samples, `valid` counts those accepted by every
integrity check, and `invalid` counts all rejected samples, including stale,
sequence and frozen faults. Therefore `total = valid + invalid` modulo 2^64.
Timeouts do not count as delivered samples. Sequence gaps include injected skips,
simulator omissions and queue drops, so `gaps` and `dropped` are distinct.
Reporting owns its snapshot copies.
No application counters or domain objects are shared between threads: each owner
publishes a value snapshot through the report queue using `K_NO_WAIT`. If that
queue fills, the new report is discarded and its producer increments a separate
report-loss counter. Subsequent successful snapshots include cumulative losses;
state events can be lost under sustained reporting overload, but sample processing
continues. Summaries expose acquisition/monitoring report losses as `report_drops`.
Counters are unsigned 64-bit values (wrap modulo 2^64). Snapshot counts may lag
and do not constitute an atomic cross-thread measurement.

After startup, only the reporter writes application logs. Zephyr logging uses
INFO for startup, recovery and two-second `SUMMARY` snapshots, WARNING for health
changes and queue overflow, and ERROR for confirmed FAULT transitions/reason
changes. Persistent faults do not produce a log line on every sample. Detailed
`COUNTERS` and `STACK` snapshots appear every ten seconds. Logging is configured
in immediate mode, so console formatting runs synchronously in the lower-priority
reporter; acquisition and monitoring never write to the console. Slow output can
fill the best-effort report queue, whose producer loss counts are reported.
`acq_scenario` is the latest acquisition snapshot; sample timestamps/sequences
refer to the latest processed sample and can differ during backlog or missing input.

A test-only build stalls monitoring once for 3000 ms to force sample queue overflow:

```sh
west build -p always -d build-pipeline-stress -b qemu_cortex_m3 . -- -DRESPMON_PIPELINE_STRESS_TEST=ON
west build -d build-pipeline-stress -t run
```

The test starts with `TEST queue_overflow`, logs `DROP acquisition ... policy=drop_newest`,
then processes the retained FIFO samples and records a sequence gap when it catches
up. The default build never intentionally stalls monitoring. Validate captured
logs using `python3 tests/check_pipeline_log.py <log>` (normal) or append `--stress`.

## Pre-filter sensor health

`SensorHealth` is RTOS-independent and receives a sample and the caller's current
monotonic uptime. `SensorHealthConfig` defaults to the synthetic inclusive range
0–500 Pa and 0–200 mL/min, maximum age 1000 ms, and at most three consecutive
identical pressure-and-airflow pairs. These values exercise the simulator; they
are not clinical limits. Configuration rejects inverted ranges, negative maximum
age, zero identical limit, and a limit too large for its bounded counter.

Priority for simultaneous sample faults is: invalid configuration; stale timestamp
(negative, future, older than maximum age, or not strictly newer than the previous
fresh timestamp); sequence order/gap; invalid source flag; out-of-range numeric
values; frozen sensor. A timeout is a separate absence event; filtered pressure
and then airflow thresholds apply only after health acceptance. Bad configuration
faults immediately; other faults follow the state machine's existing debounce.
When a sequence gap and another higher-priority fault coincide, the gap count is
still recorded. The active reason is the highest-priority fault for that event;
when multiple consecutive fault events have different causes, the latest event
becomes the latched reason after the fault streak qualifies.

The uint32 sequence uses modulo arithmetic: distance 1 is consecutive, 2 through
2^31−1 is a forward gap, and zero or 2^31 through 2^32−1 is duplicate/backward or
ambiguous and rejected without changing the baseline. This assumes fewer than
2^31 consecutive missing sequence numbers; reset or source replacement requires
explicitly resetting the health checker. Before the first sample the sequence is
assumed to start at zero. A forward gap advances the baseline but rejects that
sample, clears filtering, and counts as a fault-level event. The next fresh,
consecutive sample can begin recovery. Every fresh timestamp advances its baseline
even if the payload is rejected; stale timestamps do not. A bad payload, timeout,
or sequence/timestamp error breaks the identical-value streak. The fourth matching
sample and subsequent identical samples are rejected until values change. Recovery
requires four consecutive accepted, normal filtered samples and retains the fault
until the fourth. Filters restart empty on every rejected sample or timeout.

Zephyr uptime is signed 64-bit milliseconds; nonnegative uptime has roughly
292 million years of representable range. The timestamp check verifies bounds
before subtraction. Application deadlines assume uptime does not reach that
limit during a run. Counters use unsigned 64-bit arithmetic and wrap modulo 2^64;
state and frozen streak counters saturate at their configured bounds.

## Resource observations

The configured sample queue has four `SensorSample` entries, the report queue has
16 entries, and the moving-average buffer holds four samples. The sampling period
is 500 ms. The thread table above lists requested stack sizes and priorities.
Queues, domain objects and application threads use static storage. Runtime
application source was inspected for allocation calls and `new`/`delete`; none
are intentionally used. The built Zephyr configuration has
`CONFIG_HEAP_MEM_POOL_SIZE=0`, exceptions and RTTI disabled. This inspection does
not establish allocation behavior of every Zephyr component.

`CONFIG_INIT_STACKS` and `CONFIG_THREAD_STACK_INFO` allow the reporter to call
`k_thread_stack_space_get()` for the three pipeline threads. At 40 seconds of
simulated time in one normal QEMU run on 29 October 2026, the observed high-water
usage was 476/1024 bytes for acquisition, 852/2048 for monitoring, and 1100/2048
for reporting. These are observed stack watermarks during that run, not proven
worst-case bounds; different workloads or compiler settings can change them. If
inspection is unavailable or returns an error, a warning is logged instead of a
fabricated value.

For the normal `qemu_cortex_m3/ti_lm3s6965` build using Zephyr
`v4.5.0-rc3-40-gec98918869ca` on 29 October 2026, the clean build's linker reported
29,064 bytes in FLASH (256 KiB region) and 12,936 bytes in RAM (64 KiB region).
The Zephyr report targets gave symbol-accounted totals of 29,052 bytes ROM and
12,933 bytes RAM; those report totals differ slightly from the linker region totals.
These are build-specific observations, not runtime memory peaks. Reproduce them
from the repository root after building the normal application:

```sh
west build -d build -t rom_report
west build -d build -t ram_report
```

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

Each `update()` consumes one accepted filtered sample. A rejected sample becomes
`on_sensor_fault(reason)` and an overdue period becomes `on_timeout()`; both are
fault-level events and clear the filter. Callers must supply each event once.
The health checker rejects duplicate and out-of-order sequence numbers.
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
and current state and a named reason. The lower-priority reporter emits state
events and periodic summaries through Zephyr logging; monitoring does not log.

Reasons are `none`, `pressure_threshold`, `airflow_threshold`, `invalid_sample`,
`sensor_timeout`, `stale_timestamp`, `sequence_gap`, `sequence_order`,
`out_of_range`, `frozen_sensor`, and `invalid_configuration`. Before fault confirmation there
is no active fault reason; WARNING has reason `none`. Invalid configuration
(warning threshold >= fault threshold or any count < 2) produces an immediate,
latched FAULT on evaluation, with reason `invalid_configuration`; it cannot
recover without constructing a machine with valid settings. `reset()` returns
to INITIALISING and clears all counters/reasons, preserving configuration.
The default configuration is checked at compile time.

## Deterministic scenarios

Every scenario lasts eight acquisition attempts (`config::samples_per_scenario`).
At the configured 500 ms period, a cycle is nominally 40 seconds of simulated
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
| 32–39 | out_of_range | -1000 | 1000000 | Present, claims valid; rejected by range check |
| 40–47 | stale_timestamp | 100–107 | 40–44 | Timestamp 2000 ms behind uptime |
| 48–55 | sequence_gap | 100–107 | 40–44 | Skip one sequence before every reading |
| 56–63 | frozen | 100 | 40 | Identical values every attempt |
| 64–71 | timeout | — | — | No sample available |
| 72–79 | recovery | 100–107 | 40–44 | Ramp; gap on first delivery, then normal recovery |

Normal/elevated/critical/recovery ramps use within-scenario index modulo ten for
pressure and modulo five for airflow. Frozen means constant measurement values;
its timestamps and sequence numbers still advance. Out-of-range values deliberately
violate the configured synthetic envelope while claiming validity, to exercise
independent validation. The sequence-gap scenario skips one sequence before each
reading. Missing attempts also consume sequence numbers.

`SensorSample` records timestamp, validity and a uint32 sequence number. Sequences
start at zero and increment modulo 2^32, including missing attempts and injected
skips. In the first cycle the last pre-timeout sequence is 71 and recovery begins
at 80, leaving eight missing sequence numbers. Reset restarts schedule and sequence.
`ProcessedSample` preserves input timestamp and sequence, including on rejection.
Empty filter output has zero metadata and `valid=false`.

`SimulatedReading` carries scenario metadata separately from the measurement.
When `available=false`, its default sample is ignored and nothing is enqueued.
Acquisition counts this as `missing`, distinct from queue overflow `dropped`.
Rejected delivered samples have `filtered_valid=0`; their zero filtered values
are not measurements, even if the source validity flag was true.

## Build and run

From the repository, with the existing Zephyr workspace and SDK installed:

```sh
source ~/zephyrproject/.venv312/bin/activate
west build -p always -b qemu_cortex_m3 .
west build -t run
```

Expected startup includes `Respiratory monitor starting (simulated educational data)`,
queue configuration and all three thread startup records. Named `STATE` events
show transitions and reason changes. Every two seconds, `SUMMARY` records contain
state, active fault, acquisition scenario, raw and filtered readings, sequence,
validity, and total/valid/invalid/dropped counts. Every ten seconds, `COUNTERS`
shows detailed counts and `STACK` shows observed stack high-water values. During a
timeout, `latest_event_sample=0` and `filtered_valid=0`; the displayed values are
retained history. Counts from the two producer threads are separate snapshots and
can lag independently. See `tests/check_pipeline_log.py` for QEMU log validation.
Exit QEMU with Ctrl-A then X. Build outputs are ignored; do not commit them.
The Zephyr source checkout must remain unchanged.
Toolchain capability caches default to the ignored `.cache/zephyr` directory.
The optional external compiler cache is disabled by default to keep generated
files in the repository. Both settings can be overridden through CMake.

## Zephyr ztest on QEMU

The standalone ztest application has its own `CMakeLists.txt`, `prj.conf`, and
Twister `tests.yaml`. It exercises the ring buffer, filter, monitor state machine,
and sensor-health checks without running the application pipeline. The selected
board is `qemu_cortex_m3`; values in the tests are simulated educational data.

From the repository root:

```sh
source ~/zephyrproject/.venv312/bin/activate
mkdir -p .cache/twister-parsetab
export PARSETAB_DIR="$PWD/.cache/twister-parsetab"
export PYTHONDONTWRITEBYTECODE=1
export CCACHE_DIR="$PWD/.cache/ccache"
../zephyr/scripts/twister -T tests/ztest -p qemu_cortex_m3 --outdir build-twister --inline-logs
```

The cache settings keep generated parser and compiler files inside the repository.
Twister writes its test results, including `twister.json` and XML reports, under
the ignored `build-twister/` directory. Its summary distinguishes executed tests
from configurations that were only built. Exit with a nonzero status indicates a
test, build, or runner failure.

## GitHub Actions CI

The standalone workflow is `.github/workflows/zephyr-ci.yml`. It runs on pushes,
pull requests and manual dispatch, with read-only repository permissions. It
checks the application out under a fresh workspace and uses Zephyr commit
`ec98918869caeb2ddaef6e698f9ff36fb60a97ac`. `west update cmsis_6` fetches the
CMSIS module revision pinned in that Zephyr commit's manifest. The workflow uses
SDK 1.0.1, verifies the Linux minimal SDK and ARM toolchain archives against
committed SHA-256 values, and installs the host tools outside the application
checkout. It pins the GitHub Actions by commit and the Python build dependencies
in `ci/requirements.txt`. The runner is Ubuntu 24.04 with Python 3.12; OS package
updates and Python patch versions can still change, so this does not promise
bit-for-bit reproducibility. No dependency cache is configured.

CI builds the normal application, builds and runs the Zephyr ztests, then runs
both the normal and forced sample-queue-overflow pipeline scenarios on QEMU.
`tests/run_qemu_ci.py` captures bounded output, fails on timeout or early exit,
exits QEMU using its monitor command, and checks ztest results or invokes
`tests/check_pipeline_log.py`. Reproduce the run steps in an existing workspace:

```sh
west build -p always -b qemu_cortex_m3 -d build-ci .
west build -p always -b qemu_cortex_m3 -d build-ci-ztest tests/ztest
python tests/run_qemu_ci.py --build-dir build-ci-ztest --log build-ci-unit.log --mode unit
python tests/run_qemu_ci.py --build-dir build-ci --log build-ci-normal.log --mode normal
west build -p always -b qemu_cortex_m3 -d build-ci-stress . -- -DRESPMON_PIPELINE_STRESS_TEST=ON
python tests/run_qemu_ci.py --build-dir build-ci-stress --log build-ci-stress.log --mode stress
```

The checkout has no configured GitHub remote, so no repository-specific status
badge is included. GitHub-hosted installation and the remote workflow require an
actual run on GitHub to verify; local checks do not establish a remote CI result.

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
recovery, reason changes, custom/invalid configuration, and reset. Sensor-health
tests cover timestamp bounds, invalid/range rejection, serial-number wrap/order,
fault precedence, frozen detection and recovery. Checks remain active in
release builds. Build and test every milestone before creating a local commit;
do not push to a remote.
