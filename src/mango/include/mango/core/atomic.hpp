/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <atomic>
#include <mango/core/configure.hpp>

namespace mango
{

    // ----------------------------------------------------------------------------
    // pause()
    // ----------------------------------------------------------------------------

    static inline
    void pause()
    {
#if defined(MANGO_CPU_INTEL)
    #if defined(MANGO_COMPILER_CLANG) || defined(MANGO_COMPILER_MICROSOFT)
        _mm_pause();
    #else
        __builtin_ia32_pause();
    #endif
#elif defined(MANGO_CPU_ARM)
        asm volatile ("yield");
#elif defined(MANGO_CPU_MIPS)
        __asm__ __volatile__("pause");
#else
        // NOTE: Processor yield/pause is not supported :(
#endif
    }

    // ----------------------------------------------------------------------------
    // SpinLock
    // ----------------------------------------------------------------------------

    /* WARNING!
       Atomic locks are implemented as busy loops which potentially consume
       significant amounts of CPU time.
    */

    class SpinLock
    {
    private:
        std::atomic<bool> m_locked = { false };

    public:
        bool tryLock()
        {
            return !m_locked.load(std::memory_order_relaxed) &&
                   !m_locked.exchange(true, std::memory_order_acquire);
        }

        void lock()
        {
            while(m_locked.exchange(true, std::memory_order_acquire))
            {
                while (m_locked.load(std::memory_order_relaxed))
                {
                    pause();
                }
            }
        }

        void unlock()
        {
            m_locked.store(false, std::memory_order_release);
        }
    };

    class SpinLockGuard
    {
    private:
        SpinLock& m_spinlock;
        bool m_locked { false };

    public:
        SpinLockGuard(SpinLock& spinlock)
            : m_spinlock(spinlock)
        {
            lock();
        }

        ~SpinLockGuard()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked) {
                m_locked = true;
                m_spinlock.lock();
            }
        }

        void unlock()
        {
            if (m_locked) {
                m_locked = false;
                m_spinlock.unlock();
            }
        }
    };

    // ----------------------------------------------------------------------------
    // ReadWriteSpinLock
    // ----------------------------------------------------------------------------

    class ReadWriteSpinLock : protected SpinLock
    {
    private:
        std::atomic<int> m_read_count { 0 };

    public:
        bool tryWriteLock()
        {
            bool status = tryLock();
            if (status) {
                // acquired exclusive access - flush all readers
                while (m_read_count > 0) {
                }
            }

            return status;
        }

        void writeLock()
        {
            // acquire exclusive access
            lock();

            // flush all readers
            while (m_read_count > 0) {
            }
        }

        void writeUnlock()
        {
            // release exclusivity
            unlock();
        }

        bool tryReadLock()
        {
            bool status = tryLock();
            if (status) {
                // gained temporary exclusivity - add one reader
                m_read_count.fetch_add(1, std::memory_order_acquire);
                unlock();
            }

            return status;
        }

        void readLock()
        {
            // gain temporary exclusivity to add one reader
            lock();
            m_read_count.fetch_add(1, std::memory_order_acquire);
            unlock();
        }

        void readUnlock()
        {
            // reader can be released at any time w/o exclusivity
            m_read_count.fetch_sub(1, std::memory_order_release);
        }
    };

    class WriteSpinLockGuard
    {
    private:
        ReadWriteSpinLock& m_rwlock;
        bool m_locked { false };

    public:
        WriteSpinLockGuard(ReadWriteSpinLock& rwlock)
            : m_rwlock(rwlock)
        {
            lock();
        }

        ~WriteSpinLockGuard()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked) {
                m_locked = true;
                m_rwlock.writeLock();
            }
        }

        void unlock()
        {
            if (m_locked) {
                m_locked = false;
                m_rwlock.writeUnlock();
            }
        }
    };

    class ReadSpinLockGuard
    {
    private:
        ReadWriteSpinLock& m_rwlock;
        bool m_locked { false };

    public:
        ReadSpinLockGuard(ReadWriteSpinLock& rwlock)
            : m_rwlock(rwlock)
        {
            lock();
        }

        ~ReadSpinLockGuard()
        {
            unlock();
        }

        void lock()
        {
            if (!m_locked) {
                m_locked = true;
                m_rwlock.readLock();
            }
        }

        void unlock()
        {
            if (m_locked) {
                m_locked = false;
                m_rwlock.readUnlock();
            }
        }
    };

} // namespace mango
