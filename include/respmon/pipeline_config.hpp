#pragma once
#include <cstdint>

namespace respmon::pipeline_config
{
    // Preemptible Zephyr priorities: smaller numbers run first.
    inline constexpr int acquisition_priority = 2;
    inline constexpr int monitoring_priority = 3;
    inline constexpr int reporting_priority = 5;
    // Statically reserved stack sizes, in bytes.
    inline constexpr int acquisition_stack_bytes = 1024;
    inline constexpr int monitoring_stack_bytes = 2048;
    inline constexpr int reporting_stack_bytes = 2048;
    inline constexpr int sample_queue_capacity = 4;
    inline constexpr int report_queue_capacity = 16;
    inline constexpr std::int32_t first_sample_timeout_ms = 750;
    inline constexpr std::int32_t summary_period_ms = 2000;
    inline constexpr std::int32_t stack_report_period_ms = 10000;

    static_assert(acquisition_priority < monitoring_priority &&
                  monitoring_priority < reporting_priority);
    static_assert(sample_queue_capacity > 0 && report_queue_capacity > 0);
} // namespace respmon::pipeline_config
