{AUTOGENERATION_WARNING}

#include "erhe/gl/command_info.hpp"
#include <bitset>
#include <cctype>
#include <cstdio>
#include <map>

namespace gl
{{

namespace {{

int g_version;

std::bitset<static_cast<size_t>(Extension::Extension_Count)> g_extension_support;
std::bitset<static_cast<size_t>(Command::Command_Count)> g_command_support;

std::map<std::string, Extension> g_extension_map {{
{EXTENSION_MAP_ENTRIES}
}};

std::map<std::string, Command> g_command_map {{
{COMMAND_MAP_ENTRIES}
}};

void enable(Extension extension)
{{
    auto index = static_cast<std::size_t>(extension);
    g_extension_support.set(index);
}}

void enable(Command command)
{{
    auto index = static_cast<std::size_t>(command);
    g_command_support.set(index);
}}

bool is_enabled(Extension extension)
{{
    auto index = static_cast<std::size_t>(extension);
    return g_extension_support.test(index);
}}

bool is_enabled(Command command)
{{
    auto index = static_cast<std::size_t>(command);
    return g_command_support.test(index);
}}

void check_version(Command command, int min_version)
{{
    if (g_version >= min_version)
    {{
        enable(command);
        return;
    }}
}}

void check_extension(Command command, Extension extension)
{{
    if (is_enabled(extension))
    {{
        enable(command);
        return;
    }}
}}

//void check_alias_command(Command alias_command, Command command)
//{{
//    if (is_enabled(command))
//    {{
//        enable(alias_command);
//        return;
//    }}
//}}

}} // anonymous namespace

const char* c_str(Extension extension)
{{
    switch (extension)
    {{
{EXTENSION_CASE_ENTRIES}
        default: return "?";
    }}
}}

const char* c_str(Command command)
{{
    switch (command)
    {{
{COMMAND_CASE_ENTRIES}
        default: return "?";
    }}
}}

Extension parse_extension(const char* extension_name)
{{
    auto i = g_extension_map.find(extension_name);
    if (i != g_extension_map.end())
    {{
        return i->second;
    }}
    return Extension::Extension_None;
}}

Command parse_command(const char* command_name)
{{
    auto i = g_command_map.find(command_name);
    if (i != g_command_map.end())
    {{
        return i->second;
    }}
    return Command::Command_None;
}}

void command_info_init(int version, const std::vector<std::string>& extensions)
{{
    g_version = version;
    for (auto extension_str : extensions)
    {{
        auto extension = parse_extension(extension_str.c_str());
        enable(extension);
    }}
{COMMAND_INFO_ENTRIES}
}}

void command_info_init_all()
{{
    g_extension_support.set();
    g_command_support.set();
}}

bool is_extension_supported(Extension extension)
{{
    return is_enabled(extension);
}}

bool is_command_supported(Command command)
{{
    return is_enabled(command);
}}

}} // namespace gl
