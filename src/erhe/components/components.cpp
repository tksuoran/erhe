#include "erhe/components/components.hpp"
#include "erhe/components/component.hpp"
#include "erhe/components/log.hpp"
#include "erhe/toolkit/verify.hpp"

#define ERHE_TRACY_NO_GL 1
#include "erhe/toolkit/tracy_client.hpp"

#include <fmt/ostream.h>

#include <sstream>

namespace erhe::components
{

using std::set;
using std::shared_ptr;

Components::Components()
{
}

Components::~Components() = default;

auto Components::add(const shared_ptr<Component>& component)
-> const std::shared_ptr<erhe::components::Component>&
{
    // Silently ignores nullptr Components
    if (component)
    {
        component->register_as_component(this);
        components.insert(component);
    }
    return component;
}

void Components::cleanup_components()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->unregister();
    }

    components.clear();
}

void Components::launch_component_initialization()
{
    ZoneScoped;

    auto tid = std::this_thread::get_id();
    for (auto const& component : components)
    {
        component->connect();
        component->set_connected();
    }

    m_uninitialized_components = components;
    size_t count = m_uninitialized_components.size();

    log_components.info("{} Initializing {} Components:\n", tid, count);

    m_execution_queue = std::make_unique<mango::ConcurrentQueue>();

    for (size_t i = 0; i < count; ++i)
    {
        m_execution_queue->enqueue([this]() {
            ZoneScoped;

            auto component = get_component_to_initialize();
            if (component)
            {
                component->initialize_component();
                {
                    //ZoneScoped;
                    ZoneName(component->name(), strlen(component->name()));
                    //ZoneText(component->name(), strlen(component->name()));

                    std::lock_guard<std::mutex> lock(m_mutex);
                    component->set_ready();
                    m_uninitialized_components.erase(component);
                    for (auto component_ : m_uninitialized_components)
                    {
                        component_->remove_dependency(component);
                    }
                    std::string message_text = fmt::format("{} initialized", component->name());
                    TracyMessage(message_text.c_str(), message_text.length());
                    m_component_initialized.notify_all();
                    if (m_uninitialized_components.empty())
                    {
                        m_is_ready = true;
                        TracyMessageL("all components initialized");
                    }
                }
            }
        });
    }
}

auto Components::get_component_to_initialize() -> shared_ptr<Component>
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_uninitialized_components.empty())
        {
            FATAL("No uninitialized component found\n");
            return {};
        }
    }

    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto i = std::find_if(m_uninitialized_components.begin(),
                                  m_uninitialized_components.end(),
                                  [](auto& component) {
                                      return component->get_state() == Component::Component_state::Connected &&
                                             component->is_ready();
                                  });
            if (i != m_uninitialized_components.end())
            {
                auto component = *i;
                component->set_initializing();
                return component;
            }
        }

        {
            ZoneScopedNC("wait", 0x444444);

            std::unique_lock<std::mutex> unique_lock(m_mutex);
            m_component_initialized.wait(unique_lock);
        }
    }
}

auto Components::is_component_initialization_complete() -> bool
{
    ZoneScoped;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_is_ready;
}

void Components::wait_component_initialization_complete()
{
    ZoneScoped;

    VERIFY(m_execution_queue);
    m_execution_queue->wait();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_is_ready = true;
}

void Components::on_thread_exit()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->on_thread_exit();
    }
}

void Components::on_thread_enter()
{
    ZoneScoped;

    for (const auto& component : components)
    {
        component->on_thread_enter();
    }
}

} // namespace erhe::components
