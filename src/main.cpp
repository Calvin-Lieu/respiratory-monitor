#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "respmon/config.hpp"
#include "respmon/filter.hpp"
#include "respmon/sensor_simulator.hpp"
#include "respmon/state_machine.hpp"

int main()
{
    respmon::SensorSimulator sensor;
    respmon::Filter filter;
    respmon::StateMachine monitor;
    printk("Respiratory monitor starting (simulated educational data)\n");
    printk("STATE initial=%s\n", respmon::state_name(monitor.state()));
    std::int64_t next_sample_ms = k_uptime_get();
    while (true) {
        const auto acquisition_ms = k_uptime_get();
        const auto reading = sensor.read(acquisition_ms);
        const auto previous_state = monitor.state();
        respmon::StateMachineResult result;
        if (reading.available) {
            const auto& raw = reading.sample;
            const auto sample = filter.process(raw);
            result = monitor.update(sample);
            printk("SIM time=%lld ms scenario=%s seq=%lu valid=%u "
                   "raw_pressure=%ld Pa filtered_pressure=%ld Pa "
                   "raw_airflow=%ld mL/min filtered_airflow=%ld mL/min "
                   "filtered_valid=%u\n",
                   static_cast<long long>(raw.timestamp_ms),
                   respmon::scenario_name(reading.scenario),
                   static_cast<unsigned long>(raw.sequence),
                   static_cast<unsigned int>(raw.valid),
                   static_cast<long>(raw.pressure_pa),
                   static_cast<long>(sample.pressure_pa),
                   static_cast<long>(raw.airflow_ml_per_min),
                   static_cast<long>(sample.airflow_ml_per_min),
                   static_cast<unsigned int>(sample.valid));
        } else {
            // A missing acquisition is not an invalid measurement. Clear stale
            // history and report the absence without inserting a placeholder.
            filter.reset();
            result = monitor.on_timeout();
            printk("SIM time=%lld ms scenario=%s available=0\n",
                   static_cast<long long>(acquisition_ms),
                   respmon::scenario_name(reading.scenario));
        }
        if (result.transitioned || result.reason_changed) {
            printk("STATE time=%lld ms scenario=%s from=%s to=%s reason=%s changed=%u\n",
                   static_cast<long long>(acquisition_ms),
                   respmon::scenario_name(reading.scenario),
                   respmon::state_name(previous_state),
                   respmon::state_name(result.state),
                   respmon::fault_name(result.fault_reason),
                   static_cast<unsigned int>(result.transitioned));
        }
        // Absolute deadlines avoid drift; skip missed periods without bursts.
        next_sample_ms += respmon::config::sample_period_ms;
        const auto now_ms = k_uptime_get();
        if (next_sample_ms <= now_ms) {
            next_sample_ms += ((now_ms - next_sample_ms) /
                               respmon::config::sample_period_ms + 1) *
                              respmon::config::sample_period_ms;
        }
        k_sleep(K_TIMEOUT_ABS_MS(next_sample_ms));
    }
    return 0;
}
