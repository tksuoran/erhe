/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2016 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#include <mango/core/configure.hpp>
#include <mango/core/exception.hpp>
#include <mango/filesystem/path.hpp>
#include <mango/filesystem/fileobserver.hpp>

#if defined(MANGO_PLATFORM_LINUX) || defined(MANGO_PLATFORM_ANDROID)

// -----------------------------------------------------------------
// FileObserver: Linux and Android implementation
// Uses Linux Kernel INotify interface
//
// -----------------------------------------------------------------

#include <thread>
#include <sys/inotify.h>
#include <unistd.h>
#include <limits.h>

namespace mango::filesystem
{

    enum
    {
        EVENT_SIZE  = sizeof(inotify_event),
        BUFFER_SIZE = (EVENT_SIZE + PATH_MAX + 1) * 32
    };

	struct FileObserverState
	{
        FileObserver* m_observer;
		int m_notify;
		int m_watch;
        std::thread m_thread;

        FileObserverState(FileObserver* observer, int notify, int watch)
            : m_observer(observer)
            , m_notify(notify)
            , m_watch(watch)
        {
            // launch inotify handler in it's own thread
            m_thread = std::thread([] (FileObserver* observer, int notify, int watch)
            {
                MANGO_UNREFERENCED(watch);

                for (;;)
                {
                    char buffer[BUFFER_SIZE];

                    // read events (this call is blocking and the reason why the observer is threaded)
                    int length = read(notify, buffer, BUFFER_SIZE);
                    if (length < 0)
                    {
                        return;
                    }

                    char* ptr = buffer;
                    char* end = buffer + length;

                    while (ptr < end)
                    {
                        // extract one event
                        inotify_event* event = (inotify_event *)ptr;
                        ptr += (EVENT_SIZE + event->len);

                        if (event->mask & IN_IGNORED)
                        {
                            // watch was deleted
                            return;
                        }

                        // process event
                        if (event->len)
                        {
                            std::string filename = event->name;
                            u32 flags = 0;

                            if (event->mask & IN_ISDIR)
                            {
                                flags |= FileObserver::DIRECTORY;
                            }
                            else
                            {
                                flags |= FileObserver::FILE;
                            }

                            if (event->mask & IN_CREATE)
                            {
                                flags |= FileObserver::CREATED;
                                observer->onEvent(flags, filename);
                            }
                            else if (event->mask & IN_DELETE)
                            {
                                flags |= FileObserver::DELETED;
                                observer->onEvent(flags, filename);
                            }
                            else if (event->mask & IN_MOVED_FROM)
                            {
                                flags |= FileObserver::DELETED;
                                observer->onEvent(flags, filename);
                            }
                            else if (event->mask & IN_MOVED_TO)
                            {
                                flags |= FileObserver::CREATED;
                                observer->onEvent(flags, filename);
                            }
                            else if (event->mask & IN_MODIFY)
                            {
                                flags |= FileObserver::MODIFIED;
                                observer->onEvent(flags, filename);
                            }
#if 0
                            else if (event->mask & IN_OPEN)
                            {
                            }
                            else if (event->mask & IN_CLOSE)
                            {
                            }
                            else if (event->mask & IN_ACCESS)
                            {
                            }
                            else if (event->mask & IN_ATTRIB)
                            {
                            }
                            else if (event->mask & IN_DELETE_SELF)
                            {
                            }
                            else if (event->mask & IN_MOVE_SELF)
                            {
                            }
#endif
                        }
                    }
                }
            }, m_observer, m_notify, m_watch);
        }

        ~FileObserverState()
        {
            inotify_rm_watch(m_notify, m_watch);
            m_thread.join();
            close(m_notify);
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

        int notify = inotify_init();
        if (notify < 0)
        {
            MANGO_EXCEPTION("[FileObserver] inotify_init() failed.");
        }

        int watch = inotify_add_watch(notify, pathname.c_str(), IN_ALL_EVENTS);
        if (watch < 0)
        {
            MANGO_EXCEPTION("[FileObserver] inotify_init() failed.");
        }

        m_state = new FileObserverState(this, notify, watch);
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

#elif defined(MANGO_PLATFORM_BSD) || defined(MANGO_PLATFORM_OSX) || defined(MANGO_PLATFORM_IOS)

// -----------------------------------------------------------------
// FileObserver: BSD, macOS and iOS implementation
// Uses BSD/MACH Kernel kqueue interface
// 
// -----------------------------------------------------------------

#include <thread>
#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>

namespace mango::filesystem
{

    struct FileObserverState
    {
        int m_kqueue;
        std::thread m_thread;

        FileObserverState(const std::string& pathname, FileObserver* observer)
            : m_kqueue(0)
        {
            m_kqueue = kqueue();
            int dirfd = open(pathname.c_str(), O_RDONLY);

            struct kevent direvent;
            EV_SET(&direvent, dirfd, EVFILT_VNODE, EV_ADD | EV_CLEAR | EV_ENABLE, NOTE_WRITE, 0, (void *)observer);

            kevent(m_kqueue, &direvent, 1, NULL, 0, NULL);

            struct kevent sigevent;
            EV_SET (&sigevent, SIGINT, EVFILT_SIGNAL, EV_ADD | EV_ENABLE, 0, 0, NULL);
            signal (SIGINT, SIG_IGN);
            kevent(m_kqueue, &sigevent, 1, NULL, 0, NULL);

            // launch kevent handler in it's own thread
            m_thread = std::thread([] (FileObserver* observer, int kq)
            {
                for (;;)
                {
                    // read kevent (this call is blocking and the reason why the observer is threaded)
                    struct kevent change;
                    if (kevent(kq, NULL, 0, &change, 1, NULL) == -1)
                    {
                        // kqueue was closed
                        return 1;
                    }

                    if (change.udata)
                    {
                        FileObserver* observer = reinterpret_cast<FileObserver*>(change.udata);
                        observer->onEvent(0, "");
                    }
                }

                return 0;
            }, observer, m_kqueue);
        }

        ~FileObserverState()
        {
            if (m_kqueue)
            {
                close(m_kqueue);
            }
            m_thread.join();
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
        m_state = new FileObserverState(pathname, this);
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

#else

// -----------------------------------------------------------------
// FileObserver: SUN, HPUX, IRIX, etc. implementation
// TODO
// 
// -----------------------------------------------------------------

namespace mango::filesystem
{

    FileObserver::FileObserver()
        : m_state(nullptr)
    {
    }

    FileObserver::~FileObserver()
    {
    }

    void FileObserver::start(const std::string& pathname)
    {
        MANGO_UNREFERENCED(pathname);
    }

    void FileObserver::stop()
    {
    }

} // namespace mango::filesystem

#endif
