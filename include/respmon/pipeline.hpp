#pragma once

namespace respmon {
// Start statically defined Zephyr threads once, after main's startup output.
void start_pipeline() noexcept;
} // namespace respmon
