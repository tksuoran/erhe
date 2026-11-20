#pragma once

namespace editor {

// Main thread, before any background task runs: starts the thread pools outside the executors that outlive the
// task that first uses them, so their threads run at normal priority (doc/editor/editor.md "Outside thread pools").
void start_outside_thread_pools();

} // namespace editor
