/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2019 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <cctype>
#include <algorithm>
#include <locale>
#include <cstring>
#include <mango/core/string.hpp>
#include <mango/core/bits.hpp>

namespace
{
    using mango::u8;
    using mango::u32;

    /*
        WARNING!
        StringBuilder eliminates small-string construction overhead when
        the string is constructed one element at a time. A small temporary
        buffer is filled and flushed so that string does not have to
        continuously grow it's capacity. The constructed string is not
        a member so that optimizing compilers can utilize NRVO.

        Guardband is utilized so that insertions do not have to be checked
        individually and the loops can be unrolled. This requires intimate
        knowledge of the decoder feeding the buffer. Incorrectly configured
        guardband WILL cause buffer overflow; handle this code with care.
    */
    template <typename T, int SIZE, int GUARDBAND>
    struct StringBuilder
    {
        std::basic_string<T>& s;
        T buffer[SIZE + GUARDBAND];
        T* ptr;
        T* end;

        StringBuilder(std::basic_string<T>& str)
            : s(str), ptr(buffer), end(buffer + SIZE)
        {
        }

        ~StringBuilder()
        {
        }

        void ensure()
        {
            if (ptr >= end)
            {
                s.append(buffer, ptr);
                ptr = buffer;
            }
        }

        void flush()
        {
            s.append(buffer, ptr);
        }
    };

    template <typename T>
    inline std::vector<std::string> splitTemplate(const std::string& s, T delimiter)
    {
        std::vector<std::string> result;

        std::size_t current = 0;
        std::size_t p = s.find_first_of(delimiter, 0);

        while (p != std::string::npos)
        {
            result.emplace_back(s, current, p - current);
            current = p + 1;
            p = s.find_first_of(delimiter, current);
        }

        result.emplace_back(s, current);

        return result;
    }

    // -----------------------------------------------------------------
    // Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
    // See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
    // -----------------------------------------------------------------

    enum {
        UTF8_ACCEPT = 0,
        UTF8_REJECT = 12
    };

    inline u32 utf8_decode(u32& state, u32& code, u32 byte)
    {
        static const u8 utf8d[] = {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
            7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
            8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
            0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
            0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
            0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
            1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
            1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
            1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
            1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
        };

        const u32 type = utf8d[byte];

        code = (state != UTF8_ACCEPT) ? (byte & 0x3fu) | (code << 6) : (0xff >> type) & (byte);
        state = utf8d[256 + state * 16 + type];
        return state;
    }

    inline char* utf8_encode(char* ptr, u32 code)
    {
        const u32 mask = 0x3f;

        if (code < 0x80)
        {
            *ptr++ = code;
        }
        else if (code < 0x800)
        {
            ptr[1] = 0x80 | (code & mask); code >>= 6;
            ptr[0] = 0xc0 | code;
            ptr += 2;
        }
        else if (code < 0x10000)
        {
            ptr[2] = 0x80 | (code & mask); code >>= 6;
            ptr[1] = 0x80 | (code & mask); code >>= 6;
            ptr[0] = 0xe0 | code;
            ptr += 3;
        }
        else if (code < 0x200000)
        {
            ptr[3] = 0x80 | (code & mask); code >>= 6;
            ptr[2] = 0x80 | (code & mask); code >>= 6;
            ptr[1] = 0x80 | (code & mask); code >>= 6;
            ptr[0] = 0xf0 | code;
            ptr += 4;
        }

        return ptr;
    }

} // namespace

namespace mango
{

    // -----------------------------------------------------------------
    // unicode conversions
    // -----------------------------------------------------------------

    bool is_utf8(const std::string& source)
    {
        u32 code = 0;
        u32 state = 0;

        for (u8 c : source)
            utf8_decode(state, code, c);

        return state == UTF8_ACCEPT;
    }

    std::u32string utf32_from_utf8(const std::string& source)
    {
        std::u32string s;
        StringBuilder<char32_t, 256, 1> sb(s);

        u32 state = 0;
        u32 code = 0;

        for (u8 c : source)
        {
            if (!utf8_decode(state, code, c))
            {
                sb.ensure();
                *sb.ptr++ = code;
            }
        }

        sb.flush();
        return s;
    }

    std::string utf8_from_utf32(const std::u32string& source)
    {
        std::string s;
        StringBuilder<char, 128, 16> sb(s);

        const size_t length4 = source.length() & ~3;
        size_t i = 0;

        for (; i < length4; i += 4)
        {
            sb.ensure();
            sb.ptr = utf8_encode(sb.ptr, source[i + 0]);
            sb.ptr = utf8_encode(sb.ptr, source[i + 1]);
            sb.ptr = utf8_encode(sb.ptr, source[i + 2]);
            sb.ptr = utf8_encode(sb.ptr, source[i + 3]);
        }

        sb.ensure();
        for (; i < source.length(); ++i)
        {
            sb.ptr = utf8_encode(sb.ptr, source[i]);
        }

        sb.flush();
        return s;
    }

#if 1

    std::u16string utf16_from_utf8(const std::string& source)
    {
        std::u16string s;
        StringBuilder<char16_t, 256, 2> sb(s);

        u32 state = 0;
        u32 code = 0;

        for (u8 c : source)
        {
            if (!utf8_decode(state, code, c))
            {
                sb.ensure();

                // encode into temporary buffer
                if (code <= 0xffff)
                {
                    *sb.ptr++ = code;
                }
                else
                {
                    // encode code points above U+FFFF as surrogate pair
                    sb.ptr[0] = 0xd7c0 + (code >> 10);
                    sb.ptr[1] = 0xdc00 + (code & 0x3ff);
                    sb.ptr += 2;
                }
            }
        }

        sb.flush();
        return s;
    }

    std::string utf8_from_utf16(const std::u16string& source)
    {
        std::string s;
        StringBuilder<char, 256, 4> sb(s);

        const size_t length = source.length();

        for (size_t i = 0; i < length; ++i)
        {
            u32 code = source[i];

            // decode surrogate pair
            if ((code - 0xd800) < 0x400)
            {
                const u32 low = source[++i];

                if ((low - 0xdc00) < 0x400)
                {
                    code = ((code - 0xd800) << 10) + (low - 0xdc00) + 0x10000;
                }
            }

            sb.ensure();
            sb.ptr = utf8_encode(sb.ptr, code);
        }

        sb.flush();
        return s;
    }

#else

    std::u16string utf16_from_utf8(const std::string& source)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
        return convert.from_bytes(source);
    }

    std::string utf8_from_utf16(const std::u16string& source)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
        return convert.to_bytes(source);
    }

#endif

    std::string u16_toBytes(const std::wstring& source)
    {
        // TODO: validate against reference implementation
#if 0
        std::string s;
        StringBuilder<char, 256, 4> sb(s);

        const size_t length = source.length();

        for (size_t i = 0; i < length; ++i)
        {
            const u32 mask = 0x3f;
            const u32 code = source[i];

            sb.ensure();

            if (code < 0x80)
            {
                *sb.ptr++ = code;
            }
            else
            {
                sb.ptr[0] = 0xc0 | (code >> 6);
                sb.ptr[1] = 0x80 | (code & mask);
                sb.ptr += 2;
            }
        }

        sb.flush();
        return s;
#else
        const wchar_t* src = source.c_str();
        std::string s;

        while (*src)
        {
            u32 c = *src++;

            if (c < 0x80)
            {
                s.push_back(c);
            }
            else if (c < 0x800)
            {
                s.push_back(0xc0 | (c >> 6));
                s.push_back(0x80 | (c & 0x3f));
            }
            else if (c < 0x10000)
            {
                s.push_back(0xe0 | (c >> 12));
                s.push_back(0x80 | ((c >> 6) & 0x3f));
                s.push_back(0x80 | (c & 0x3f));
            }
            else if (c < 0x200000)
            {
                s.push_back(0xf0 | (c >> 18));
                s.push_back(0x80 | ((c >> 12) & 0x3f));
                s.push_back(0x80 | ((c >> 6) & 0x3f));
                s.push_back(0x80 | (c & 0x3f));
            }
        }

        return s;
#endif
    }

    std::wstring u16_fromBytes(const std::string& source)
    {
        // TODO: validate against reference implementation
#if 0
        std::wstring s;
        StringBuilder<wchar_t, 256, 1> sb(s);

        u32 state = 0;
        u32 code = 0;

        for (u8 c : source)
        {
            if (!utf8_decode(state, code, c))
            {
                if (code <= 0xffff)
                {
                    sb.ensure();
                    *sb.ptr++ = code;
                }
            }
        }

        sb.flush() ;
        return s;
#else
        const char* src = source.c_str();
        std::wstring s;

        while (*src)
        {
            u32 c = static_cast<u8>(*src++);
            u32 d;

            if (c < 0x80)
            {
                d = c;
            }
            else if ((c >> 5) == 6)
            {
                if ((*src & 0xc0) != 0x80)
                    break;
                d = ((c & 0x1f) << 6) | (*src & 0x3f);
                src++;
            }
            else if ((c >> 4) == 14)
            {
                if ((src[0] & 0xc0) != 0x80 || (src[1] & 0xc0) != 0x80)
                    break;
                d = ((c & 0xf) << 12) | ((src[0] & 0x3f) << 6) | (src[1] & 0x3f);
                src += 2;
            }
            else if ((c >> 3) == 30)
            {
                if ((src[0] & 0xc0) != 0x80 || (src[1] & 0xc0) != 0x80 || (src[2] & 0xc0) != 0x80)
                    break;
                d = ((c & 7) << 18) | ((src[0] & 0x3f) << 12) | ((src[1] & 0x3f) << 6) | (src[2] & 0x3f);
                src += 3;
            }
            else
            {
                // Ignore bad characters
                continue;
            }

            if (d > 0xffff)
            {
                if (d > 0x10ffff)
                    continue;
                s.push_back(((d - 0x10000) >> 10) + 0xd800);
                s.push_back((d & 0x3ff) + 0xdc00);
            }
            else
            {
                s.push_back(d);
            }
        }

        return s;
#endif
    }

    // -----------------------------------------------------------------
    // string utilities
    // -----------------------------------------------------------------

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    std::string toUpper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    }

    std::string removePrefix(const std::string& s, const std::string& prefix)
    {
        std::string temp = s;
        size_t pos = temp.find(prefix);
    
        if (pos != std::string::npos)
        {
            temp.erase(pos, prefix.length());
        }

        return temp;
    }

    bool isPrefix(const std::string& str, const std::string& prefix)
    {
        return str.length() > prefix.length() && !str.find(prefix, 0);
    }

    void replace(std::string& s, const std::string& from, const std::string& to)
    {
        if (!from.empty())
        {
            size_t start = 0;
            while ((start = s.find(from, start)) != std::string::npos)
            {
                s.replace(start, from.length(), to);
                start += to.length();
            }
        }
    }

    std::vector<std::string> split(const std::string& s, char delimiter)
    {
        return splitTemplate(s, delimiter);
    }

    std::vector<std::string> split(const std::string& s, const char* delimiter)
    {
        return splitTemplate(s, delimiter);
    }

    std::vector<std::string> split(const std::string& s, const std::string& delimiter)
    {
        return splitTemplate(s, delimiter);
    }

    std::string makeString(const char* format, ...)
    {
        constexpr size_t max_length = 512;
        char buffer[max_length];

        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, max_length, format, args);
        va_end(args);

        return buffer;
    }

    // ----------------------------------------------------------------------------
    // memchr()
    // ----------------------------------------------------------------------------

#if defined(MANGO_ENABLE_SSE2)

    const u8* memchr(const u8* p, u8 value, size_t count)
    {
        __m128i ref = _mm_set1_epi8(value);
        while (count >= 16)
        {
            __m128i v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(p));
            u32 mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, ref));
            if (mask)
            {
                int index = u32_tzcnt(mask);
                for (int i = index; i < 16; ++i)
                {
                    if (p[i] == value)
                        return p + i;
                }
            }
            count -= 16;
            p += 16;
        }

        for (size_t i = 0; i < count; ++i)
        {
            if (p[i] == value)
                return p + i;
        }

        return p;
    }

#else

    const u8* memchr(const u8* p, u8 value, size_t count)
    {
        p = reinterpret_cast<const u8 *>(std::memchr(p, value, count));
        return p ? p : p + count;
    }

#endif

} // namespace mango
