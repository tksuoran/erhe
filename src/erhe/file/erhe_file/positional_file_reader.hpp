#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace erhe::file {

// A read-only file handle for positional reads: read_at() takes the offset
// with every call and keeps no shared file position, so any number of
// threads read through one handle at once without a lock (ReadFile with an
// OVERLAPPED offset on Windows, pread elsewhere). An open handle can keep
// the file from being replaced (a save through a temporary file renames
// over it on Windows): owners close it as soon as their reads are done.
class Positional_file_reader
{
public:
    Positional_file_reader() = default;
    ~Positional_file_reader() noexcept;
    Positional_file_reader(const Positional_file_reader&) = delete;
    auto operator=(const Positional_file_reader&) -> Positional_file_reader& = delete;

    // Opens `path`, closing a file opened before. False when it cannot be opened.
    [[nodiscard]] auto open(const std::filesystem::path& path) -> bool;
    void close();
    [[nodiscard]] auto is_open() const -> bool;

    // Reads out.size() bytes at `offset`. False when the read fails or the
    // file ends first. Thread-safe.
    [[nodiscard]] auto read_at(std::uint64_t offset, std::span<std::byte> out) const -> bool;

private:
#if defined(_WIN32)
    void* m_handle{nullptr}; // HANDLE; null when closed
#else
    int   m_fd{-1};
#endif
};

} // namespace erhe::file
