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
// GCC/Clang intrinsic works with Zephyr's minimal C++ library, which does not
// provide <type_traits>. k_msgq copies object bytes, never pointers to storage.
static_assert(__is_trivially_copyable(SensorSample),
              "SensorSample must be safe for byte-copying through k_msgq");
// Filtered values retain the input units. Ignore values when valid is false.
struct ProcessedSample {
    std::int32_t pressure_pa{0};
    std::int32_t airflow_ml_per_min{0};
    std::int64_t timestamp_ms{0};
    bool valid{false};
    std::uint32_t sequence{0}; // Newest retained input's sequence.
};
enum class MonitorState : std::uint8_t { INITIALISING, MONITORING, WARNING, FAULT };
enum class FaultReason : std::uint8_t {
    none, invalid_sample, sensor_timeout, pressure_threshold, airflow_threshold,
    invalid_configuration, stale_timestamp, sequence_gap, sequence_order,
    out_of_range, frozen_sensor
};
} // namespace respmon
