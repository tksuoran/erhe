// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_texture_heap.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
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
    // log_texture_frame->trace("Texture_heap_impl::Texture_heap_impl()");

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
        m_textures.resize(device.get_info().max_texture_image_units);
        m_samplers.resize(device.get_info().max_texture_image_units);
        m_gl_textures.resize(device.get_info().max_texture_image_units);
        m_gl_samplers.resize(device.get_info().max_texture_image_units);
        m_zero_vector.resize(device.get_info().max_texture_image_units);
        reset();
    }
}

Texture_heap_impl::~Texture_heap_impl()
{
    // log_texture_frame->trace("Texture_heap_impl::~Texture_heap_impl()");
}

void Texture_heap_impl::reset()
{
    // log_texture_frame->trace("Texture_heap_impl::reset()");

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
        // log_texture_frame->trace("assigned texture heap slot {} for texture {}, sampler {} bindless handle {}", slot, texture->gl_name(), sampler->gl_name(), format_texture_handle(gl_bindless_texture_handle));
        return gl_bindless_texture_handle;

    } else {

        m_textures   [slot] = texture;
        m_samplers   [slot] = sampler;
        m_gl_textures[slot] = texture->get_impl().gl_name();
        m_gl_samplers[slot] = sampler->get_impl().gl_name();
        // log_texture_frame->trace("assigned texture heap slot {} for texture {}, sampler {}", slot, texture->gl_name(), sampler->gl_name());
        return slot;
    }
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // const GLuint texture_name = texture->gl_name(); // get_texture_from_handle(handle);
    // const GLuint sampler_name = sampler->gl_name(); // get_sampler_from_handle(handle);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            // log_texture_frame->trace("cache hit texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
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
        ++m_used_slot_count;
        // log_texture_frame->trace(
        //     "allocated texture heap slot {} for texture {}, sampler {} bindless handle = {}",
        //     slot, texture_name, sampler_name, format_texture_handle(gl_bindless_texture_handle)
        // );
        return gl_bindless_texture_handle;

    } else {

        if (m_reserved_slot_count + m_used_slot_count < m_textures.size()) {
            const std::size_t slot = m_reserved_slot_count + m_used_slot_count;
            m_textures   [slot] = texture;
            m_samplers   [slot] = sampler;
            m_gl_textures[slot] = texture->get_impl().gl_name();
            m_gl_samplers[slot] = sampler->get_impl().gl_name();
            ++m_used_slot_count;
            // log_texture_frame->trace("allocated texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
            return static_cast<uint64_t>(slot - m_reserved_slot_count);
        }

        // log_texture_frame->trace("texture heap is full, unable to allocate slot for texture {}, sampler {}", texture_name, sampler_name);
        return {};
    }
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap_impl::unbind()
{
    // log_texture_frame->trace("Texture_heap_impl::unbind()");

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                // log_texture_frame->trace(
                //     "making texture handle {} non-resident / texture {}, sampler {}",
                //     format_texture_handle(gl_bindless_texture_handle),
                //     m_textures[slot]->gl_name(),
                //     m_samplers[slot]->gl_name()
                // );
                gl::make_texture_handle_non_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = false;
            }
        }
    } else {
        gl::bind_textures(0, m_device.get_info().max_texture_image_units, m_zero_vector.data());
        gl::bind_samplers(0, m_device.get_info().max_texture_image_units, m_zero_vector.data());
    }
}

// TODO Maybe this should use Render_command_encoder?
auto Texture_heap_impl::bind() -> std::size_t
{
    // log_texture_frame->trace("Texture_heap_impl::bind()");

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (!m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                // log_texture_frame->trace(
                //     "making texture handle {} resident / texture {}, sampler {}",
                //     format_texture_handle(gl_bindless_texture_handle),
                //     m_textures[slot]->gl_name(),
                //     m_samplers[slot]->gl_name()
                // );
                gl::make_texture_handle_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = true;
            }
        }
        return m_used_slot_count;

    } else {

        gl::bind_textures(0, m_device.get_info().max_texture_image_units, m_gl_textures.data());
        gl::bind_samplers(0, m_device.get_info().max_texture_image_units, m_gl_samplers.data());
        return m_used_slot_count;
    }
}

} // namespace erhe::graphics
