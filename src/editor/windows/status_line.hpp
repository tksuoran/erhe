#pragma once

#include "erhe_geometry/geometry_progress.hpp"

namespace editor {

class App_context;

// The editor's status line: the average frame time, the running / queued
// async operations and the geogram operation progress. Drawn at the bottom
// of the editor window (the window host's status bar) and at the top of the
// Headset window. One instance per place it is drawn: it owns the progress
// scratch get_geogram_progress() fills, so drawing allocates nothing in
// steady-state frames. Ends with ImGui::SameLine(), so callers may append.
class Status_line
{
public:
    void imgui(App_context& context);

private:
    erhe::geometry::Geogram_progress_state m_geogram_progress;
};

}
