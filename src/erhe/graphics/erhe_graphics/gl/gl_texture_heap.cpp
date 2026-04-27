#include "erhe_graphics/gl/gl_texture_heap.hpp"
#include "erhe_graphics/gl/gl_bind_group_layout.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_gl/wrapper_functions.hpp"

// Uncomment the next two lines to enable extra logging and checking.
#define ERHE_TEXTURE_HEAP_LOG 1
#define ERHE_TEXTURE_HEAP_PARANOID_SANITY_CHECKS 1

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    const Bind_group_layout*   bind_group_layout
)
    : m_device             {device}
    , m_fallback_texture   {fallback_texture}
    , m_fallback_sampler   {fallback_sampler}
    , m_used_slot_count    {0}
{
#if ERHE_TEXTURE_HEAP_LOG
    log_texture_heap->trace("Texture_heap_impl::Texture_heap_impl()");
#endif

    // Sampler-array path: derive the texture-unit offset and count from the
    // GL Bind_group_layout_impl, which knows which texture units are claimed
    // by explicit combined_image_sampler bindings (bound by set_sampled_image)
    // and which units are left for the heap's flat material array.
    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        reset_heap();
    } else {
        if (bind_group_layout != nullptr) {
            const Bind_group_layout_impl& layout_impl = bind_group_layout->get_impl();
            m_sampler_array_offset = layout_impl.get_texture_heap_base_unit();
        }
        const uint32_t max_units = device.get_info().max_per_stage_descriptor_samplers;
        m_textures   .resize(max_units);
        m_samplers   .resize(max_units);
        m_gl_textures.resize(max_units);
        m_gl_samplers.resize(max_units);
        m_zero_vector.resize(max_units);
        reset_heap();
    }
}

Texture_heap_impl::~Texture_heap_impl() noexcept
{
#if ERHE_TEXTURE_HEAP_LOG
    log_texture_heap->trace("Texture_heap_impl::~Texture_heap_impl()");
#endif
}

void Texture_heap_impl::reset_heap()
{
    Scoped_debug_group debug_group{"Texture_heap_impl::reset_heap()"};

#if ERHE_TEXTURE_HEAP_LOG
    log_texture_heap->trace("Texture_heap_impl::reset_heap()");
#endif

    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        m_textures.clear();
        m_samplers.clear();
        m_gl_bindless_texture_handles.clear();
        m_gl_bindless_texture_resident.clear();
    } else {
        const GLuint fallback_texture_name = m_fallback_texture.get_impl().gl_name();
        const GLuint fallback_sampler_name = m_fallback_sampler.get_impl().gl_name();
        std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
        std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
        std::fill(m_gl_textures.begin(), m_gl_textures.end(), fallback_texture_name);
        std::fill(m_gl_samplers.begin(), m_gl_samplers.end(), fallback_sampler_name);
    }
    m_used_slot_count = 0;
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    ERHE_VERIFY(texture != nullptr);
    ERHE_VERIFY(sampler != nullptr);

    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        for (std::size_t slot = 0; slot < m_used_slot_count; ++slot) {
            if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
                return m_gl_bindless_texture_handles[slot];
            }
        }
    } else {
        for (std::size_t slot = m_sampler_array_offset; slot < m_sampler_array_offset + m_used_slot_count; ++slot) {
            if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
                return static_cast<uint64_t>(slot - m_sampler_array_offset);
            }
        }
    }

    // Not found. Emit enough context to diagnose Pass 1 / Pass 2 mismatches
    // (the same draw-cmd's texture_reference resolving to different Texture
    // pointers between allocate() and get_shader_handle() calls).
    log_texture_heap->error(
        "Texture_heap::get_shader_handle miss: looking for texture gl={} '{}' ptr={} {}x{} sampler gl={} '{}' ptr={}",
        texture->get_impl().gl_name(),
        texture->get_debug_label().string_view(),
        static_cast<const void*>(texture),
        texture->get_width(),
        texture->get_height(),
        sampler->get_impl().gl_name(),
        sampler->get_debug_label().string_view(),
        static_cast<const void*>(sampler)
    );
    const std::size_t heap_begin =
        (m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)
            ? std::size_t{0}
            : m_sampler_array_offset;
    const std::size_t heap_end = heap_begin + m_used_slot_count;
    for (std::size_t slot = heap_begin; slot < heap_end; ++slot) {
        const Texture* slot_tex = m_textures[slot];
        const Sampler* slot_smp = m_samplers[slot];
        log_texture_heap->error(
            "  heap[{}] texture gl={} '{}' ptr={} sampler gl={} '{}' ptr={}",
            slot,
            slot_tex ? slot_tex->get_impl().gl_name()                 : GLuint{0},
            slot_tex ? slot_tex->get_debug_label().string_view()      : std::string_view{"<null>"},
            static_cast<const void*>(slot_tex),
            slot_smp ? slot_smp->get_impl().gl_name()                 : GLuint{0},
            slot_smp ? slot_smp->get_debug_label().string_view()      : std::string_view{"<null>"},
            static_cast<const void*>(slot_smp)
        );
    }
    ERHE_FATAL("texture %u sampler %u not found in texture heap", texture->get_impl().gl_name(), sampler->get_impl().gl_name());
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

#if ERHE_TEXTURE_HEAP_LOG
    const GLuint texture_name = texture->get_impl().gl_name(); // get_texture_from_handle(handle);
    const GLuint sampler_name = sampler->get_impl().gl_name(); // get_sampler_from_handle(handle);
#endif

    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        for (std::size_t slot = 0; slot < m_used_slot_count; ++slot) {
            if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
#if ERHE_TEXTURE_HEAP_LOG
                log_texture_heap->trace("cache hit texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
#endif
                return m_gl_bindless_texture_handles[slot];
            }
        }

#if ERHE_TEXTURE_HEAP_LOG
        const std::size_t slot = m_used_slot_count;
#endif
        const uint64_t gl_bindless_texture_handle = m_device.get_handle(*texture, *sampler);
        m_gl_bindless_texture_handles .push_back(gl_bindless_texture_handle);
        m_gl_bindless_texture_resident.push_back(false);
        m_textures                    .push_back(texture);
        m_samplers                    .push_back(sampler);
#if ERHE_TEXTURE_HEAP_LOG
        log_texture_heap->trace(
            "allocated texture heap slot {} for texture {} {}, sampler {} {} bindless handle = {}",
            slot,
            texture_name, texture->get_debug_label().string_view(),
            sampler_name, sampler->get_debug_label().string_view(),
            format_texture_handle(gl_bindless_texture_handle)
        );
#endif
        ++m_used_slot_count;
        return gl_bindless_texture_handle;

    } else {

        // Sampler-array path: search and allocate within the heap's region
        // [m_sampler_array_offset, m_sampler_array_offset + used_count). The
        // returned handle is the shader-visible material index, i.e. the
        // offset relative to the heap region's base.
        for (std::size_t slot = m_sampler_array_offset; slot < m_sampler_array_offset + m_used_slot_count; ++slot) {
            if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
#if ERHE_TEXTURE_HEAP_LOG
                log_texture_heap->trace("cache hit texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
#endif
                return static_cast<uint64_t>(slot - m_sampler_array_offset);
            }
        }

        if (m_sampler_array_offset + m_used_slot_count < m_textures.size()) {
            const std::size_t slot = m_sampler_array_offset + m_used_slot_count;
            m_textures   [slot] = texture;
            m_samplers   [slot] = sampler;
            m_gl_textures[slot] = texture->get_impl().gl_name();
            m_gl_samplers[slot] = sampler->get_impl().gl_name();
            ++m_used_slot_count;
#if ERHE_TEXTURE_HEAP_LOG
            log_texture_heap->trace(
                "allocated texture heap slot {} for texture {} {}, sampler {} {}",
                slot,
                texture_name, texture->get_debug_label().string_view(),
                sampler_name, sampler->get_debug_label().string_view()
            );
#endif
            return static_cast<uint64_t>(slot - m_sampler_array_offset);
        }

#if ERHE_TEXTURE_HEAP_LOG
        log_texture_heap->trace(
            "texture heap is full, unable to allocate slot for texture {} {}, sampler {} {}",
            texture_name, texture->get_debug_label().string_view(),
            sampler_name, sampler->get_debug_label().string_view()
        );
#endif
        return invalid_texture_handle;
    }
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap_impl::unbind()
{
    Scoped_debug_group debug_group{"Texture_heap_impl::unbind()"};

#if ERHE_TEXTURE_HEAP_LOG
    log_texture_heap->trace("Texture_heap_impl::unbind()");
#endif

    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        for (std::size_t slot = 0; slot < m_used_slot_count; ++slot) {
            if (m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
#if ERHE_TEXTURE_HEAP_LOG
                log_texture_heap->trace(
                    "making texture handle {} non-resident / texture {}, sampler {}",
                    format_texture_handle(gl_bindless_texture_handle),
                    m_textures[slot]->get_impl().gl_name(),
                    m_samplers[slot]->get_impl().gl_name()
                );
#endif
                gl::make_texture_handle_non_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = false;
            }
        }
    } else {
        // Mirror the offset used in bind(): only unbind the texture-heap
        // region, leaving dedicated sampler units alone.
        const uint32_t offset = static_cast<uint32_t>(m_sampler_array_offset);
        const uint32_t count  = m_device.get_info().max_per_stage_descriptor_samplers - offset;
        if (m_device.get_info().use_direct_state_access) {
            gl::bind_textures(offset, count, m_zero_vector.data() + offset);
            gl::bind_samplers(offset, count, m_zero_vector.data() + offset);
        } else {
            for (uint32_t i = offset; i < offset + count; ++i) {
                const gl::Texture_target gl_target = m_textures.at(i)->get_impl().get_gl_texture_target();
                m_device.get_impl().get_binding_state().bind_texture(i, gl_target, 0);
                m_device.get_impl().get_binding_state().bind_sampler(i, 0);
            }
        }
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

auto Texture_heap_impl::bind(Render_command_encoder& /*encoder*/) -> std::size_t
{
    Scoped_debug_group debug_group{"Texture_heap_impl::bind()"};

#if ERHE_TEXTURE_HEAP_LOG
    log_texture_heap->trace(
        "Texture_heap_impl::bind() {}",
        (m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)
            ? "bindless"
            : "not bindless"
    );
#endif

    if ((m_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures)) {
        // Make every allocated material handle resident. Dedicated samplers
        // are bound via Render_command_encoder::set_sampled_image() and live
        // in a separate (texture-unit) namespace; they are not in this heap.
        for (std::size_t slot = 0; slot < m_used_slot_count; ++slot) {
            if (!m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
#if ERHE_TEXTURE_HEAP_LOG
                log_texture_heap->trace(
                    "making texture handle {} resident / texture {}, sampler {}",
                    format_texture_handle(gl_bindless_texture_handle),
                    m_textures[slot]->get_impl().gl_name(),
                    m_samplers[slot]->get_impl().gl_name()
                );
#endif
                gl::make_texture_handle_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = true;
            }
        }
        return m_used_slot_count;

    } else {
        // Start from m_sampler_array_offset, the first texture unit owned
        // by the heap (anything below is a dedicated combined_image_sampler
        // bound via set_sampled_image() and must not be stomped here). The
        // offset is derived at construction from the GL Bind_group_layout_impl.
        const uint32_t offset = static_cast<uint32_t>(m_sampler_array_offset);
        const uint32_t count  = m_device.get_info().max_per_stage_descriptor_samplers - offset;
        if (m_device.get_info().use_direct_state_access) {
            gl::bind_textures(offset, count, m_gl_textures.data() + offset);
            gl::bind_samplers(offset, count, m_gl_samplers.data() + offset);
        } else {
            for (uint32_t i = offset; i < offset + count; ++i) {
                const gl::Texture_target gl_target = m_textures.at(i)->get_impl().get_gl_texture_target();
                m_device.get_impl().get_binding_state().bind_texture(i, gl_target, m_gl_textures.at(i));
                m_device.get_impl().get_binding_state().bind_sampler(i, m_gl_samplers.at(i));
            }
        }

#if ERHE_TEXTURE_HEAP_PARANOID_SANITY_CHECKS
        // Neither GL_TEXTURE_BINDING_* nor GL_SAMPLER_BINDING is a legal pname
        // for glGetIntegeri_v -- both are per-active-unit state queried via
        // glActiveTexture + glGetIntegerv. (macOS OpenGL 4.1 enforces this
        // strictly; some desktop drivers silently accept the indexed form.)
        GLint saved_active_texture{0};
        gl::get_integer_v(gl::Get_p_name::active_texture, &saved_active_texture);
        bool ok = true;
        std::stringstream ss;
        for (uint32_t i = offset, end = offset + count; i < end; ++i) {
            if (i != offset) {
                ss << ", ";
            }
            GLint bound_texture{0};
            GLint bound_sampler{0};
            const gl::Texture_target gl_target         = m_textures.at(i)->get_impl().get_gl_texture_target();
            const gl::Get_p_name     gl_binding_p_name = get_binding_p_name(gl_target);
            gl::active_texture(static_cast<gl::Texture_unit>(static_cast<unsigned int>(gl::Texture_unit::texture0) + i));
            gl::get_integer_v(gl_binding_p_name,                &bound_texture);
            gl::get_integer_v(gl::Get_p_name::sampler_binding,  &bound_sampler);
            if (bound_texture != static_cast<GLint>(m_gl_textures.at(i))) {
                ok = false;
            }
            if (bound_sampler != static_cast<GLint>(m_gl_samplers.at(i))) {
                ok = false;
            }

            ss << fmt::format("[{}] = {}/{}.{}/{}", i, m_gl_textures.at(i), bound_texture, m_gl_samplers.at(i), bound_sampler);
        }
        gl::active_texture(static_cast<gl::Texture_unit>(saved_active_texture));
        log_texture_heap->trace("bind {}", ss.str());

        if (!ok) {
            for (uint32_t i = offset, end = offset + count; i < end; ++i) {
                if (m_device.get_info().use_direct_state_access) {
                    gl::bind_texture_unit(i, m_gl_textures.at(i));
                } else {
                    const gl::Texture_target gl_target2 = m_textures.at(i)->get_impl().get_gl_texture_target();
                    m_device.get_impl().get_binding_state().bind_texture(i, gl_target2, m_gl_textures.at(i));
                }
                const gl::Texture_target gl_target         = m_textures.at(i)->get_impl().get_gl_texture_target();
                const gl::Get_p_name     gl_binding_p_name = get_binding_p_name(gl_target);
                GLint bound_texture{0};
                gl::active_texture(static_cast<gl::Texture_unit>(static_cast<unsigned int>(gl::Texture_unit::texture0) + i));
                gl::get_integer_v(gl_binding_p_name, &bound_texture);
                if (bound_texture == static_cast<GLint>(m_gl_textures.at(i))) {
                    log_texture_heap->trace("glBindTextureUnit({}, {}) ok", i, m_gl_textures.at(i));
                } else {
                    log_texture_heap->trace("glBindTextureUnit({}, {}) failed - texture unit binding is {}", i, m_gl_textures.at(i), bound_texture);
                }
            }
            gl::active_texture(static_cast<gl::Texture_unit>(saved_active_texture));
        }
        ERHE_VERIFY(ok);
#endif
        return m_used_slot_count;
    }
}

} // namespace erhe::graphics
