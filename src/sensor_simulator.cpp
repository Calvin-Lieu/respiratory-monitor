#include "respmon/sensor_simulator.hpp"
#include "respmon/config.hpp"

namespace respmon {
namespace {
constexpr SensorScenario schedule[] = {
    SensorScenario::normal, SensorScenario::elevated, SensorScenario::critical,
    SensorScenario::invalid, SensorScenario::out_of_range, SensorScenario::frozen,
    SensorScenario::timeout, SensorScenario::recovery
};
constexpr std::uint32_t scenario_count = sizeof(schedule) / sizeof(schedule[0]);
static_assert(config::samples_per_scenario > 0, "Scenario duration must be positive");
static_assert(config::samples_per_scenario <= UINT32_MAX / scenario_count,
              "Scenario schedule must fit the step counter");
constexpr auto cycle_length = scenario_count * config::samples_per_scenario;
} // namespace

const char* scenario_name(SensorScenario scenario) noexcept
{
    switch (scenario) {
    case SensorScenario::normal: return "normal";
    case SensorScenario::elevated: return "elevated";
    case SensorScenario::critical: return "critical";
    case SensorScenario::invalid: return "invalid";
    case SensorScenario::out_of_range: return "out_of_range";
    case SensorScenario::frozen: return "frozen";
    case SensorScenario::timeout: return "timeout";
    case SensorScenario::recovery: return "recovery";
    }
    return "unknown";
}

SimulatedReading SensorSimulator::read(std::int64_t timestamp_ms) noexcept
{
    const auto scenario = schedule[step_ / config::samples_per_scenario];
    const auto phase = step_ % config::samples_per_scenario;
    const auto sequence = sequence_++;
    step_ = (step_ + 1U) % cycle_length;
    if (scenario == SensorScenario::timeout) {
        return {{}, scenario, false}; // No measurement is fabricated for this tick.
    }

    SensorSample sample{
        config::base_pressure_pa + static_cast<std::int32_t>(phase % config::waveform_steps),
        config::base_airflow_ml_per_min + static_cast<std::int32_t>(phase % 5U),
        timestamp_ms, true, sequence};
    switch (scenario) {
    case SensorScenario::elevated:
        sample.pressure_pa += 100;
        sample.airflow_ml_per_min += 40;
        break;
    case SensorScenario::critical:
        sample.pressure_pa += 300;
        sample.airflow_ml_per_min += 120;
        break;
    case SensorScenario::invalid:
        sample.valid = false; // Transport/sensor validity failure, nominal values.
        break;
    case SensorScenario::out_of_range:
        sample.pressure_pa = -1000;
        sample.airflow_ml_per_min = 1000000;
        sample.valid = false; // Outside the simulator's documented output envelope.
        break;
    case SensorScenario::frozen:
        sample.pressure_pa = config::base_pressure_pa;
        sample.airflow_ml_per_min = config::base_airflow_ml_per_min;
        break;
    default:
        break;
    }
    return {sample, scenario, true};
}
} // namespace respmon
