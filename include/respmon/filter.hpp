#pragma once
#include "respmon/config.hpp"
#include "respmon/ring_buffer.hpp"
#include "respmon/types.hpp"

namespace respmon {
// Average available valid samples, including partial startup windows.
// Division truncates toward zero; invalid input clears history.
class Filter {
public:
    ProcessedSample process(const SensorSample& sample) noexcept;
    // Current window average, with newest retained timestamp. An empty window
    // returns zero values, timestamp zero and valid=false, without division.
    ProcessedSample average() const noexcept;
    void reset() noexcept;
private:
    RingBuffer<SensorSample, config::filter_window_size> history_{};
};
} // namespace respmon
