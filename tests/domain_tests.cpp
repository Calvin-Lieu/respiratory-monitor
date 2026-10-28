#include <cstdio>
#include <limits>
#include "respmon/filter.hpp"
#include "respmon/sensor_simulator.hpp"
#include "respmon/state_machine.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

int main()
{
    respmon::RingBuffer<int, 2> ring;
    static_assert(decltype(ring)::capacity() == 2);
    CHECK(ring.empty() && !ring.full() && ring.size() == 0);
    int value = 99;
    CHECK(!ring.get(0, value) && value == 99);
    ring.push(1);
    CHECK(!ring.empty() && !ring.full() && ring.size() == 1);
    ring.push(2);
    CHECK(ring.full());
    ring.push(3);
    CHECK(ring.size() == 2);
    CHECK(ring.get(0, value) && value == 2);
    CHECK(ring.get(1, value) && value == 3);
    CHECK(!ring.get(2, value));
    ring.clear();
    CHECK(ring.empty() && !ring.full());
    ring.push(4);
    CHECK(ring.size() == 1 && ring.get(0, value) && value == 4);
    CHECK(!ring.get(std::numeric_limits<std::size_t>::max(), value) && value == 4);
    for (int i = 0; i < 100; ++i) {
        ring.push(i);
        CHECK(ring.full() && ring.size() == ring.capacity());
        CHECK(ring.get(1, value) && value == i);
        CHECK(ring.get(0, value) && value == (i == 0 ? 4 : i - 1));
    }
    respmon::RingBuffer<int, 1> single;
    static_assert(decltype(single)::capacity() == 1);
    CHECK(single.empty() && !single.full());
    single.push(10);
    single.push(20);
    CHECK(single.full() && single.size() == 1);
    CHECK(single.get(0, value) && value == 20);
    single.clear();
    CHECK(single.empty() && !single.get(0, value));

    respmon::SensorSimulator sensor;
    for (int i = 0; i < 25; ++i) {
        const auto sample = sensor.read(i * 500);
        CHECK(sample.valid && sample.timestamp_ms == i * 500);
        CHECK(sample.pressure_pa == 100 + i % 10);
        CHECK(sample.airflow_ml_per_min == 40 + i % 5);
    }
    sensor.reset();
    CHECK(sensor.read(0).pressure_pa == 100);

    respmon::Filter filter;
    const auto empty_average = filter.average();
    CHECK(!empty_average.valid && empty_average.timestamp_ms == 0);
    CHECK(empty_average.pressure_pa == 0 && empty_average.airflow_ml_per_min == 0);
    auto sample = filter.process({10, -10, 100, true});
    CHECK(sample.valid && sample.pressure_pa == 10);
    sample = filter.process({21, -21, 200, true});
    CHECK(sample.pressure_pa == 15 && sample.airflow_ml_per_min == -15);
    CHECK(sample.timestamp_ms == 200);
    filter.process({30, -30, 300, true});
    filter.process({40, -40, 400, true});
    sample = filter.process({50, -50, 500, true});
    CHECK(sample.pressure_pa == 35 && sample.airflow_ml_per_min == -35);
    CHECK(filter.average().pressure_pa == 35 && filter.average().timestamp_ms == 500);
    sample = filter.process({999, 999, 600, false});
    CHECK(!sample.valid && sample.timestamp_ms == 600);
    CHECK(sample.pressure_pa == 0 && sample.airflow_ml_per_min == 0);
    CHECK(!filter.average().valid);
    sample = filter.process({7, -7, 700, true});
    CHECK(sample.pressure_pa == 7 && sample.airflow_ml_per_min == -7);
    filter.reset();
    CHECK(!filter.average().valid && filter.average().timestamp_ms == 0);
    // Compare many wraparounds against an independent last-four-samples sum.
    for (int i = 0; i < 100; ++i) {
        sample = filter.process({i * 7 - 100, 250 - i * 11, i * 500, true});
        const int first = i < 3 ? 0 : i - 3;
        int pressure_sum = 0;
        int airflow_sum = 0;
        for (int j = first; j <= i; ++j) {
            pressure_sum += j * 7 - 100;
            airflow_sum += 250 - j * 11;
        }
        CHECK(sample.valid && sample.timestamp_ms == i * 500);
        CHECK(sample.pressure_pa == pressure_sum / (i - first + 1));
        CHECK(sample.airflow_ml_per_min == airflow_sum / (i - first + 1));
    }
    filter.reset();
    for (int i = 0; i < 8; ++i) {
        sample = filter.process({std::numeric_limits<std::int32_t>::max(),
                                 std::numeric_limits<std::int32_t>::min(), i, true});
        CHECK(sample.pressure_pa == std::numeric_limits<std::int32_t>::max());
        CHECK(sample.airflow_ml_per_min == std::numeric_limits<std::int32_t>::min());
    }
    respmon::StateMachine monitor;
    CHECK(monitor.state() == respmon::MonitorState::initializing);
    CHECK(monitor.fault() == respmon::FaultReason::none);
    monitor.update(sample);
    CHECK(monitor.state() == respmon::MonitorState::monitoring);
    monitor.update({});
    CHECK(monitor.state() == respmon::MonitorState::sensor_fault);
    CHECK(monitor.fault() == respmon::FaultReason::invalid_sample);
    monitor.update(sample);
    CHECK(monitor.state() == respmon::MonitorState::monitoring);
    CHECK(monitor.fault() == respmon::FaultReason::none);
    monitor.reset();
    CHECK(monitor.state() == respmon::MonitorState::initializing);
    CHECK(monitor.fault() == respmon::FaultReason::none);
    std::puts("All domain buffering, filtering and foundation checks passed");
    return 0;
}
