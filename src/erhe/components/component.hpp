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

enum class Component_state : unsigned int
{
    Constructed,
    Connected,
    Initializing,
    Ready,
    Deinitializing,
    Deinitialized
};

auto c_str(const Component_state state) -> const char*;

class Component
{
protected:
    Component     (const Component&) = delete;
    Component     (Component&&)      = delete;
    void operator=(const Component&) = delete;
    void operator=(Component&&)      = delete;

    explicit Component(const std::string_view name);

    virtual ~Component();

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
        depends_on(component);
        return component;
    }

    virtual auto get_type_hash                  () const -> uint32_t = 0;
    virtual void initialize_component           () {}
    virtual void on_thread_exit                 () {}
    virtual void on_thread_enter                () {}
    virtual auto processing_requires_main_thread() const -> bool;

    auto name                    () const -> std::string_view;
    auto get_state               () const -> Component_state;
    auto is_registered           () const -> bool;
    void register_as_component   (Components* components);
    auto is_ready_to_initialize  (const bool in_worker_thread) const -> bool;
    auto is_ready_to_deinitialize() const -> bool;
    void component_initialized   (Component* component);
    void component_deinitialized (Component* component);
    void depends_on              (Component* dependency);
    auto dependencies            () -> const std::set<Component*>&;
    auto depended_by             () -> const std::set<Component*>&;
    void set_connected           ();
    void set_initializing        ();
    void set_ready               ();
    void set_deinitializing      ();

protected:
    Components*          m_components{nullptr};

private:
    void add_depended_by(Component* component);
    void unregister     ();

    std::string_view     m_name;
    Component_state      m_state{Component_state::Constructed};
    std::set<Component*> m_dependencies;
    std::set<Component*> m_depended_by;
};

} // namespace erhe::components
