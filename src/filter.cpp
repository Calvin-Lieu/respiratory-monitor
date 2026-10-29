#include "respmon/filter.hpp"

namespace respmon
{
    ProcessedSample Filter::process(const SensorSample &sample) noexcept
    {
        if (!sample.valid)
        {
            reset();
            return {0, 0, sample.timestamp_ms, false, sample.sequence};
        }
        history_.push(sample);
        return average();
    }

    ProcessedSample Filter::average() const noexcept
    {
        if (history_.empty())
        {
            return {};
        }
        std::int64_t pressure_sum = 0;
        std::int64_t airflow_sum = 0;
        // The bound keeps sums of int32_t values within int64_t.
        static_assert(config::filter_window_size > 0, "Filter window must be nonempty");
        static_assert(config::filter_window_size <= 2147483647U,
                      "Filter window exceeds safe int64_t accumulator bound");
        std::int64_t newest_timestamp_ms = 0;
        std::uint32_t newest_sequence = 0;
        for (std::size_t i = 0; i < history_.size(); ++i)
        {
            SensorSample previous{};
            history_.get(i, previous);
            pressure_sum += previous.pressure_pa;
            airflow_sum += previous.airflow_ml_per_min;
            newest_timestamp_ms = previous.timestamp_ms;
            newest_sequence = previous.sequence;
        }
        const auto count = static_cast<std::int64_t>(history_.size());
        return {static_cast<std::int32_t>(pressure_sum / count),
                static_cast<std::int32_t>(airflow_sum / count), newest_timestamp_ms, true,
                newest_sequence};
    }
    void Filter::reset() noexcept
    {
        history_.clear();
    }
} // namespace respmon
