#include "erhe_file/positional_file_reader.hpp"

#include <algorithm>
#include <limits>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   include <fcntl.h>
#   include <unistd.h>
#endif

namespace erhe::file {

Positional_file_reader::~Positional_file_reader() noexcept
{
    close();
}

#if defined(_WIN32)

auto Positional_file_reader::open(const std::filesystem::path& path) -> bool
{
    close();
    const HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    m_handle = handle;
    return true;
}

void Positional_file_reader::close()
{
    if (m_handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

auto Positional_file_reader::is_open() const -> bool
{
    return m_handle != nullptr;
}

auto Positional_file_reader::read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool
{
    if (m_handle == nullptr) {
        return false;
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const std::uint64_t position = offset + done;
        const DWORD         request  = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, std::size_t{1} << 30));
        OVERLAPPED overlapped{};
        overlapped.Offset     = static_cast<DWORD>(position & 0xffffffffu);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
        DWORD read_count = 0;
        if (!ReadFile(static_cast<HANDLE>(m_handle), out.data() + done, request, &read_count, &overlapped) || (read_count == 0)) {
            return false;
        }
        done += read_count;
    }
    return true;
}

#else

auto Positional_file_reader::open(const std::filesystem::path& path) -> bool
{
    close();
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    m_fd = fd;
    return true;
}

void Positional_file_reader::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

auto Positional_file_reader::is_open() const -> bool
{
    return m_fd >= 0;
}

auto Positional_file_reader::read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool
{
    if (m_fd < 0) {
        return false;
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const ssize_t read_count = ::pread(m_fd, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
        if (read_count <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(read_count);
    }
    return true;
}

#endif

} // namespace erhe::file
