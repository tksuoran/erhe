#pragma once

#include <vector>
#include <string>

namespace hextiles
{

auto read_file       (const char* const path) -> std::vector<unsigned char>;
auto read_file_string(const char* const path) -> std::string;
auto write_file      (const char* const path, const unsigned char* buffer, const size_t length) -> bool;
auto write_file      (const char* const path, const std::string& text) -> bool;
auto get_string      (const std::vector<unsigned char>& data, const size_t offset, const size_t length) -> std::string;

}
