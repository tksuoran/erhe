/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include <mango/core/configure.hpp>
#include <mango/core/stream.hpp>
#include <mango/filesystem/mapper.hpp>
#include <mango/filesystem/path.hpp>

namespace mango::filesystem
{

    class File : protected NonCopyable
    {
    protected:
        std::string m_filename;
        std::unique_ptr<Path> m_path;
        std::unique_ptr<VirtualMemory> m_virtual_memory;
        ConstMemory m_memory;

        void initMemory(Mapper& mapper);

    public:
        File(const std::string& filename);
        File(const Path& path, const std::string& filename);
        File(ConstMemory memory, const std::string& extension, const std::string& filename);
        ~File();

        const Path& path() const;
        const std::string& filename() const;
        const std::string& pathname() const;

        // memory
        operator ConstMemory () const;
        operator const u8* () const;
        const u8* data() const;
        u64 size() const;
    };

    class FileStream : public Stream
    {
    protected:
		struct FileHandle* m_handle;

    public:
        FileStream(const std::string& filename, OpenMode mode);
        ~FileStream();

        const std::string& filename() const;

        u64 size() const override;
        u64 offset() const override;
        void seek(s64 distance, SeekMode mode) override;
        void read(void* dest, u64 size) override;
        void write(const void* data, u64 size) override;

        void write(ConstMemory memory)
        {
            Stream::write(memory);
        }
    };

    class InputFileStream : public FileStream
    {
    public:
        InputFileStream(const std::string& filename)
            : FileStream(filename, Stream::READ)
        {
        }
    };

    class OutputFileStream : public FileStream
    {
    public:
        OutputFileStream(const std::string& filename)
            : FileStream(filename, Stream::WRITE)
        {
        }
    };

} // namespace mango::filesystem
