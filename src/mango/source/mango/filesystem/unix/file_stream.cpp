/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/

#if __ANDROID_API__ < __ANDROID_API_N__
#define _FILE_OFFSET_BITS 64 /* LFS: 64 bit off_t */
#endif

#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

#include <mango/core/string.hpp>
#include <mango/core/exception.hpp>
#include <mango/filesystem/file.hpp>

namespace mango::filesystem
{

    // -----------------------------------------------------------------
	// FileHandle
    // -----------------------------------------------------------------

	struct FileHandle
	{
		FILE* m_file;
        std::string m_filename;

        FileHandle(const std::string& filename, const char* mode)
            : m_file(std::fopen(filename.c_str(), mode))
            , m_filename(filename)
		{
		}

		~FileHandle()
		{
            std::fclose(m_file);
		}

        const std::string& filename() const
        {
            return m_filename;
        }

        u64 size() const
		{
            struct stat sb;
            int fd = ::fileno(m_file);
            ::fflush(m_file);
            ::fstat(fd, &sb);
            return sb.st_size;
		}

		u64 offset() const
		{
	        return ftello(m_file);
		}

		void seek(s64 distance, int method)
		{
	        fseeko(m_file, distance, method);
		}

	    void read(void* dest, u64 size)
	    {
    	    size_t status = std::fread(dest, 1, size_t(size), m_file);
	        MANGO_UNREFERENCED(status);
	    }

	    void write(const void* data, u64 size)
	    {
	        size_t status = std::fwrite(data, 1, size_t(size), m_file);
	        MANGO_UNREFERENCED(status);
	    }
	};

    // -----------------------------------------------------------------
    // FileStream
    // -----------------------------------------------------------------

    FileStream::FileStream(const std::string& filename, OpenMode openmode)
        : m_handle(nullptr)
    {
		const char* mode;

       	switch (openmode)
        {
   	        case READ:
                mode = "rb";
                break;

   	        case WRITE:
       	        mode = "wb";
           	    break;

            default:
	            MANGO_EXCEPTION("[FileStream] Incorrect OpenMode.");
                break;
        }

		m_handle = new FileHandle(filename, mode);
    }

    FileStream::~FileStream()
    {
		delete m_handle;
    }

    const std::string& FileStream::filename() const
    {
        return m_handle->filename();
    }

    u64 FileStream::size() const
    {
		return m_handle->size();
    }

    u64 FileStream::offset() const
    {
		return m_handle->offset();
    }

    void FileStream::seek(s64 distance, SeekMode mode)
    {
        int method;

        switch (mode)
        {
            case BEGIN:
                method = SEEK_SET;
                break;

            case CURRENT:
                method = SEEK_CUR;
                break;

            case END:
                method = SEEK_END;
                break;

            default:
                MANGO_EXCEPTION("[FileStream] Invalid seek mode.");
        }

		m_handle->seek(distance, method);
    }

    void FileStream::read(void* dest, u64 size)
    {
		m_handle->read(dest, size);
    }

    void FileStream::write(const void* data, u64 size)
    {
		m_handle->write(data, size);
    }

} // namespace mango::filesystem
