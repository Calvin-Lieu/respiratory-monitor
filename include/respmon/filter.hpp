#pragma once
#include "respmon/config.hpp"
#include "respmon/ring_buffer.hpp"
#include "respmon/types.hpp"

namespace respmon
{
    // Average available samples using integer division; invalid input clears the window.
    class Filter
    {
    public:
        ProcessedSample process(const SensorSample &sample) noexcept;
        // An empty window returns an invalid sample; otherwise use the newest timestamp.
        ProcessedSample average() const noexcept;
        void reset() noexcept;

    private:
        RingBuffer<SensorSample, config::filter_window_size> history_{};
    };
} // namespace respmon
