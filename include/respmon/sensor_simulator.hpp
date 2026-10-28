#pragma once
#include "respmon/types.hpp"

namespace respmon {
// Caller supplies monotonic time; no RTOS dependency.
class SensorSimulator {
public:
    SensorSample read(std::int64_t timestamp_ms) noexcept;
    void reset() noexcept { step_ = 0; }
private:
    std::uint32_t step_{0};
};
} // namespace respmon
