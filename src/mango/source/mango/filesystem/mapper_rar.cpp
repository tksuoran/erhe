/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2018 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
/*
    RAR decompression code: Alexander L. Roshal / unRAR library.
*/
#include <map>
#include <algorithm>
#include <mango/core/string.hpp>
#include <mango/core/exception.hpp>
#include <mango/core/pointer.hpp>
#include <mango/filesystem/mapper.hpp>
#include <mango/filesystem/path.hpp>
#include "indexer.hpp"

#include "../../external/unrar/rar.hpp"

// https://www.rarlab.com/technote.htm

namespace
{
    // -----------------------------------------------------------------
    // interface to "unrar" library to do the decompression
    // -----------------------------------------------------------------

    using mango::Memory;
    using mango::ConstMemory;
    using mango::VirtualMemory;
    using mango::filesystem::Indexer;

    using mango::u8;
    using mango::u16;
    using mango::u32;
    using mango::u64;

    class VirtualMemoryRAR : public mango::VirtualMemory
    {
    protected:
        const u8* m_delete_address;

    public:
        VirtualMemoryRAR(const u8* address, const u8* delete_address, size_t size)
            : m_delete_address(delete_address)
        {
            m_memory = ConstMemory(address, size);
        }

        ~VirtualMemoryRAR()
        {
            delete [] m_delete_address;
        }
    };
    
    bool decompress(u8* output, const u8* input, u64 unpacked_size, u64 packed_size, u8 version)
    {
        ComprDataIO subDataIO;
        subDataIO.Init();

        Unpack unpack(&subDataIO);
        unpack.Init();

        subDataIO.UnpackToMemory = true;
        subDataIO.UnpackToMemorySize = size_t(unpacked_size);
        subDataIO.UnpackToMemoryAddr = output;

        subDataIO.UnpackFromMemory = true;
        subDataIO.UnpackFromMemorySize = size_t(packed_size);
        subDataIO.UnpackFromMemoryAddr = const_cast<u8*>(input);

        subDataIO.UnpPackedSize = packed_size;
        unpack.SetDestSize(unpacked_size);

        unpack.DoUnpack(version, false);

        return true;
    }

    // -----------------------------------------------------------------
    // RAR unicode filename conversion code
    // -----------------------------------------------------------------

    void decodeUnicode(const u8* name, const u8* encName, size_t encSize, wchar_t* unicodeName, size_t maxDecSize)
    {
        size_t encPos = 0;
        size_t decPos = 0;
        int flagBits = 0;
        u8 flags = 0;
        u8 highByte = encName[encPos++];

        while (encPos < encSize && decPos < maxDecSize)
        {
            if (flagBits == 0)
            {
                flags = encName[encPos++];
                flagBits = 8;
            }

            switch(flags >> 6)
            {
                case 0:
                    unicodeName[decPos++] = encName[encPos++];
                    break;
                case 1:
                    unicodeName[decPos++] = static_cast<wchar_t>(encName[encPos++] + (highByte << 8));
                    break;
                case 2:
                    unicodeName[decPos++] = static_cast<wchar_t>(encName[encPos] + (encName[encPos + 1] << 8));
                    encPos += 2;
                    break;
                case 3:
                {
                    int length = encName[encPos++];
                    if (length & 0x80)
                    {
                        u8 correction = encName[encPos++];
                        for (length = (length & 0x7f) + 2; length > 0 && decPos < maxDecSize; length--, decPos++)
                        {
                            unicodeName[decPos] = static_cast<wchar_t>(((name[decPos] + correction) & 0xff) + (highByte << 8));
                        }
                    }
                    else
                    {
                        for (length += 2; length > 0 && decPos < maxDecSize; length--, decPos++)
                        {
                            unicodeName[decPos] = name[decPos];
                        }
                    }
                    break;
                }
            }

            flags <<= 2;
            flagBits -= 2;
        }

        unicodeName[decPos < maxDecSize ? decPos : maxDecSize - 1] = 0;
    }

    std::string decodeUnicodeFilename(const char* data, size_t filename_size)
    {
        constexpr size_t UNICODE_FILENAME_MAX_LENGTH = 1024;

        if (filename_size >= UNICODE_FILENAME_MAX_LENGTH)
        {
            // empty filename is used later to signify file is not present
            return "";
        }

        char buffer[UNICODE_FILENAME_MAX_LENGTH];
        std::memcpy(buffer, data, filename_size);

        size_t length;
        for (length = 0; length < filename_size; ++length)
        {
            if (!buffer[length])
                break;
        }
        buffer[length++] = 0;

        std::string s;

        if (length <= filename_size)
        {
            wchar_t temp[UNICODE_FILENAME_MAX_LENGTH];
            const u8* u = reinterpret_cast<const u8*>(buffer);
            decodeUnicode(u, u + length, filename_size - length, temp, UNICODE_FILENAME_MAX_LENGTH);
            s = mango::u16_toBytes(temp);
        }
        else
        {
            s = buffer;
        }

        return s;
    }

    // -----------------------------------------------------------------
    // RAR parsing code
    // -----------------------------------------------------------------

    enum
    {
        MARK_HEAD    = 0x72,
        MAIN_HEAD    = 0x73,
        FILE_HEAD    = 0x74,
        COMM_HEAD    = 0x75,
        AV_HEAD      = 0x76,
        SUB_HEAD     = 0x77,
        PROTECT_HEAD = 0x78,
        SIGN_HEAD    = 0x79,
        NEWSUB_HEAD  = 0x7a,
        ENDARC_HEAD  = 0x7b
    };

    enum
    {
        MHD_VOLUME       = 0x0001,
        MHD_COMMENT      = 0x0002,
        MHD_LOCK         = 0x0004,
        MHD_SOLID        = 0x0008,
        MHD_PACK_COMMENT = 0x0010, // MHD_NEWNUMBERING
        MHD_AV           = 0x0020,
        MHD_PROTECT      = 0x0040,
        MHD_PASSWORD     = 0x0080,
        MHD_FIRSTVOLUME  = 0x0100,
        MHD_ENCRYPTVER   = 0x0200
    };

    enum
    {
        LHD_SPLIT_BEFORE = 0x0001, // TODO: not supported
        LHD_SPLIT_AFTER  = 0x0002, // TODO: not supported
        LHD_PASSWORD     = 0x0004,
        LHD_COMMENT      = 0x0008,
        LHD_SOLID        = 0x0010,
        LHD_LARGE        = 0x0100,
        LHD_UNICODE      = 0x0200,
        LHD_SALT         = 0x0400,
        LHD_VERSION      = 0x0800,
        LHD_EXTTIME      = 0x1000,
        LHD_EXTFLAGS     = 0x2000,
        LHD_SKIP_UNKNOWN = 0x4000
    };

    struct Header
    {
        // common
        u16  crc;
        u8   type;
        u16  flags;
        u16  size;

        // type: FILE_HEAD
        u64  packed_size;
        u64  unpacked_size;
        u32  file_crc;
        u8   version;
        u8   method;
        std::string filename;
        bool is_encrypted { false };

        Header(const u8* address)
        {
            mango::LittleEndianConstPointer p = address;

            crc   = p.read16();
            type  = p.read8();
            flags = p.read16();
            size  = p.read16();

            if (flags & LHD_SKIP_UNKNOWN)
            {
                return;
            }

            if (flags & 0x8000 && type != FILE_HEAD)
            {
			    size += p.read32();
            }

            // TODO: header CRC check

            switch (type)
            {
                case MAIN_HEAD:
                {
                    if (flags & MHD_ENCRYPTVER)
                    {
                        // encrypted
                        is_encrypted = true;
                    }
                    break;
                }

                case FILE_HEAD:
                {
                    packed_size = p.read32();
                    unpacked_size = p.read32();
                    ++p; // Host OS
                    file_crc = p.read32();
                    p += 4; // FileTime
                    version = p.read8();
                    method = p.read8();
                    int filename_size = p.read16();
                    p += 4; // FileAttr

                    if (flags & LHD_LARGE)
                    {
                        // 64 bit files
                        u64 packed_high = p.read32();
                        u64 unpacked_high = p.read32();
                        packed_size |= (packed_high << 32);
                        unpacked_size |= (unpacked_high << 32);
                    }

                    const u8* us = p;
                    const char* s = reinterpret_cast<const char*>(us);
                    p += filename_size;

                    //printf("RAR version: %d, method: %d\n", version, method);
                    if (isSupportedVersion())
                    {
                        if (flags & LHD_UNICODE)
                        {
                            // unicode filename
                            filename = decodeUnicodeFilename(s, filename_size);
                        }
                        else
                        {
                            // ascii filename
                            filename = std::string(s, filename_size);
                        }

                        std::replace(filename.begin(), filename.end(), '\\', '/');
                    }

                    if (flags & LHD_SALT)
                    {
                        // encryption salt is present
                        p += 8;
                    }

                    if (flags & LHD_EXTTIME)
                    {
                        p += 2;
                    }

                    break;
                }

                case MARK_HEAD:
                case COMM_HEAD:
                case AV_HEAD:
                case SUB_HEAD:
                case PROTECT_HEAD:
                case SIGN_HEAD:
                case NEWSUB_HEAD:
                case ENDARC_HEAD:
                    break;
            }
        }

        ~Header()
        {
        }

        bool isSupportedVersion() const
        {
            return method >= 0x30 && method <= 0x35 && version <= 36 && !filename.empty();
        }
    };

    struct FileHeader
    {
        u64  packed_size;
        u64  unpacked_size;
        u32  crc;
        u8   version;
        u8   method;
        bool is_rar5;
        std::string filename;

        bool folder;
        const u8* data;

        bool compressed() const
        {
            if (is_rar5)
            {
                return method != 0;
            }
            return method != 0x30;
        }

        VirtualMemory* mmap() const
        {
            VirtualMemory* memory;

            if (!compressed())
            {
                // no compression
                memory = new VirtualMemoryRAR(data, nullptr, size_t(unpacked_size));
            }
            else
            {
                size_t size = size_t(unpacked_size);
                u8* buffer = new u8[size];

                bool status = decompress(buffer, data, unpacked_size, packed_size, version);
                if (!status)
                {
                    delete[] buffer;
                    MANGO_EXCEPTION("[mapper.rar] Decompression failed.");
                }

                memory = new VirtualMemoryRAR(buffer, buffer, size);
            }

            return memory;
        }
    };

} // namespace

namespace mango::filesystem
{

    // -----------------------------------------------------------------
    // MapperRAR
    // -----------------------------------------------------------------

    class MapperRAR : public AbstractMapper
    {
    public:
        std::string m_password;
        std::vector<FileHeader> m_files;
        Indexer<FileHeader> m_folders;
        bool is_encrypted { false };

        MapperRAR(ConstMemory parent, const std::string& password)
            : m_password(password)
        {
            if (parent.address)
            {
                const u8* ptr = parent.address;
                const u8* end = parent.address + parent.size;

                const u8 rar4_signature[] = { 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00 };
                const u8 rar5_signature[] = { 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x01, 0x00 };

                if (!std::memcmp(ptr, rar4_signature, 7))
                {
                    // RAR 4.x
                    parse_rar4(ptr + 7, end);
                }
                else if (!std::memcmp(ptr, rar5_signature, 8))
                {
                    // RAR 5.0
                    parse_rar5(ptr + 8, end);
                }
                else
                {
                    // Incorrect signature
                }

                for (auto& header : m_files)
                {
                    std::string filename = header.filename;
                    while (!filename.empty())
                    {
                        std::string folder = getPath(filename.substr(0, filename.length() - 1));

                        header.filename = filename.substr(folder.length());
                        m_folders.insert(folder, filename, header);
                        header.folder = true;
                        filename = folder;
                    }
                }
            }
        }

        ~MapperRAR()
        {
        }

        void parse_rar4(const u8* start, const u8* end)
        {
            const u8* p = start;

            for ( ; p < end; )
            {
                const u8* h = p;
                Header header(p);
                p = h + header.size;

                is_encrypted = header.is_encrypted;

                switch (header.type)
                {
                    case FILE_HEAD:
                    {
                        if (header.isSupportedVersion())
                        {
                            FileHeader file;

                            file.packed_size = header.packed_size;
                            file.unpacked_size = header.unpacked_size;
                            file.crc = header.file_crc;
                            file.version = header.version;
                            file.method  = header.method;
                            file.is_rar5 = false;

                            int dict_flags = (header.flags >> 5) & 7;
                            file.folder = (dict_flags == 7);
                            file.data = p;

                            file.filename = header.filename;
                            if (file.folder)
                            {
                                file.filename += "/";
                            }

                            m_files.push_back(file);
                        }
                        else
                        {
                            // ignore file (unsupported compression -or- incorrect filename)
                        }

                        // skip compressed data
                        p += header.packed_size;

                        break;
                    }

                    case ENDARC_HEAD:
                    {
                        p = end; // terminate parsing
                        break;
                    }
                }
            }
        }

        u64 vint(mango::LittleEndianConstPointer& p)
        {
            u64 value = 0;
            int shift = 0;
            for (int i = 0; i < 10; ++i)
            {
                u8 sample = *p++;
                value |= ((sample & 0x7f) << shift);
                shift += 7;
                if ((sample & 0x80) != 0x80)
                    break;
            }
            return value;
        }

        void parse_rar5_file_header(mango::LittleEndianConstPointer p, ConstMemory compressed_data)
        {
            u64 flags = vint(p);
            u64 unpacked_size = vint(p);
            u64 attributes = vint(p);

            u32 mtime = 0;
            u32 crc = 0;

            if (flags & 2)
            {
                mtime = p.read32();
            }
            
            if (flags & 4)
            {
                crc = p.read32();
            }

            u64 compression = vint(p);
            u64 host_os = vint(p);
            u64 length = vint(p);

            MANGO_UNREFERENCED(attributes);
            MANGO_UNREFERENCED(mtime);
            MANGO_UNREFERENCED(host_os);

            bool is_directory = (flags & 1) != 0;

            // compression
            u32 algorithm = compression & 0x3f; // 0
            bool is_solid = (compression & 0x40) != 0;
            u32 method = (compression & 0x380) >> 7; // 0..5
            //u32 min_dict_size = (compression & 0x3c00) >> 10;

            if (flags & 8)
            {
                // unpacked_size is undefined
                return;
            }

            if (is_solid)
            {
                // solid archives are unsupported at this time
                return;
            }

            if (!compressed_data.size && !is_directory)
            {
                // empty non-directory files are not supported
                return;
            }

            // read filename
            const u8* ptr = p;
            const char* s = reinterpret_cast<const char *>(ptr);
            std::string filename(s, int(length));

            //printf("  %s%s [algorithm: %d, solid: %d, method: %d]\n", 
            //    filename.c_str(), is_directory ? "/" : "", algorithm, is_solid, method);

            FileHeader file;

            file.packed_size = compressed_data.size;
            file.unpacked_size = unpacked_size;
            file.crc = crc;
            file.version = algorithm;
            file.method  = method;
            file.is_rar5 = true;

            file.folder = is_directory;
            file.data = compressed_data.address;

            file.filename = filename;
            if (file.folder)
            {
                file.filename += "/";
            }

            m_files.push_back(file);
        }

        void parse_rar5(const u8* start, const u8* end)
        {
            mango::LittleEndianConstPointer p = start;

            for ( ; p < end; )
            {
                u32 crc = p.read32();
                u64 header_size = vint(p);
                const u8* base = p;

                u32 type = u32(vint(p));
                u32 flags = u32(vint(p));

                u64 extra_size = 0;
                u64 data_size = 0;

                if (flags & 1)
                {
                    extra_size = vint(p);
                }

                if (flags & 2)
                {
                    data_size = vint(p);
                }

                ConstMemory compressed_data(base + header_size, size_t(data_size));

                //printf("crc: %.8x, type: %x, flags: %x, header: %x, extra: %x, data: %x\n", 
                //    crc, type, flags, (int)header_size, (int)extra_size, (int)data_size);

                MANGO_UNREFERENCED(crc);
                MANGO_UNREFERENCED(extra_size);

                // TODO: add support for AES decryption headers
                // TODO: add support for RAR 5.0 compression

                switch (type)
                {
                    case 1:
                        // Main archive header
                        break;
                    case 2:
                        // File header
                        parse_rar5_file_header(p, compressed_data);
                        break;
                    case 3:
                        // Service header
                        break;
                    case 4:
                        // Archive encryption header
                        is_encrypted = true;
                        break;
                    case 5:
                        // End of archive header
                        break;
                }

                p = base + header_size + data_size;
            }
        }

        bool isFile(const std::string& filename) const override
        {
            const FileHeader* ptrHeader = m_folders.getHeader(filename);
            if (ptrHeader)
            {
                return !ptrHeader->folder;
            }

            return false;
        }

        void getIndex(FileIndex& index, const std::string& pathname) override
        {
            const Indexer<FileHeader>::Folder* ptrFolder = m_folders.getFolder(pathname);
            if (ptrFolder)
            {
                for (auto i : ptrFolder->headers)
                {
                    const FileHeader& header = *i.second;

                    u32 flags = 0;
                    u64 size = header.unpacked_size;

                    if (header.folder)
                    {
                        flags |= FileInfo::DIRECTORY;
                        size = 0;
                    }

                    if (header.compressed())
                    {
                        flags |= FileInfo::COMPRESSED;
                    }

                    if (is_encrypted)
                    {
                        flags |= FileInfo::ENCRYPTED;
                    }

                    index.emplace(header.filename, size, flags);
                }
            }
        }

        VirtualMemory* mmap(const std::string& filename) override
        {
            const FileHeader* ptrHeader = m_folders.getHeader(filename);
            if (!ptrHeader)
            {
                MANGO_EXCEPTION("[mapper.rar] File \"%s\" not found.", filename.c_str());
            }

            const FileHeader& header = *ptrHeader;
            return header.mmap();
        }
    };

    // -----------------------------------------------------------------
    // functions
    // -----------------------------------------------------------------

    AbstractMapper* createMapperRAR(ConstMemory parent, const std::string& password)
    {
        AbstractMapper* mapper = new MapperRAR(parent, password);
        return mapper;
    }

} // namespace mango::filesystem
