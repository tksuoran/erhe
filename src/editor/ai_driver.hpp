#pragma once

namespace editor {

// AI-driven editor runs (see doc/agents/editor_runs.md): when an AI coding agent launches the
// editor it sets ERHE_AI_DRIVER=1. The run then behaves as an unattended
// diagnostic session rather than a user session:
//
// - error artifacts (device / validation errors, shader compile errors) go to
//   files under logs/ that the agent can read, instead of the clipboard (which
//   assumed a human pastes the prepared message into an AI chat)
// - the user state file (inventory / hotbar slots, per scene view selections)
//   is neither read nor written, so an agent run starts from the defaults and
//   leaves the user's own state untouched (Editor_settings_store)
// - no configuration file is written at all: main() sets
//   erhe::codegen::Config_persistence::read_only, which save_config() and the
//   ImGui layout .ini files honor, so an agent run leaves config/ untouched
[[nodiscard]] auto is_ai_driver() -> bool;

// ERHE_DETERMINISTIC_CLOCK=1: the editor clock advances a fixed step per frame
// instead of following the wall clock (doc/editor/editor.md "Clock"), so a
// driven run times ImGui, animation and simulation the same on every machine.
[[nodiscard]] auto is_deterministic_clock() -> bool;

// ERHE_DEFAULT_LAYOUT=1: the per-user dock layout .ini is not read; the window
// layout is the default one (doc/agents/editor_runs.md), the same on every machine.
[[nodiscard]] auto is_default_layout() -> bool;

}
