#include "respmon/sensor_simulator.hpp"
#include "respmon/config.hpp"

namespace respmon {
SensorSample SensorSimulator::read(std::int64_t timestamp_ms) noexcept
{
    const SensorSample sample{
        config::base_pressure_pa + static_cast<std::int32_t>(step_),
        config::base_airflow_ml_per_min + static_cast<std::int32_t>(step_ % 5U),
        timestamp_ms, true};
    step_ = (step_ + 1U) % config::waveform_steps;
    return sample;
}
} // namespace respmon
