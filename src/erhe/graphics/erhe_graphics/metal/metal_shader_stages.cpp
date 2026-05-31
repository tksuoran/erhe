#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/device.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Shader_stages_impl::Shader_stages_impl(Device& device, const std::string& non_functional_name)
    : m_device_impl{device.get_impl()}
    , m_name       {non_functional_name}
    , m_is_valid   {false}
{
}

Shader_stages_impl::Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype)
    : m_device_impl{device.get_impl()}
{
    reload(std::move(prototype));
}

Shader_stages_impl::Shader_stages_impl(Shader_stages_impl&& from) noexcept
    : m_device_impl      {from.m_device_impl}
    , m_name             {std::move(from.m_name)}
    , m_is_valid         {from.m_is_valid}
    , m_vertex_function  {from.m_vertex_function}
    , m_fragment_function{from.m_fragment_function}
    , m_compute_function {from.m_compute_function}
    , m_compute_workgroup_size{from.m_compute_workgroup_size}
{
    from.m_vertex_function   = nullptr;
    from.m_fragment_function = nullptr;
    from.m_compute_function  = nullptr;
}

Shader_stages_impl& Shader_stages_impl::operator=(Shader_stages_impl&& from) noexcept
{
    if (this != &from) {
        defer_release_functions();
        m_name              = std::move(from.m_name);
        m_is_valid          = from.m_is_valid;
        m_vertex_function   = from.m_vertex_function;
        m_fragment_function = from.m_fragment_function;
        m_compute_function  = from.m_compute_function;
        m_compute_workgroup_size = from.m_compute_workgroup_size;
        from.m_vertex_function   = nullptr;
        from.m_fragment_function = nullptr;
        from.m_compute_function  = nullptr;
    }
    return *this;
}

Shader_stages_impl::~Shader_stages_impl()
{
    defer_release_functions();
}

void Shader_stages_impl::defer_release_functions()
{
    MTL::Function* vertex_function   = m_vertex_function;
    MTL::Function* fragment_function = m_fragment_function;
    MTL::Function* compute_function  = m_compute_function;
    m_vertex_function   = nullptr;
    m_fragment_function = nullptr;
    m_compute_function  = nullptr;
    if ((vertex_function != nullptr) || (fragment_function != nullptr) || (compute_function != nullptr)) {
        m_device_impl.add_completion_handler(
            [vertex_function, fragment_function, compute_function](Device_impl&) {
                if (vertex_function   != nullptr) vertex_function  ->release();
                if (fragment_function != nullptr) fragment_function->release();
                if (compute_function  != nullptr) compute_function ->release();
            }
        );
    }
}

void Shader_stages_impl::reload(Shader_stages_prototype&& prototype)
{
    if (!prototype.is_valid()) {
        invalidate();
        return;
    }

    // Release old functions
    defer_release_functions();

    m_name = prototype.name();

    // Transfer function ownership from prototype
    Shader_stages_prototype_impl& proto_impl = prototype.get_impl();
    m_vertex_function   = proto_impl.get_vertex_function();
    m_fragment_function = proto_impl.get_fragment_function();
    m_compute_function  = proto_impl.get_compute_function();
    m_compute_workgroup_size = proto_impl.get_compute_workgroup_size();
    // Retain since prototype destructor will release
    if (m_vertex_function != nullptr) {
        m_vertex_function->retain();
    }
    if (m_fragment_function != nullptr) {
        m_fragment_function->retain();
    }
    if (m_compute_function != nullptr) {
        m_compute_function->retain();
    }

    // Apply debug labels to shader functions
    if (!m_name.empty()) {
        NS::String* label = NS::String::alloc()->init(
            m_name.c_str(),
            NS::UTF8StringEncoding
        );
        if (m_vertex_function != nullptr) {
            m_vertex_function->setLabel(label);
        }
        if (m_fragment_function != nullptr) {
            m_fragment_function->setLabel(label);
        }
        if (m_compute_function != nullptr) {
            m_compute_function->setLabel(label);
        }
        label->release();
    }

    m_is_valid = true;
}

void Shader_stages_impl::invalidate()
{
    m_is_valid = false;
    defer_release_functions();
}

auto Shader_stages_impl::name() const -> const std::string& { return m_name; }
auto Shader_stages_impl::gl_name() const -> unsigned int { return 0; }
auto Shader_stages_impl::is_valid() const -> bool { return m_is_valid; }
auto Shader_stages_impl::get_bind_group_layout() const -> const Bind_group_layout* { return nullptr; }

auto Shader_stages_impl::get_vertex_function() const -> MTL::Function*
{
    return m_vertex_function;
}

auto Shader_stages_impl::get_fragment_function() const -> MTL::Function*
{
    return m_fragment_function;
}

auto Shader_stages_impl::get_compute_function() const -> MTL::Function*
{
    return m_compute_function;
}

auto Shader_stages_impl::get_compute_workgroup_size() const -> std::array<uint32_t, 3>
{
    return m_compute_workgroup_size;
}

auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool { return &lhs == &rhs; }
auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool { return !(lhs == rhs); }

void Shader_stages_tracker::reset() { m_last = 0; }
void Shader_stages_tracker::execute(const Shader_stages* state) { static_cast<void>(state); m_last = 0; }
auto Shader_stages_tracker::get_draw_id_uniform_location() const -> int { return -1; }

} // namespace erhe::graphics
