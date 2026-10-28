#pragma once
#include <cstdint>

namespace respmon {
// Integer units: pascals, signed millilitres per minute, monotonic milliseconds.
struct SensorSample {
    std::int32_t pressure_pa{0};
    std::int32_t airflow_ml_per_min{0};
    std::int64_t timestamp_ms{0};
    bool valid{false};
};
// Filtered values retain the input units. Ignore values when valid is false.
struct ProcessedSample {
    std::int32_t pressure_pa{0};
    std::int32_t airflow_ml_per_min{0};
    std::int64_t timestamp_ms{0};
    bool valid{false};
};
enum class MonitorState : std::uint8_t { initializing, monitoring, sensor_fault };
enum class FaultReason : std::uint8_t { none, invalid_sample };
} // namespace respmon
