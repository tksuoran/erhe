#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/verify.hpp"
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
    [[nodiscard]] auto get() const -> std::shared_ptr<T>
    {
        if (m_components == nullptr)
        {
            return nullptr;
        }

        return m_components->get<T>();
    }

    template<typename T>
    auto require() -> std::shared_ptr<T>
    {
        const auto component = get<T>();
        //ERHE_VERIFY(component != nullptr);
        if (component != nullptr)
        {
            depends_on(component);
        }
        return component;
    }

    // Public interface
    [[nodiscard]] virtual auto get_type_hash                  () const -> uint32_t = 0;
    [[nodiscard]] virtual auto processing_requires_main_thread() const -> bool;
    virtual void initialize_component() {}
    virtual void on_thread_exit      () {}
    virtual void on_thread_enter     () {}

    // Public non-virtual API
    [[nodiscard]] auto name                    () const -> std::string_view;
    [[nodiscard]] auto get_state               () const -> Component_state;
    [[nodiscard]] auto is_registered           () const -> bool;
    [[nodiscard]] auto is_ready_to_initialize  (const bool in_worker_thread) const -> bool;
    [[nodiscard]] auto is_ready_to_deinitialize() const -> bool;
    [[nodiscard]] auto dependencies            () -> const std::set<std::shared_ptr<Component>>&;
    void register_as_component(Components* components);
    void component_initialized(Component* component);
    void depends_on           (const std::shared_ptr<Component>& dependency);
    void set_connected        ();
    void set_initializing     ();
    void set_ready            ();
    void set_deinitializing   ();

protected:
    Components* m_components{nullptr};

private:
    void unregister();

    std::string_view                     m_name;
    Component_state                      m_state{Component_state::Constructed};
    std::set<std::shared_ptr<Component>> m_dependencies;
};

} // namespace erhe::components
