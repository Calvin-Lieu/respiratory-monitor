#pragma once
#include "respmon/config.hpp"
#include "respmon/types.hpp"

namespace respmon
{

    struct StateMachineResult
    {
        MonitorState state{MonitorState::INITIALISING};
        bool transitioned{false}; // State changed during this event.
        FaultReason fault_reason{FaultReason::none};
        bool reason_changed{false}; // Allows cause changes within FAULT to be logged.
    };

    const char *state_name(MonitorState state) noexcept;
    const char *fault_name(FaultReason reason) noexcept;

    // Thresholds are inclusive. FAULT takes precedence and stays latched until
    // the configured number of consecutive normal samples has arrived.
    class StateMachine
    {
    public:
        explicit StateMachine(MonitorConfig config = {}) noexcept : config_(config) {}
        StateMachineResult update(const ProcessedSample &sample) noexcept;
        StateMachineResult on_timeout() noexcept;
        StateMachineResult on_sensor_fault(FaultReason reason) noexcept;
        // Clear entry/recovery streaks without changing the state or active fault.
        void interrupt_streaks() noexcept;
        void reset() noexcept;
        MonitorState state() const noexcept
        {
            return state_;
        }
        FaultReason fault() const noexcept
        {
            return fault_;
        }

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
