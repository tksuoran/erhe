#pragma once
{AUTOGENERATION_WARNING}

#include <string>
#include <vector>

namespace gl
{{

enum class Extension : unsigned int {{
    Extension_None = 0,
{EXTENSION_ENUM_DECLARATIONS}
}};

enum class Command : unsigned int {{
    Command_None = 0,
{COMMAND_ENUM_DECLARATIONS}
}};

auto c_str                 (const Extension extension) -> const char*;
auto c_str                 (const Command command) -> const char*;
auto parse_extension       (const std::string& extension_name) -> Extension;
auto parse_command         (const std::string& command_name) -> Command;
void command_info_init     (const int version, const std::vector<std::string>& extensions);
void command_info_init_all ();
auto is_extension_supported(const Extension extension) -> bool;
auto is_command_supported  (const Command command) -> bool;

}} // namespace gl
