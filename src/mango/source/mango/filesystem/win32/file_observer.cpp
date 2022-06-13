/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2016 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/configure.hpp>
#include <mango/core/string.hpp>
#include <mango/filesystem/path.hpp>
#include <mango/filesystem/fileobserver.hpp>

#include <thread>

namespace mango::filesystem
{

    enum
    {
        BUFFER_SIZE = 4096,
        BUFFER_BYTES = sizeof(DWORD) * BUFFER_SIZE
    };

    static void processNotify(FileObserver* observer, BYTE* buffer, DWORD bytes, u32 flags0)
    {
        for (; buffer;)
        {
            FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)(buffer);
            buffer = notify->NextEntryOffset ? buffer + notify->NextEntryOffset : NULL;

            u32 flags = 0;

            switch (notify->Action)
            {
            case FILE_ACTION_ADDED:
                flags |= FileObserver::CREATED;
                break;
            case FILE_ACTION_REMOVED:
                flags |= FileObserver::DELETED;
                break;
            case FILE_ACTION_MODIFIED:
                flags |= FileObserver::MODIFIED;
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                flags |= FileObserver::DELETED;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                flags |= FileObserver::CREATED;
                break;
            }

            if (flags)
            {
                // Extract and convert the filename to UTF-8
                const std::wstring u16filename(notify->FileName, notify->FileNameLength / 2);
                const std::string filename = u16_toBytes(u16filename);

                // Generate event
                observer->onEvent(flags | flags0, filename);
            }
        }
    }

    struct FileObserverState
    {
        HANDLE m_directory[2];
        HANDLE m_handle[3];
        std::thread m_thread;
        bool m_started;

        HANDLE openDirectory(const std::wstring& pathname) const
        {
            HANDLE handle = CreateFileW(pathname.c_str(),
                                        FILE_LIST_DIRECTORY,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        NULL,
                                        OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                        NULL);
            return handle;
        }

        void closeDirectory(HANDLE handle)
        {
            if (handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }
        }

        void cleanup()
        {
            closeDirectory(m_directory[0]);
            closeDirectory(m_directory[1]);

            if (m_handle[0])
                CloseHandle(m_handle[0]);

            if (m_handle[1])
                CloseHandle(m_handle[1]);

            if (m_handle[2])
                CloseHandle(m_handle[2]);
        }

        FileObserverState(FileObserver* observer, const std::string& u8pathname)
            : m_started(false)
        {
            m_directory[0] = INVALID_HANDLE_VALUE;
            m_directory[1] = INVALID_HANDLE_VALUE;
            m_handle[0] = NULL;
            m_handle[1] = NULL;
            m_handle[2] = NULL;

            // Convert pathname from UTF-8 to wchar
            const std::wstring pathname = u16_fromBytes(u8pathname);

            // Create directory handles
            m_directory[0] = openDirectory(pathname);
            m_directory[1] = openDirectory(pathname);

            if (m_directory[0] == INVALID_HANDLE_VALUE || m_directory[1] == INVALID_HANDLE_VALUE)
            {
                cleanup();
                return;
            }

            // Create event handles
            m_handle[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
            m_handle[1] = CreateEvent(NULL, FALSE, FALSE, NULL);
            m_handle[2] = CreateEvent(NULL, FALSE, FALSE, NULL);

            if (!m_handle[0] || !m_handle[1] || !m_handle[2])
            {
                cleanup();
                return;
            }

            // Mark the thread started
            m_started = true;

            // Create file observer thread
            m_thread = std::thread([=]
            {
                const DWORD filter[] =
                {
                    FILE_NOTIFY_CHANGE_FILE_NAME,
                    FILE_NOTIFY_CHANGE_DIR_NAME
                };

                const u32 flags[] =
                {
                    FileObserver::FILE,
                    FileObserver::DIRECTORY
                };

                OVERLAPPED overlapped[2];
                overlapped[0].hEvent = m_handle[0];
                overlapped[1].hEvent = m_handle[1];

                DWORD buffer[BUFFER_SIZE * 2];
                DWORD bytes;

                if (!ReadDirectoryChangesW(m_directory[0], buffer + 0 * BUFFER_SIZE, BUFFER_BYTES, FALSE, filter[0], &bytes, &overlapped[0], NULL))
                {
                    return;
                }

                if (!ReadDirectoryChangesW(m_directory[1], buffer + 1 * BUFFER_SIZE, BUFFER_BYTES, FALSE, filter[1], &bytes, &overlapped[1], NULL))
                {
                    return;
                }

                for (bool looping = true; looping;)
                {
                    DWORD status = WaitForMultipleObjects(3, m_handle, FALSE, INFINITE);
                    switch (status)
                    {
                        case WAIT_OBJECT_0 + 0:
                        case WAIT_OBJECT_0 + 1:
                        {
                            const DWORD index = status - WAIT_OBJECT_0;

                            if (GetOverlappedResult(m_directory[index], &overlapped[index], &bytes, TRUE))
                            {
                                if (bytes > 0)
                                {
                                    processNotify(observer, (BYTE*)(buffer + index * BUFFER_SIZE), bytes, flags[index]);
                                }

                                // Restart the read directory
                                if (!ReadDirectoryChangesW(m_directory[index], buffer + index * BUFFER_SIZE, BUFFER_BYTES, FALSE, filter[index], &bytes, &overlapped[index], NULL))
                                {
                                    looping = false;
                                }
                            }
                            break;
                        }

                        case WAIT_OBJECT_0 + 2:
                        {
                            // Received cancel event
                            looping = false;
                            break;
                        }
                    }
                }
            });
        }

        ~FileObserverState()
        {
            if (m_started)
            {
                // Send cancel event
                SetEvent(m_handle[2]);

                // Wait for the thread to terminate
                m_thread.join();

                // Close handles
                cleanup();
            }
        }
    };

    // -----------------------------------------------------------------
    // FileObserver
    // -----------------------------------------------------------------

	FileObserver::FileObserver()
        : m_state(nullptr)
	{
	}

	FileObserver::~FileObserver()
	{
        stop();
    }

    void FileObserver::start(const std::string& pathname)
    {
        stop();
        m_state = new FileObserverState(this, pathname);
    }

    void FileObserver::stop()
    {
        if (m_state)
        {
            delete m_state;
            m_state = nullptr;
        }
    }

} // namespace mango::filesystem
