#include <zephyr/ztest.h>

#include "respmon/filter.hpp"
#include "respmon/ring_buffer.hpp"
#include "respmon/sensor_health.hpp"
#include "respmon/state_machine.hpp"

using namespace respmon;

ZTEST(ring_buffer, test_empty_and_insertion)
{
    RingBuffer<int, 3> buffer;
    int value = 42;
    zassert_true(buffer.empty());
    zassert_false(buffer.full());
    zassert_equal(buffer.size(), 0U);
    zassert_equal(buffer.capacity(), 3U);
    zassert_false(buffer.get(0, value));
    zassert_equal(value, 42);

    buffer.push(10);
    buffer.push(20);
    zassert_equal(buffer.size(), 2U);
    zassert_false(buffer.empty());
    zassert_false(buffer.full());
    zassert_true(buffer.get(0, value));
    zassert_equal(value, 10);
    zassert_true(buffer.get(1, value));
    zassert_equal(value, 20);
    zassert_false(buffer.get(2, value));
    zassert_equal(value, 20);
}

ZTEST(ring_buffer, test_wrap_overwrite_and_clear)
{
    RingBuffer<int, 3> buffer;
    for (int i = 1; i <= 8; ++i) { buffer.push(i); }
    zassert_true(buffer.full());
    zassert_equal(buffer.size(), 3U);
    int value = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        zassert_true(buffer.get(i, value));
        zassert_equal(value, static_cast<int>(i) + 6);
    }
    buffer.clear();
    zassert_true(buffer.empty());
    zassert_false(buffer.full());
    zassert_false(buffer.get(0, value));
    buffer.push(9);
    zassert_true(buffer.get(0, value));
    zassert_equal(value, 9);
}

ZTEST_SUITE(ring_buffer, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(filter, test_empty_and_partial_average)
{
    Filter filter;
    auto result = filter.average();
    zassert_false(result.valid);
    zassert_equal(result.pressure_pa, 0);
    zassert_equal(result.airflow_ml_per_min, 0);
    result = filter.process({10, -10, 100, true, 1});
    zassert_true(result.valid);
    zassert_equal(result.pressure_pa, 10);
    zassert_equal(result.airflow_ml_per_min, -10);
    result = filter.process({21, -21, 200, true, 2});
    zassert_equal(result.pressure_pa, 15);
    zassert_equal(result.airflow_ml_per_min, -15);
    zassert_equal(result.timestamp_ms, 200);
    zassert_equal(result.sequence, 2U);
}

ZTEST(filter, test_full_window_and_wrap)
{
    Filter filter;
    filter.process({10, -10, 100, true, 1});
    filter.process({20, -20, 200, true, 2});
    filter.process({30, -30, 300, true, 3});
    auto result = filter.process({40, -40, 400, true, 4});
    zassert_equal(result.pressure_pa, 25);
    zassert_equal(result.airflow_ml_per_min, -25);
    result = filter.process({50, -50, 500, true, 5});
    zassert_equal(result.pressure_pa, 35);
    zassert_equal(result.airflow_ml_per_min, -35);
    zassert_equal(result.sequence, 5U);
}

ZTEST(filter, test_integer_boundaries_and_invalid_reset)
{
    Filter filter;
    for (std::uint32_t i = 0; i < 8; ++i) {
        const auto result = filter.process({INT32_MAX, INT32_MIN, i, true, i});
        zassert_equal(result.pressure_pa, INT32_MAX);
        zassert_equal(result.airflow_ml_per_min, INT32_MIN);
    }
    const auto invalid = filter.process({0, 0, 100, false, 8});
    zassert_false(invalid.valid);
    zassert_equal(invalid.timestamp_ms, 100);
    zassert_equal(invalid.sequence, 8U);
    zassert_false(filter.average().valid);
    const auto recovered = filter.process({12, -12, 101, true, 9});
    zassert_equal(recovered.pressure_pa, 12);
    zassert_equal(recovered.airflow_ml_per_min, -12);
}

ZTEST_SUITE(filter, nullptr, nullptr, nullptr, nullptr, nullptr);

namespace {
constexpr ProcessedSample normal{100, 40, 0, true, 0};
constexpr ProcessedSample warning{180, 40, 0, true, 0};
constexpr ProcessedSample critical{350, 40, 0, true, 0};

void enter_monitoring(StateMachine& monitor, std::uint32_t count = 4)
{
    for (std::uint32_t i = 0; i < count; ++i) { monitor.update(normal); }
    zassert_true(monitor.state() == MonitorState::MONITORING);
}
} // namespace

ZTEST(monitor, test_initialising_to_monitoring)
{
    StateMachine monitor;
    zassert_true(monitor.state() == MonitorState::INITIALISING);
    for (int i = 0; i < 3; ++i) {
        const auto result = monitor.update(normal);
        zassert_true(result.state == MonitorState::INITIALISING);
        zassert_false(result.transitioned);
    }
    const auto result = monitor.update(normal);
    zassert_true(result.state == MonitorState::MONITORING);
    zassert_true(result.transitioned);
    zassert_true(result.fault_reason == FaultReason::none);
}

ZTEST(monitor, test_warning_debounce_and_threshold_boundaries)
{
    StateMachine monitor;
    enter_monitoring(monitor);
    zassert_false(monitor.update({179, 69, 0, true, 0}).transitioned);
    zassert_false(monitor.update(warning).transitioned);
    zassert_false(monitor.update(warning).transitioned);
    monitor.update(normal); // Interrupted streak must start over.
    zassert_false(monitor.update(warning).transitioned);
    zassert_false(monitor.update(warning).transitioned);
    const auto result = monitor.update(warning);
    zassert_true(result.state == MonitorState::WARNING);
    zassert_true(result.transitioned);
    zassert_true(result.fault_reason == FaultReason::none);
}

ZTEST(monitor, test_critical_samples_enter_fault)
{
    StateMachine monitor;
    enter_monitoring(monitor);
    monitor.update(critical);
    monitor.update(critical);
    const auto result = monitor.update(critical);
    zassert_true(result.state == MonitorState::FAULT);
    zassert_true(result.transitioned);
    zassert_true(result.fault_reason == FaultReason::pressure_threshold);
}

ZTEST(monitor, test_hysteresis_and_configured_recovery)
{
    MonitorConfig config;
    config.recovery_samples = 3;
    StateMachine monitor(config);
    enter_monitoring(monitor, config.recovery_samples);
    for (int i = 0; i < 3; ++i) { monitor.update(critical); }
    zassert_true(monitor.state() == MonitorState::FAULT);
    zassert_false(monitor.update(normal).transitioned);
    zassert_false(monitor.update(normal).transitioned);
    zassert_true(monitor.update(warning).state == MonitorState::FAULT);
    zassert_false(monitor.update(normal).transitioned);
    zassert_false(monitor.update(normal).transitioned);
    const auto result = monitor.update(normal);
    zassert_true(result.state == MonitorState::MONITORING);
    zassert_true(result.transitioned);
    zassert_true(result.fault_reason == FaultReason::none);
}

ZTEST(monitor, test_invalid_reading_fault)
{
    StateMachine monitor;
    enter_monitoring(monitor);
    for (int i = 0; i < 2; ++i) { zassert_false(monitor.update({}).transitioned); }
    const auto result = monitor.update({});
    zassert_true(result.state == MonitorState::FAULT);
    zassert_true(result.fault_reason == FaultReason::invalid_sample);
}

ZTEST(monitor, test_timeout_fault)
{
    StateMachine monitor;
    enter_monitoring(monitor);
    for (int i = 0; i < 2; ++i) { zassert_false(monitor.on_timeout().transitioned); }
    const auto result = monitor.on_timeout();
    zassert_true(result.state == MonitorState::FAULT);
    zassert_true(result.fault_reason == FaultReason::sensor_timeout);
}

ZTEST_SUITE(monitor, nullptr, nullptr, nullptr, nullptr, nullptr);

ZTEST(sensor_health, test_frozen_sensor_fault_and_recovery)
{
    SensorHealthConfig config;
    config.max_identical_samples = 2;
    SensorHealth health(config);
    StateMachine monitor;
    for (std::uint32_t i = 0; i < 2; ++i) {
        const auto result = health.inspect({100, 40, i, true, i}, i);
        zassert_true(result.accepted());
        monitor.update(normal);
    }
    for (std::uint32_t i = 2; i < 5; ++i) {
        const auto result = health.inspect({100, 40, i, true, i}, i);
        zassert_true(result.reason == FaultReason::frozen_sensor);
        monitor.on_sensor_fault(result.reason);
    }
    zassert_true(monitor.state() == MonitorState::FAULT);
    zassert_true(monitor.fault() == FaultReason::frozen_sensor);
    for (std::uint32_t i = 5; i < 9; ++i) {
        const SensorSample sample{100 + static_cast<std::int32_t>(i), 40, i, true, i};
        zassert_true(health.inspect(sample, i).accepted());
        const auto result = monitor.update({sample.pressure_pa, sample.airflow_ml_per_min,
                                            sample.timestamp_ms, true, sample.sequence});
        zassert_true(result.state == (i == 8 ? MonitorState::MONITORING : MonitorState::FAULT));
    }
}

ZTEST(sensor_health, test_simultaneous_fault_precedence)
{
    SensorHealth health;
    zassert_true(health.inspect({100, 40, 1000, true, 0}, 1000).accepted());
    const auto stale_and_gap = health.inspect({-1, 201, 0, false, 3}, 2000);
    zassert_true(stale_and_gap.reason == FaultReason::stale_timestamp);
    zassert_equal(stale_and_gap.missing_sequences, 2U);
    zassert_true(health.inspect({-1, 201, 2001, false, 5}, 2001).reason == FaultReason::sequence_gap);
    zassert_true(health.inspect({-1, 201, 2002, false, 6}, 2002).reason == FaultReason::invalid_sample);
    zassert_true(health.inspect({-1, 201, 2003, true, 7}, 2003).reason == FaultReason::out_of_range);

    StateMachine monitor;
    for (int i = 0; i < 3; ++i) { monitor.update({350, 140, 0, true, 0}); }
    zassert_true(monitor.fault() == FaultReason::pressure_threshold);
    monitor.reset();
    for (int i = 0; i < 3; ++i) { monitor.update({350, 140, 0, false, 0}); }
    zassert_true(monitor.fault() == FaultReason::invalid_sample);
}

ZTEST(sensor_health, test_timestamp_and_sequence_boundaries)
{
    SensorHealth health;
    zassert_true(health.inspect({100, 40, 0, true, 0}, 0).accepted());
    zassert_true(health.inspect({101, 41, 500, true, 1}, 1500).accepted());
    zassert_true(health.inspect({102, 42, 500, true, 2}, 1501).reason == FaultReason::stale_timestamp);
    zassert_true(health.inspect({103, 43, 1600, true, 2}, 1600).reason == FaultReason::sequence_order);
    zassert_true(health.inspect({104, 44, 2000, true, 3}, 2000).accepted());

    SensorHealth wrap;
    zassert_true(wrap.inspect({100, 40, 0, true, UINT32_MAX - 1}, 0).reason == FaultReason::sequence_gap);
    zassert_true(wrap.inspect({101, 41, 1, true, UINT32_MAX}, 1).accepted());
    zassert_true(wrap.inspect({102, 42, 2, true, 0}, 2).accepted());
}

ZTEST(sensor_health, test_range_boundaries)
{
    SensorHealth health;
    zassert_true(health.inspect({0, 0, 0, true, 0}, 0).accepted());
    zassert_true(health.inspect({500, 200, 1, true, 1}, 1).accepted());
    zassert_true(health.inspect({501, 200, 2, true, 2}, 2).reason == FaultReason::out_of_range);
    zassert_true(health.inspect({0, -1, 3, true, 3}, 3).reason == FaultReason::out_of_range);
}

ZTEST_SUITE(sensor_health, nullptr, nullptr, nullptr, nullptr, nullptr);
