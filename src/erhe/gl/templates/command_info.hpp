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

const char* c_str(Extension extension);

const char* c_str(Command command);

Extension parse_extension(const char* extension_name);

Command parse_command(const char* command_name);

void command_info_init(int version, const std::vector<std::string>& extensions);

void command_info_init_all();

bool is_extension_supported(Extension extension);

bool is_command_supported(Command command);

}} // namespace gl
