# Editor command scripts: outstanding work

Status: proposed

This plan extends `doc/editor/command_script.md` (the startup `commands.json` script)
with the two things it does not offer.

## Re-run a script without restarting

The script fires exactly once, before the main loop. A Developer-menu
"Run startup script" button on `Commands_window` would replay it, which is
what iterating on a script needs.

That only becomes safe once `scene.add_cameras` stops creating its viewport as
a non-undoable side effect (`doc/editor/command_script.md` "Limitations"): move the
`Viewport_scene_view` plus `Viewport_window` plumbing into its own one-shot
setup hook, or a dedicated non-undoable command, so a second run does not
stack a second viewport on the first.

## Several named scripts

One script per saved scene preset needs either several `commands_*.json` files
chosen at runtime, or a top-level map of named command lists in one JSON.
