#pragma once

namespace erhe::scene_renderer::test {

// Initializes the loggers of the libraries the scene_renderer GPU tests
// exercise, once per process (a second initialize_logging() of a library
// throws: its logger name is already registered). Call from a fixture's
// SetUp(), after the GPU environment has created the log sinks.
void initialize_scene_renderer_test_logging();

// Deviceless binary: creates the log sinks (no GPU test environment does) and
// then initializes the loggers as above, once per process.
void initialize_deviceless_test_logging();

} // namespace erhe::scene_renderer::test
