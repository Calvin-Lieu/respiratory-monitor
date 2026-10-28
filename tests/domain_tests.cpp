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
    int value = 99;
    CHECK(!ring.get(0, value) && value == 99);
    ring.push(1);
    ring.push(2);
    ring.push(3);
    CHECK(ring.size() == 2);
    CHECK(ring.get(0, value) && value == 2);
    CHECK(ring.get(1, value) && value == 3);
    CHECK(!ring.get(2, value));
    ring.clear();
    ring.push(4);
    CHECK(ring.size() == 1 && ring.get(0, value) && value == 4);

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
    auto sample = filter.process({10, -10, 100, true});
    CHECK(sample.valid && sample.pressure_pa == 10);
    sample = filter.process({21, -21, 200, true});
    CHECK(sample.pressure_pa == 15 && sample.airflow_ml_per_min == -15);
    CHECK(sample.timestamp_ms == 200);
    filter.process({30, -30, 300, true});
    filter.process({40, -40, 400, true});
    sample = filter.process({50, -50, 500, true});
    CHECK(sample.pressure_pa == 35 && sample.airflow_ml_per_min == -35);
    sample = filter.process({999, 999, 600, false});
    CHECK(!sample.valid && sample.timestamp_ms == 600);
    sample = filter.process({7, -7, 700, true});
    CHECK(sample.pressure_pa == 7 && sample.airflow_ml_per_min == -7);
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
    std::puts("All domain foundation checks passed");
    return 0;
}
