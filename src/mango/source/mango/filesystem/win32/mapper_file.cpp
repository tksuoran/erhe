/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2017 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/exception.hpp>
#include <mango/core/string.hpp>
#include <mango/filesystem/mapper.hpp>
#include <mango/filesystem/path.hpp>

#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace
{
    using namespace mango;
    using namespace mango::filesystem;

    // -----------------------------------------------------------------
    // FileMemory
    // -----------------------------------------------------------------

    class FileMemory : public VirtualMemory
    {
    protected:
        LPVOID  m_address;
        HANDLE  m_file;
        HANDLE  m_map;

    public:
        FileMemory(const std::string& filename, u64 x_offset, u64 x_size)
            : m_address(nullptr)
            , m_file(INVALID_HANDLE_VALUE)
            , m_map(nullptr)
        {
            m_file = CreateFileW(u16_fromBytes(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            // special handling when too long filename
            if (m_file == INVALID_HANDLE_VALUE)
            {
                // TODO: use UNC filename or ShortPath
                if (filename.length() > MAX_PATH)
                {
                    m_memory.address = nullptr;
                    m_memory.size = 0;
                    return;
                }
            }

            if (m_file != INVALID_HANDLE_VALUE)
            {
                LARGE_INTEGER file_size;
                GetFileSizeEx(m_file, &file_size);

                if (file_size.QuadPart > 0)
                {
                    DWORD maxSizeHigh = 0;
                    DWORD maxSizeLow = 0;
                    m_map = CreateFileMappingW(m_file, NULL, PAGE_READONLY, maxSizeHigh, maxSizeLow, NULL);

                    if (m_map)
                    {
                        DWORD offsetHigh = 0;
                        DWORD offsetLow = 0;
                        SIZE_T bytes = 0;

                        u64 page_offset = 0;
                        if (x_offset > 0)
                        {
                            SYSTEM_INFO info;
                            ::GetSystemInfo(&info);
                            const DWORD page_size = info.dwAllocationGranularity;
                            const DWORD page_number = static_cast<DWORD>(x_offset / page_size);
                            page_offset = page_number * page_size;
                            offsetHigh = DWORD(page_offset >> 32);
                            offsetLow = DWORD(page_offset & 0xffffffff);
                        }

                        m_memory.size = size_t(file_size.QuadPart);
                        if (x_size > 0)
                        {
                            m_memory.size = std::min(m_memory.size, static_cast<size_t>(x_size));
                            bytes = static_cast<SIZE_T>(m_memory.size);
                        }

                        LPVOID address_ = MapViewOfFile(m_map, FILE_MAP_READ, offsetHigh, offsetLow, bytes);

                        m_address = address_;
                        m_memory.address = reinterpret_cast<u8*>(address_) + (x_offset - page_offset);
                    }
                    else
                    {
                        MANGO_EXCEPTION("[FileMemory] Memory \"%s\" mapping failed.", filename.c_str());
                    }
                }
            }
            else
            {
                MANGO_EXCEPTION("[FileMemory] File \"%s\" cannot be opened.", filename.c_str());
            }
        }

        ~FileMemory()
        {
            if (m_address)
            {
                UnmapViewOfFile(m_address);
            }

            if (m_map)
            {
                CloseHandle(m_map);
            }

            if (m_file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_file);
            }
        }
    };

    // -----------------------------------------------------------------
    // FileMapper
    // -----------------------------------------------------------------

    class FileMapper : public AbstractMapper
    {
    protected:
        std::string m_basepath;

    public:
        FileMapper(const std::string& basepath)
            : m_basepath(basepath)
        {
        }

        ~FileMapper()
        {
        }

        bool isFile(const std::string& filename) const override
        {
            bool is = false;

            struct __stat64 s;

            if (_wstat64(u16_fromBytes(m_basepath + filename).c_str(), &s) == 0)
            {
                is = (s.st_mode & _S_IFDIR) == 0;
            }

            return is;
        }

        void getIndex(FileIndex& index, const std::string& pathname) override
        {
            std::string fullname = m_basepath + pathname;
            if (fullname.empty())
            {
                // does _wfindfirst64() supports empty fullname?
                fullname = "./";
            }

            std::wstring filespec = u16_fromBytes(fullname + "*");

            _wfinddata64_t cfile;

            intptr_t hfile = ::_wfindfirst64(filespec.c_str(), &cfile);

            // find first file in current directory
            if (hfile != -1L)
            {
                for (;;)
                {
                    std::string filename = u16_toBytes(cfile.name);
                    filename = removePrefix(filename, m_basepath);

                    // skip "." and ".."
                    if (filename != "." && filename != "..")
                    {
                        bool isfile = (cfile.attrib & _A_SUBDIR) == 0;

                        if (isfile)
                        {
                            size_t filesize = size_t(cfile.size);
                            index.emplace(filename, filesize, 0);
                        }
                        else
                        {
                            index.emplace(filename + "/", 0, FileInfo::DIRECTORY);
                        }
                    }

                    if (::_wfindnext64(hfile, &cfile) != 0)
                        break;
                }

                ::_findclose(hfile);
            }
        }

        VirtualMemory* mmap(const std::string& filename) override
        {
            VirtualMemory* memory = new FileMemory(m_basepath + filename, 0, 0);
            return memory;
        }
    };

} // namespace

namespace mango::filesystem
{

    // -----------------------------------------------------------------
    // Mapper::createFileMapper()
    // -----------------------------------------------------------------

    AbstractMapper* Mapper::createFileMapper(const std::string& basepath)
    {
        AbstractMapper* mapper = new FileMapper(basepath);
        m_mappers.emplace_back(mapper);
        return mapper;
    }

} // namespace mango::filesystem
