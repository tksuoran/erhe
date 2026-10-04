#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_debug.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <mutex>
#include <optional>

namespace erhe::graphics {

void dump_fbo_attachment(Device& device, const int fbo_name, const gl::Framebuffer_attachment attachment)
{
    ERHE_PROFILE_FUNCTION();

    int type{0};
    gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_type, &type);
    if (type != GL_NONE) {
        int name{0};
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_name, &name);
        int samples        {0};
        int width          {0};
        int height         {0};
        int internal_format{0};
        std::string debug_label{};
        if (device.get_info().use_debug_output) {
            GLsizei length{0};
            gl::Object_identifier gl_type = static_cast<gl::Object_identifier>(type);
            gl::get_object_label(gl_type, name, 0, &length, nullptr);
            if (length > 0) {
                debug_label.resize(length + 1);
                gl::get_object_label(gl_type, name, length + 1, nullptr, debug_label.data());
                debug_label.resize(length);
            }
        }
        if (type == GL_RENDERBUFFER) {
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_samples,         &samples);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_width,           &width);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_height,          &height);
            gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_internal_format, &internal_format);
        }
        if (type == GL_TEXTURE) {
            int level{0};
            gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_texture_level, &level);
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_width,           &width);
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_height,          &height);
            gl::get_texture_level_parameter_iv(name, level, gl::Get_texture_parameter::texture_internal_format, &internal_format);
            gl::get_texture_level_parameter_iv(
                name,
                level,
                static_cast<gl::Get_texture_parameter>(GL_TEXTURE_SAMPLES), // TODO gl_extra
                &samples
            );
        }

        int component_type{0};
        int red_size{0};
        int green_size{0};
        int blue_size{0};
        int alpha_size{0};
        int depth_size{0};
        int stencil_size{0};
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_component_type, &component_type);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_red_size, &red_size);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_green_size, &green_size);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_blue_size, &blue_size);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_alpha_size, &alpha_size);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_depth_size, &depth_size);
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment,
            gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_stencil_size, &stencil_size);

        log_render_pass->info(
            "\t{} {} attachment {} {} samples = {} size = {} x {} format = {} component_type = {}, RGBA size = {}.{}.{}.{}, DS size = {}.{}",
            debug_label,
            c_str(attachment),
            gl::enum_string(type),
            name,
            samples,
            width,
            height,
            gl::enum_string(internal_format),
            gl::enum_string(component_type),
            red_size, green_size, blue_size, alpha_size,
            depth_size, stencil_size
        );
    }
}

void dump_fbo(Device& device, const int fbo_name)
{
    int samples       {0};
    int sample_buffers{0};
    gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::samples, &samples);
    gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::sample_buffers, &sample_buffers);

    log_render_pass->info(
        "FBO {} uses {} samples {} sample buffers",
        fbo_name,
        samples,
        sample_buffers
    );

    dump_fbo_attachment(device, fbo_name, gl::Framebuffer_attachment::color_attachment0);
    dump_fbo_attachment(device, fbo_name, gl::Framebuffer_attachment::depth_attachment);
    dump_fbo_attachment(device, fbo_name, gl::Framebuffer_attachment::stencil_attachment);
}


// TODO move to graphics::Device?
thread_local Render_pass_impl* Device_impl::s_active_render_pass = nullptr;


Render_pass_impl::Render_pass_impl(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_swapchain           {descriptor.swapchain}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name      {erhe::utility::Debug_label{fmt::format("Render pass: {}", descriptor.debug_label.string_view())}}
    , m_begin_debug_group_name{erhe::utility::Debug_label{fmt::format("Render_pass_impl::start_render_pass() {}", descriptor.debug_label.string_view())}}
    , m_end_debug_group_name  {erhe::utility::Debug_label{fmt::format("Render_pass_impl::end_render_pass() {}", descriptor.debug_label.string_view())}}
{
    auto check_multisample_resolve = [this](const Render_pass_attachment_descriptor& attachment)
    {
        if (!attachment.is_defined()) {
            return;
        }
        if (
            (attachment.store_action == Store_action::Multisample_resolve) ||
            (attachment.store_action == Store_action::Store_and_multisample_resolve)
        ) {
            m_uses_multisample_resolve = true;
        }
    };
    for (const Render_pass_attachment_descriptor& color_attachment : m_color_attachments) {
        check_multisample_resolve(color_attachment);
    }
    check_multisample_resolve(m_depth_attachment);
    check_multisample_resolve(m_stencil_attachment);

    // m_draw_buffers depends only on the swapchain / attachment descriptors,
    // which are set above and never change - so it is computed once here.
    // Per-context framebuffer creation must not write shared members: two
    // contexts adopting the same pass concurrently would otherwise clear and
    // refill this vector under each other.
    if (m_swapchain != nullptr) {
        m_draw_buffers.push_back(gl::Color_buffer::back);
    } else {
        unsigned int color_index = 0;
        for (const Render_pass_attachment_descriptor& attachment : m_color_attachments) {
            if (attachment.texture != nullptr) {
                m_draw_buffers.push_back(
                    static_cast<gl::Color_buffer>(static_cast<unsigned int>(gl::Color_buffer::color_attachment0) + color_index)
                );
            }
            ++color_index;
        }
    }

    ensure_created_on_current_context();
}

Render_pass_impl::~Render_pass_impl() noexcept
{
    // Deleting a per-context GL object is only possible on its own context:
    // the destroying thread deletes its own context's instances directly
    // and queues every other context's names for that context to delete at
    // its drain point. The mutex excludes a concurrent first-use adoption
    // storing fresh names into a slot the walk has already passed.
    const std::lock_guard<std::mutex> lock{m_adoption_mutex};
    const int context_index = get_gl_context_index();
    for (int slot = 0; slot < gl_context_slot_count; ++slot) {
        unsigned int name = m_context_slots[slot].framebuffer.load(std::memory_order_relaxed);
        unsigned int multisample_resolve_name = m_context_slots[slot].multisample_resolve_framebuffer.load(std::memory_order_relaxed);
        if (name != 0) {
            if (slot == context_index) {
                m_device.get_impl().get_binding_state().on_framebuffer_deleted(name);
                gl::delete_framebuffers(1, &name);
            } else {
                m_device.get_impl().queue_framebuffer_delete_on_context(slot, name);
            }
            m_context_slots[slot].framebuffer.store(0, std::memory_order_relaxed);
        }
        if (multisample_resolve_name != 0) {
            if (slot == context_index) {
                m_device.get_impl().get_binding_state().on_framebuffer_deleted(multisample_resolve_name);
                gl::delete_framebuffers(1, &multisample_resolve_name);
            } else {
                m_device.get_impl().queue_framebuffer_delete_on_context(slot, multisample_resolve_name);
            }
            m_context_slots[slot].multisample_resolve_framebuffer.store(0, std::memory_order_relaxed);
        }
    }
}

auto Render_pass_impl::ensure_created_on_current_context() const -> unsigned int
{
    ERHE_PROFILE_FUNCTION();

    if (m_swapchain != nullptr) {
        // Default framebuffer; nothing to create on any context.
        return 0;
    }

    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);

    // Populated fast path: relaxed load of our own slot, no lock.
    Context_slot& slot = m_context_slots[context_index];
    const unsigned int existing_name = slot.framebuffer.load(std::memory_order_relaxed);
    if (existing_name != 0) {
        return existing_name;
    }

    // First use on this context: create and attach under the adoption
    // mutex, so destruction cannot race a concurrent adoption.
    const std::lock_guard<std::mutex> lock{m_adoption_mutex};
    const unsigned int recheck_name = slot.framebuffer.load(std::memory_order_relaxed);
    if (recheck_name != 0) {
        return recheck_name;
    }

    GLuint framebuffer_name{0};
    gl::create_framebuffers(1, &framebuffer_name);
    ERHE_VERIFY(framebuffer_name != 0);
    GLuint multisample_resolve_name{0};
    if (m_uses_multisample_resolve) {
        gl::create_framebuffers(1, &multisample_resolve_name);
        ERHE_VERIFY(multisample_resolve_name != 0);
    }

    auto process_attachment = [](
        const GLuint                             fbo_name,
        const gl::Framebuffer_attachment         attachment_point,
        const Render_pass_attachment_descriptor& attachment
    ) {
        if (attachment.texture != nullptr) {
            ERHE_VERIFY(attachment.texture->get_width() >= 1);
            ERHE_VERIFY(attachment.texture->get_height() >= 1);
            if (attachment.texture->is_layered()) {
                gl::named_framebuffer_texture_layer(
                    fbo_name,
                    attachment_point,
                    attachment.texture->get_impl().gl_name(),
                    attachment.texture_level,
                    attachment.texture_layer
                );
            } else {
                gl::named_framebuffer_texture(
                    fbo_name,
                    attachment_point,
                    attachment.texture->get_impl().gl_name(),
                    attachment.texture_level
                );
            }
            return true;
        }
        return false;
    };

    auto process_multisample_resolve_attachment = [](
        const GLuint                       fbo_name,
        const gl::Framebuffer_attachment         attachment_point,
        const Render_pass_attachment_descriptor& attachment
    ) {
        if (attachment.resolve_texture != nullptr) {
            ERHE_VERIFY(attachment.resolve_texture->get_width() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_height() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_sample_count() <= 1);
            if (attachment.resolve_texture->is_layered()) {
                gl::named_framebuffer_texture_layer(
                    fbo_name,
                    attachment_point,
                    attachment.resolve_texture->get_impl().gl_name(),
                    attachment.resolve_level,
                    attachment.resolve_layer
                );
            } else {
                gl::named_framebuffer_texture(
                    fbo_name,
                    attachment_point,
                    attachment.resolve_texture->get_impl().gl_name(),
                    attachment.resolve_level
                );
            }
        }
    };

    {
        unsigned int color_index = 0;
        for (const Render_pass_attachment_descriptor& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(
                static_cast<unsigned int>(gl::Framebuffer_attachment::color_attachment0) + color_index
            );
            process_attachment(framebuffer_name, attachment_point, attachment);
            ++color_index;
        }
    }
    process_attachment(framebuffer_name, gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
    process_attachment(framebuffer_name, gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);

    if (!m_draw_buffers.empty()) {
        gl::named_framebuffer_draw_buffers(framebuffer_name, static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer(framebuffer_name, m_draw_buffers.front());
    } else {
        // No color attachments (e.g. depth-only shadow maps): set draw/read
        // buffer to GL_NONE so the framebuffer is complete.
        gl::named_framebuffer_draw_buffers(framebuffer_name, 0, nullptr);
        gl::named_framebuffer_read_buffer(framebuffer_name, gl::Color_buffer::none);
    }

    if (m_device.get_info().use_debug_output) {
        erhe::utility::Debug_label debug_label{ fmt::format("(F:{}) {}", framebuffer_name, m_debug_label.string_view()) };
        gl::object_label(gl::Object_identifier::framebuffer, framebuffer_name, -1, debug_label.data());
    }

    if (m_uses_multisample_resolve) {
        unsigned int color_index = 0;
        for (const Render_pass_attachment_descriptor& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(static_cast<unsigned int>(
                gl::Framebuffer_attachment::color_attachment0) + color_index
            );
            process_multisample_resolve_attachment(multisample_resolve_name, attachment_point, attachment);
        }
        process_multisample_resolve_attachment(multisample_resolve_name, gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
        process_multisample_resolve_attachment(multisample_resolve_name, gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);

        const std::string multisample_resolve_debug_label = fmt::format(
            "(F:{}) {} Multisample Resolve",
            multisample_resolve_name, m_debug_label.string_view()
        );
        if (m_device.get_info().use_debug_output) {
            gl::object_label(gl::Object_identifier::framebuffer, multisample_resolve_name, -1, multisample_resolve_debug_label.c_str());
        }
    }

    // Publish the names; gl_name() reads on this context from here on.
    slot.framebuffer.store(framebuffer_name, std::memory_order_relaxed);
    if (m_uses_multisample_resolve) {
        slot.multisample_resolve_framebuffer.store(multisample_resolve_name, std::memory_order_relaxed);
    }

    ERHE_VERIFY(check_status());
    return framebuffer_name;
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    // Texture sample counts 0 and 1 both mean single-sample (gl_texture.hpp
    // is_multisample_sample_count); every attachment must agree once
    // normalized, and a pass without attachments is single-sample like on
    // the other backends.
    std::optional<int> sample_count{};
    auto update_sample_count = [&sample_count](const int sample_count_in_raw){
        const int sample_count_in = std::max(1, sample_count_in_raw);
        if (!sample_count.has_value()) {
            sample_count = sample_count_in;
        } else {
            ERHE_VERIFY(sample_count.value() == sample_count_in);
        }
    };

    for (const Render_pass_attachment_descriptor& attachment : m_color_attachments) {
        if (attachment.texture != nullptr) {
            update_sample_count(attachment.texture->get_sample_count());
        }
        //// if (attachment.renderbuffer != nullptr) {
        ////     update_sample_count(attachment.renderbuffer->get_sample_count());
        //// }
    }
    if (m_depth_attachment.texture != nullptr) {
        update_sample_count(m_depth_attachment.texture->get_sample_count());
    }
    if (m_stencil_attachment.texture != nullptr) {
        update_sample_count(m_stencil_attachment.texture->get_sample_count());
    }
    return sample_count.has_value() ? static_cast<unsigned int>(sample_count.value()) : 1u;
}

auto Render_pass_impl::check_status() const -> bool
{
#if !defined(NDEBUG)
    gl::Framebuffer_status status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
    if (status != gl::Framebuffer_status::framebuffer_complete) {
        log_render_pass->warn(
            "Render_pass_impl {} FBO {} not complete: {}",
            get_debug_label().string_view(), gl_name(), gl::c_str(status)
        );
        dump_fbo(m_device, gl_name());
        return false;
    }

    if (m_uses_multisample_resolve) {
        status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
        if (status != gl::Framebuffer_status::framebuffer_complete) {
            log_render_pass->warn(
                "Render_pass_impl {} multisample resolve FBO {} not complete: {}",
                get_debug_label().string_view(), gl_multisample_resolve_name(), gl::c_str(status)
            );
            dump_fbo(m_device, gl_multisample_resolve_name());
            return false;
        }
    }
#endif
    return true;
}

auto Render_pass_impl::gl_name() const -> unsigned int
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_context_slots[context_index].framebuffer.load(std::memory_order_relaxed);
}

auto Render_pass_impl::gl_multisample_resolve_name() const -> unsigned int
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_context_slots[context_index].multisample_resolve_framebuffer.load(std::memory_order_relaxed);
}

auto Render_pass_impl::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass_impl::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass_impl::get_swapchain() const -> Swapchain*
{
    return m_swapchain;
}

auto Render_pass_impl::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Render_pass_impl::start_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_before, Render_pass* const render_pass_after)
{
    // The OpenGL driver handles all cross-pass synchronization implicitly,
    // so these hints are ignored here. command_buffer is unused on GL --
    // there is no native command buffer to record into.
    static_cast<void>(command_buffer);
    static_cast<void>(render_pass_before);
    static_cast<void>(render_pass_after);

    ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT();
    ERHE_VERIFY(Device_impl::s_active_render_pass == nullptr);
    Device_impl::s_active_render_pass = this;
    ERHE_VERIFY(!m_is_active);
    m_is_active = true;

    m_outer_debug_group = std::make_unique<Scoped_debug_group>(command_buffer, m_debug_group_name);
    Scoped_debug_group begin_debug_group{command_buffer, m_begin_debug_group_name};

    if (m_device.get_info().vendor == Vendor::Nvidia) {
        // Workaround for https://developer.nvidia.com/bugs/5799090
        m_device.get_impl().reset_shader_stages_state_tracker();
    }

    // Resolve the calling thread's per-context caches once for the pass.
    OpenGL_state_tracker& tracker = m_device.get_impl().get_state_tracker();

    m_device.get_impl().get_binding_state().bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());

    if ((m_render_target_width > 0) && (m_render_target_height > 0)) {
        tracker.viewport_rect.execute(
            Viewport_rect_state{
                .x      = 0.0f,
                .y      = 0.0f,
                .width  = static_cast<float>(m_render_target_width),
                .height = static_cast<float>(m_render_target_height)
            }
        );
        tracker.scissor.execute(
            Scissor_state{
                .x      = 0,
                .y      = 0,
                .width  = m_render_target_width,
                .height = m_render_target_height
            }
        );
    }

    // To be able to clear color the color write masks must be enabled (part of color blend state)
    tracker.color_blend.execute(
        Color_blend_state{
            .write_mask = {
                .red   = true,
                .green = true,
                .blue  = true,
                .alpha = true
            }
        }
    );
    // To be able to clear depth/stencil the depth write masks / stencil must write_mask be enabled (part of depth stencil state)
    tracker.depth_stencil.execute(
        Depth_stencil_state{
            .depth_write_enable = true,
            .stencil_front = {
                .write_mask = 0xffu
            },
            .stencil_back = {
                .write_mask = 0xffu
            }
        }
    );

#if !defined(NDEBUG)
    if (m_swapchain == nullptr) {
        const gl::Framebuffer_status status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
        ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
    }
#endif

    const GLint name = gl_name();
    for (size_t color_index = 0; color_index < m_color_attachments.size(); ++color_index) {
        const Render_pass_attachment_descriptor& attachment = m_color_attachments[color_index];
        if ((m_swapchain != nullptr) && (color_index > 0)) {
            continue;
        }
        if ((m_swapchain == nullptr) && !attachment.is_defined()) {
            continue;
        }
        if (attachment.load_action == Load_action::Clear) {
            const erhe::dataformat::Format      pixelformat = attachment.get_pixelformat();
            const erhe::dataformat::Format_kind format_kind =
                (m_swapchain != nullptr)
                    ? erhe::dataformat::Format_kind::format_kind_float // default framebuffer is always unorm
                    : erhe::dataformat::get_format_kind(pixelformat);
            switch (format_kind) {
                case erhe::dataformat::Format_kind::format_kind_float: {
                    ERHE_VERIFY(
                        (
                            (name == 0) && (color_index == 0) && (m_draw_buffers[0] == gl::Color_buffer::back)
                        ) ||
                        (
                            (name != 0) &&
                            (
                                m_draw_buffers[color_index] == 
                                    static_cast<gl::Color_buffer>(
                                        static_cast<unsigned int>(gl::Color_buffer::color_attachment0)
                                        + color_index
                                )
                            )
                        )
                    );
                    const GLfloat f[4] = {
                        static_cast<GLfloat>(attachment.clear_value[0]),
                        static_cast<GLfloat>(attachment.clear_value[1]),
                        static_cast<GLfloat>(attachment.clear_value[2]),
                        static_cast<GLfloat>(attachment.clear_value[3])
                    };

                    gl::clear_named_framebuffer_fv(name, gl::Buffer::color, static_cast<GLint>(color_index), &f[0]);
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                    const GLint i[4] = {
                        static_cast<GLint>(attachment.clear_value[0]),
                        static_cast<GLint>(attachment.clear_value[1]),
                        static_cast<GLint>(attachment.clear_value[2]),
                        static_cast<GLint>(attachment.clear_value[3])
                    };
                    gl::clear_named_framebuffer_iv(name, gl::Buffer::color, static_cast<GLint>(color_index), &i[0]);
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                    const GLuint ui[4] = {
                        static_cast<GLuint>(attachment.clear_value[0]),
                        static_cast<GLuint>(attachment.clear_value[1]),
                        static_cast<GLuint>(attachment.clear_value[2]),
                        static_cast<GLuint>(attachment.clear_value[3])
                    };
                    gl::clear_named_framebuffer_uiv(name, gl::Buffer::color, static_cast<GLint>(color_index), &ui[0]);
                    break;
                }
                default: {
                    ERHE_FATAL("TODO");
                }
            }
        }
    }
    const bool clear_depth   = ((m_swapchain != nullptr) && m_swapchain->has_depth  ()) || (m_depth_attachment  .is_defined() && (m_depth_attachment  .load_action == Load_action::Clear));
    const bool clear_stencil = ((m_swapchain != nullptr) && m_swapchain->has_stencil()) || (m_stencil_attachment.is_defined() && (m_stencil_attachment.load_action == Load_action::Clear));
    if (clear_depth && clear_stencil) {
        gl::clear_named_framebufferf_i(
            name,
            gl::Buffer::depth_stencil,
            0,
            static_cast<float>(m_depth_attachment.clear_value[0]),
            static_cast<GLint>(m_stencil_attachment.clear_value[0])
        );
    } else {
        if (clear_depth) {
            const GLfloat f[4] = {
                static_cast<GLfloat>(m_depth_attachment.clear_value[0]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[1]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[2]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[3])
            };
            gl::clear_named_framebuffer_fv(name, gl::Buffer::depth, 0, &f[0]);
        }
        if (clear_stencil) {
            const GLuint ui[4] = {
                static_cast<GLuint>(m_stencil_attachment.clear_value[0]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[1]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[2]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[3])
            };
            gl::clear_named_framebuffer_uiv(name, gl::Buffer::stencil, 0, &ui[0]);
        }
    }

}

void Render_pass_impl::end_render_pass(Command_buffer& command_buffer, Render_pass* const render_pass_after)
{
    static_cast<void>(render_pass_after);

    ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT();
    ERHE_VERIFY(Device_impl::s_active_render_pass == this);
    Device_impl::s_active_render_pass = nullptr;

    ERHE_VERIFY(m_is_active);
    m_is_active = false;

    Scoped_debug_group end_debug_group{command_buffer, m_end_debug_group_name};

    std::array<gl::Framebuffer_attachment, 4> color_attachment_points = {
        gl::Framebuffer_attachment::color_attachment0,
        gl::Framebuffer_attachment::color_attachment1,
        gl::Framebuffer_attachment::color_attachment2,
        gl::Framebuffer_attachment::color_attachment3
    };
    std::array<gl::Invalidate_framebuffer_attachment, 8> invalidate_attachments;
    int invalidate_attachments_count = 0;

    auto check_multisample_resolve = [this](const Render_pass_attachment_descriptor& attachment, int& blit_width, int& blit_height) -> bool
    {
        if (!attachment.is_defined() || (attachment.resolve_texture == nullptr)) {
            return false;
        }
        if (
            (attachment.store_action != Store_action::Multisample_resolve) &&
            (attachment.store_action != Store_action::Store_and_multisample_resolve)
        ) {
            return false;
        }
        const int src_width  = (attachment.texture != nullptr) ? attachment.texture->get_width (attachment.texture_level) : 0; //attachment.renderbuffer->get_width();
        const int src_height = (attachment.texture != nullptr) ? attachment.texture->get_height(attachment.texture_level) : 0; //attachment.renderbuffer->get_height();
        const int dst_width  = attachment.resolve_texture->get_width (attachment.texture_level);
        const int dst_height = attachment.resolve_texture->get_height(attachment.texture_level);
        blit_width  = std::min(src_width, dst_width);
        blit_height = std::min(src_height, dst_height);
        return (blit_width > 0) && (blit_height > 0);
    };

    if (m_uses_multisample_resolve) {
        m_device.get_impl().get_binding_state().bind_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
#if !defined(NDEBUG)
        {
            const gl::Framebuffer_status status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::read_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete) {
                log_render_pass->error(
                    "{} multisample resolve source BlitFramebuffer read framebuffer status = {}",
                    gl_name(),
                    gl::c_str(status)
                );
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif

        m_device.get_impl().get_binding_state().bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name());
#if !defined(NDEBUG)
        {
            const gl::Framebuffer_status status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete) {
                log_render_pass->error(
                    "{} multisample resolve destination BlitFramebuffer draw framebuffer status = {}",
                    gl_multisample_resolve_name(),
                    gl::c_str(status)
                );
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif
        int blit_width = 0;
        int blit_height = 0;
        for (size_t color_index = 0; color_index < m_color_attachments.size(); ++color_index) {
            if ((m_swapchain != nullptr) && (color_index > 0)) {
                continue;
            }
            const Render_pass_attachment_descriptor& attachment = m_color_attachments[color_index];
            if (check_multisample_resolve(attachment, blit_width, blit_height)) {
                const gl::Color_buffer color_buffer = static_cast<gl::Color_buffer>(static_cast<unsigned int>(gl::Color_buffer::color_attachment0) + color_index);
                // Read and draw FBOs are already bound above
                gl::named_framebuffer_read_buffer(gl_name(), color_buffer);
                gl::named_framebuffer_draw_buffers(gl_multisample_resolve_name(), 1, &color_buffer);
                gl::blit_named_framebuffer(
                    gl_name(),
                    gl_multisample_resolve_name(),
                    0, 0, blit_width, blit_height,
                    0, 0, blit_width, blit_height,
                    gl::Clear_buffer_mask::color_buffer_bit,
                    gl::Blit_framebuffer_filter::linear
                );
            }
        }
        // Restore draw buffer state
        gl::named_framebuffer_draw_buffers(gl_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer (gl_name(), gl::Color_buffer::color_attachment0);
        gl::named_framebuffer_draw_buffers(gl_multisample_resolve_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer (gl_multisample_resolve_name(), gl::Color_buffer::color_attachment0);

        // NOTE: Depth/stencil blit does not involve draw buffers.
        // glBlitFramebuffer requires GL_NEAREST when the mask includes depth
        // or stencil -- GL_LINEAR is GL_INVALID_OPERATION for depth/stencil
        // resolves.
        if (check_multisample_resolve(m_depth_attachment, blit_width, blit_height)) {
            gl::blit_named_framebuffer(
                gl_name(),
                gl_multisample_resolve_name(),
                0, 0, blit_width, blit_height,
                0, 0, blit_width, blit_height,
                gl::Clear_buffer_mask::depth_buffer_bit,
                gl::Blit_framebuffer_filter::nearest
            );
        }
        if (check_multisample_resolve(m_stencil_attachment, blit_width, blit_height)) {
            gl::blit_named_framebuffer(
                gl_name(),
                gl_multisample_resolve_name(),
                0, 0, blit_width, blit_height,
                0, 0, blit_width, blit_height,
                gl::Clear_buffer_mask::stencil_buffer_bit,
                gl::Blit_framebuffer_filter::nearest
            );
        }
    }

    auto check_invalidate_attachment = [&invalidate_attachments, &invalidate_attachments_count](const Render_pass_attachment_descriptor& attachment, gl::Framebuffer_attachment attachment_point)
    {
        if (attachment.texture != nullptr) { // || (attachment.renderbuffer != nullptr)){
            if (attachment.store_action == Store_action::Dont_care) {
                ERHE_VERIFY(invalidate_attachments_count < static_cast<int>(invalidate_attachments.size()));
                invalidate_attachments[invalidate_attachments_count++] = static_cast<gl::Invalidate_framebuffer_attachment>(attachment_point);
            }
        }
    };

    for (size_t color_index = 0; color_index < m_color_attachments.size(); ++color_index) {
        if ((m_swapchain != nullptr) && (color_index > 0)) {
            continue;
        }
        check_invalidate_attachment(m_color_attachments[color_index], color_attachment_points[color_index]);
    }
    check_invalidate_attachment(m_depth_attachment,   gl::Framebuffer_attachment::depth_attachment);
    check_invalidate_attachment(m_stencil_attachment, gl::Framebuffer_attachment::stencil_attachment);

    if (invalidate_attachments_count > 0) {
        m_device.get_impl().get_binding_state().bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
        //gl::invalidate_framebuffer(gl::Framebuffer_target::draw_framebuffer, invalidate_attachments_count, invalidate_attachments.data());
    }

    // TODO Strictly speaking this is redundant, but might be useful for debugging
    m_device.get_impl().get_binding_state().bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

    m_outer_debug_group.reset();
}

} // namespace erhe::graphics
