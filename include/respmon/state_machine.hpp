#pragma once
#include "respmon/types.hpp"

namespace respmon {
// Foundation validity transitions only; no alarm or persistence policy yet.
class StateMachine {
public:
    void update(const ProcessedSample& sample) noexcept;
    // Called by acquisition orchestration when an expected sample is absent.
    void on_timeout() noexcept;
    void reset() noexcept;
    MonitorState state() const noexcept { return state_; }
    FaultReason fault() const noexcept { return fault_; }
private:
    MonitorState state_{MonitorState::initializing};
    FaultReason fault_{FaultReason::none};
};
} // namespace respmon
