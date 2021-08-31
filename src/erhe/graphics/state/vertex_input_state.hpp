#pragma once

#include "erhe/graphics/gl_objects.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_stream_binding.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

#include <fmt/ostream.h>
#include <gsl/gsl>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <vector>
#include <mutex>

namespace erhe::graphics
{

class Buffer;
class Vertex_attribute_mapping;
class Vertex_attribute_mappings;
class Vertex_format;

static constexpr int MAX_ATTRIBUTE_COUNT { 16 }; // TODO(tksuoran@gmail.com): Get rid of this kind of constant?

class Vertex_input_state final
{
public:
    class Binding
    {
    public:
        Binding(const Buffer*                                    vertex_buffer,
                const std::shared_ptr<Vertex_attribute_mapping>& mapping,
                const Vertex_attribute*                          attribute,
                size_t                                           stride)
            : vertex_buffer           {vertex_buffer}
            , vertex_attribute_mapping{mapping}
            , vertex_attribute        {attribute}
            , stride                  {stride}
        {
        }

        Binding(const Vertex_stream_binding& other)
            : vertex_buffer           {other.vertex_buffer}
            , vertex_attribute_mapping{other.vertex_attribute_mapping}
            , vertex_attribute        {other.vertex_attribute}
            , stride                  {other.stride}
        {
        }

        void operator=(const Binding&) = delete;

        const Buffer*                             vertex_buffer   {nullptr};
        std::shared_ptr<Vertex_attribute_mapping> vertex_attribute_mapping;
        const Vertex_attribute*                   vertex_attribute{nullptr};
        size_t                                    stride          {0};
    };

    using Binding_collection = std::vector<std::shared_ptr<Binding>>;

    Vertex_input_state();

    Vertex_input_state(const Vertex_attribute_mappings& attribute_mappings,
                       const Vertex_format&             vertex_format,
                       gsl::not_null<const Buffer*>     vertex_buffer,
                       const Buffer*                    index_buffer);

    void touch()
    {
        m_serial = get_next_serial();
    }

    ~Vertex_input_state();

    Vertex_input_state(const Vertex_input_state&) = delete;

    auto operator=(const Vertex_input_state&)
    -> Vertex_input_state& = delete;

    Vertex_input_state(Vertex_input_state&& other) = delete; //noexcept
    //{
    //    m_bindings        = std::move(other.m_bindings);
    //    m_index_buffer    = other.m_index_buffer;
    //    m_gl_vertex_array = std::move(other.m_gl_vertex_array);
    //}

    auto operator=(Vertex_input_state&& other) = delete; //noexcept
    //-> Vertex_input_state&
    //{
    //    m_bindings        = std::move(other. m_bindings);
    //    m_index_buffer    = other.m_index_buffer;
    //    m_gl_vertex_array = std::move(other.m_gl_vertex_array);
    //    return *this;
    //}


    void emplace_back(gsl::not_null<const Buffer*>                     vertex_buffer,
                      const std::shared_ptr<Vertex_attribute_mapping>& vertex_attribute_mapping,
                      const Vertex_attribute*                          attribute,
                      const size_t                                     stride);
    //-> Binding&;

    void use() const;

    void set_index_buffer(const Buffer* buffer);

    auto index_buffer() -> const Buffer*
    {
        return m_index_buffer;
    }

    auto gl_name() const -> unsigned int;

    void create();

    void reset();

    void update();

    auto bindings() const
    -> const Binding_collection&
    {
        return m_bindings;
    }

    static void on_thread_enter()
    {
        std::lock_guard lock{s_mutex};
        for (auto* vertex_input_state : s_all_vertex_input_states)
        {
            log_threads.trace("{}: on thread enter: vertex input state @ {} owned by thread {}\n",
                              std::this_thread::get_id(),
                              fmt::ptr(vertex_input_state),
                              vertex_input_state->m_owner_thread);
            if (vertex_input_state->m_owner_thread == std::thread::id{})
            {
                vertex_input_state->create();
            }
        }
    }

    static void on_thread_exit()
    {
        std::lock_guard lock{s_mutex};
        gl::bind_vertex_array(0);
        auto this_thread_id = std::this_thread::get_id();
        for (auto* vertex_input_state : s_all_vertex_input_states)
        {
            log_threads.trace("{}: on thread exit: vertex input state @ {} owned by thread {}\n",
                              std::this_thread::get_id(),
                              fmt::ptr(vertex_input_state),
                              vertex_input_state->m_owner_thread);
            if (vertex_input_state->m_owner_thread == this_thread_id)
            {
                vertex_input_state->reset();
            }
        }
    }

    auto serial() const
    -> size_t
    {
        return m_serial;
    }

    static auto get_next_serial()
    -> size_t
    {
        std::lock_guard lock{s_mutex};

        do
        {
            s_serial++;
        }
        while (s_serial == 0);

        return s_serial;
    }

private:
    Binding_collection             m_bindings;
    const Buffer*                  m_index_buffer{nullptr};
    std::optional<Gl_vertex_array> m_gl_vertex_array;
    size_t                         m_serial;
    std::thread::id                m_owner_thread;

    static std::mutex                       s_mutex;
    static size_t                           s_serial;
    static std::vector<Vertex_input_state*> s_all_vertex_input_states;
};

class Vertex_input_state_tracker
{
public:
    void reset()
    {
        gl::bind_vertex_array(0);
        m_last = 0;
    }

    void execute(const Vertex_input_state* state)
    {
        if ((state != nullptr) && (m_last == state->serial()))
        {
            return;
        }
        unsigned int name = (state != nullptr) ? state->gl_name() : 0;
        gl::bind_vertex_array(name);
        m_last = (state != nullptr) ? state->serial() : 0;
    }

private:
    size_t m_last{0};
};

} // namespace erhe::graphics
