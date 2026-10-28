#pragma once
#include <cstddef>
#include <cstdint>

namespace respmon::config {
inline constexpr std::int32_t sample_period_ms = 500;
inline constexpr std::size_t filter_window_size = 4;
// Educational synthetic values only; these are not clinical thresholds.
inline constexpr std::uint32_t waveform_steps = 10;
inline constexpr std::int32_t base_pressure_pa = 100;
inline constexpr std::int32_t base_airflow_ml_per_min = 40;
inline constexpr std::uint32_t samples_per_scenario = 8;
} // namespace respmon::config

namespace respmon {
// Synthetic educational thresholds in integer Pa and mL/min. Not clinically
// validated. Only upper excursions are classified in this demonstration.
struct MonitorConfig {
    std::int32_t warning_pressure_pa{180};
    std::int32_t fault_pressure_pa{350};
    std::int32_t warning_airflow_ml_per_min{70};
    std::int32_t fault_airflow_ml_per_min{140};
    std::uint32_t warning_samples{3};
    std::uint32_t fault_samples{3};
    std::uint32_t recovery_samples{4};

    constexpr bool valid() const noexcept
    {
        return warning_pressure_pa < fault_pressure_pa &&
               warning_airflow_ml_per_min < fault_airflow_ml_per_min &&
               warning_samples >= 2 && fault_samples >= 2 && recovery_samples >= 2;
    }
};
static_assert(MonitorConfig{}.valid(), "Default monitor configuration must be valid");

// Demonstration input envelope and integrity limits, not clinical thresholds.
struct SensorHealthConfig {
    std::int32_t min_pressure_pa{0};
    std::int32_t max_pressure_pa{500};
    std::int32_t min_airflow_ml_per_min{0};
    std::int32_t max_airflow_ml_per_min{200};
    std::int64_t max_age_ms{1000};
    std::uint32_t max_identical_samples{3};
    constexpr bool valid() const noexcept
    {
        return min_pressure_pa <= max_pressure_pa &&
               min_airflow_ml_per_min <= max_airflow_ml_per_min &&
               max_age_ms >= 0 && max_identical_samples > 0 &&
               max_identical_samples < UINT32_MAX;
    }
};
static_assert(SensorHealthConfig{}.valid(), "Default sensor health config must be valid");
} // namespace respmon
