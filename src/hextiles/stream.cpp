#include "stream.hpp"
#include "hextiles_log.hpp"

namespace hextiles
{

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

void File_read_stream::op(uint8_t&  v) const { fread(&v, 1, sizeof(uint8_t ), m_file); }
void File_read_stream::op(uint16_t& v) const { fread(&v, 1, sizeof(uint16_t), m_file); }
void File_read_stream::op(uint32_t& v) const { fread(&v, 1, sizeof(uint32_t), m_file); }
void File_read_stream::op(int8_t&   v) const { fread(&v, 1, sizeof(int8_t  ), m_file); }
void File_read_stream::op(int16_t&  v) const { fread(&v, 1, sizeof(int16_t ), m_file); }
void File_read_stream::op(int32_t&  v) const { fread(&v, 1, sizeof(int32_t ), m_file); }

} // namespace hextiles
