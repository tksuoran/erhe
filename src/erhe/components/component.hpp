#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/verify.hpp"
//#include "erhe/toolkit/string_hash.hpp"
#include "erhe/toolkit/xxhash.hpp"
#include <memory>
#include <set>
#include <string_view>

namespace erhe::components
{

class Components;

class Time_context
{
public:
    double   dt;
    double   time;
    uint64_t frame_number;
};

class IUpdate_fixed_step
{
public:
    virtual void update_fixed_step(const Time_context&) = 0;
};

class IUpdate_once_per_frame
{
public:
    virtual void update_once_per_frame(const Time_context&) = 0;
};

class Component
{
public:
    enum class Component_state : unsigned int
    {
        Constructed,
        Connected,
        Initializing,
        Ready
    };

protected:
    Component     (const Component&) = delete;
    Component     (Component&&)      = delete;
    void operator=(const Component&) = delete;
    void operator=(Component&&)      = delete;

    explicit Component(const std::string_view name);

    virtual ~Component() = default;

public:
    virtual void connect() {};

    template<typename T>
    auto get() const -> T*
    {
        if (m_components == nullptr)
        {
            return nullptr;
        }

        for (const auto& component : m_components->components)
        {
            if (component->get_type_hash() == T::hash)
            {
                return reinterpret_cast<T*>(component.get());
            }
        }
        return {};
    }

    template<typename T>
    auto require() -> T*
    {
        const auto component = get<T>();
        VERIFY(component);
        initialization_depends_on(component);
        return component;
    }

    virtual auto get_type_hash() const -> uint32_t = 0;

    virtual void initialize_component() {}

    virtual void on_thread_exit() {}

    virtual void on_thread_enter() {}

    virtual auto initialization_requires_main_thread() const -> bool
    {
        return false;
    }

    auto name() const -> std::string_view
    {
        return m_name;
    }

    auto get_state() const -> Component_state;

    auto is_registered() const -> bool
    {
        return m_components != nullptr;
    }

    void register_as_component(Components* components)
    {
        m_components = components;
    }

    void unregister()
    {
        m_components = nullptr;
    }

    auto is_ready_to_initialize(const bool in_worker_thread) const -> bool;

    void remove_dependency(Component* component);

    void initialization_depends_on(Component* dependency);

    auto dependencies() -> const std::set<Component*>&
    {
        return m_dependencies;
    }

    void set_connected();
    void set_initializing();
    void set_ready();

protected:
    Components*          m_components{nullptr};

private:
    std::string_view     m_name;
    Component_state      m_state      {Component_state::Constructed};
    std::set<Component*> m_dependencies;
};

} // namespace erhe::components
