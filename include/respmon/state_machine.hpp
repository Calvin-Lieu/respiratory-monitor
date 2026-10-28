#pragma once
#include "respmon/config.hpp"
#include "respmon/types.hpp"

namespace respmon {

struct StateMachineResult {
    MonitorState state{MonitorState::INITIALISING};
    bool transitioned{false}; // State changed during this event.
    FaultReason fault_reason{FaultReason::none};
    bool reason_changed{false}; // Allows cause changes within FAULT to be logged.
};

const char* state_name(MonitorState state) noexcept;
const char* fault_name(FaultReason reason) noexcept;

// Single-owner domain logic. Each update/timeout is one acquisition event.
// Thresholds are inclusive; fault takes precedence over warning. Abnormal and
// fault-level streaks reset on normal input; warning-only input breaks a fault
// streak. INITIALISING/WARNING/FAULT require a full normal streak to MONITORING.
// FAULT stays latched through warning-level input. No simulator/RTOS dependency.
class StateMachine {
public:
    explicit StateMachine(MonitorConfig config = {}) noexcept : config_(config) {}
    StateMachineResult update(const ProcessedSample& sample) noexcept;
    StateMachineResult on_timeout() noexcept;
    // Missing sequence numbers break consecutiveness but retain any active fault.
    void interrupt_streaks() noexcept;
    void reset() noexcept;
    MonitorState state() const noexcept { return state_; }
    FaultReason fault() const noexcept { return fault_; }
private:
    StateMachineResult evaluate(bool abnormal, FaultReason reason) noexcept;
    MonitorConfig config_;
    MonitorState state_{MonitorState::INITIALISING};
    FaultReason fault_{FaultReason::none};
    std::uint32_t abnormal_count_{0};
    std::uint32_t fault_count_{0};
    std::uint32_t normal_count_{0};
};
} // namespace respmon
