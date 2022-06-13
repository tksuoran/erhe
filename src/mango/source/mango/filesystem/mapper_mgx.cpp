/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/core.hpp>
#include <mango/filesystem/filesystem.hpp>
#include <mango/image/fourcc.hpp>
#include "indexer.hpp"

namespace
{
    using namespace mango;
    namespace fs = mango::filesystem;

    constexpr u64 mgx_header_size = 24;

    struct Block
    {
        u64 offset;
        u64 compressed;
        u64 uncompressed;
        u32 method;
    };

    struct FileHeader
    {
        struct Segment
        {
            u32 block;
            u32 offset;
            u32 size;
        };

        u64 size;
        u32 checksum;
        bool is_compressed;
        std::vector<Segment> segments;
        std::string filename;

        bool isCompressed() const
        {
            return is_compressed;
        }

        bool isMultiSegment() const
        {
            return segments.size() > 1;
        }

        bool isFolder() const
        {
            return segments.empty();
        }
    };

    struct HeaderMGX
    {
        ConstMemory m_memory;
        fs::Indexer<FileHeader> m_folders;
        std::vector<Block> m_blocks;

        HeaderMGX(ConstMemory memory)
            : m_memory(memory)
        {
            if (!memory.address)
            {
                //MANGO_EXCEPTION("[mapper.mgx] Parent container doesn't have memory");
                return;
            }

            LittleEndianConstPointer p = memory.address;
            u32 magic0 = p.read32();
            if (magic0 != u32_mask('m', 'g', 'x', '0'))
            {
                //MANGO_EXCEPTION("[mapper.mgx] Incorrect file identifier (%x)", magic0);
                return;
            }

            u64 header_offset = memory.size - mgx_header_size;
            p = memory.address + header_offset;

            u32 magic3 = p.read32();
            if (magic3 != u32_mask('m', 'g', 'x', '3'))
            {
                //MANGO_EXCEPTION("[mapper.mgx] Incorrect header identifier (%x)", magic3);
                return;
            }

            u32 version = p.read32();
            u64 block_offset = p.read64();
            u64 file_offset = p.read64();

            parseBlocks(memory.address + block_offset);
            parseFiles(memory.address + file_offset);

            MANGO_UNREFERENCED(version);
        }

        void parseBlocks(LittleEndianConstPointer p)
        {
            u32 magic1 = p.read32();
            if (magic1 != u32_mask('m', 'g', 'x', '1'))
            {
                MANGO_EXCEPTION("[mapper.mgx] Incorrect block identifier (%x)", magic1);
            }

            u32 num_blocks = p.read32();
            for (u32 i = 0; i < num_blocks; ++i)
            {
                Block block;
                block.offset = p.read64();
                block.compressed = p.read64();
                block.uncompressed = p.read64();
                block.method = p.read32();

                m_blocks.push_back(block);
            }

            u32 magic2 = p.read32();
            if (magic2 != u32_mask('m', 'g', 'x', '2'))
            {
                MANGO_EXCEPTION("[mapper.mgx] Incorrect block terminator (%x)", magic2);
            }
        }

        void parseFiles(LittleEndianConstPointer p)
        {
            u32 magic2 = p.read32();
            if (magic2 != u32_mask('m', 'g', 'x', '2'))
            {
                MANGO_EXCEPTION("[mapper.mgx] Incorrect block identifier (%x)", magic2);
            }

            u32 num_files = p.read32();
            for (u32 i = 0; i < num_files; ++i)
            {
                FileHeader header;

                u32 length = p.read32();
                const u8* ptr = p;
                std::string filename(reinterpret_cast<const char *>(ptr), length);
                p += length;

                header.size = p.read64();
                header.checksum = p.read32();
                header.is_compressed = false;

                u32 num_segment = p.read32();
                for (u32 j = 0; j < num_segment; ++j)
                {
                    u32 block_idx = p.read32();
                    u32 offset = p.read32();
                    u32 size = p.read32();
                    header.segments.push_back({block_idx, offset, size});

                    // inspect block
                    Block& block = m_blocks[block_idx];
                    if (block.method > 0)
                    {
                        // if ANY of the blocks in the file segments is compressed
                        // the whole file is considered compressed (= cannot be mapped directly)
                        header.is_compressed = true;
                    }
                }

                std::string folder = header.isFolder() ?
                    fs::getPath(filename.substr(0, length - 1)) :
                    fs::getPath(filename);

                header.filename = filename.substr(folder.length());
                m_folders.insert(folder, filename, header);
            }

            u32 magic3 = p.read32();
            if (magic3 != u32_mask('m', 'g', 'x', '3'))
            {
                MANGO_EXCEPTION("[mapper.mgx] Incorrect block terminator (%x)", magic3);
            }
        }
    };

} // namespace

namespace mango::filesystem
{

    // -----------------------------------------------------------------
    // VirtualMemoryMGX
    // -----------------------------------------------------------------

    class VirtualMemoryMGX : public mango::VirtualMemory
    {
    protected:
        const u8* m_delete_address;

    public:
        VirtualMemoryMGX(const u8* address, const u8* delete_address, size_t size)
            : m_delete_address(delete_address)
        {
            m_memory = ConstMemory(address, size);
        }

        ~VirtualMemoryMGX()
        {
            delete [] m_delete_address;
        }
    };

    // -----------------------------------------------------------------
    // MapperMGX
    // -----------------------------------------------------------------

    class MapperMGX : public AbstractMapper
    {
    public:
        HeaderMGX m_header;
        std::string m_password;

    public:
        MapperMGX(ConstMemory parent, const std::string& password)
            : m_header(parent)
            , m_password(password)
        {
        }

        bool isFile(const std::string& filename) const override
        {
            const FileHeader* ptrHeader = m_header.m_folders.getHeader(filename);
            if (ptrHeader)
            {
                return !ptrHeader->isFolder();
            }
            return false;
        }

        void getIndex(FileIndex& index, const std::string& pathname) override
        {
            const fs::Indexer<FileHeader>::Folder* ptrFolder = m_header.m_folders.getFolder(pathname);
            if (ptrFolder)
            {
                for (auto i : ptrFolder->headers)
                {
                    const FileHeader& header = *i.second;

                    u32 flags = 0;

                    if (header.isFolder())
                    {
                        flags |= FileInfo::DIRECTORY;
                    }

                    if (header.isCompressed())
                    {
                        flags |= FileInfo::COMPRESSED;
                    }

                    index.emplace(header.filename, header.size, flags);
                }
            }
        }

        VirtualMemory* mmap(const std::string& filename) override
        {
            const FileHeader* ptrHeader = m_header.m_folders.getHeader(filename);
            if (!ptrHeader)
            {
                MANGO_EXCEPTION("[mapper.mgx] File \"%s\" not found.", filename.c_str());
            }

            const FileHeader& file = *ptrHeader;

            // TODO: compute segment.size instead of storing it in .mgx container
            // TODO: checksum
            // TODO: encryption

            if (!file.isMultiSegment())
            {
                const auto& segment = file.segments[0];
                Block& block = m_header.m_blocks[segment.block];

                if (file.isCompressed())
                {
                    if (segment.size != block.uncompressed && !file.isMultiSegment())
                    {
                        // a small file stored in one block with other small files
                        // cache the decompressed buffers

                        // TODO: decompression cache for small-file blocks
                        // NOTE: this will fall-through into the generic compression case for now
#if 0
                        // simulate almost-zero-cost (AZC) decompression
                        u8* ptr = new u8[file.size];
                        std::memset(ptr, 0, file.size);
                        VirtualMemoryMGX* vm = new VirtualMemoryMGX(ptr, ptr, file.size);
                        return vm;
#endif
                    }
                }
                else
                {
                    // The file is encoded as a single, non-compressed block, so
                    // we can simply map it into parent's memory

                    const u8* ptr = m_header.m_memory.address + block.offset + segment.offset;

                    if (block.offset + segment.offset + file.size > m_header.m_memory.size)
                    {
                        MANGO_EXCEPTION("[mapper.mgx] File \"%s\" has mapped region outside of parent memory.", filename.c_str());
                    }

                    VirtualMemoryMGX* vm = new VirtualMemoryMGX(ptr, nullptr, size_t(file.size));
                    return vm;
                }
            }

            // generic compression case

            u8* ptr = new u8[size_t(file.size)];
            u8* x = ptr;

            ConcurrentQueue q("mgx.decompessor", Priority::HIGH);

            for (auto &segment : file.segments)
            {
                const Block& block = m_header.m_blocks[segment.block];

                if (block.method)
                {
                    Compressor compressor = getCompressor(Compressor::Method(block.method));
                    ConstMemory src(m_header.m_memory.address + block.offset, size_t(block.compressed));

                    q.enqueue([=, &block, &segment]
                    {
                        if (block.uncompressed == segment.size && segment.offset == 0)
                        {
                            // segment is full-block so we can decode directly w/o intermediate buffer
                            Memory dest(x, size_t(block.uncompressed));
                            compressor.decompress(dest, src);
                        }
                        else
                        {
                            Buffer dest(size_t(block.uncompressed));
                            compressor.decompress(dest, src);
                            std::memcpy(x, Memory(dest).address + segment.offset, segment.size);
                        }
                    });
                }
                else
                {
                    q.enqueue([=, &block, &segment]
                    {
                        // no compression
                        std::memcpy(x, m_header.m_memory.address + block.offset + segment.offset, segment.size);
                    });
                }

                x += segment.size;
            }

            q.wait();

            VirtualMemoryMGX* vm = new VirtualMemoryMGX(ptr, ptr, size_t(file.size));
            return vm;
        }
    };

    // -----------------------------------------------------------------
    // functions
    // -----------------------------------------------------------------

    AbstractMapper* createMapperMGX(ConstMemory parent, const std::string& password)
    {
        AbstractMapper* mapper = new MapperMGX(parent, password);
        return mapper;
    }

} // namespace mango::filesystem
