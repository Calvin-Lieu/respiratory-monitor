#include "respmon/filter.hpp"

namespace respmon {
ProcessedSample Filter::process(const SensorSample& sample) noexcept
{
    if (!sample.valid) {
        reset();
        return {0, 0, sample.timestamp_ms, false};
    }
    history_.push(sample);
    std::int64_t pressure_sum = 0;
    std::int64_t airflow_sum = 0;
    // Bound sums of arbitrary int32_t inputs within int64_t.
    static_assert(config::filter_window_size <= 2147483647U);
    for (std::size_t i = 0; i < history_.size(); ++i) {
        SensorSample previous{};
        history_.get(i, previous);
        pressure_sum += previous.pressure_pa;
        airflow_sum += previous.airflow_ml_per_min;
    }
    const auto count = static_cast<std::int64_t>(history_.size());
    return {static_cast<std::int32_t>(pressure_sum / count),
            static_cast<std::int32_t>(airflow_sum / count),
            sample.timestamp_ms, true};
}
void Filter::reset() noexcept { history_.clear(); }
} // namespace respmon
