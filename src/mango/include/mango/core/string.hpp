/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <string>
#include <vector>
#include <cstdarg>
#include <mango/core/configure.hpp>

namespace mango
{

    // unicode conversions
    bool is_utf8(const std::string& s);
    std::u16string utf16_from_utf8(const std::string& str);
    std::u32string utf32_from_utf8(const std::string& str);
    std::string utf8_from_utf16(const std::u16string& str);
    std::string utf8_from_utf32(const std::u32string& str);

    // microsoft specific unicode conversions
    std::string u16_toBytes(const std::wstring& source);
    std::wstring u16_fromBytes(const std::string& source);

    // string utilities
    std::string toLower(std::string s);
    std::string toUpper(std::string s);
    std::string removePrefix(const std::string& s, const std::string& prefix);
    bool isPrefix(const std::string& s, const std::string& prefix);
    void replace(std::string& s, const std::string& from, const std::string& to);
    std::vector<std::string> split(const std::string& s, char delimiter);
    std::vector<std::string> split(const std::string& s, const char* delimiter);
    std::vector<std::string> split(const std::string& s, const std::string& delimiter);
    std::string makeString(const char* format, ...);
    const u8* memchr(const u8* p, u8 value, size_t count);

} // namespace mango
