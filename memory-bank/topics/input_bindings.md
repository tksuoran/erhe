§MBEL:5.0
©erhe::Topic::input_bindings
@scope::User-editable persistent input bindings: erhe::commands defaults+overrides, Binding_desc text form, Input Bindings window, input_bindings.json, MCP binding tools
@docs::doc/erhe/commands.md{Binding-model}+doc/editor/input_bindings.md{window+persistence+MCP}

[STATE]
@input-bindings::DONE-2026-09-26{6-phases;ce241510b+1a82adc8f+c742928cf+154d8e357+b80f69320+docs-commit;UNPUSHED;plan-doc-deleted}
  lib::Commands-records-default-Binding_desc-per-bind_command_to_*;override-per-command-replaces-all-editable-bindings;dispatch-tables-rebuilt-lazily-at-next-tick|sort_bindings{never-per-frame};Input_kind-from-defaults{menu-only=button};register_command-ERHE_FATAL-on-duplicate-name;mixed-kind-defaults=FATAL
  editor::Input_bindings_store{load-after-fill_app_context,save-at-change-site;AI-driver-skips-read+write}+Input_bindings_window{rows-rebuilt-via-Commands::add_bindings_changed_callback;capture-modal-owns-keyboard}+MCP{list_input_bindings,set_command_bindings,reset_command_bindings}
  tests::erhe_commands_tests-14{binding_desc+overrides}+Mcp_test.set_command_bindings_replaces_the_default_chord
  ?user-interactive{capture-modal-feel-in-windowed-build;real-keyboard-chords;Settings>User-Interface>Edit-Input-Bindings-button}

[TRAPS]
!command-names=persistence-keys{renaming-a-command-orphans-user-overrides;unknown-names-round-trip-in-file}
!editor-window-visibility-file=config/editor/desktop_windows.json{prefix-desktop_+windows.json};config/editor/windows.json-is-unused-legacy;missing-key->window-VISIBLE-on-first-run->new-windows-need-"<ini_label>":false-there
!Imgui_window-ctor-runs-before-fill_app_context->App_context-pointers-null->pass-Commands&-explicitly
!ImGui::BeginTable-pushes-own-id->PushID(x)-must-be-popped-after-EndTable
!default-conflicts-are-many-by-design{C:brush-preview+mesh-paint-hotkey,E:hud+fly-down,D:fly-right-any-mods-vs-Ctrl+D};marked-informational-only
