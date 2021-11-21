#pragma once

#include <string>
#include <thread>
#include <cstdio>
#include <wchar.h>
#include <cstring>
#include <cstdlib>
#include <hidapi.h>

namespace erhe::toolkit
{

class Space_mouse_listener
{
public:
    virtual auto is_active     () -> bool = 0;
    virtual void set_active    (const bool value) = 0;
    virtual void on_translation(const int tx, const int ty, const int tz) = 0;
    virtual void on_rotation   (const int rx, const int ry, const int rz) = 0;
    virtual void on_button     (const int id) = 0;
};

class Space_mouse_controller
{
public:
    explicit Space_mouse_controller(Space_mouse_listener& spacemouse_listener);
    ~Space_mouse_controller();

    void run ();
    void stop();

private:
    auto initialize() -> bool;

    Space_mouse_listener&        m_listener;
    bool                         m_mouse_found{false};
    bool                         m_running    {false};
    hid_device*                  m_device     {nullptr};
    std::unique_ptr<std::thread> m_thread;
};

}