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
    respmon::SensorSimulator replay;
    respmon::Filter scenario_filter;
    respmon::StateMachine scenario_monitor;
    constexpr respmon::SensorScenario expected[] = {
        respmon::SensorScenario::normal, respmon::SensorScenario::elevated,
        respmon::SensorScenario::critical, respmon::SensorScenario::invalid,
        respmon::SensorScenario::out_of_range, respmon::SensorScenario::frozen,
        respmon::SensorScenario::timeout, respmon::SensorScenario::recovery
    };
    constexpr auto duration = respmon::config::samples_per_scenario;
    constexpr auto cycle = duration * 8U;
    for (std::uint32_t i = 0; i < cycle * 3U; ++i) {
        // Uneven caller times do not alter count-based scenario progression.
        const auto now = static_cast<std::int64_t>(i) * 537;
        const auto reading = sensor.read(now);
        const auto again = replay.read(now);
        const auto phase = i % duration;
        const auto scenario = expected[(i % cycle) / duration];
        CHECK(reading.scenario == scenario && again.scenario == scenario);
        CHECK(reading.available == again.available);
        CHECK(reading.available == (scenario != respmon::SensorScenario::timeout));
        CHECK(respmon::scenario_name(scenario)[0] != '\0');
        if (!reading.available) {
            scenario_filter.reset();
            scenario_monitor.on_timeout();
            CHECK(!scenario_filter.average().valid);
            CHECK(scenario_monitor.state() == respmon::MonitorState::sensor_fault);
            CHECK(scenario_monitor.fault() == respmon::FaultReason::sensor_timeout);
            continue;
        }
        const auto& raw = reading.sample;
        CHECK(raw.timestamp_ms == now && raw.sequence == i);
        CHECK(raw.pressure_pa == again.sample.pressure_pa);
        CHECK(raw.airflow_ml_per_min == again.sample.airflow_ml_per_min);
        CHECK(raw.timestamp_ms == again.sample.timestamp_ms);
        CHECK(raw.valid == again.sample.valid && raw.sequence == again.sample.sequence);
        const bool invalid = scenario == respmon::SensorScenario::invalid ||
                             scenario == respmon::SensorScenario::out_of_range;
        CHECK(raw.valid == !invalid);
        const auto ramp = static_cast<std::int32_t>(phase % respmon::config::waveform_steps);
        const auto flow_ramp = static_cast<std::int32_t>(phase % 5U);
        switch (scenario) {
        case respmon::SensorScenario::elevated:
            CHECK(raw.pressure_pa == 200 + ramp && raw.airflow_ml_per_min == 80 + flow_ramp);
            break;
        case respmon::SensorScenario::critical:
            CHECK(raw.pressure_pa == 400 + ramp && raw.airflow_ml_per_min == 160 + flow_ramp);
            break;
        case respmon::SensorScenario::out_of_range:
            CHECK(raw.pressure_pa == -1000 && raw.airflow_ml_per_min == 1000000);
            break;
        case respmon::SensorScenario::frozen:
            CHECK(raw.pressure_pa == 100 && raw.airflow_ml_per_min == 40);
            break;
        default:
            CHECK(raw.pressure_pa == 100 + ramp && raw.airflow_ml_per_min == 40 + flow_ramp);
            break;
        }
        const auto processed = scenario_filter.process(raw);
        scenario_monitor.update(processed);
        CHECK(processed.sequence == i && processed.timestamp_ms == now);
        CHECK(processed.valid == !invalid);
        CHECK(scenario_monitor.fault() == (invalid ? respmon::FaultReason::invalid_sample
                                                   : respmon::FaultReason::none));
        if (scenario == respmon::SensorScenario::recovery && phase == 0) {
            CHECK(processed.pressure_pa == 100 && processed.airflow_ml_per_min == 40);
            CHECK(scenario_monitor.state() == respmon::MonitorState::monitoring);
        }
    }
    sensor.reset();
    const auto reset_reading = sensor.read(12345);
    CHECK(reset_reading.scenario == respmon::SensorScenario::normal);
    CHECK(reset_reading.available && reset_reading.sample.valid);
    CHECK(reset_reading.sample.sequence == 0 && reset_reading.sample.timestamp_ms == 12345);
    CHECK(reset_reading.sample.pressure_pa == 100);

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
