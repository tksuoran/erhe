/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <memory>
#include <limits>
#include <algorithm>
#include <mango/core/configure.hpp>

namespace mango
{

    namespace detail
    {

        template <typename T>
        struct Memory
        {
            T* address;
            size_t size;

            Memory()
                : address(nullptr)
                , size(0)
            {
            }

            Memory(T* address, size_t bytes)
                : address(address)
                , size(bytes)
            {
            }

            operator T* () const
            {
                return address;
            }

            template <typename S>
            operator Memory<S> () const
            {
                return Memory<S>(address, size);
            }

            template <typename S>
            S* cast() const
            {
                return reinterpret_cast<S*>(address);
            }

            Memory slice(size_t slice_offset, size_t slice_size = 0) const
            {
                Memory memory(address + slice_offset, size - slice_offset);
                if (slice_size)
                {
                    memory.size = std::min(memory.size, slice_size);
                }
                return memory;
            }
        };

    } // namespace detail

    // -----------------------------------------------------------------------
    // NonCopyable
    // -----------------------------------------------------------------------

    class NonCopyable
    {
    protected:
        NonCopyable() = default;

    private:
        NonCopyable(const NonCopyable&) = delete;
        NonCopyable& operator = (const NonCopyable&) = delete;
    };

    // -----------------------------------------------------------------------
    // memory
    // -----------------------------------------------------------------------

    using Memory = detail::Memory<u8>;
    using ConstMemory = detail::Memory<const u8>;

    class SharedMemory
    {
    private:
        Memory m_memory;
        std::shared_ptr<u8> m_ptr;

    public:
        SharedMemory(size_t bytes);
        SharedMemory(u8* address, size_t bytes);

        operator Memory () const
        {
            return m_memory;
        }
    };

    class VirtualMemory : private NonCopyable
    {
    protected:
        ConstMemory m_memory;

    public:
        VirtualMemory() = default;
        virtual ~VirtualMemory() {}

        const ConstMemory* operator -> () const
        {
            return &m_memory;
        }

        operator ConstMemory () const
        {
            return m_memory;
        }
    };

    // -----------------------------------------------------------------------
    // Alignment
    // -----------------------------------------------------------------------

    // NOTE: The alignment has to be a power-of-two and at least sizeof(void*)

    class Alignment
    {
    protected:
        u32 m_alignment;

    public:
        Alignment(); // default alignment
        Alignment(u32 alignment);

        operator u32 () const;
    };

    // -----------------------------------------------------------------------
    // aligned malloc / free
    // -----------------------------------------------------------------------

    void* aligned_malloc(size_t bytes, Alignment alignment = Alignment());
    void aligned_free(void* aligned);

    template <typename T>
    T* aligned_alloc(size_t size, u32 alignment)
    {
        return reinterpret_cast<T*>(aligned_malloc(size * sizeof(T), Alignment(alignment)));
    }

    // -----------------------------------------------------------------------
    // AlignedStorage
    // -----------------------------------------------------------------------

    template <typename T>
    class AlignedStorage : public NonCopyable
    {
    private:
        T* m_data;
        size_t m_size;

        void allocate(size_t size, Alignment alignment)
        {
            m_data = aligned_alloc<T>(size, alignment);
            m_size = size;
        }

        void clear()
        {
            if (m_data)
            {
                aligned_free(m_data);
                m_data = nullptr;
                m_size = 0;
            }
        }

    public:
        AlignedStorage()
            : m_data(nullptr)
            , m_size(0)
        {
        }

        AlignedStorage(size_t size, Alignment alignment = Alignment())
        {
            allocate(size, alignment);
        }

        ~AlignedStorage()
        {
            clear();
        }

        void resize(size_t size, Alignment alignment = Alignment())
        {
            if (size != m_size)
            {
                clear();

                if (size)
                {
                    allocate(size, alignment);
                }
            }
        }

        operator T* () const
        {
            return m_data;
        }

        T* data() const
        {
            return m_data;
        }

        size_t size() const
        {
            return m_size;
        }

        T& operator [] (int index)
        {
            return m_data[index];
        }

        const T& operator [] (int index) const
        {
            return m_data[index];
        }

        T* begin()
        {
            return m_data;
        }

        T* end()
        {
            return m_data + m_size;
        }

        const T* begin() const
        {
            return m_data;
        }

        const T* end() const
        {
            return m_data + m_size;
        }
    };

    // -----------------------------------------------------------------------
    // align_pointer
    // -----------------------------------------------------------------------

    template <size_t alignment>
    constexpr inline
    uintptr_t align_pointer(uintptr_t pointer)
    {
        static_assert((alignment & (alignment - 1)) == 0, "alignment must be a power of two.");
        constexpr size_t mask = alignment - 1;
        return (pointer + mask) & ~mask;
    }

    template <size_t alignment>
    constexpr inline
    const u8* align_pointer(const u8* pointer)
    {
        uintptr_t p = align_pointer<alignment>(reinterpret_cast<uintptr_t>(pointer));
        return reinterpret_cast<const u8*>(p);
    }

    template <size_t alignment>
    constexpr inline
    u8* align_pointer(u8* pointer)
    {
        uintptr_t p = align_pointer<alignment>(reinterpret_cast<uintptr_t>(pointer));
        return reinterpret_cast<u8*>(p);
    }

} // namespace mango
