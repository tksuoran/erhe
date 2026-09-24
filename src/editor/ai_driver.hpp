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
[[nodiscard]] auto is_ai_driver() -> bool;

}
