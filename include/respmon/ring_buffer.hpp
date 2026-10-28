#pragma once
#include <cstddef>

namespace respmon {
// Inline fixed storage; push replaces exactly the oldest sample when full.
// Retained samples are indexed oldest (0) to newest (size() - 1).
// Single owner; not thread-safe. T must support default construction and copy
// assignment without allocation or exceptions for deterministic runtime use.
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "Capacity must be positive");
public:
    void push(const T& value) noexcept
    {
        values_[next_] = value;
        next_ = (next_ + 1) % Capacity;
        if (size_ < Capacity) { ++size_; }
    }
    // Oldest-first indexing. Invalid index leaves output unchanged.
    bool get(std::size_t index, T& output) const noexcept
    {
        if (index >= size_) { return false; }
        const auto oldest = size_ == Capacity ? next_ : 0;
        output = values_[(oldest + index) % Capacity];
        return true;
    }
    std::size_t size() const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return Capacity; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == Capacity; }
    void clear() noexcept { next_ = 0; size_ = 0; }
private:
    T values_[Capacity]{};
    std::size_t next_{0};
    std::size_t size_{0};
};
} // namespace respmon
