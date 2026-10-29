#include "respmon/sensor_health.hpp"

namespace respmon
{
    SensorHealthResult SensorHealth::inspect(const SensorSample &sample, std::int64_t now_ms) noexcept
    {
        if (!config_.valid())
        {
            return {FaultReason::invalid_configuration, 0};
        }

        // Sequence numbers use modulo-2^32 serial arithmetic.
        FaultReason sequence_fault = FaultReason::none;
        std::uint32_t missing = 0;
        if (!have_sequence_)
        {
            missing = sample.sequence;
            if (missing != 0)
            {
                sequence_fault = FaultReason::sequence_gap;
            }
            have_sequence_ = true;
            last_sequence_ = sample.sequence;
        }
        else
        {
            const auto distance = static_cast<std::uint32_t>(sample.sequence - last_sequence_);
            if (distance == 0 || distance >= 0x80000000U)
            {
                sequence_fault = FaultReason::sequence_order;
            }
            else
            {
                missing = distance - 1U;
                if (missing != 0)
                {
                    sequence_fault = FaultReason::sequence_gap;
                }
                last_sequence_ = sample.sequence;
            }
        }

        // Test sign and ordering before subtraction to avoid signed overflow.
        const bool timestamp_ok = now_ms >= 0 && sample.timestamp_ms >= 0 &&
                                  sample.timestamp_ms <= now_ms && now_ms - sample.timestamp_ms <= config_.max_age_ms &&
                                  (!have_timestamp_ || sample.timestamp_ms > last_timestamp_ms_);
        if (timestamp_ok)
        {
            // Track fresh timestamps even for rejected payloads to reject duplicates.
            have_timestamp_ = true;
            last_timestamp_ms_ = sample.timestamp_ms;
        }

        FaultReason reason = FaultReason::none;
        if (!timestamp_ok)
        {
            reason = FaultReason::stale_timestamp;
        }
        else if (sequence_fault != FaultReason::none)
        {
            reason = sequence_fault;
        }
        else if (!sample.valid)
        {
            reason = FaultReason::invalid_sample;
        }
        else if (sample.pressure_pa < config_.min_pressure_pa ||
                 sample.pressure_pa > config_.max_pressure_pa ||
                 sample.airflow_ml_per_min < config_.min_airflow_ml_per_min ||
                 sample.airflow_ml_per_min > config_.max_airflow_ml_per_min)
        {
            reason = FaultReason::out_of_range;
        }

        if (reason != FaultReason::none)
        {
            identical_count_ = 0;
        }
        else
        {
            if (identical_count_ != 0 && sample.pressure_pa == last_pressure_ &&
                sample.airflow_ml_per_min == last_airflow_)
            {
                if (identical_count_ <= config_.max_identical_samples)
                {
                    ++identical_count_;
                }
            }
            else
            {
                identical_count_ = 1;
                last_pressure_ = sample.pressure_pa;
                last_airflow_ = sample.airflow_ml_per_min;
            }
            if (identical_count_ > config_.max_identical_samples)
            {
                reason = FaultReason::frozen_sensor;
            }
        }
        return {reason, missing};
    }
} // namespace respmon
