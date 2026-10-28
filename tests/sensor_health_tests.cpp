#include <cstdio>
#include <limits>
#include "respmon/sensor_health.hpp"
#include "respmon/filter.hpp"
#include "respmon/state_machine.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

int main()
{
    using namespace respmon;
    SensorHealth health;
    CHECK(health.inspect({100, 40, 0, true, 0}, 0).accepted());
    CHECK(health.inspect({101, 41, 500, true, 1}, 500).accepted());
    // Exact age boundary accepted, above it rejected; duplicate, future and
    // decreasing timestamps are all stale even with new sequence numbers.
    CHECK(health.inspect({102, 42, 1000, true, 2}, 2000).accepted());
    CHECK(health.inspect({103, 43, 1000, true, 3}, 2000).reason == FaultReason::stale_timestamp);
    CHECK(health.inspect({104, 44, 1500, true, 4}, 2501).reason == FaultReason::stale_timestamp);
    CHECK(health.inspect({105, 40, 3001, true, 5}, 3000).reason == FaultReason::stale_timestamp);
    CHECK(health.inspect({106, 41, 999, true, 6}, 1200).reason == FaultReason::stale_timestamp);
    CHECK(health.inspect({107, 42, 3500, true, 7}, 3500).accepted());
    CHECK(health.inspect({107, 42, -1, true, 8}, 4000).reason == FaultReason::stale_timestamp);
    CHECK(health.inspect({107, 42, 0, true, 9}, std::numeric_limits<std::int64_t>::max()).reason == FaultReason::stale_timestamp);

    SensorHealth ranges;
    CHECK(ranges.inspect({0, 0, 0, true, 0}, 0).accepted());
    CHECK(ranges.inspect({500, 200, 1, true, 1}, 1).accepted());
    CHECK(ranges.inspect({-1, 40, 2, true, 2}, 2).reason == FaultReason::out_of_range);
    CHECK(ranges.inspect({501, 40, 3, true, 3}, 3).reason == FaultReason::out_of_range);
    CHECK(ranges.inspect({100, -1, 4, true, 4}, 4).reason == FaultReason::out_of_range);
    CHECK(ranges.inspect({100, 201, 5, true, 5}, 5).reason == FaultReason::out_of_range);
    CHECK(ranges.inspect({1000, 1000, 6, false, 6}, 6).reason == FaultReason::invalid_sample);
    CHECK(ranges.inspect({100, 40, 7, true, 7}, 7).accepted());

    SensorHealth order;
    CHECK(order.inspect({100, 40, 0, true, 0}, 0).accepted());
    auto gap = order.inspect({101, 41, 1, true, 4}, 1);
    CHECK(gap.reason == FaultReason::sequence_gap && gap.missing_sequences == 3);
    CHECK(order.inspect({102, 42, 2, true, 5}, 2).accepted());
    CHECK(order.inspect({103, 43, 3, true, 5}, 3).reason == FaultReason::sequence_order);
    CHECK(order.inspect({104, 44, 4, true, 4}, 4).reason == FaultReason::sequence_order);
    CHECK(order.inspect({105, 40, 5, true, 0x80000005U}, 5).reason == FaultReason::sequence_order);
    CHECK(order.inspect({106, 41, 6, true, 6}, 6).accepted());
    SensorHealth wrap;
    CHECK(wrap.inspect({100, 40, 0, true, UINT32_MAX - 1}, 0).reason == FaultReason::sequence_gap);
    CHECK(wrap.inspect({101, 41, 1, true, UINT32_MAX}, 1).accepted());
    CHECK(wrap.inspect({102, 42, 2, true, 0}, 2).accepted());
    CHECK(wrap.inspect({103, 43, 3, true, 1}, 3).accepted());

    SensorHealth frozen;
    for (std::uint32_t i = 0; i < 3; ++i) {
        CHECK(frozen.inspect({100, 40, i, true, i}, i).accepted());
    }
    for (std::uint32_t i = 3; i < 10; ++i) {
        CHECK(frozen.inspect({100, 40, i, true, i}, i).reason == FaultReason::frozen_sensor);
    }
    CHECK(frozen.inspect({101, 40, 10, true, 10}, 10).accepted());
    frozen.on_timeout();
    CHECK(frozen.inspect({101, 40, 11, true, 11}, 11).accepted());

    // Simultaneous faults: timestamp > sequence > status > range > frozen.
    SensorHealth precedence;
    CHECK(precedence.inspect({100, 40, 1000, true, 0}, 1000).accepted());
    auto multiple = precedence.inspect({-1, 201, 0, false, 3}, 2000);
    CHECK(multiple.reason == FaultReason::stale_timestamp && multiple.missing_sequences == 2);
    CHECK(precedence.inspect({-1, 201, 2001, false, 5}, 2001).reason == FaultReason::sequence_gap);
    CHECK(precedence.inspect({-1, 201, 2002, false, 6}, 2002).reason == FaultReason::invalid_sample);
    CHECK(precedence.inspect({-1, 201, 2003, true, 7}, 2003).reason == FaultReason::out_of_range);
    SensorHealthConfig bad;
    bad.max_identical_samples = 0;
    SensorHealth misconfigured(bad);
    const auto config_fault = misconfigured.inspect({-1, -1, -1, false, 0}, 0);
    CHECK(config_fault.reason == FaultReason::invalid_configuration);
    StateMachine config_monitor;
    CHECK(config_monitor.on_sensor_fault(config_fault.reason).state == MonitorState::FAULT);

    // Rejected samples do not enter the moving average, and normal recovery
    // takes four accepted samples after the fault has been confirmed.
    SensorHealth pipeline;
    Filter filter;
    StateMachine monitor;
    for (std::uint32_t i = 0; i < 4; ++i) {
        SensorSample raw{100 + static_cast<std::int32_t>(i), 40, i, true, i};
        CHECK(pipeline.inspect(raw, i).accepted());
        monitor.update(filter.process(raw));
    }
    CHECK(monitor.state() == MonitorState::MONITORING);
    for (std::uint32_t i = 4; i < 7; ++i) {
        SensorSample raw{100000, 40, i, true, i};
        const auto result = pipeline.inspect(raw, i);
        CHECK(result.reason == FaultReason::out_of_range);
        filter.reset();
        CHECK(!filter.average().valid);
        monitor.on_sensor_fault(result.reason);
    }
    CHECK(monitor.state() == MonitorState::FAULT);
    for (std::uint32_t i = 7; i < 11; ++i) {
        SensorSample raw{110 + static_cast<std::int32_t>(i), 40, i, true, i};
        CHECK(pipeline.inspect(raw, i).accepted());
        auto filtered = filter.process(raw);
        CHECK(filtered.pressure_pa < 500);
        monitor.update(filtered);
        CHECK(monitor.state() == (i == 10 ? MonitorState::MONITORING : MonitorState::FAULT));
    }
    std::puts("Sensor health, precedence, wraparound and recovery checks passed");
    return 0;
}
