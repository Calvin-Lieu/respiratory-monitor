#pragma once
#include "respmon/types.hpp"

namespace respmon {
enum class SensorScenario : std::uint8_t {
    normal, elevated, critical, invalid, out_of_range, stale_timestamp,
    sequence_gap, frozen, timeout, recovery
};

const char* scenario_name(SensorScenario scenario) noexcept;

struct SimulatedReading {
    SensorSample sample{}; // Meaningful only when available is true.
    SensorScenario scenario{SensorScenario::normal};
    bool available{false};
};

// Each read advances one acquisition attempt, including missing samples.
// Fixed-length scenarios repeat forever. Caller supplies monotonic time;
// no RTOS, wall-clock, random generator, allocation or monitoring policy here.
class SensorSimulator {
public:
    SimulatedReading read(std::int64_t timestamp_ms) noexcept;
    void reset() noexcept { step_ = 0; sequence_ = 0; }
private:
    std::uint32_t step_{0};
    std::uint32_t sequence_{0};
};
} // namespace respmon
