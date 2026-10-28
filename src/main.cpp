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
    std::int64_t next_sample_ms = k_uptime_get();
    while (true) {
        const auto raw = sensor.read(k_uptime_get());
        const auto sample = filter.process(raw);
        monitor.update(sample);
        printk("SIM time=%lld ms raw_pressure=%ld Pa filtered_pressure=%ld Pa "
               "raw_airflow=%ld mL/min filtered_airflow=%ld mL/min state=%u fault=%u\n",
               static_cast<long long>(sample.timestamp_ms),
               static_cast<long>(raw.pressure_pa),
               static_cast<long>(sample.pressure_pa),
               static_cast<long>(raw.airflow_ml_per_min),
               static_cast<long>(sample.airflow_ml_per_min),
               static_cast<unsigned int>(monitor.state()),
               static_cast<unsigned int>(monitor.fault()));
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
