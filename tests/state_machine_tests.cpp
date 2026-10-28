#include <cstdio>
#include "respmon/state_machine.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (false)

int main()
{
    using namespace respmon;
    const ProcessedSample normal{100, 40, 0, true};
    const ProcessedSample warning{180, 40, 0, true};
    const ProcessedSample critical{350, 40, 0, true};
    StateMachine monitor;
    CHECK(monitor.state() == MonitorState::INITIALISING);
    for (int i = 0; i < 4; ++i) {
        const auto result = monitor.update(normal);
        CHECK(result.state == (i < 3 ? MonitorState::INITIALISING : MonitorState::MONITORING));
        CHECK(result.transitioned == (i == 3));
        CHECK(result.fault_reason == FaultReason::none && !result.reason_changed);
    }
    // Exact warning boundary; a single normal event breaks the entry streak.
    CHECK(!monitor.update(warning).transitioned);
    CHECK(!monitor.update(warning).transitioned);
    monitor.update(normal);
    CHECK(!monitor.update(warning).transitioned);
    CHECK(!monitor.update(warning).transitioned);
    auto result = monitor.update(warning);
    CHECK(result.transitioned && result.state == MonitorState::WARNING);
    CHECK(result.fault_reason == FaultReason::none);
    CHECK(!monitor.update(warning).transitioned);
    // Recovery must be consecutive; warning input interrupts the normal streak.
    for (int i = 0; i < 3; ++i) { CHECK(!monitor.update(normal).transitioned); }
    monitor.update(warning);
    for (int i = 0; i < 3; ++i) { CHECK(!monitor.update(normal).transitioned); }
    CHECK(monitor.update(normal).state == MonitorState::MONITORING);

    // Critical boundary and independent critical streak interrupted by warning.
    monitor.update(critical);
    monitor.update(critical);
    monitor.update(warning);
    CHECK(!monitor.update(critical).transitioned);
    CHECK(!monitor.update(critical).transitioned);
    result = monitor.update(critical);
    CHECK(result.transitioned && result.state == MonitorState::FAULT);
    CHECK(result.fault_reason == FaultReason::pressure_threshold && result.reason_changed);
    for (int i = 0; i < 10; ++i) {
        result = monitor.update(warning);
        CHECK(result.state == MonitorState::FAULT && !result.transitioned);
        CHECK(result.fault_reason == FaultReason::pressure_threshold);
    }
    for (int i = 0; i < 3; ++i) {
        result = monitor.update(normal);
        CHECK(result.state == MonitorState::FAULT && !result.reason_changed);
    }
    result = monitor.update(normal);
    CHECK(result.state == MonitorState::MONITORING && result.transitioned);
    CHECK(result.fault_reason == FaultReason::none && result.reason_changed);

    // Both channels: just below each threshold stays normal; equality triggers.
    monitor.reset();
    for (int i = 0; i < 4; ++i) { monitor.update({179, 69, 0, true}); }
    CHECK(monitor.state() == MonitorState::MONITORING);
    for (int i = 0; i < 3; ++i) { result = monitor.update({100, 70, 0, true}); }
    CHECK(result.state == MonitorState::WARNING);
    for (int i = 0; i < 3; ++i) { result = monitor.update({349, 139, 0, true}); }
    CHECK(result.state == MonitorState::WARNING);
    for (int i = 0; i < 3; ++i) { result = monitor.update({100, 140, 0, true}); }
    CHECK(result.state == MonitorState::FAULT && result.fault_reason == FaultReason::airflow_threshold);

    // Invalid input overrides numeric thresholds; mixed fault-level events
    // count consecutively. Cause is the most recent confirmed fault-level input.
    monitor.reset();
    monitor.update(critical);
    monitor.on_timeout();
    result = monitor.update({1000, 1000, 0, false});
    CHECK(result.state == MonitorState::FAULT && result.transitioned);
    CHECK(result.fault_reason == FaultReason::invalid_sample);
    result = monitor.on_timeout();
    CHECK(!result.transitioned && result.reason_changed);
    CHECK(result.fault_reason == FaultReason::sensor_timeout);
    result = monitor.on_timeout();
    CHECK(!result.transitioned && !result.reason_changed);
    monitor.reset();
    CHECK(monitor.state() == MonitorState::INITIALISING && monitor.fault() == FaultReason::none);
    CHECK(!monitor.on_timeout().transitioned);
    CHECK(!monitor.on_timeout().transitioned);
    result = monitor.on_timeout();
    CHECK(result.state == MonitorState::FAULT && result.fault_reason == FaultReason::sensor_timeout);
    for (int i = 0; i < 4; ++i) { result = monitor.update(normal); }
    CHECK(result.state == MonitorState::MONITORING && result.fault_reason == FaultReason::none);
    monitor.reset();
    CHECK(!monitor.update({}).transitioned);
    CHECK(!monitor.update({}).transitioned);
    CHECK(monitor.update({}).fault_reason == FaultReason::invalid_sample);

    // Direct INITIALISING -> FAULT, custom thresholds and independent counts.
    MonitorConfig custom;
    custom.warning_pressure_pa = 120;
    custom.fault_pressure_pa = 150;
    custom.warning_samples = 2;
    custom.fault_samples = 4;
    custom.recovery_samples = 2;
    StateMachine configured(custom);
    CHECK(!configured.update({150, 40, 0, true}).transitioned);
    CHECK(configured.update({150, 40, 0, true}).state == MonitorState::WARNING);
    CHECK(!configured.update({150, 40, 0, true}).transitioned);
    CHECK(configured.update({150, 40, 0, true}).state == MonitorState::FAULT);
    CHECK(!configured.update(normal).transitioned);
    CHECK(configured.update(normal).state == MonitorState::MONITORING);
    // Repeated events stay stable with saturating counters.
    for (int i = 0; i < 10000; ++i) {
        result = configured.update(normal);
        CHECK(!result.transitioned && !result.reason_changed);
    }
    custom.fault_samples = 1;
    CHECK(!custom.valid());
    StateMachine invalid_config(custom);
    result = invalid_config.update(normal);
    CHECK(result.state == MonitorState::FAULT && result.transitioned);
    CHECK(result.fault_reason == FaultReason::invalid_configuration);
    CHECK(!invalid_config.on_timeout().transitioned);
    custom = {};
    custom.warning_pressure_pa = custom.fault_pressure_pa;
    CHECK(!custom.valid());
    custom = {};
    custom.recovery_samples = 0;
    CHECK(!custom.valid());
    std::puts("All state-machine transition checks passed");
    return 0;
}
