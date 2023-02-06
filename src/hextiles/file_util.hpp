#pragma once

#include <vector>
#include <string>

namespace hextiles
{

auto read_file       (const char* path) -> std::vector<unsigned char>;
auto read_file_string(const char* path) -> std::string;
auto write_file      (const char* path, const unsigned char* buffer, std::size_t length) -> bool;
auto write_file      (const char* path, const std::string& text) -> bool;
auto get_string      (const std::vector<unsigned char>& data, std::size_t offset, std::size_t length) -> std::string;

}
