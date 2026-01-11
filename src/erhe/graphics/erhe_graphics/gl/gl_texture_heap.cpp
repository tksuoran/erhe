// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_texture_heap.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&        device,
    const Texture& fallback_texture,
    const Sampler& fallback_sampler,
    std::size_t    reserved_slot_count
)
    : m_device             {device}
    , m_fallback_texture   {fallback_texture}
    , m_fallback_sampler   {fallback_sampler}
    , m_reserved_slot_count{reserved_slot_count}
    , m_used_slot_count    {0}
{
    // log_texture_heap->trace("Texture_heap_impl::Texture_heap_impl()");

    if (m_device.get_info().use_bindless_texture) {
        const uint64_t fallback_texture_handle = m_device.get_handle(m_fallback_texture, m_fallback_sampler);
        m_textures.resize(m_reserved_slot_count);
        m_samplers.resize(m_reserved_slot_count);
        m_assigned.resize(m_reserved_slot_count);
        m_gl_bindless_texture_handles.resize(m_reserved_slot_count);
        m_gl_bindless_texture_resident.resize(m_reserved_slot_count);
        std::fill(m_assigned.begin(), m_assigned.end(), false);
        std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
        std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
        std::fill(m_gl_bindless_texture_handles.begin(), m_gl_bindless_texture_handles.end(), fallback_texture_handle);
        std::fill(m_gl_bindless_texture_resident.begin(), m_gl_bindless_texture_resident.end(), false);
        m_used_slot_count = 0;
    } else {
        m_textures.resize(device.get_info().max_per_stage_descriptor_samplers);
        m_samplers.resize(device.get_info().max_per_stage_descriptor_samplers);
        m_gl_textures.resize(device.get_info().max_per_stage_descriptor_samplers);
        m_gl_samplers.resize(device.get_info().max_per_stage_descriptor_samplers);
        m_zero_vector.resize(device.get_info().max_per_stage_descriptor_samplers);
        reset();
    }
}

Texture_heap_impl::~Texture_heap_impl()
{
    // log_texture_heap->trace("Texture_heap_impl::~Texture_heap_impl()");
}

void Texture_heap_impl::reset()
{
    // log_texture_heap->trace("Texture_heap_impl::reset()");

    if (!m_device.get_info().use_bindless_texture) {
        const GLuint fallback_texture_name = m_fallback_texture.get_impl().gl_name();
        const GLuint fallback_sampler_name = m_fallback_sampler.get_impl().gl_name();
        std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
        std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
        std::fill(m_gl_textures.begin(), m_gl_textures.end(), fallback_texture_name);
        std::fill(m_gl_samplers.begin(), m_gl_samplers.end(), fallback_sampler_name);
        m_used_slot_count = 0;
    } else {
        ERHE_FATAL("This should not happen");
    }
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    ERHE_VERIFY(texture != nullptr);
    ERHE_VERIFY(sampler != nullptr);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            if (m_device.get_info().use_bindless_texture) {
                return m_gl_bindless_texture_handles[slot];
            } else {
                return static_cast<uint64_t>(slot - m_reserved_slot_count);
            }
        }
    }
    ERHE_FATAL("texture %u sampler %u not found in texture heap", texture->get_impl().gl_name(), sampler->get_impl().gl_name());
}

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if (texture == nullptr) {
        texture = &m_fallback_texture;
    }
    if (sampler == nullptr) {
        sampler = &m_fallback_sampler;
    }

    if (m_device.get_info().use_bindless_texture) {
        const uint64_t gl_bindless_texture_handle = m_device.get_handle(*texture, *sampler);
        m_assigned                    [slot] = true;
        m_gl_bindless_texture_handles [slot] = gl_bindless_texture_handle;
        m_gl_bindless_texture_resident[slot] = false;
        m_textures                    [slot] = texture;
        m_samplers                    [slot] = sampler;
        // log_texture_heap->trace(
        //     "assigned texture heap slot {} for texture {}, sampler {} bindless handle {}",
        //     slot, texture->get_impl().gl_name(), sampler->get_impl().gl_name(), format_texture_handle(gl_bindless_texture_handle)
        // );
        return gl_bindless_texture_handle;

    } else {

        m_textures   [slot] = texture;
        m_samplers   [slot] = sampler;
        m_gl_textures[slot] = texture->get_impl().gl_name();
        m_gl_samplers[slot] = sampler->get_impl().gl_name();
        // log_texture_heap->trace(
        //     "assigned texture heap slot {} for texture {}, sampler {}",
        //     slot, texture->get_impl().gl_name(), sampler->get_impl().gl_name()
        // );
        return slot;
    }
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // const GLuint texture_name = texture->get_impl().gl_name(); // get_texture_from_handle(handle);
    // const GLuint sampler_name = sampler->get_impl().gl_name(); // get_sampler_from_handle(handle);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            // log_texture_heap->trace("cache hit texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
            if (m_device.get_info().use_bindless_texture) {
                return m_gl_bindless_texture_handles[slot];
            } else {
                return static_cast<uint64_t>(slot);
            }
        }
    }

    if (m_device.get_info().use_bindless_texture) {
        // const std::size_t slot = m_reserved_slot_count + m_used_slot_count;
        const uint64_t gl_bindless_texture_handle = m_device.get_handle(*texture, *sampler);
        m_gl_bindless_texture_handles .push_back(gl_bindless_texture_handle);
        m_gl_bindless_texture_resident.push_back(false);
        m_textures                    .push_back(texture);
        m_samplers                    .push_back(sampler);
        // log_texture_heap->trace(
        //     "allocated texture heap slot {} for texture {}, sampler {} bindless handle = {}",
        //     m_used_slot_count, texture_name, sampler_name, format_texture_handle(gl_bindless_texture_handle)
        // );
        ++m_used_slot_count;
        return gl_bindless_texture_handle;

    } else {

        if (m_reserved_slot_count + m_used_slot_count < m_textures.size()) {
            const std::size_t slot = m_reserved_slot_count + m_used_slot_count;
            m_textures   [slot] = texture;
            m_samplers   [slot] = sampler;
            m_gl_textures[slot] = texture->get_impl().gl_name();
            m_gl_samplers[slot] = sampler->get_impl().gl_name();
            ++m_used_slot_count;
            // log_texture_heap->trace("allocated texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
            return static_cast<uint64_t>(slot - m_reserved_slot_count);
        }

        // log_texture_heap->trace("texture heap is full, unable to allocate slot for texture {}, sampler {}", texture_name, sampler_name);
        return {};
    }
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap_impl::unbind()
{
    // log_texture_heap->trace("Texture_heap_impl::unbind()");

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                // log_texture_heap->trace(
                //     "making texture handle {} non-resident / texture {}, sampler {}",
                //     format_texture_handle(gl_bindless_texture_handle),
                //     m_textures[slot]->get_impl().gl_name(),
                //     m_samplers[slot]->get_impl().gl_name()
                // );
                gl::make_texture_handle_non_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = false;
            }
        }
    } else {
        gl::bind_textures(0, m_device.get_info().max_per_stage_descriptor_samplers, m_zero_vector.data());
        gl::bind_samplers(0, m_device.get_info().max_per_stage_descriptor_samplers, m_zero_vector.data());
    }
}

auto get_binding_p_name(const gl::Texture_target gl_target) -> gl::Get_p_name
{
    switch (gl_target) {
        case gl::Texture_target::texture_buffer:               return gl::Get_p_name::texture_binding_buffer;
        case gl::Texture_target::texture_1d_array:             return gl::Get_p_name::texture_binding_1d_array;
        case gl::Texture_target::texture_1d:                   return gl::Get_p_name::texture_binding_1d;
        case gl::Texture_target::texture_2d_multisample_array: return gl::Get_p_name::texture_binding_2d_multisample_array;
        case gl::Texture_target::texture_2d_array:             return gl::Get_p_name::texture_binding_2d_array;
        case gl::Texture_target::texture_2d_multisample:       return gl::Get_p_name::texture_binding_2d_multisample;
        case gl::Texture_target::texture_2d:                   return gl::Get_p_name::texture_binding_2d;
        case gl::Texture_target::texture_3d:                   return gl::Get_p_name::texture_binding_3d;
        case gl::Texture_target::texture_cube_map_array:       return gl::Get_p_name::texture_binding_cube_map_array;
        case gl::Texture_target::texture_cube_map:             return gl::Get_p_name::texture_binding_cube_map;
        default: ERHE_FATAL("bad texture target");
    }
}

// TODO Maybe this should use Render_command_encoder?
auto Texture_heap_impl::bind() -> std::size_t
{
    // log_texture_heap->trace(
    //     "Texture_heap_impl::bind() {}",
    //     m_device.get_info().use_bindless_texture
    //         ? "bindless"
    //         : "not bindless"
    // );

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (!m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                log_texture_heap->trace(
                    "making texture handle {} resident / texture {}, sampler {}",
                    format_texture_handle(gl_bindless_texture_handle),
                    m_textures[slot]->get_impl().gl_name(),
                    m_samplers[slot]->get_impl().gl_name()
                );
                gl::make_texture_handle_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = true;
            }
        }
        return m_used_slot_count;

    } else {   
        gl::bind_textures(0, m_device.get_info().max_per_stage_descriptor_samplers, m_gl_textures.data());
        gl::bind_samplers(0, m_device.get_info().max_per_stage_descriptor_samplers, m_gl_samplers.data());

#if 0 // PARANOID SANITY CHECKS
        bool ok = true;
        std::stringstream ss;
        for (uint32_t i = 0, end = m_device.get_info().max_per_stage_descriptor_samplers; i < end; ++i) {
            if (i != 0) {
                ss << ", ";
            }
            GLint bound_texture{0};
            const gl::Texture_target gl_target         = m_textures.at(i)->get_impl().get_gl_texture_target();
            const gl::Get_p_name     gl_binding_p_name = get_binding_p_name(gl_target);
            gl::get_integer_iv(gl_binding_p_name, static_cast<int>(i), &bound_texture);
            if (bound_texture != static_cast<GLint>(m_gl_textures.at(i))) {
                ok = false;
            }
            GLint bound_sampler{0};
            gl::get_integer_iv(gl::Get_p_name::sampler_binding, static_cast<int>(i), &bound_sampler);
            if (bound_sampler != static_cast<GLint>(m_gl_samplers.at(i))) {
                ok = false;
            }

            ss << fmt::format("[{}] = {}/{}.{}/{}", i, m_gl_textures.at(i), bound_texture, m_gl_samplers.at(i), bound_sampler);
        }
        log_texture_heap->trace("bind {}", ss.str());

        if (!ok) {
            for (uint32_t i = 0, end = m_device.get_info().max_per_stage_descriptor_samplers; i < end; ++i) {
                gl::bind_texture_unit(i, m_gl_textures.at(i));
                const gl::Texture_target gl_target         = m_textures.at(i)->get_impl().get_gl_texture_target();
                const gl::Get_p_name     gl_binding_p_name = get_binding_p_name(gl_target);
                GLint bound_texture{0};
                gl::get_integer_iv(gl_binding_p_name, static_cast<int>(i), &bound_texture);
                if (bound_texture == static_cast<GLint>(m_gl_textures.at(i))) {
                    log_texture_heap->trace("glBindTextureUnit({}, {}) ok", i, m_gl_textures.at(i));
                } else {
                    log_texture_heap->trace("glBindTextureUnit({}, {}) failed - texture unit binding is {}", i, m_gl_textures.at(i), bound_texture);
                }
            }
        }
        ERHE_VERIFY(ok);
#endif
        return m_used_slot_count;
    }
}

} // namespace erhe::graphics
