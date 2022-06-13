/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cstddef>
#include <mango/core/configure.hpp>
#include <mango/core/memory.hpp>
#include <mango/core/stream.hpp>

namespace mango
{

    class Buffer : public NonCopyable
    {
    private:
        Memory m_memory;
        size_t m_capacity;
        Alignment m_alignment;

    public:
        explicit Buffer(Alignment alignment = Alignment());
        explicit Buffer(size_t bytes, Alignment alignment = Alignment());
        explicit Buffer(size_t bytes, u8 value, Alignment alignment = Alignment());
        explicit Buffer(const u8* source, size_t bytes, Alignment alignment = Alignment());
        explicit Buffer(ConstMemory memory, Alignment alignment = Alignment());
        explicit Buffer(Stream& stream, Alignment alignment = Alignment());
        ~Buffer();

        operator ConstMemory () const;
        operator Memory () const;
        operator u8* () const;
        u8* data() const;

        size_t size() const;
        size_t capacity() const;

        void reset();
        void reset(size_t bytes);
        void reset(size_t bytes, u8 value);
        void resize(size_t bytes);
        void reserve(size_t bytes);
        void append(const void* source, size_t bytes);

    private:
        u8* allocate(size_t bytes, Alignment alignment) const;
        void free(u8* ptr) const;
    };

    class BufferStream : public Stream
    {
    private:
        Buffer m_buffer;
        u64 m_offset;

    public:
        BufferStream();
        BufferStream(const u8* source, u64 bytes);
        BufferStream(ConstMemory memory);
        ~BufferStream();

        operator ConstMemory () const;
        operator Memory () const;
        operator u8* () const;
        u8* data() const;

        u64 size() const override;
        u64 offset() const override;
        void seek(s64 distance, SeekMode mode) override;
        void read(void* dest, u64 bytes) override;
        void write(const void* source, u64 bytes) override;

        void write(ConstMemory memory)
        {
            Stream::write(memory);
        }
    };

} // namespace mango
