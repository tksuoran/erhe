/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/core/configure.hpp>
#include <mango/core/endian.hpp>
#include <mango/core/memory.hpp>

namespace mango
{

    namespace detail
    {

        // --------------------------------------------------------------
        // Pointer
        // --------------------------------------------------------------

        template <typename P>
        class Pointer
        {
        protected:
            P* p;

        public:
            explicit Pointer(const Pointer& pointer)
                : p(pointer.p)
            {
            }

            Pointer(P* address)
                : p(address)
            {
            }

            Pointer() = default;
            ~Pointer() = default;

            template <typename T>
            T* cast() const
            {
                return reinterpret_cast<T*>(p);
            }

            operator P* () const
            {
                return p;
            }

            P& operator * () const
            {
                return *p;
            }

            P* operator ++ ()
            {
                return ++p;
            }

            P* operator ++ (int)
            {
                return p++;
            }

            P* operator -- ()
            {
                return --p;
            }

            P* operator -- (int)
            {
                return p--;
            }

            P* operator += (size_t count)
            {
                p += count;
                return p;
            }

            P* operator -= (size_t count)
            {
                p -= count;
                return p;
            }
        };

        // --------------------------------------------------------------
        // ReadPointer
        // --------------------------------------------------------------

        template <typename P>
        class ReadPointer : public Pointer<P>
        {
        protected:
            using Pointer<P>::p;

        public:
            ReadPointer(P* address)
                : Pointer<P>(address)
            {
            }

            template <typename T>
            ReadPointer(const T* address)
                : Pointer<P>(reinterpret_cast<P*>(address))
            {
            }

            ReadPointer(mango::ConstMemory memory)
                : Pointer<P>(memory.address)
            {
            }

            void read(void* dest, size_t count)
            {
                std::memcpy(dest, p, count);
                p += count;
            }

            u8 read8()
            {
                return *p++;
            }

            u16 read16()
            {
                u16 value = uload16(p);
                p += 2;
                return value;
            }

            u32 read32()
            {
                u32 value = uload32(p);
                p += 4;
                return value;
            }

            u64 read64()
            {
                u64 value = uload64(p);
                p += 8;
                return value;
            }

            float16 read16f()
            {
                Half value;
                value.u = uload16(p);
                p += 2;
                return value;
            }

            float read32f()
            {
                Float value;
                value.u = uload32(p);
                p += 4;
                return value;
            }

            double read64f()
            {
                Double value;
                value.u = uload64(p);
                p += 8;
                return value;
            }
        };

        // --------------------------------------------------------------
        // ReadWritePointer
        // --------------------------------------------------------------

        template <typename P>
        class ReadWritePointer : public ReadPointer<P>
        {
        protected:
            using Pointer<P>::p;

        public:
            ReadWritePointer(P* address)
                : ReadPointer<P>(address)
            {
            }

            template <typename T>
            ReadWritePointer(T* address)
                : ReadPointer<P>(reinterpret_cast<P*>(address))
            {
            }

            ReadWritePointer(mango::Memory memory)
                : Pointer<P>(memory.address)
            {
            }

            void write(const void* source, size_t count)
            {
                std::memcpy(p, source, count);
                p += count;
            }

            void write(mango::Memory memory)
            {
                std::memcpy(p, memory.address, memory.size);
                p += memory.size;
            }

            void write8(u8 value)
            {
                *p++ = value;
            }

            void write16(u16 value)
            {
                ustore16(p, value);
                p += 2;
            }

            void write32(u32 value)
            {
                ustore32(p, value);
                p += 4;
            }

            void write64(u64 value)
            {
                ustore64(p, value);
                p += 8;
            }

            void write16f(Half value)
            {
                ustore16(p, value.u);
                p += 2;
            }

            void write32f(Float value)
            {
                ustore32(p, value.u);
                p += 4;
            }

            void write64f(Double value)
            {
                ustore64(p, value.u);
                p += 8;
            }
        };

        // --------------------------------------------------------------
        // SwapEndianReadPointer
        // --------------------------------------------------------------

        template <typename P>
        class SwapEndianReadPointer : public Pointer<P>
        {
        protected:
            using Pointer<P>::p;

        public:
            SwapEndianReadPointer(P* address)
                : Pointer<P>(address)
            {
            }

            template <typename T>
            SwapEndianReadPointer(const T* address)
                : Pointer<P>(reinterpret_cast<P*>(address))
            {
            }

            SwapEndianReadPointer(mango::ConstMemory memory)
                : Pointer<P>(memory.address)
            {
            }

            void read(void* dest, size_t count)
            {
                std::memcpy(dest, p, count);
                p += count;
            }

            u8 read8()
            {
                return *p++;
            }

            u16 read16()
            {
                u16 value = uload16swap(p);
                p += 2;
                return value;
            }

            u32 read32()
            {
                u32 value = uload32swap(p);
                p += 4;
                return value;
            }

            u64 read64()
            {
                u64 value = uload64swap(p);
                p += 8;
                return value;
            }

            float16 read16f()
            {
                Half value;
                value.u = uload16swap(p);
                p += 2;
                return value;
            }

            float read32f()
            {
                Float value;
                value.u = uload32swap(p);
                p += 4;
                return value;
            }

            double read64f()
            {
                Double value;
                value.u = uload64swap(p);
                p += 8;
                return value;
            }
        };

        // --------------------------------------------------------------
        // SwapEndianReadWritePointer
        // --------------------------------------------------------------

        template <typename P>
        class SwapEndianReadWritePointer : public SwapEndianReadPointer<P>
        {
        protected:
            using Pointer<P>::p;

        public:
            SwapEndianReadWritePointer(P* address)
                : SwapEndianReadPointer<P>(address)
            {
            }

            template <typename T>
            SwapEndianReadWritePointer(T* address)
                : SwapEndianReadPointer<P>(reinterpret_cast<P*>(address))
            {
            }

            SwapEndianReadWritePointer(mango::Memory memory)
                : Pointer<P>(memory.address)
            {
            }

            void write(const void* source, size_t count)
            {
                std::memcpy(p, source, count);
                p += count;
            }

            void write(mango::Memory memory)
            {
                std::memcpy(p, memory.address, memory.size);
                p += memory.size;
            }

            void write8(u8 value)
            {
                *p++ = value;
            }

            void write16(u16 value)
            {
                ustore16swap(p, value);
                p += 2;
            }

            void write32(u32 value)
            {
                ustore32swap(p, value);
                p += 4;
            }

            void write64(u64 value)
            {
                ustore64swap(p, value);
                p += 8;
            }

            void write16f(Half value)
            {
                ustore16swap(p, value.u);
                p += 2;
            }

            void write32f(Float value)
            {
                ustore32swap(p, value.u);
                p += 4;
            }

            void write64f(Double value)
            {
                ustore64swap(p, value.u);
                p += 8;
            }
        };

    } // namespace detail

    // --------------------------------------------------------------
    // pointer types
    // --------------------------------------------------------------

    using Pointer = detail::ReadWritePointer<u8>;
    using ConstPointer = detail::ReadPointer<const u8>;

    using SwapEndianPointer = detail::SwapEndianReadWritePointer<u8>;
    using SwapEndianConstPointer = detail::SwapEndianReadPointer<const u8>;

#ifdef MANGO_LITTLE_ENDIAN

    using LittleEndianPointer = Pointer;
    using LittleEndianConstPointer = ConstPointer;

    using BigEndianPointer = SwapEndianPointer;
    using BigEndianConstPointer = SwapEndianConstPointer;

#else

    using LittleEndianPointer = SwapEndianPointer;
    using LittleEndianConstPointer = SwapEndianConstPointer;

    using BigEndianPointer = Pointer;
    using BigEndianConstPointer = ConstPointer;

#endif

} // namespace mango
