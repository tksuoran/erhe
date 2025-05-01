#pragma once

#include "erhe_verify/verify.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace hextiles {

class File_write_stream
{
public:
    explicit File_write_stream(const std::filesystem::path& path);
    explicit File_write_stream(const char* path);
    ~File_write_stream() noexcept;

    void op(const uint8_t&  v) const;
    void op(const uint16_t& v) const;
    void op(const uint32_t& v) const;
    void op(const int8_t&   v) const;
    void op(const int16_t&  v) const;
    void op(const int32_t&  v) const;

    template<typename T>
    void op(const std::vector<T>& v)
    {
        const size_t size = v.size();
        ERHE_VERIFY(size <= std::numeric_limits<uint32_t>::max());
        const uint32_t size_u32 = static_cast<uint32_t>(size);
        op(size_u32);
        for (const auto& element : v) {
            op(v);
        }
    }

private:
    FILE* m_file{nullptr}; // owning pointer
};

class File_read_stream
{
public:
    explicit File_read_stream(const std::filesystem::path& path);
    explicit File_read_stream(const char* path);
    ~File_read_stream() noexcept;

    void op(uint8_t&  v) const;
    void op(uint16_t& v) const;
    void op(uint32_t& v) const;
    void op(int8_t&   v) const;
    void op(int16_t&  v) const;
    void op(int32_t&  v) const;

    template<typename T>
    void op(const std::vector<T>& v)
    {
        uint32_t size_u32;
        op(size_u32);
        v.resize(size_u32);
        v.shrink_to_fit();
        for (auto& element : v) {
            op(v);
        }
    }

private:
    FILE* m_file{nullptr}; // owning pointer
};

} // namespace hextiles
