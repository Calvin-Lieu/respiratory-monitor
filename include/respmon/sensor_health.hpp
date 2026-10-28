#pragma once
#include "respmon/config.hpp"
#include "respmon/types.hpp"

namespace respmon {
struct SensorHealthResult {
    FaultReason reason{FaultReason::none};
    std::uint32_t missing_sequences{0};
    bool accepted() const noexcept { return reason == FaultReason::none; }
};

// Owned by monitoring. `now_ms` and sample timestamps share a monotonic clock.
// Every delivered sample is inspected before it reaches the filter. Fault
// priority: configuration, timestamp, sequence, validity flag, range, frozen.
class SensorHealth {
public:
    explicit SensorHealth(SensorHealthConfig config = {}) noexcept : config_(config) {}
    SensorHealthResult inspect(const SensorSample& sample, std::int64_t now_ms) noexcept;
    // An absence breaks the identical-reading run but keeps sequence/timestamp
    // baselines, so the next delivery can detect the missing sequence numbers.
    void on_timeout() noexcept { identical_count_ = 0; }
private:
    SensorHealthConfig config_;
    bool have_sequence_{false};
    bool have_timestamp_{false};
    std::uint32_t last_sequence_{0};
    std::int64_t last_timestamp_ms_{0};
    std::int32_t last_pressure_{0};
    std::int32_t last_airflow_{0};
    std::uint32_t identical_count_{0};
};
} // namespace respmon
