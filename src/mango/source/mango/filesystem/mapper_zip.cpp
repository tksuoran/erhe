/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2018 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/pointer.hpp>
#include <mango/core/string.hpp>
#include <mango/core/exception.hpp>
#include <mango/core/compress.hpp>
#include <mango/filesystem/mapper.hpp>
#include <mango/filesystem/path.hpp>
#include "indexer.hpp"

#include "../../external/libdeflate/libdeflate.h"

/*
https://courses.cs.ut.ee/MTAT.07.022/2015_fall/uploads/Main/dmitri-report-f15-16.pdf

[1] PKWARE Inc. APPNOTE.TXT – .ZIP File Format Specification, version 6.3.4
https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT

[2] PKWARE Inc. .ZIP Application Note
https://www.pkware.com/support/zip-app-note/

[3] PKWARE Inc. APPNOTE.TXT, version 1.0
https://pkware.cachefly.net/webdocs/APPNOTE/APPNOTE-1.0.txt

[4] WinZip. AES Encryption Information: Encryption Specification AE-1 and AE-2
http://www.winzip.com/aes_info.htm
*/

namespace
{
    using namespace mango;
    using mango::filesystem::Indexer;

    enum { DCKEYSIZE = 12 };

    enum Encryption : u8
    {
        ENCRYPTION_NONE    = 0,
        ENCRYPTION_CLASSIC = 1,
        ENCRYPTION_AES128  = 2,
        ENCRYPTION_AES192  = 3,
        ENCRYPTION_AES256  = 4,
    };

    enum Compression : u16
    {
        COMPRESSION_NONE      = 0,  // stored (no compression)
        COMPRESSION_SHRUNK    = 1,  // Shrunk
        COMPRESSION_REDUCE_1  = 2,  // Reduced with compression factor 1
        COMPRESSION_REDUCE_2  = 3,  // Reduced with compression factor 2
        COMPRESSION_REDUCE_3  = 4,  // Reduced with compression factor 3
        COMPRESSION_REDUCE_4  = 5,  // Reduced with compression factor 4
        COMPRESSION_IMPLODE   = 6,  // Imploded
        COMPRESSION_DEFLATE   = 8,  // Deflated
        COMPRESSION_DEFLATE64 = 9,  // Enhanced Deflating using Deflate64(tm)
        COMPRESSION_DCLI      = 10, // PKWARE Data Compression Library Imploding (old IBM TERSE)
        COMPRESSION_BZIP2     = 12, // compressed using BZIP2 algorithm
        COMPRESSION_LZMA      = 14, // LZMA
        COMPRESSION_CMPSC     = 16, // IBM z/OS CMPSC Compression
        COMPRESSION_TERSE     = 18, // IBM TERSE (new)
        COMPRESSION_LZ77      = 19, // IBM LZ77 z Architecture 
        COMPRESSION_ZSTD      = 93, // Zstandard (zstd) Compression 
        COMPRESSION_MP3       = 94, // MP3 Compression 
        COMPRESSION_XZ        = 95, // XZ Compression 
        COMPRESSION_JPEG      = 96, // JPEG variant
        COMPRESSION_WAVPACK   = 97, // WavPack compressed data
        COMPRESSION_PPMD      = 98, // PPMd version I, Rev 1
        COMPRESSION_AES       = 99, // AE-x encryption marker
    };

    bool isCompressionSupported(u16 compression)
    {
        switch (compression)
        {
            case COMPRESSION_NONE:
            case COMPRESSION_DEFLATE:
            case COMPRESSION_BZIP2:
            case COMPRESSION_LZMA:
            case COMPRESSION_ZSTD:
            case COMPRESSION_PPMD:
                return true;

            default:
                return false;
        }
    }

    u32 getSaltLength(Encryption encryption)
    {
        u32 length = 0;
        switch (encryption)
        {
            case ENCRYPTION_AES128:
                length = 8;
                break;
            case ENCRYPTION_AES192:
                length = 12;
                break;
            case ENCRYPTION_AES256:
                length = 16;
                break;
            default:
                length = 0;
                break;
        }
        return length;
    }

    struct LocalFileHeader
    {
        u32  signature;         // 0x04034b50 ("PK..")
        u16  versionNeeded;     // version needed to extract
        u16  flags;             //
        u16  compression;       // compression method
        u16  lastModTime;       // last mod file time
        u16  lastModDate;       // last mod file date
        u32  crc;               //
        u64  compressedSize;    //
        u64  uncompressedSize;  //
        u16  filenameLen;       // length of the filename field following this structure
        u16  extraFieldLen;     // length of the extra field following the filename field

        LocalFileHeader(LittleEndianConstPointer p)
        {
            signature = p.read32();
            if (status())
			{
			    versionNeeded    = p.read16();
			    flags            = p.read16();
			    compression      = p.read16();
			    lastModTime      = p.read16();
			    lastModDate      = p.read16();
			    crc              = p.read32();
			    compressedSize   = p.read32(); // ZIP64: 0xffffffff
			    uncompressedSize = p.read32(); // ZIP64: 0xffffffff
			    filenameLen      = p.read16();
			    extraFieldLen    = p.read16();

                p += filenameLen;

                // read extra fields
                const u8* ext = p;
                const u8* end = p + extraFieldLen;
                for ( ; ext < end;)
                {
                    LittleEndianConstPointer e = ext;
                    u16 magic = e.read16();
                    u16 size = e.read16();
                    const u8* next = e + size;
                    switch (magic)
                    {
                        case 0x0001:
                        {
                            // ZIP64 extended field
                            if (uncompressedSize == 0xffffffff)
                            {
                                uncompressedSize = e.read64();
                            }
                            if (compressedSize == 0xffffffff)
                            {
                                compressedSize = e.read64();
                            }
                            break;
                        }

                        case 0x9901:
                        {
                            // AES header
                            break;
                        }
                    }
                    ext = next;
                }
            }
        }

        bool status() const
        {
            return signature == 0x04034b50;
        }
    };

    struct FileHeader
    {
        u32	signature;         // 0x02014b50
        u16	versionUsed;       //
        u16	versionNeeded;     //
        u16	flags;             //
        u16	compression;       // compression method
        u16	lastModTime;       //
        u16	lastModDate;       //
        u32	crc;               //
        u64	compressedSize;    // ZIP64: 0xffffffff
        u64	uncompressedSize;  // ZIP64: 0xffffffff
        u16	filenameLen;       // length of the filename field following this structure
        u16	extraFieldLen;     // length of the extra field following the filename field
        u16	commentLen;        // length of the file comment field following the extra field
        u16	diskStart;         // the number of the disk on which this file begins, ZIP64: 0xffff
        u16	internal;          // internal file attributes
        u32	external;          // external file attributes
        u64	localOffset;       // relative offset of the local file header, ZIP64: 0xffffffff

        std::string filename;      // filename is stored after the header
        bool        is_folder;     // if the last character of filename is "/", it is a folder
        Encryption  encryption;

        bool read(LittleEndianConstPointer& p)
        {
            signature = p.read32();
            if (signature != 0x02014b50)
            {
                return false;
            }

            versionUsed      = p.read16();
            versionNeeded    = p.read16();
            flags            = p.read16();
            compression      = p.read16();
            lastModTime      = p.read16();
            lastModDate      = p.read16();
            crc              = p.read32();
            compressedSize   = p.read32();
            uncompressedSize = p.read32();
            filenameLen      = p.read16();
            extraFieldLen    = p.read16();
            commentLen       = p.read16();
            diskStart        = p.read16();
            internal         = p.read16();
            external         = p.read32();
            localOffset      = p.read32();

            // read filename
            const u8* us = p;
            const char* s = reinterpret_cast<const char*>(us);
            p += filenameLen;

            if (s[filenameLen - 1] == '/')
            {
                is_folder = true;
            }
            else
            {
                is_folder = false;
            }

            filename = std::string(s, filenameLen);
            encryption = flags & 1 ? ENCRYPTION_CLASSIC : ENCRYPTION_NONE;

            // read extra fields
            const u8* ext = p;
            const u8* end = p + extraFieldLen;
            for ( ; ext < end;)
            {
                LittleEndianConstPointer e = ext;
                u16 magic = e.read16();
                u16 size = e.read16();
                const u8* next = e + size;
                switch (magic)
                {
                    case 0x0001:
                    {
                        // ZIP64 extended field
                        if (uncompressedSize == 0xffffffff)
                        {
                            uncompressedSize = e.read64();
                        }
                        if (compressedSize == 0xffffffff)
                        {
                            compressedSize = e.read64();
                        }
                        if (localOffset == 0xffffffff)
                        {
                            localOffset = e.read64();
                        }
                        if (diskStart == 0xffff)
                        {
                            e += 4;
                        }
                        break;
                    }

                    case 0x9901:
                    {
                        // AES header
                        u16 version = e.read16();
                        u16 magic = e.read16(); // must be 'AE' (0x41, 0x45)
                        u8 mode = e.read8();
                        compression = e.read8(); // override compression algorithm

                        if (version < 1 || version > 2 || magic != 0x4541)
                        {
                            MANGO_EXCEPTION("[mapper.zip] Incorrect AES header.");
                        }

                        // select encryption mode
                        switch (mode)
                        {
                            case 1:
                                encryption = ENCRYPTION_AES128;
                                break;
                            case 2:
                                encryption = ENCRYPTION_AES192;
                                break;
                            case 3:
                                encryption = ENCRYPTION_AES256;
                                break;
                            default:
                                MANGO_EXCEPTION("[mapper.zip] Incorrect AES encryption mode.");
                        }

                        break;
                    }
                }
                ext = next;
            }

            p += extraFieldLen;
            p += commentLen;

            return true;
		}
	};

	struct DirEndRecord
	{
		u32	signature;         // 0x06054b50
		u16	thisDisk;          // number of this disk
		u16	dirStartDisk;      // number of the disk containing the start of the central directory
		u64	numEntriesOnDisk;  // # of entries in the central directory on this disk
		u64	numEntriesTotal;   // total # of entries in the central directory
		u64	dirSize;           // size of the central directory
		u64	dirStartOffset;    // offset of the start of central directory on the disk
		u16	commentLen;        // zip file comment length

		DirEndRecord(ConstMemory memory)
		{
            std::memset(this, 0, sizeof(DirEndRecord));

			// find central directory end record signature
			// by scanning backwards from the end of the file
            const u8* start = memory.address;
            const u8* end = memory.address + memory.size;

            end -= 22; // header size is 22 bytes

            for ( ; end >= start; --end)
			{
                LittleEndianConstPointer p = end;

				signature = p.read32();
				if (status())
                {
                    // central directory detected
    				thisDisk         = p.read16();
	    			dirStartDisk     = p.read16();
		    		numEntriesOnDisk = p.read16();
			    	numEntriesTotal  = p.read16();
				    dirSize          = p.read32();
				    dirStartOffset   = p.read32();
				    commentLen       = p.read16();

                    if (thisDisk != 0 || dirStartDisk != 0 || numEntriesOnDisk != numEntriesTotal)
                    {
                        // multi-volume archives are not supported
                        signature = 0;
                    }

                    if (dirStartOffset == 0xffffffff)
                    {
                        p = end - 20;
                        u32 magic = p.read32();
                        if (magic == 0x07064b50)
                        {
                            // ZIP64 detected
                            p += 4;
                            u64 offset = p.read64();

                            p = start + offset;
                            magic = p.read32();
                            if (magic == 0x06064b50)
                            {
                                // ZIP64 End of Central Directory
                                p += 20;
		    		            numEntriesOnDisk = p.read64();
			    	            numEntriesTotal  = p.read64();
                                dirSize          = p.read64();
				                dirStartOffset   = p.read64();
                            }
                        }
                    }

                    break;
                }
			}
		}

        ~DirEndRecord()
        {
        }

        bool status() const
        {
            return signature == 0x06054b50;
        }
	};

    // --------------------------------------------------------------------
    // zip functions
    // --------------------------------------------------------------------

    inline u32 zip_crc32(u32 crc, u8 v)
    {
        static const u32 crc_table[] =
        {
            0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
            0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
        };

        u32 x = crc;
        x = (x >> 4) ^ crc_table[(x & 0xf) ^ (v & 0xf)];
        x = (x >> 4) ^ crc_table[(x & 0xf) ^ (v >> 4)];
        return x;
	}

	inline u8 zip_decrypt_value(u32* keys)
	{
		u32 temp = (keys[2] & 0xffff) | 2;
		return u8(((temp * (temp ^ 1)) >> 8) & 0xff);
	}

	inline void zip_update(u32* keys, u8 v)
	{
		keys[0] = zip_crc32(keys[0], v);
		keys[1] = 1 + (keys[1] + (keys[0] & 0xff)) * 134775813L;
		keys[2] = zip_crc32(keys[2], u8(keys[1] >> 24));
	}

	void zip_decrypt_buffer(u8* out, const u8* in, u64 size, u32* keys)
	{
		for (u64 i = 0; i < size; ++i)
		{
			u8 v = in[i] ^ zip_decrypt_value(keys);
			zip_update(keys, v);
			out[i] = v;
		}
	}

	void zip_init_keys(u32* keys, const char* password)
	{
		keys[0] = 305419896L;
		keys[1] = 591751049L;
		keys[2] = 878082192L;

		for (; *password; ++password)
		{
			zip_update(keys, *password);
		}
	}

	bool zip_decrypt(u8* out, const u8* in, u64 size, const u8* dcheader, int version, u32 crc, const std::string& password)
	{
		if (password.empty())
		{
			// missing password
		    return false;
		}

		// decryption keys
		u32 keys[3];
		zip_init_keys(keys, password.c_str());

		// decrypt the 12 byte encryption header
		u8 keyfile[DCKEYSIZE];
		zip_decrypt_buffer(keyfile, dcheader, DCKEYSIZE, keys);

		// check that password is correct one
		if (version < 20)
		{
			u16* p = reinterpret_cast<u16*>(&keyfile[10]);
			if (*p != (crc >> 16))
			{
				// incorrect password
				return false;
			}
		}
		else if (version < 30)
		{
			if (keyfile[11] != (crc >> 24))
			{
				// incorrect password
				return false;
			}
		}
		else
		{
		    // NOTE: the CRC/password check should be same for version > 3.0
		    //       but some compression programs don't create compatible
		    //       dcheader so the check would fail above.

#if 0 // TODO: the condition is reversed here.. need more testing (check specs!)
			if (keyfile[11] == (crc >> 24))
			{
				// incorrect password
				return false;
			}
#endif
		}

		// read compressed data & decrypt
		zip_decrypt_buffer(out, in, size, keys);

		return true;
	}

	u64 zip_decompress(const u8* compressed, u8* uncompressed, u64 compressedLen, u64 uncompressedLen)
	{
        libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();

        size_t bytes_out = 0;
        libdeflate_result result = libdeflate_deflate_decompress(decompressor,
            compressed, size_t(compressedLen),
            uncompressed, size_t(uncompressedLen), &bytes_out);

        libdeflate_free_decompressor(decompressor);

        const char* error = nullptr;
        switch (result)
        {
            default:
            case LIBDEFLATE_SUCCESS:
                break;
            case LIBDEFLATE_BAD_DATA:
                error = "Bad data";
                break;
            case LIBDEFLATE_SHORT_OUTPUT:
                error = "Short output";
                break;
            case LIBDEFLATE_INSUFFICIENT_SPACE:
                error = "Insufficient space";
                break;
        }

        if (error)
        {
            MANGO_EXCEPTION("[mapper.zip] %s.", error);
        }

        return bytes_out;
    }

} // namespace

namespace mango::filesystem
{

    // -----------------------------------------------------------------
    // VirtualMemoryZIP
    // -----------------------------------------------------------------

    class VirtualMemoryZIP : public mango::VirtualMemory
    {
    protected:
        const u8* m_delete_address;

    public:
        VirtualMemoryZIP(const u8* address, const u8* delete_address, size_t size)
            : m_delete_address(delete_address)
        {
            m_memory = ConstMemory(address, size);
        }

        ~VirtualMemoryZIP()
        {
            delete [] m_delete_address;
        }
    };

    // -----------------------------------------------------------------
    // MapperZIP
    // -----------------------------------------------------------------

    class MapperZIP : public AbstractMapper
    {
    public:
        ConstMemory m_parent_memory;
        std::string m_password;
        Indexer<FileHeader> m_folders;

        MapperZIP(ConstMemory parent, const std::string& password)
            : m_parent_memory(parent)
            , m_password(password)
        {
            if (parent.address)
            {
                DirEndRecord record(parent);
                if (record.status())
                {
                    const int numFiles = int(record.numEntriesTotal);

                    // read file headers
                    LittleEndianConstPointer p = parent.address + record.dirStartOffset;

                    for (int i = 0; i < numFiles; ++i)
                    {
                        FileHeader header;
                        if (header.read(p))
                        {
                            // NOTE: Don't index files that can't be decompressed
                            if (isCompressionSupported(header.compression))
                            {
                                std::string filename = header.filename;
                                while (!filename.empty())
                                {
                                    std::string folder = getPath(filename.substr(0, filename.length() - 1));

                                    header.filename = filename.substr(folder.length());
                                    m_folders.insert(folder, filename, header);
                                    header.is_folder = true;
                                    filename = folder;
                                }
                            }
                        }
                    }
                }
            }
        }

        ~MapperZIP()
        {
        }

        VirtualMemory* mmap(FileHeader header, const u8* start, const std::string& password)
        {
            LittleEndianConstPointer p = start + header.localOffset;

            LocalFileHeader localHeader(p);
            if (!localHeader.status())
            {
                MANGO_EXCEPTION("[mapper.zip] Invalid local header.");
            }

            u64 offset = header.localOffset + 30 + localHeader.filenameLen + localHeader.extraFieldLen;

            const u8* address = start + offset;
            u64 size = 0;

            u8* buffer = nullptr; // remember allocated memory

            //printf("[ZIP] compression: %d, encryption: %d \n", header.compression, header.encryption);

            switch (header.encryption)
            {
                case ENCRYPTION_NONE:
                    break;

                case ENCRYPTION_CLASSIC:
                {
                    // decryption header
                    const u8* dcheader = address;
                    address += DCKEYSIZE;

                    // NOTE: decryption capability reduced on 32 bit platforms
                    const size_t compressed_size = size_t(header.compressedSize);
                    buffer = new u8[compressed_size];

                    bool status = zip_decrypt(buffer, address, header.compressedSize,
                        dcheader, header.versionUsed & 0xff, header.crc, password);
                    if (!status)
                    {
                        delete[] buffer;
                        MANGO_EXCEPTION("[mapper.zip] Decryption failed (probably incorrect password).");
                    }

                    address = buffer;
                    break;
                }

                case ENCRYPTION_AES128:
                case ENCRYPTION_AES192:
                case ENCRYPTION_AES256:
                {
                    u32 salt_length = getSaltLength(header.encryption);
                    MANGO_UNREFERENCED(salt_length);
                    MANGO_EXCEPTION("[mapper.zip] AES encryption is not yet supported.");
#if 0                
                    u8* saltvalue = address;
                    address += salt_length;

                    u8* passverify = address;
                    address += AES_PWVERIFYSIZE;

                    address += HMAC_LENGTH;

                    size_t compressed_size = size_t(header.compressedSize);
                    compressed_size -= salt_length;
                    compressed_size -= AES_PWVERIFYSIZE;
                    compressed_size -= HMAC_LENGTH;

                    // TODO: password + salt --> key
                    // TODO: decrypt using the generated key
#endif                
                    break;
                }
            }

            Compressor compressor;

            switch (header.compression)
            {
                case COMPRESSION_NONE:
                    size = header.uncompressedSize;
                    break;

                case COMPRESSION_DEFLATE:
                {
                    const size_t uncompressed_size = size_t(header.uncompressedSize);
                    u8* uncompressed_buffer = new u8[uncompressed_size];

                    u64 outsize = zip_decompress(address, uncompressed_buffer, header.compressedSize, header.uncompressedSize);

                    delete[] buffer;
                    buffer = uncompressed_buffer;

                    if (outsize != header.uncompressedSize)
                    {
                        // incorrect output size
                        delete[] buffer;
                        MANGO_EXCEPTION("[mapper.zip] Incorrect decompressed size.");
                    }

                    // use decode_buffer as memory map
                    address = buffer;
                    size = header.uncompressedSize;
                    break;
                }

                case COMPRESSION_LZMA:
                {
                    // parse LZMA compression header
                    p = address;
                    p += 2; // skip LZMA version
                    u16 lzma_propsize = p.read16();
                    address = p;

                    if (lzma_propsize != 5)
                    {
                        delete[] buffer;
                        MANGO_EXCEPTION("[mapper.zip] Incorrect LZMA header.");
                    }

                    header.compressedSize -= 4;

                    compressor = getCompressor(Compressor::LZMA);
                    break;
                }

                case COMPRESSION_PPMD:
                {
                    compressor = getCompressor(Compressor::PPMD8);
                    break;
                }

                case COMPRESSION_BZIP2:
                {
                    compressor = getCompressor(Compressor::BZIP2);
                    break;
                }

                case COMPRESSION_ZSTD:
                {
                    compressor = getCompressor(Compressor::ZSTD);
                    break;
                }

                case COMPRESSION_SHRUNK:
                case COMPRESSION_REDUCE_1:
                case COMPRESSION_REDUCE_2:
                case COMPRESSION_REDUCE_3:
                case COMPRESSION_REDUCE_4:
                case COMPRESSION_IMPLODE:
                case COMPRESSION_DCLI:
                case COMPRESSION_DEFLATE64:
                case COMPRESSION_CMPSC:
                case COMPRESSION_TERSE:
                case COMPRESSION_LZ77:
                case COMPRESSION_MP3:
                case COMPRESSION_XZ:
                case COMPRESSION_WAVPACK:
                case COMPRESSION_JPEG:
                case COMPRESSION_AES:
                    MANGO_EXCEPTION("[mapper.zip] Unsupported compression algorithm (%d).", header.compression);
                    break;
            }

            if (compressor.decompress)
            {
                const size_t uncompressed_size = size_t(header.uncompressedSize);
                u8* uncompressed_buffer = new u8[uncompressed_size];

                ConstMemory input(address, size_t(header.compressedSize));
                Memory output(uncompressed_buffer, size_t(header.uncompressedSize));
                compressor.decompress(output, input);

                delete[] buffer;
                buffer = uncompressed_buffer;

                // use decode_buffer as memory map
                address = buffer;
                size = header.uncompressedSize;
            }

            VirtualMemory* memory = new VirtualMemoryZIP(address, buffer, size_t(size));
            return memory;
        }

        bool isFile(const std::string& filename) const override
        {
            const FileHeader* ptrHeader = m_folders.getHeader(filename);
            if (ptrHeader)
            {
                return !ptrHeader->is_folder;
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
                    u64 size = header.uncompressedSize;

                    if (header.is_folder)
                    {
                        flags |= FileInfo::DIRECTORY;
                        size = 0;
                    }

                    if (header.compression > 0)
                    {
                        flags |= FileInfo::COMPRESSED;
                    }

                    if (header.encryption != ENCRYPTION_NONE)
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
                MANGO_EXCEPTION("[mapper.zip] File \"%s\" not found.", filename.c_str());
            }

            const FileHeader& header = *ptrHeader;
            return mmap(header, m_parent_memory.address, m_password);
        }
    };

    // -----------------------------------------------------------------
    // functions
    // -----------------------------------------------------------------

    AbstractMapper* createMapperZIP(ConstMemory parent, const std::string& password)
    {
        AbstractMapper* mapper = new MapperZIP(parent, password);
        return mapper;
    }

} // namespace mango::filesystem
