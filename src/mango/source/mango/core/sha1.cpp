/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2019 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/hash.hpp>
#include <mango/core/exception.hpp>
#include <mango/core/bits.hpp>
#include <mango/core/endian.hpp>
#include <mango/core/cpuinfo.hpp>

namespace
{
    using namespace mango;

#if defined(__ARM_FEATURE_CRYPTO)

    // ----------------------------------------------------------------------------------------
    // ARM Crypto SHA1
    // ----------------------------------------------------------------------------------------

#define ROUND0(K, A, B, C, D) \
    a = vgetq_lane_u32(abcd, 0);       \
    e0 = vsha1h_u32(a);                \
    abcd = vsha1cq_u32(abcd, e1, wk1); \
    wk1 = vaddq_u32(w3, K);            \
    A = vsha1su1q_u32(A, D);           \
    B = vsha1su0q_u32(B, C, D);

#define ROUND1(K, A, B, C, D) \
    a = vgetq_lane_u32(abcd, 0);       \
    e1 = vsha1h_u32(a);                \
    abcd = vsha1cq_u32(abcd, e0, wk0); \
    wk0 = vaddq_u32(w0, K);            \
    A = vsha1su1q_u32(A, D);           \
    B = vsha1su0q_u32(B, C, D);

    void arm_sha1_update(u32 state[5], const u8* block, int count)
    {
        // set K0..K3 constants
        uint32x4_t k0 = vdupq_n_u32(0x5A827999);
        uint32x4_t k1 = vdupq_n_u32(0x6ED9EBA1);
        uint32x4_t k2 = vdupq_n_u32(0x8F1BBCDC);
        uint32x4_t k3 = vdupq_n_u32(0xCA62C1D6);

        // load state
        uint32x4_t abcd = vld1q_u32(state);
        uint32_t e = state[4];

        for (int i = 0; i < count; ++i)
        {
            uint32x4_t abcd0 = abcd;

            // load message
            uint32x4_t w0 = vld1q_u32(reinterpret_cast<const uint32_t *>(block +  0));
            uint32x4_t w1 = vld1q_u32(reinterpret_cast<const uint32_t *>(block + 16));
            uint32x4_t w2 = vld1q_u32(reinterpret_cast<const uint32_t *>(block + 32));
            uint32x4_t w3 = vld1q_u32(reinterpret_cast<const uint32_t *>(block + 48));
            block += 64;

#ifdef MANGO_LITTLE_ENDIAN
            w0 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(w0)));
            w1 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(w1)));
            w2 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(w2)));
            w3 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(w3)));
#endif

            // initialize wk0, wk1
            uint32x4_t wk0 = vaddq_u32(w0, k0);
            uint32x4_t wk1 = vaddq_u32(w1, k0);

            uint32_t a, e0, e1;

            a = vgetq_lane_u32(abcd, 0);
            e1 = vsha1h_u32(a);
            abcd = vsha1cq_u32(abcd, e, wk0);
            wk0 = vaddq_u32(w2, k0);
            w0 = vsha1su0q_u32(w0, w1, w2);

            ROUND0(k0, w0, w1, w2, w3);
            ROUND1(k0, w1, w2, w3, w0);
            ROUND0(k1, w2, w3, w0, w1);
            ROUND1(k1, w3, w0, w1, w2);
            ROUND0(k1, w0, w1, w2, w3);
            ROUND1(k1, w1, w2, w3, w0);
            ROUND0(k1, w2, w3, w0, w1);
            ROUND1(k2, w3, w0, w1, w2);
            ROUND0(k2, w0, w1, w2, w3);
            ROUND1(k2, w1, w2, w3, w0);
            ROUND0(k2, w2, w3, w0, w1);
            ROUND1(k2, w3, w0, w1, w2);
            ROUND0(k3, w0, w1, w2, w3);
            ROUND1(k3, w1, w2, w3, w0);
            ROUND0(k3, w2, w3, w0, w1);
            ROUND1(k3, w3, w0, w1, w2);

            a = vgetq_lane_u32(abcd, 0);
            e0 = vsha1h_u32(a);
            abcd = vsha1pq_u32(abcd, e1, wk1);
            wk1 = vaddq_u32(w3, k3);
            w0 = vsha1su1q_u32(w0, w3);

            a = vgetq_lane_u32(abcd, 0);
            e1 = vsha1h_u32(a);
            abcd = vsha1pq_u32(abcd, e0, wk0);

            a = vgetq_lane_u32(abcd, 0);
            e0 = vsha1h_u32(a);
            abcd = vsha1pq_u32(abcd, e1, wk1);

            // update state
            abcd = vaddq_u32(abcd, abcd0);
            e += e0;
        }

        // store state
        vst1q_u32(state, abcd);
        state[4] = e;
    }

#undef ROUND0
#undef ROUND1

#elif defined(MANGO_ENABLE_SHA)

    /*******************************************************************************
    * Copyright (c) 2013, Intel Corporation 
    * 
    * All rights reserved. 
    * 
    * Redistribution and use in source and binary forms, with or without
    * modification, are permitted provided that the following conditions are
    * met: 
    * 
    * * Redistributions of source code must retain the above copyright
    *   notice, this list of conditions and the following disclaimer.  
    * 
    * * Redistributions in binary form must reproduce the above copyright
    *   notice, this list of conditions and the following disclaimer in the
    *   documentation and/or other materials provided with the
    *   distribution. 
    * 
    * * Neither the name of the Intel Corporation nor the names of its
    *   contributors may be used to endorse or promote products derived from
    *   this software without specific prior written permission. 
    * 
    * 
    * THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
    * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
    * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
    * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
    * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
    * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
    ********************************************************************************
    *
    * Intel SHA Extensions optimized implementation of a SHA-1 update function 
    * 
    * The function takes a pointer to the current hash values, a pointer to the 
    * input data, and a number of 64 byte blocks to process.  Once all blocks have 
    * been processed, the digest pointer is  updated with the resulting hash value.
    * The function only processes complete blocks, there is no functionality to 
    * store partial blocks.  All message padding and hash value initialization must
    * be done outside the update function.  
    * 
    * The indented lines in the loop are instructions related to rounds processing.
    * The non-indented lines are instructions related to the message schedule.
    * 
    * Author: Sean Gulley <sean.m.gulley@intel.com>
    * Date:   July 2013
    *
    *******************************************************************************/

    void intel_sha1_update(u32 *digest, const u8 *data, int num_blks)
    {
        __m128i abcd, e0, e1;
        __m128i abcd_save, e_save;
        __m128i msg0, msg1, msg2, msg3;

        __m128i e_mask    = _mm_set_epi64x(0xFFFFFFFF00000000ull, 0x0000000000000000ull);
        __m128i shuf_mask = _mm_set_epi64x(0x0001020304050607ull, 0x08090a0b0c0d0e0full);

        // Load initial hash values
        e0        = _mm_setzero_si128();
        abcd      = _mm_loadu_si128((__m128i*) digest);
        e0        = _mm_insert_epi32(e0, *(digest+4), 3);
        abcd      = _mm_shuffle_epi32(abcd, 0x1B);
        e0        = _mm_and_si128(e0, e_mask);

        while (num_blks > 0) {
            // Save hash values for addition after rounds
            abcd_save = abcd;
            e_save    = e0;

            // Rounds 0-3
            msg0 = _mm_loadu_si128((__m128i*) data);
            msg0 = _mm_shuffle_epi8(msg0, shuf_mask);
            e0   = _mm_add_epi32(e0, msg0);
            e1   = abcd;
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);

            // Rounds 4-7
            msg1 = _mm_loadu_si128((__m128i*) (data+16));
            msg1 = _mm_shuffle_epi8(msg1, shuf_mask);
            e1   = _mm_sha1nexte_epu32(e1, msg1);
            e0   = abcd;
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
            msg0 = _mm_sha1msg1_epu32(msg0, msg1);

            // Rounds 8-11
            msg2 = _mm_loadu_si128((__m128i*) (data+32));
            msg2 = _mm_shuffle_epi8(msg2, shuf_mask);
            e0   = _mm_sha1nexte_epu32(e0, msg2);
            e1   = abcd;
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
            msg1 = _mm_sha1msg1_epu32(msg1, msg2);
            msg0 = _mm_xor_si128(msg0, msg2);

            // Rounds 12-15
            msg3 = _mm_loadu_si128((__m128i*) (data+48));
            msg3 = _mm_shuffle_epi8(msg3, shuf_mask);
            e1   = _mm_sha1nexte_epu32(e1, msg3);
            e0   = abcd;
            msg0 = _mm_sha1msg2_epu32(msg0, msg3);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
            msg2 = _mm_sha1msg1_epu32(msg2, msg3);
            msg1 = _mm_xor_si128(msg1, msg3);

            // Rounds 16-19
            e0   = _mm_sha1nexte_epu32(e0, msg0);
            e1   = abcd;
            msg1 = _mm_sha1msg2_epu32(msg1, msg0);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
            msg3 = _mm_sha1msg1_epu32(msg3, msg0);
            msg2 = _mm_xor_si128(msg2, msg0);

            // Rounds 20-23
            e1   = _mm_sha1nexte_epu32(e1, msg1);
            e0   = abcd;
            msg2 = _mm_sha1msg2_epu32(msg2, msg1);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
            msg0 = _mm_sha1msg1_epu32(msg0, msg1);
            msg3 = _mm_xor_si128(msg3, msg1);
            
            // Rounds 24-27
            e0   = _mm_sha1nexte_epu32(e0, msg2);
            e1   = abcd;
            msg3 = _mm_sha1msg2_epu32(msg3, msg2);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
            msg1 = _mm_sha1msg1_epu32(msg1, msg2);
            msg0 = _mm_xor_si128(msg0, msg2);

            // Rounds 28-31
            e1   = _mm_sha1nexte_epu32(e1, msg3);
            e0   = abcd;
            msg0 = _mm_sha1msg2_epu32(msg0, msg3);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
            msg2 = _mm_sha1msg1_epu32(msg2, msg3);
            msg1 = _mm_xor_si128(msg1, msg3);

            // Rounds 32-35
            e0   = _mm_sha1nexte_epu32(e0, msg0);
            e1   = abcd;
            msg1 = _mm_sha1msg2_epu32(msg1, msg0);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 1);
            msg3 = _mm_sha1msg1_epu32(msg3, msg0);
            msg2 = _mm_xor_si128(msg2, msg0);

            // Rounds 36-39
            e1   = _mm_sha1nexte_epu32(e1, msg1);
            e0   = abcd;
            msg2 = _mm_sha1msg2_epu32(msg2, msg1);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
            msg0 = _mm_sha1msg1_epu32(msg0, msg1);
            msg3 = _mm_xor_si128(msg3, msg1);
            
            // Rounds 40-43
            e0   = _mm_sha1nexte_epu32(e0, msg2);
            e1   = abcd;
            msg3 = _mm_sha1msg2_epu32(msg3, msg2);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
            msg1 = _mm_sha1msg1_epu32(msg1, msg2);
            msg0 = _mm_xor_si128(msg0, msg2);

            // Rounds 44-47
            e1   = _mm_sha1nexte_epu32(e1, msg3);
            e0   = abcd;
            msg0 = _mm_sha1msg2_epu32(msg0, msg3);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
            msg2 = _mm_sha1msg1_epu32(msg2, msg3);
            msg1 = _mm_xor_si128(msg1, msg3);

            // Rounds 48-51
            e0   = _mm_sha1nexte_epu32(e0, msg0);
            e1   = abcd;
            msg1 = _mm_sha1msg2_epu32(msg1, msg0);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
            msg3 = _mm_sha1msg1_epu32(msg3, msg0);
            msg2 = _mm_xor_si128(msg2, msg0);

            // Rounds 52-55
            e1   = _mm_sha1nexte_epu32(e1, msg1);
            e0   = abcd;
            msg2 = _mm_sha1msg2_epu32(msg2, msg1);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
            msg0 = _mm_sha1msg1_epu32(msg0, msg1);
            msg3 = _mm_xor_si128(msg3, msg1);
            
            // Rounds 56-59
            e0   = _mm_sha1nexte_epu32(e0, msg2);
            e1   = abcd;
            msg3 = _mm_sha1msg2_epu32(msg3, msg2);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 2);
            msg1 = _mm_sha1msg1_epu32(msg1, msg2);
            msg0 = _mm_xor_si128(msg0, msg2);

            // Rounds 60-63
            e1   = _mm_sha1nexte_epu32(e1, msg3);
            e0   = abcd;
            msg0 = _mm_sha1msg2_epu32(msg0, msg3);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
            msg2 = _mm_sha1msg1_epu32(msg2, msg3);
            msg1 = _mm_xor_si128(msg1, msg3);

            // Rounds 64-67
            e0   = _mm_sha1nexte_epu32(e0, msg0);
            e1   = abcd;
            msg1 = _mm_sha1msg2_epu32(msg1, msg0);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);
            msg3 = _mm_sha1msg1_epu32(msg3, msg0);
            msg2 = _mm_xor_si128(msg2, msg0);

            // Rounds 68-71
            e1   = _mm_sha1nexte_epu32(e1, msg1);
            e0   = abcd;
            msg2 = _mm_sha1msg2_epu32(msg2, msg1);
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
            msg3 = _mm_xor_si128(msg3, msg1);
            
            // Rounds 72-75
            e0   = _mm_sha1nexte_epu32(e0, msg2);
            e1   = abcd;
            msg3 = _mm_sha1msg2_epu32(msg3, msg2);
            abcd = _mm_sha1rnds4_epu32(abcd, e0, 3);

            // Rounds 76-79
            e1   = _mm_sha1nexte_epu32(e1, msg3);
            e0   = abcd;
            abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

            // Add current hash values with previously saved
            e0   = _mm_sha1nexte_epu32(e0, e_save);
            abcd = _mm_add_epi32(abcd, abcd_save);

            data += 64;
            num_blks--;
        }

        abcd = _mm_shuffle_epi32(abcd, 0x1B);
        _mm_store_si128((__m128i*) digest, abcd);
        *(digest+4) = _mm_extract_epi32(e0, 3);
    }

#endif

    // ----------------------------------------------------------------------------------------
    // Generic C++ SHA1
    // ----------------------------------------------------------------------------------------

#define SHW(index) \
    u32 x = (w[(index -  3) & 15] ^ \
             w[(index -  8) & 15] ^ \
             w[(index - 14) & 15] ^ \
             w[(index - 16) & 15]); \
    x = (x << 1) | (x >> 31); \
    w[index & 15] = x;

#define PASS0(a, b, c, d, e, index) { \
    u32 x = uload32be(block + index * 4); \
    w[index] = x; \
    e = ((a << 5) | (a >> 27)) + ((b & c) | ((~b) & d)) + e + 0x5A827999 + x; } \
    b = (b << 30) | (b >> 2)

#define PASS1(a, b, c, d, e, index) { \
    SHW(index); \
    e = ((a << 5) | (a >> 27)) + ((b & c) | ((~b) & d)) + e + 0x5A827999 + x; } \
    b = (b << 30) | (b >> 2)

#define PASS2(a, b, c, d, e, index) { \
    SHW(index); \
    e = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + 0x6ED9EBA1 + x; } \
    b = (b << 30) | (b >> 2)

#define PASS3(a, b, c, d, e, index) { \
    SHW(index); \
    e = ((a << 5) | (a >> 27)) + ((b & c) | (b & d) | (c & d)) + e + 0x8F1BBCDC + x; } \
    b = (b << 30) | (b >> 2)

#define PASS4(a, b, c, d, e, index) { \
    SHW(index); \
    e = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + 0xCA62C1D6 + x; } \
    b = (b << 30) | (b >> 2)

    void generic_sha1_update(u32 state[5], const u8* block, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            u32 a = state[0];
            u32 b = state[1];
            u32 c = state[2];
            u32 d = state[3];
            u32 e = state[4];

            u32 w[16];

            PASS0(a, b, c, d, e, 0);
            PASS0(e, a, b, c, d, 1);
            PASS0(d, e, a, b, c, 2);
            PASS0(c, d, e, a, b, 3);
            PASS0(b, c, d, e, a, 4);
            PASS0(a, b, c, d, e, 5);
            PASS0(e, a, b, c, d, 6);
            PASS0(d, e, a, b, c, 7);
            PASS0(c, d, e, a, b, 8);
            PASS0(b, c, d, e, a, 9);
            PASS0(a, b, c, d, e, 10);
            PASS0(e, a, b, c, d, 11);
            PASS0(d, e, a, b, c, 12);
            PASS0(c, d, e, a, b, 13);
            PASS0(b, c, d, e, a, 14);
            PASS0(a, b, c, d, e, 15);

            PASS1(e, a, b, c, d, 16);
            PASS1(d, e, a, b, c, 17);
            PASS1(c, d, e, a, b, 18);
            PASS1(b, c, d, e, a, 19);

            PASS2(a, b, c, d, e, 20);
            PASS2(e, a, b, c, d, 21);
            PASS2(d, e, a, b, c, 22);
            PASS2(c, d, e, a, b, 23);
            PASS2(b, c, d, e, a, 24);
            PASS2(a, b, c, d, e, 25);
            PASS2(e, a, b, c, d, 26);
            PASS2(d, e, a, b, c, 27);
            PASS2(c, d, e, a, b, 28);
            PASS2(b, c, d, e, a, 29);
            PASS2(a, b, c, d, e, 30);
            PASS2(e, a, b, c, d, 31);
            PASS2(d, e, a, b, c, 32);
            PASS2(c, d, e, a, b, 33);
            PASS2(b, c, d, e, a, 34);
            PASS2(a, b, c, d, e, 35);
            PASS2(e, a, b, c, d, 36);
            PASS2(d, e, a, b, c, 37);
            PASS2(c, d, e, a, b, 38);
            PASS2(b, c, d, e, a, 39);

            PASS3(a, b, c, d, e, 40);
            PASS3(e, a, b, c, d, 41);
            PASS3(d, e, a, b, c, 42);
            PASS3(c, d, e, a, b, 43);
            PASS3(b, c, d, e, a, 44);
            PASS3(a, b, c, d, e, 45);
            PASS3(e, a, b, c, d, 46);
            PASS3(d, e, a, b, c, 47);
            PASS3(c, d, e, a, b, 48);
            PASS3(b, c, d, e, a, 49);
            PASS3(a, b, c, d, e, 50);
            PASS3(e, a, b, c, d, 51);
            PASS3(d, e, a, b, c, 52);
            PASS3(c, d, e, a, b, 53);
            PASS3(b, c, d, e, a, 54);
            PASS3(a, b, c, d, e, 55);
            PASS3(e, a, b, c, d, 56);
            PASS3(d, e, a, b, c, 57);
            PASS3(c, d, e, a, b, 58);
            PASS3(b, c, d, e, a, 59);

            PASS4(a, b, c, d, e, 60);
            PASS4(e, a, b, c, d, 61);
            PASS4(d, e, a, b, c, 62);
            PASS4(c, d, e, a, b, 63);
            PASS4(b, c, d, e, a, 64);
            PASS4(a, b, c, d, e, 65);
            PASS4(e, a, b, c, d, 66);
            PASS4(d, e, a, b, c, 67);
            PASS4(c, d, e, a, b, 68);
            PASS4(b, c, d, e, a, 69);
            PASS4(a, b, c, d, e, 70);
            PASS4(e, a, b, c, d, 71);
            PASS4(d, e, a, b, c, 72);
            PASS4(c, d, e, a, b, 73);
            PASS4(b, c, d, e, a, 74);
            PASS4(a, b, c, d, e, 75);
            PASS4(e, a, b, c, d, 76);
            PASS4(d, e, a, b, c, 77);
            PASS4(c, d, e, a, b, 78);
            PASS4(b, c, d, e, a, 79);

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }
    }

} // namespace

namespace mango
{

    SHA1 sha1(ConstMemory memory)
    {
        SHA1 hash;
        hash.data[0] = 0x67452301;
        hash.data[1] = 0xEFCDAB89;
        hash.data[2] = 0x98BADCFE;
        hash.data[3] = 0x10325476;
        hash.data[4] = 0xC3D2E1F0;

        auto transform = generic_sha1_update;
#if defined(__ARM_FEATURE_CRYPTO)
        if ((getCPUFlags() & ARM_SHA1) != 0)
        {
            transform = arm_sha1_update;
        }
#elif defined(MANGO_ENABLE_SHA)
        if ((getCPUFlags() & CPU_SHA) != 0)
        {
            transform = intel_sha1_update;
        }
#endif

        const u32 len = u32(memory.size);
        const u8* message = memory.address;

        int block_count = len / 64;
        transform(hash.data, message, block_count);
        message += block_count * 64;
        u32 i = block_count * 64;

        u8 block[64];
        u32 rem = len - i;
        memcpy(block, message + i, rem);

        block[rem++] = 0x80;
        if (64 - rem >= 8)
        {
            memset(block + rem, 0, 56 - rem);
        }
        else
        {
            memset(block + rem, 0, 64 - rem);
            transform(hash.data, block, 1);
            memset(block, 0, 56);
        }

        ustore64be(block + 56, memory.size * 8);
        transform(hash.data, block, 1);

#ifdef MANGO_LITTLE_ENDIAN
        hash.data[0] = byteswap(hash.data[0]);
        hash.data[1] = byteswap(hash.data[1]);
        hash.data[2] = byteswap(hash.data[2]);
        hash.data[3] = byteswap(hash.data[3]);
        hash.data[4] = byteswap(hash.data[4]);
#endif

        return hash;
    }

} // namespace mango
