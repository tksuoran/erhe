/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <algorithm>
#include <mango/filesystem/path.hpp>

namespace mango::filesystem
{

    // -----------------------------------------------------------------
    // Path
    // -----------------------------------------------------------------

    Path::Path(const std::string& pathname, const std::string& password)
        : m_mapper(std::make_shared<Mapper>(pathname, password))
    {
    }

    Path::Path(const Path& path, const std::string& pathname, const std::string& password)
        : m_mapper(std::make_shared<Mapper>(path.m_mapper, pathname, password))
    {
    }

    Path::Path(ConstMemory memory, const std::string& extension, const std::string& password)
        : m_mapper(std::make_shared<Mapper>(memory, extension, password))
    {
    }

    Path::~Path()
    {
    }

    void Path::updateIndex() const
    {
        if (m_index_is_dirty)
        {
            m_mapper->getIndex(m_index, m_mapper->basepath());
            m_index_is_dirty = false;
        }
    }

    Mapper& Path::getMapper() const
    {
        return *m_mapper.get();
    }

    // -----------------------------------------------------------------
    // filename manipulation functions
    // -----------------------------------------------------------------

    std::string getPath(const std::string& filename)
    {
        size_t n = filename.find_last_of("/\\:");
        std::string s;
        if (n != std::string::npos)
            s = filename.substr(0, n + 1);
        return s;
    }

    std::string removePath(const std::string& filename)
    {
        size_t n = filename.find_last_of("/\\:");
        std::string s;
        if (n != std::string::npos)
            s = filename.substr(n + 1);
        else
            s = filename;
        return s;
    }

    std::string getExtension(const std::string& filename)
    {
        size_t n = filename.find_last_of('.');
        std::string s;
        if (n != std::string::npos)
            s = filename.substr(n);
        return s;
    }

    std::string removeExtension(const std::string& filename)
    {
        size_t n = filename.find_last_of('.');
        return filename.substr(0, n);
    }

} // namespace mango::filesystem
