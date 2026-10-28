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
