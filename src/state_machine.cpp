#include "respmon/state_machine.hpp"

namespace respmon
{
    namespace
    {
        void increment(std::uint32_t &count, std::uint32_t limit) noexcept
        {
            // Saturate to avoid overflow on long runs.
            if (count < limit)
            {
                ++count;
            }
        }
    } // namespace

    const char *state_name(MonitorState state) noexcept
    {
        switch (state)
        {
        case MonitorState::INITIALISING:
            return "INITIALISING";
        case MonitorState::MONITORING:
            return "MONITORING";
        case MonitorState::WARNING:
            return "WARNING";
        case MonitorState::FAULT:
            return "FAULT";
        }
        return "UNKNOWN";
    }

    const char *fault_name(FaultReason reason) noexcept
    {
        switch (reason)
        {
        case FaultReason::none:
            return "none";
        case FaultReason::invalid_sample:
            return "invalid_sample";
        case FaultReason::sensor_timeout:
            return "sensor_timeout";
        case FaultReason::pressure_threshold:
            return "pressure_threshold";
        case FaultReason::airflow_threshold:
            return "airflow_threshold";
        case FaultReason::invalid_configuration:
            return "invalid_configuration";
        case FaultReason::stale_timestamp:
            return "stale_timestamp";
        case FaultReason::sequence_gap:
            return "sequence_gap";
        case FaultReason::sequence_order:
            return "sequence_order";
        case FaultReason::out_of_range:
            return "out_of_range";
        case FaultReason::frozen_sensor:
            return "frozen_sensor";
        }
        return "unknown";
    }

    StateMachineResult StateMachine::update(const ProcessedSample &sample) noexcept
    {
        if (!sample.valid)
        {
            return evaluate(true, FaultReason::invalid_sample);
        }
        if (sample.pressure_pa >= config_.fault_pressure_pa)
        {
            return evaluate(true, FaultReason::pressure_threshold);
        }
        if (sample.airflow_ml_per_min >= config_.fault_airflow_ml_per_min)
        {
            return evaluate(true, FaultReason::airflow_threshold);
        }
        return evaluate(sample.pressure_pa >= config_.warning_pressure_pa ||
                            sample.airflow_ml_per_min >= config_.warning_airflow_ml_per_min,
                        FaultReason::none);
    }

    StateMachineResult StateMachine::on_timeout() noexcept
    {
        return evaluate(true, FaultReason::sensor_timeout);
    }

    StateMachineResult StateMachine::on_sensor_fault(FaultReason reason) noexcept
    {
        if (reason == FaultReason::invalid_configuration)
        {
            const auto previous_state = state_;
            const auto previous_fault = fault_;
            state_ = MonitorState::FAULT;
            fault_ = reason;
            interrupt_streaks();
            return {state_, state_ != previous_state, fault_, fault_ != previous_fault};
        }
        return evaluate(true, reason == FaultReason::none ? FaultReason::invalid_sample : reason);
    }

    StateMachineResult StateMachine::evaluate(bool abnormal, FaultReason reason) noexcept
    {
        const auto previous_state = state_;
        const auto previous_fault = fault_;
        if (!config_.valid())
        {
            state_ = MonitorState::FAULT;
            fault_ = FaultReason::invalid_configuration;
        }
        else if (abnormal)
        {
            normal_count_ = 0;
            increment(abnormal_count_, config_.warning_samples);
            if (reason != FaultReason::none)
            {
                increment(fault_count_, config_.fault_samples);
            }
            else
            {
                fault_count_ = 0;
            }
            if (fault_count_ >= config_.fault_samples)
            {
                state_ = MonitorState::FAULT;
                fault_ = reason;
            }
            else if (state_ != MonitorState::FAULT && abnormal_count_ >= config_.warning_samples)
            {
                state_ = MonitorState::WARNING;
            }
        }
        else
        {
            abnormal_count_ = 0;
            fault_count_ = 0;
            increment(normal_count_, config_.recovery_samples);
            if (normal_count_ >= config_.recovery_samples)
            {
                state_ = MonitorState::MONITORING;
                fault_ = FaultReason::none;
            }
        }
        return {state_, state_ != previous_state, fault_, fault_ != previous_fault};
    }

    void StateMachine::reset() noexcept
    {
        state_ = MonitorState::INITIALISING;
        fault_ = FaultReason::none;
        interrupt_streaks();
    }

    void StateMachine::interrupt_streaks() noexcept
    {
        abnormal_count_ = 0;
        fault_count_ = 0;
        normal_count_ = 0;
    }
} // namespace respmon
