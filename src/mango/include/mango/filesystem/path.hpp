/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <string>
#include <vector>
#include <mango/core/configure.hpp>
#include <mango/filesystem/mapper.hpp>

namespace mango::filesystem
{

    class Path : protected NonCopyable
    {
    protected:
        std::shared_ptr<Mapper> m_mapper;

        mutable FileIndex m_index;
        mutable bool m_index_is_dirty = true;

        void updateIndex() const;

    public:
        Path(const std::string& pathname, const std::string& password = "");
        Path(const Path& path, const std::string& filename, const std::string& password = "");
        Path(ConstMemory memory, const std::string& extension, const std::string& password = "");
        ~Path();

        Mapper& getMapper() const;

        const std::string& pathname() const
        {
            return m_mapper->pathname();
        }

        auto begin() const -> decltype(m_index.begin())
        {
            updateIndex();
            return m_index.begin();
        }

        auto end() const -> decltype(m_index.end())
        {
            updateIndex();
            return m_index.end();
        }
 
        auto size() const -> decltype(m_index.size())
        {
            updateIndex();
            return m_index.size();
        }

        bool empty() const
        {
            updateIndex();
            return m_index.empty();
        }

        const FileInfo& operator [] (int index) const
        {
            updateIndex();
            return m_index[index];
        }
    };

    // -----------------------------------------------------------------------
    // filename manipulation functions
    // -----------------------------------------------------------------------

    // example: "foo/bar/readme.txt"

    std::string getPath(const std::string& filename);           // "foo/bar/"
    std::string removePath(const std::string& filename);        // "readme.txt"
    std::string getExtension(const std::string& filename);      // ".txt"
    std::string removeExtension(const std::string& filename);   // "foo/bar/readme"

} // namespace mango::filesystem
