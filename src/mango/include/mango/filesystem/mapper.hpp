/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <string>
#include <vector>
#include <mango/core/configure.hpp>
#include <mango/core/memory.hpp>

namespace mango::filesystem
{

    struct FileInfo
    {
        enum Flags
        {
            DIRECTORY  = 0x01,
            CONTAINER  = 0x02,
            COMPRESSED = 0x04,
            ENCRYPTED  = 0x08,
        };

        u64 size;
        u32 flags;
        std::string name;

        FileInfo();
        FileInfo(const std::string& name, u64 size, u32 flags = 0);
        ~FileInfo();

        bool isFile() const;
        bool isDirectory() const;
        bool isContainer() const;
        bool isCompressed() const;
        bool isEncrypted() const;
    };

    struct FileIndex
    {
        std::vector<FileInfo> files;

        void emplace(const std::string& name, u64 size, u32 flags);

        size_t size() const
        {
            return files.size();
        }

        bool empty() const
        {
            return files.empty();
        }

        void clear()
        {
            files.clear();
        }

        FileInfo& operator [] (int index)
        {
            return files[index];
        }

        const FileInfo& operator [] (int index) const
        {
            return files[index];
        }

        std::vector<FileInfo>::iterator begin()
        {
            return files.begin();
        }

        std::vector<FileInfo>::iterator end()
        {
            return files.end();
        }

        std::vector<FileInfo>::const_iterator begin() const
        {
            return files.begin();
        }

        std::vector<FileInfo>::const_iterator end() const
        {
            return files.end();
        }
    };

    class AbstractMapper : protected NonCopyable
    {
    public:
        AbstractMapper() = default;
        virtual ~AbstractMapper() = default;

        virtual bool isFile(const std::string& filename) const = 0;
        virtual void getIndex(FileIndex& index, const std::string& pathname) = 0;
        virtual VirtualMemory* mmap(const std::string& filename) = 0;
    };

    class Mapper : public AbstractMapper
    {
    protected:
        std::shared_ptr<Mapper> m_parent_mapper;
        std::vector<std::unique_ptr<AbstractMapper>> m_mappers;
        AbstractMapper* m_current_mapper { nullptr };
        VirtualMemory* m_parent_memory { nullptr };

        std::string m_basepath;
        std::string m_pathname;

        AbstractMapper* createFileMapper(const std::string& basepath);
        std::string parse(const std::string& pathname, const std::string& password);

    public:
        Mapper(const std::string& pathname, const std::string& password);
        Mapper(std::shared_ptr<Mapper> mapper, const std::string& filename, const std::string& password);
        Mapper(ConstMemory memory, const std::string& extension, const std::string& password);
        ~Mapper();

        static bool isCustomMapper(const std::string& filename);

        const std::string& basepath() const;
        const std::string& pathname() const;

        bool isFile(const std::string& filename) const override;
        void getIndex(FileIndex& index, const std::string& pathname) override;
        VirtualMemory* mmap(const std::string& filename) override;
    };

} // namespace mango::filesystem
