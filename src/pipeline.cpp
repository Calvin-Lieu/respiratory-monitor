#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "respmon/config.hpp"
#include "respmon/filter.hpp"
#include "respmon/pipeline.hpp"
#include "respmon/pipeline_config.hpp"
#include "respmon/sensor_simulator.hpp"
#include "respmon/sensor_health.hpp"
#include "respmon/state_machine.hpp"

LOG_MODULE_REGISTER(respmon, LOG_LEVEL_INF);

namespace respmon {
namespace {
namespace settings = pipeline_config;
static_assert(settings::first_sample_timeout_ms > config::sample_period_ms,
              "Initial timeout must allow one sampling period plus scheduling slack");

struct AcquisitionStats {
    std::uint64_t attempts{0};
    std::uint64_t enqueued{0};
    std::uint64_t dropped{0};
    std::uint64_t missing{0};
    std::uint64_t report_drops{0};
    bool queue_full{false};
    SensorScenario scenario{SensorScenario::normal};
};
struct MonitoringStats {
    std::uint64_t processed{0};
    std::uint64_t valid{0};
    std::uint64_t invalid{0};
    std::uint64_t timeouts{0};
    std::uint64_t sequence_gaps{0};
    std::uint64_t report_drops{0};
};
enum class ReportSource : std::uint8_t { acquisition, monitoring };
struct Report {
    ReportSource source{ReportSource::acquisition};
    bool startup{false};
    bool has_sample{false};
    std::int64_t event_ms{0};
    AcquisitionStats acquisition{};
    MonitoringStats monitoring{};
    SensorSample raw{};
    ProcessedSample filtered{};
    MonitorState previous_state{MonitorState::INITIALISING};
    StateMachineResult result{};
    FaultReason health_reason{FaultReason::none};
    bool health_changed{false};
};
static_assert(__is_trivially_copyable(Report), "Reports must be byte-copyable");

K_MSGQ_DEFINE(sample_queue, sizeof(SensorSample), settings::sample_queue_capacity,
              alignof(SensorSample));
K_MSGQ_DEFINE(report_queue, sizeof(Report), settings::report_queue_capacity, alignof(Report));

extern const k_tid_t acquisition_thread;
extern const k_tid_t monitoring_thread;
extern const k_tid_t reporting_thread;

void publish(const Report& report, std::uint64_t& dropped) noexcept
{
    // Best-effort diagnostics. The next successful snapshot includes any losses.
    if (k_msgq_put(&report_queue, &report, K_NO_WAIT) != 0) { ++dropped; }
}

void acquisition_entry(void*, void*, void*)
{
    SensorSimulator sensor;
    AcquisitionStats stats;
    Report report;
    report.source = ReportSource::acquisition;
    report.startup = true;
    publish(report, stats.report_drops);
    report.startup = false;
    std::int64_t next_ms = k_uptime_get();
    while (true) {
        const auto now = k_uptime_get();
        const auto reading = sensor.read(now);
        ++stats.attempts;
        stats.scenario = reading.scenario;
        stats.queue_full = false;
        if (!reading.available) {
            ++stats.missing;
        } else if (k_msgq_put(&sample_queue, &reading.sample, K_NO_WAIT) == 0) {
            ++stats.enqueued;
        } else {
            // Drop newest: retain already queued samples, never wait on monitoring.
            ++stats.dropped;
            stats.queue_full = true;
        }
        report.event_ms = now;
        report.acquisition = stats;
        publish(report, stats.report_drops);
        next_ms += config::sample_period_ms;
        const auto after = k_uptime_get();
        if (next_ms <= after) {
            next_ms += ((after - next_ms) / config::sample_period_ms + 1) *
                       config::sample_period_ms;
        }
        k_sleep(K_TIMEOUT_ABS_MS(next_ms));
    }
}

void monitoring_entry(void*, void*, void*)
{
    Filter filter;
    SensorHealth health;
    StateMachine monitor;
    MonitoringStats stats;
    Report report;
    report.source = ReportSource::monitoring;
    report.startup = true;
    publish(report, stats.report_drops);
    report.startup = false;
    FaultReason previous_health = FaultReason::none;
    auto wait_ms = settings::first_sample_timeout_ms;
#ifdef RESPMON_PIPELINE_STRESS_TEST
    // Test-only startup stall: four slots fill, then acquisition must drop
    // newest without blocking. Normal builds never intentionally stall.
    k_sleep(K_MSEC(3000));
#endif
    while (true) {
        SensorSample raw;
        const int received = k_msgq_get(&sample_queue, &raw, K_MSEC(wait_ms));
        report.previous_state = monitor.state();
        report.event_ms = k_uptime_get();
        if (received == 0) {
            ++stats.processed;
            const auto checked = health.inspect(raw, report.event_ms);
            stats.sequence_gaps += checked.missing_sequences;
            report.raw = raw;
            report.health_reason = checked.reason;
            if (checked.accepted()) {
                ++stats.valid;
                report.filtered = filter.process(raw);
                report.result = monitor.update(report.filtered);
            } else {
                ++stats.invalid;
                filter.reset();
                report.filtered = {0, 0, raw.timestamp_ms, false, raw.sequence};
                report.result = monitor.on_sensor_fault(checked.reason);
            }
            report.has_sample = true;
            wait_ms = settings::first_sample_timeout_ms;
        } else {
            ++stats.timeouts;
            filter.reset();
            health.on_timeout();
            report.result = monitor.on_timeout();
            report.health_reason = FaultReason::sensor_timeout;
            report.has_sample = false;
            // After the first overdue sample, emit one timeout per expected period.
            wait_ms = config::sample_period_ms;
        }
        report.health_changed = report.health_reason != previous_health;
        previous_health = report.health_reason;
        report.monitoring = stats;
        publish(report, stats.report_drops);
    }
}

void print_summary(const AcquisitionStats& acquisition, const Report& latest,
                   bool have_sample, const SensorSample& raw,
                   const ProcessedSample& filtered)
{
    // The sample values are retained during a timeout; the validity field below
    // says whether they describe the latest monitoring event.
    LOG_INF("SUMMARY time=%lld ms acq_scenario=%s state=%s reason=%s "
            "seq=%lu sample_time=%lld ms raw_pressure=%ld Pa filtered_pressure=%ld Pa "
            "raw_airflow=%ld mL/min filtered_airflow=%ld mL/min "
            "valid=%u filtered_valid=%u latest_event_sample=%u "
            "total=%llu valid_count=%llu invalid=%llu dropped=%llu",
            static_cast<long long>(k_uptime_get()), scenario_name(acquisition.scenario),
            state_name(latest.result.state), fault_name(latest.result.fault_reason),
            static_cast<unsigned long>(raw.sequence), static_cast<long long>(raw.timestamp_ms),
            static_cast<long>(raw.pressure_pa), static_cast<long>(filtered.pressure_pa),
            static_cast<long>(raw.airflow_ml_per_min), static_cast<long>(filtered.airflow_ml_per_min),
            static_cast<unsigned int>(have_sample && raw.valid),
            static_cast<unsigned int>(have_sample && latest.has_sample && filtered.valid),
            static_cast<unsigned int>(latest.has_sample),
            static_cast<unsigned long long>(latest.monitoring.processed),
            static_cast<unsigned long long>(latest.monitoring.valid),
            static_cast<unsigned long long>(latest.monitoring.invalid),
            static_cast<unsigned long long>(acquisition.dropped));
}

void print_counters(const AcquisitionStats& acquisition, const Report& latest)
{
    LOG_INF("COUNTERS time=%lld ms attempts=%llu enqueued=%llu dropped=%llu missing=%llu "
            "total=%llu valid=%llu invalid=%llu timeouts=%llu gaps=%llu report_drops=%llu/%llu",
            static_cast<long long>(k_uptime_get()),
            static_cast<unsigned long long>(acquisition.attempts),
            static_cast<unsigned long long>(acquisition.enqueued),
            static_cast<unsigned long long>(acquisition.dropped),
            static_cast<unsigned long long>(acquisition.missing),
            static_cast<unsigned long long>(latest.monitoring.processed),
            static_cast<unsigned long long>(latest.monitoring.valid),
            static_cast<unsigned long long>(latest.monitoring.invalid),
            static_cast<unsigned long long>(latest.monitoring.timeouts),
            static_cast<unsigned long long>(latest.monitoring.sequence_gaps),
            static_cast<unsigned long long>(acquisition.report_drops),
            static_cast<unsigned long long>(latest.monitoring.report_drops));
}

void report_stack_usage()
{
    std::size_t acquisition_unused = 0;
    std::size_t monitoring_unused = 0;
    std::size_t reporting_unused = 0;
    const int acquisition_rc = k_thread_stack_space_get(acquisition_thread, &acquisition_unused);
    const int monitoring_rc = k_thread_stack_space_get(monitoring_thread, &monitoring_unused);
    const int reporting_rc = k_thread_stack_space_get(reporting_thread, &reporting_unused);
    if (acquisition_rc != 0 || monitoring_rc != 0 || reporting_rc != 0 ||
        acquisition_unused > settings::acquisition_stack_bytes ||
        monitoring_unused > settings::monitoring_stack_bytes ||
        reporting_unused > settings::reporting_stack_bytes) {
        LOG_WRN("STACK inspection unavailable rc=%d/%d/%d unused=%zu/%zu/%zu",
                acquisition_rc, monitoring_rc, reporting_rc,
                acquisition_unused, monitoring_unused, reporting_unused);
        return;
    }
    LOG_INF("STACK high_water_bytes acquisition=%zu/%d monitoring=%zu/%d reporting=%zu/%d",
            settings::acquisition_stack_bytes - acquisition_unused, settings::acquisition_stack_bytes,
            settings::monitoring_stack_bytes - monitoring_unused, settings::monitoring_stack_bytes,
            settings::reporting_stack_bytes - reporting_unused, settings::reporting_stack_bytes);
}

void reporting_entry(void*, void*, void*)
{
    LOG_INF("THREAD reporting started priority=%d stack=%d bytes",
            settings::reporting_priority, settings::reporting_stack_bytes);
    AcquisitionStats acquisition;
    Report latest;
    SensorSample raw;
    ProcessedSample filtered;
    bool have_sample = false;
    std::int64_t next_summary = k_uptime_get() + settings::summary_period_ms;
    std::int64_t next_stack_report = k_uptime_get() + settings::stack_report_period_ms;
    while (true) {
        Report report;
        const auto now = k_uptime_get();
        const auto remaining = next_summary > now ? next_summary - now : 0;
        if (k_msgq_get(&report_queue, &report, K_MSEC(remaining)) == 0) {
            if (report.startup) {
                const bool is_acquisition = report.source == ReportSource::acquisition;
                LOG_INF("THREAD %s started priority=%d stack=%d bytes",
                        is_acquisition ? "acquisition" : "monitoring",
                        is_acquisition ? settings::acquisition_priority : settings::monitoring_priority,
                        is_acquisition ? settings::acquisition_stack_bytes : settings::monitoring_stack_bytes);
            } else if (report.source == ReportSource::acquisition) {
                if (report.acquisition.queue_full && !acquisition.queue_full) {
                    LOG_WRN("DROP acquisition time=%lld ms total=%llu policy=drop_newest",
                            static_cast<long long>(report.event_ms),
                            static_cast<unsigned long long>(report.acquisition.dropped));
                }
                acquisition = report.acquisition;
            } else {
                if (report.health_changed) {
                    if (report.health_reason == FaultReason::none) {
                        LOG_INF("HEALTH time=%lld ms reason=%s gaps=%llu",
                                static_cast<long long>(report.event_ms), fault_name(report.health_reason),
                                static_cast<unsigned long long>(report.monitoring.sequence_gaps));
                    } else {
                        LOG_WRN("HEALTH time=%lld ms reason=%s gaps=%llu",
                                static_cast<long long>(report.event_ms), fault_name(report.health_reason),
                                static_cast<unsigned long long>(report.monitoring.sequence_gaps));
                    }
                }
                if (report.result.transitioned || report.result.reason_changed) {
                    if (report.result.state == MonitorState::FAULT) {
                        LOG_ERR("STATE time=%lld ms from=%s to=%s reason=%s changed=%u",
                                static_cast<long long>(report.event_ms), state_name(report.previous_state),
                                state_name(report.result.state), fault_name(report.result.fault_reason),
                                static_cast<unsigned int>(report.result.transitioned));
                    } else if (report.result.state == MonitorState::WARNING) {
                        LOG_WRN("STATE time=%lld ms from=%s to=%s reason=%s changed=%u",
                                static_cast<long long>(report.event_ms), state_name(report.previous_state),
                                state_name(report.result.state), fault_name(report.result.fault_reason),
                                static_cast<unsigned int>(report.result.transitioned));
                    } else {
                        LOG_INF("STATE time=%lld ms from=%s to=%s reason=%s changed=%u",
                                static_cast<long long>(report.event_ms), state_name(report.previous_state),
                                state_name(report.result.state), fault_name(report.result.fault_reason),
                                static_cast<unsigned int>(report.result.transitioned));
                    }
                }
                latest = report;
                if (report.has_sample) {
                    raw = report.raw;
                    filtered = report.filtered;
                    have_sample = true;
                }
            }
        }
        const auto current_ms = k_uptime_get();
        if (current_ms >= next_summary) {
            print_summary(acquisition, latest, have_sample, raw, filtered);
            next_summary = k_uptime_get() + settings::summary_period_ms;
        }
        if (current_ms >= next_stack_report) {
            print_counters(acquisition, latest);
            report_stack_usage();
            next_stack_report = k_uptime_get() + settings::stack_report_period_ms;
        }
    }
}

K_THREAD_DEFINE(acquisition_thread, settings::acquisition_stack_bytes, acquisition_entry,
                nullptr, nullptr, nullptr, settings::acquisition_priority, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(monitoring_thread, settings::monitoring_stack_bytes, monitoring_entry,
                nullptr, nullptr, nullptr, settings::monitoring_priority, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(reporting_thread, settings::reporting_stack_bytes, reporting_entry,
                nullptr, nullptr, nullptr, settings::reporting_priority, 0, SYS_FOREVER_MS);
} // namespace

void start_pipeline() noexcept
{
#ifdef RESPMON_PIPELINE_STRESS_TEST
    LOG_INF("TEST queue_overflow monitoring_start_delay=3000 ms");
#endif
    LOG_INF("PIPELINE sample_queue=%d report_queue=%d period=%ld ms timeout=%ld ms",
           settings::sample_queue_capacity, settings::report_queue_capacity,
           static_cast<long>(config::sample_period_ms),
           static_cast<long>(settings::first_sample_timeout_ms));
    k_thread_start(reporting_thread);
    k_thread_start(monitoring_thread);
    k_thread_start(acquisition_thread);
}
} // namespace respmon
