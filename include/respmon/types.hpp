#pragma once
#include <cstdint>

namespace respmon {
// Integer units: pascals, signed millilitres per minute, monotonic milliseconds.
struct SensorSample {
    std::int32_t pressure_pa{0};
    std::int32_t airflow_ml_per_min{0};
    std::int64_t timestamp_ms{0};
    bool valid{false};
    // Acquisition-attempt counter, modulo 2^32; missing attempts leave gaps.
    std::uint32_t sequence{0};
};
// Filtered values retain the input units. Ignore values when valid is false.
struct ProcessedSample {
    std::int32_t pressure_pa{0};
    std::int32_t airflow_ml_per_min{0};
    std::int64_t timestamp_ms{0};
    bool valid{false};
    std::uint32_t sequence{0}; // Newest retained input's sequence.
};
enum class MonitorState : std::uint8_t { initializing, monitoring, sensor_fault };
enum class FaultReason : std::uint8_t { none, invalid_sample, sensor_timeout };
} // namespace respmon
