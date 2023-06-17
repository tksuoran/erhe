#include "stream.hpp"
#include "hextiles_log.hpp"

namespace hextiles
{

File_write_stream::File_write_stream(const std::filesystem::path& path)
{
    m_file = fopen(path.string().c_str(), "wb");
    if (!m_file) {
        log_stream->error("File open fail: {} - {}", path.string(), strerror(errno));
        abort();
    }
}

File_write_stream::File_write_stream(const char* path)
{
    m_file = fopen(path, "wb");
    if (!m_file) {
        log_stream->error("File open fail: {} - {}", path, strerror(errno));
        abort();
    }
}

File_write_stream::~File_write_stream() noexcept
{
    fclose(m_file);
}

void File_write_stream::op(const uint8_t&  v) const { fwrite(&v, 1, sizeof(uint8_t ), m_file); }
void File_write_stream::op(const uint16_t& v) const { fwrite(&v, 1, sizeof(uint16_t), m_file); }
void File_write_stream::op(const uint32_t& v) const { fwrite(&v, 1, sizeof(uint32_t), m_file); }
void File_write_stream::op(const int8_t&   v) const { fwrite(&v, 1, sizeof(int8_t  ), m_file); }
void File_write_stream::op(const int16_t&  v) const { fwrite(&v, 1, sizeof(int16_t ), m_file); }
void File_write_stream::op(const int32_t&  v) const { fwrite(&v, 1, sizeof(int32_t ), m_file); }

File_read_stream::File_read_stream(const std::filesystem::path& path)
{
    m_file = fopen(path.string().c_str(), "rb");
    if (!m_file) {
        log_stream->error("File open fail: {} - {}", path.string(), strerror(errno));
        abort();
    }
}

File_read_stream::File_read_stream(const char* path)
{
    m_file = fopen(path, "rb");
    if (!m_file) {
        log_stream->error("File open fail: {} - {}", path, strerror(errno));
        abort();
    }
}

File_read_stream::~File_read_stream() noexcept
{
    fclose(m_file);
}

void File_read_stream::op(uint8_t&  v) const { const auto read_count = fread(&v, 1, sizeof(uint8_t ), m_file); if (read_count != sizeof(uint8_t )) { log_stream->error("failed to read uint8_t "); } }
void File_read_stream::op(uint16_t& v) const { const auto read_count = fread(&v, 1, sizeof(uint16_t), m_file); if (read_count != sizeof(uint16_t)) { log_stream->error("failed to read uint16_t"); } }
void File_read_stream::op(uint32_t& v) const { const auto read_count = fread(&v, 1, sizeof(uint32_t), m_file); if (read_count != sizeof(uint32_t)) { log_stream->error("failed to read uint32_t"); } }
void File_read_stream::op(int8_t&   v) const { const auto read_count = fread(&v, 1, sizeof(int8_t  ), m_file); if (read_count != sizeof(int8_t  )) { log_stream->error("failed to read int8_t  "); } }
void File_read_stream::op(int16_t&  v) const { const auto read_count = fread(&v, 1, sizeof(int16_t ), m_file); if (read_count != sizeof(int16_t )) { log_stream->error("failed to read int16_t "); } }
void File_read_stream::op(int32_t&  v) const { const auto read_count = fread(&v, 1, sizeof(int32_t ), m_file); if (read_count != sizeof(int32_t )) { log_stream->error("failed to read int32_t "); } }

} // namespace hextiles
