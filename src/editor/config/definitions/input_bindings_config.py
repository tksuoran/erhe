from erhe_codegen import *

# The user's input binding overrides, saved to config/editor/input_bindings.json
# by Input_bindings_store. Only commands the user has edited are listed; every
# other command uses the default bindings its code declares
# (doc/erhe/commands.md, doc/editor/input_bindings.md).
struct("Input_binding_override",
    version=1,
    short_desc="Input binding override",
    long_desc="The bindings that replace the default bindings of one command.",
    developer=False,
    fields=[
        field("command",  String,         added_in=1, default='""', short_desc="Command",  long_desc="Command name (erhe::commands::Command::get_name())."),
        field("bindings", Vector(String), added_in=1,               short_desc="Bindings", long_desc="erhe::commands::Binding_desc text forms, e.g. key:ctrl+x; empty unbinds the command."),
    ],
)

struct("Input_bindings_config",
    version=1,
    short_desc="Input bindings",
    long_desc="User overrides of command input bindings, saved to input_bindings.json.",
    developer=False,
    fields=[
        field("overrides", Vector(StructRef("Input_binding_override")), added_in=1, short_desc="Overrides", long_desc="One entry per command whose bindings the user has edited."),
    ],
)
