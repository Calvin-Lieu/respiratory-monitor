#include "respmon/state_machine.hpp"

namespace respmon {
void StateMachine::update(const ProcessedSample& sample) noexcept
{
    state_ = sample.valid ? MonitorState::monitoring : MonitorState::sensor_fault;
    fault_ = sample.valid ? FaultReason::none : FaultReason::invalid_sample;
}
void StateMachine::on_timeout() noexcept
{
    state_ = MonitorState::sensor_fault;
    fault_ = FaultReason::sensor_timeout;
}
void StateMachine::reset() noexcept
{
    state_ = MonitorState::initializing;
    fault_ = FaultReason::none;
}
} // namespace respmon
