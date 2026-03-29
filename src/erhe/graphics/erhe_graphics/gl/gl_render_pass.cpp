#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_debug.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>
#include <thread>

namespace erhe::graphics {

void dump_fbo_attachment(Device& device, const int fbo_name, const gl::Framebuffer_attachment attachment)
{
    ERHE_PROFILE_FUNCTION();

    const bool use_dsa = device.get_info().use_direct_state_access;

    // For non-DSA, push the framebuffer so get_framebuffer_attachment_parameter_iv works
    std::optional<Framebuffer_binding_guard> fb_guard;
    if (!use_dsa) {
        fb_guard.emplace(
            device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::draw_framebuffer, fbo_name)
        );
    }

    int type{0};
    if (use_dsa) {
        gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_type, &type);
    } else {
        gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_type, &type);
    }
    if (type != GL_NONE) {
        int name{0};
        if (use_dsa) {
            gl::get_named_framebuffer_attachment_parameter_iv(fbo_name, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_name, &name);
        } else {
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_object_name, &name);
        }
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
            if (use_dsa) {
                gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_samples,         &samples);
                gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_width,           &width);
                gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_height,          &height);
                gl::get_named_renderbuffer_parameter_iv(name, gl::Renderbuffer_parameter_name::renderbuffer_internal_format, &internal_format);
            } else {
                auto rb_guard = device.get_impl().get_binding_state().push_renderbuffer(name);
                gl::get_renderbuffer_parameter_iv(gl::Renderbuffer_target::renderbuffer, gl::Renderbuffer_parameter_name::renderbuffer_samples,         &samples);
                gl::get_renderbuffer_parameter_iv(gl::Renderbuffer_target::renderbuffer, gl::Renderbuffer_parameter_name::renderbuffer_width,           &width);
                gl::get_renderbuffer_parameter_iv(gl::Renderbuffer_target::renderbuffer, gl::Renderbuffer_parameter_name::renderbuffer_height,          &height);
                gl::get_renderbuffer_parameter_iv(gl::Renderbuffer_target::renderbuffer, gl::Renderbuffer_parameter_name::renderbuffer_internal_format, &internal_format);
            }
        }
        if (type == GL_TEXTURE) {
            int level{0};
            if (use_dsa) {
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
            } else {
                gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment, gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_texture_level, &level);
                // For non-DSA get_tex_level_parameter_iv, we need to bind the texture.
                // We need to know the texture target — query it from the texture object if possible.
                // For diagnostic purposes, use texture_2d as a reasonable default.
                constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
                auto tex_guard = device.get_impl().get_binding_state().push_texture(scratch_unit, gl::Texture_target::texture_2d, name);
                gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, level, gl::Get_texture_parameter::texture_width,           &width);
                gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, level, gl::Get_texture_parameter::texture_height,          &height);
                gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, level, gl::Get_texture_parameter::texture_internal_format, &internal_format);
                gl::get_tex_level_parameter_iv(
                    gl::Texture_target::texture_2d,
                    level,
                    static_cast<gl::Get_texture_parameter>(GL_TEXTURE_SAMPLES), // TODO gl_extra
                    &samples
                );
            }
        }

        int component_type{0};
        int red_size{0};
        int green_size{0};
        int blue_size{0};
        int alpha_size{0};
        int depth_size{0};
        int stencil_size{0};
        if (use_dsa) {
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
        } else {
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_component_type, &component_type);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_red_size, &red_size);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_green_size, &green_size);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_blue_size, &blue_size);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_alpha_size, &alpha_size);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_depth_size, &depth_size);
            gl::get_framebuffer_attachment_parameter_iv(gl::Framebuffer_target::draw_framebuffer, attachment,
                gl::Framebuffer_attachment_parameter_name::framebuffer_attachment_stencil_size, &stencil_size);
        }

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
    const bool use_dsa = device.get_info().use_direct_state_access;

    int samples       {0};
    int sample_buffers{0};
    if (use_dsa) {
        gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::samples, &samples);
        gl::get_named_framebuffer_parameter_iv(fbo_name, gl::Get_framebuffer_parameter::sample_buffers, &sample_buffers);
    } else {
        // get_framebuffer_parameter_iv is GL 4.3 — skip on pre-DSA
        // The information is diagnostic only, so we can log without it
    }

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
ERHE_PROFILE_MUTEX(std::mutex, Render_pass_impl::s_mutex);
std::vector<Render_pass_impl*> Render_pass_impl::s_all_framebuffers{};
Render_pass_impl*              Render_pass_impl::s_active_render_pass = nullptr;


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
    , m_end_debug_group_name  {erhe::utility::Debug_label{fmt::format("Render_pass_impl::end_render_pass() {}", descriptor.debug_label.string_view())}}
    , m_begin_debug_group_name{erhe::utility::Debug_label{fmt::format("Render_pass_impl::start_render_pass() {}", descriptor.debug_label.string_view())}}
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

    const std::lock_guard lock{s_mutex};

    s_all_framebuffers.push_back(this);

    create();
}

Render_pass_impl::~Render_pass_impl() noexcept
{
    const std::lock_guard lock{s_mutex};

    s_all_framebuffers.erase(
        std::remove(
            s_all_framebuffers.begin(),
            s_all_framebuffers.end(),
            this
        ),
        s_all_framebuffers.end()
    );
}

void Render_pass_impl::on_thread_enter()
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard lock{s_mutex};

    for (auto* framebuffer : s_all_framebuffers) {
        if (framebuffer->m_owner_thread == std::thread::id{}) {
            framebuffer->create();
        }
    }
}

void Render_pass_impl::on_thread_exit()
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard lock{s_mutex};

    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    auto this_thread_id = std::this_thread::get_id();
    for (auto* framebuffer : s_all_framebuffers) {
        if (framebuffer->m_owner_thread == this_thread_id) {
            framebuffer->reset();
        }
    }
}

void Render_pass_impl::reset()
{
    m_owner_thread = {};
    m_draw_buffers.clear();
    m_gl_framebuffer.reset();
    m_gl_multisample_resolve_framebuffer.reset();
}

void Render_pass_impl::create()
{
    ERHE_PROFILE_FUNCTION();

    m_draw_buffers.clear();
    if (m_swapchain != nullptr) {
        m_draw_buffers.push_back(gl::Color_buffer::back);
        return;
    }

    if (m_gl_framebuffer.has_value()) {
        ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        return;
    }

    m_owner_thread = std::this_thread::get_id();
    m_gl_framebuffer.emplace(m_device.get_impl().create_framebuffer());

    if (m_uses_multisample_resolve) {
        m_gl_multisample_resolve_framebuffer.emplace(m_device.get_impl().create_framebuffer());
    }

    const bool use_dsa = m_device.get_info().use_direct_state_access;

    // For non-DSA, push the framebuffer so framebuffer_texture / framebuffer_texture_layer work
    std::optional<Framebuffer_binding_guard> fb_guard;
    if (!use_dsa) {
        fb_guard.emplace(
            m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name())
        );
    }

    auto process_attachment = [this, use_dsa](
        const GLuint                       fbo_name,
        const gl::Framebuffer_attachment   attachment_point,
        Render_pass_attachment_descriptor& attachment
    ) -> bool {
        if (attachment.texture != nullptr) {
            ERHE_VERIFY(attachment.texture->get_width() >= 1);
            ERHE_VERIFY(attachment.texture->get_height() >= 1);
            if (attachment.texture->is_layered()) {
                if (use_dsa) {
                    gl::named_framebuffer_texture_layer(
                        fbo_name,
                        attachment_point,
                        attachment.texture->get_impl().gl_name(),
                        attachment.texture_level,
                        attachment.texture_layer
                    );
                } else {
                    gl::framebuffer_texture_layer(
                        gl::Framebuffer_target::draw_framebuffer,
                        attachment_point,
                        attachment.texture->get_impl().gl_name(),
                        attachment.texture_level,
                        attachment.texture_layer
                    );
                }
            } else {
                if (use_dsa) {
                    gl::named_framebuffer_texture(
                        fbo_name,
                        attachment_point,
                        attachment.texture->get_impl().gl_name(),
                        attachment.texture_level
                    );
                } else {
                    gl::framebuffer_texture(
                        gl::Framebuffer_target::draw_framebuffer,
                        attachment_point,
                        attachment.texture->get_impl().gl_name(),
                        attachment.texture_level
                    );
                }
            }
            return true;
        }
        return false;
    };

    auto process_multisample_resolve_attachment = [this, use_dsa](
        const GLuint                       fbo_name,
        const gl::Framebuffer_attachment   attachment_point,
        Render_pass_attachment_descriptor& attachment
    ) {
        if (attachment.resolve_texture != nullptr) {
            ERHE_VERIFY(attachment.resolve_texture->get_width() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_height() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_sample_count() <= 1);
            if (attachment.resolve_texture->is_layered()) {
                if (use_dsa) {
                    gl::named_framebuffer_texture_layer(
                        fbo_name,
                        attachment_point,
                        attachment.resolve_texture->get_impl().gl_name(),
                        attachment.resolve_level,
                        attachment.resolve_layer
                    );
                } else {
                    gl::framebuffer_texture_layer(
                        gl::Framebuffer_target::draw_framebuffer,
                        attachment_point,
                        attachment.resolve_texture->get_impl().gl_name(),
                        attachment.resolve_level,
                        attachment.resolve_layer
                    );
                }
            } else {
                if (use_dsa) {
                    gl::named_framebuffer_texture(
                        fbo_name,
                        attachment_point,
                        attachment.resolve_texture->get_impl().gl_name(),
                        attachment.resolve_level
                    );
                } else {
                    gl::framebuffer_texture(
                        gl::Framebuffer_target::draw_framebuffer,
                        attachment_point,
                        attachment.resolve_texture->get_impl().gl_name(),
                        attachment.resolve_level
                    );
                }
            }
        }
    };

    m_draw_buffers.clear();
    {
        unsigned int color_index = 0;
        for (auto& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(
                static_cast<unsigned int>(gl::Framebuffer_attachment::color_attachment0) + color_index
            );
            bool has_attachment = process_attachment(gl_name(), attachment_point, attachment);
            if (has_attachment) {
                const gl::Color_buffer color_buffer = static_cast<gl::Color_buffer>(
                    static_cast<unsigned int>(gl::Color_buffer::color_attachment0) + color_index
                );
                m_draw_buffers.push_back(color_buffer);
            }
            ++color_index;
        }
    }
    process_attachment(gl_name(), gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
    process_attachment(gl_name(), gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);
    
    if (!m_draw_buffers.empty()) {
        if (use_dsa) {
            gl::named_framebuffer_draw_buffers(gl_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
            gl::named_framebuffer_read_buffer(gl_name(), m_draw_buffers.front());
        } else {
            gl::draw_buffers(static_cast<GLsizei>(m_draw_buffers.size()), reinterpret_cast<const gl::Draw_buffer_mode*>(m_draw_buffers.data()));
            // read_buffer operates on GL_READ_FRAMEBUFFER, so temporarily bind there too
            auto read_fb_guard = m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
            gl::read_buffer(static_cast<gl::Read_buffer_mode>(m_draw_buffers.front()));
        }
    } else {
        // No color attachments (e.g. depth-only shadow maps): set draw/read
        // buffer to GL_NONE so the framebuffer is complete.
        if (use_dsa) {
            gl::named_framebuffer_draw_buffers(gl_name(), 0, nullptr);
            gl::named_framebuffer_read_buffer(gl_name(), gl::Color_buffer::none);
        } else {
            gl::Draw_buffer_mode none_buf = gl::Draw_buffer_mode::none;
            gl::draw_buffers(1, &none_buf);
            auto read_fb_guard = m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
            gl::read_buffer(gl::Read_buffer_mode::none);
        }
    }

    if (m_device.get_info().use_debug_output) {
        erhe::utility::Debug_label debug_label{ fmt::format("(F:{}) {}", gl_name(), m_debug_label.string_view()) };
        gl::object_label(gl::Object_identifier::framebuffer, gl_name(), -1, debug_label.data());
    }

    // Release the main framebuffer guard before binding the resolve framebuffer
    fb_guard.reset();

    if (m_uses_multisample_resolve) {
        // For non-DSA, bind the resolve framebuffer
        std::optional<Framebuffer_binding_guard> resolve_fb_guard;
        if (!use_dsa) {
            resolve_fb_guard.emplace(
                m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name())
            );
        }

        unsigned int color_index = 0;
        for (auto& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(static_cast<unsigned int>(
                gl::Framebuffer_attachment::color_attachment0) + color_index
            );
            process_multisample_resolve_attachment(gl_multisample_resolve_name(), attachment_point, attachment);
        }
        process_multisample_resolve_attachment(gl_multisample_resolve_name(), gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
        process_multisample_resolve_attachment(gl_multisample_resolve_name(), gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);

        const std::string multisample_resolve_debug_label = fmt::format(
            "(F:{}) {} Multisample Resolve",
            gl_multisample_resolve_name(), m_debug_label.string_view()
        );
        if (m_device.get_info().use_debug_output) {
            gl::object_label(gl::Object_identifier::framebuffer, gl_multisample_resolve_name(), -1, multisample_resolve_debug_label.c_str());
        }
    }

    ERHE_VERIFY(check_status());
}

auto Render_pass_impl::get_sample_count() const -> unsigned int
{
    std::optional<int> sample_count{};
    auto update_sample_count = [&sample_count](const int sample_count_in){
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
    return sample_count.has_value() ? sample_count.value() : 0;
}

auto Render_pass_impl::check_status() const -> bool
{
#if !defined(NDEBUG)
    const bool use_dsa = m_device.get_info().use_direct_state_access;

    gl::Framebuffer_status status;
    if (use_dsa) {
        status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
    } else {
        auto fb_guard = m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
        status = gl::check_framebuffer_status(gl::Framebuffer_target::draw_framebuffer);
    }
    if (status != gl::Framebuffer_status::framebuffer_complete) {
        log_render_pass->warn(
            "Render_pass_impl {} FBO {} not complete: {}",
            get_debug_label().string_view(), gl_name(), gl::c_str(status)
        );
        dump_fbo(m_device, gl_name());
        return false;
    }

    if (m_uses_multisample_resolve) {
        if (use_dsa) {
            status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
        } else {
            auto fb_guard = m_device.get_impl().get_binding_state().push_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name());
            status = gl::check_framebuffer_status(gl::Framebuffer_target::draw_framebuffer);
        }
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
    return m_gl_framebuffer.has_value() ? m_gl_framebuffer.value().gl_name() : 0;
}

auto Render_pass_impl::gl_multisample_resolve_name() const -> unsigned int
{
    return m_gl_multisample_resolve_framebuffer.has_value() ? m_gl_multisample_resolve_framebuffer.value().gl_name() : 0;
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

void Render_pass_impl::start_render_pass()
{
    ERHE_VERIFY(s_active_render_pass == nullptr);
    s_active_render_pass = this;
    ERHE_VERIFY(!m_is_active);
    m_is_active = true;

    m_outer_debug_group = std::make_unique<Scoped_debug_group>(m_debug_group_name);
    Scoped_debug_group begin_debug_group{m_begin_debug_group_name};

    if (m_device.get_info().vendor == Vendor::Nvidia) {
        // Workaround for https://developer.nvidia.com/bugs/5799090
        m_device.get_impl().reset_shader_stages_state_tracker();
    }

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());

    if ((m_render_target_width > 0) && (m_render_target_height > 0)) {
        m_device.get_impl().m_gl_state_tracker.viewport_rect.execute(
            Viewport_rect_state{
                .x      = 0.0f,
                .y      = 0.0f,
                .width  = static_cast<float>(m_render_target_width),
                .height = static_cast<float>(m_render_target_height)
            }
        );
        m_device.get_impl().m_gl_state_tracker.scissor.execute(
            Scissor_state{
                .x      = 0,
                .y      = 0,
                .width  = m_render_target_width,
                .height = m_render_target_height
            }
        );
    }

    // To be able to clear color the color write masks must be enabled (part of color blend state)
    m_device.get_impl().m_gl_state_tracker.color_blend.execute(
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
    m_device.get_impl().m_gl_state_tracker.depth_stencil.execute(
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

    const bool use_dsa = m_device.get_info().use_direct_state_access;

#if !defined(NDEBUG)
    if (m_swapchain == nullptr) {
        // Draw framebuffer is already bound above
        gl::Framebuffer_status status;
        if (use_dsa) {
            status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
        } else {
            status = gl::check_framebuffer_status(gl::Framebuffer_target::draw_framebuffer);
        }
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

                    if (use_dsa) {
                        gl::clear_named_framebuffer_fv(name, gl::Buffer::color, static_cast<GLint>(color_index), &f[0]);
                    } else {
                        gl::clear_buffer_fv(gl::Buffer::color, static_cast<GLint>(color_index), &f[0]);
                    }
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                    const GLint i[4] = {
                        static_cast<GLint>(attachment.clear_value[0]),
                        static_cast<GLint>(attachment.clear_value[1]),
                        static_cast<GLint>(attachment.clear_value[2]),
                        static_cast<GLint>(attachment.clear_value[3])
                    };
                    if (use_dsa) {
                        gl::clear_named_framebuffer_iv(name, gl::Buffer::color, static_cast<GLint>(color_index), &i[0]);
                    } else {
                        gl::clear_buffer_iv(gl::Buffer::color, static_cast<GLint>(color_index), &i[0]);
                    }
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                    const GLuint ui[4] = {
                        static_cast<GLuint>(attachment.clear_value[0]),
                        static_cast<GLuint>(attachment.clear_value[1]),
                        static_cast<GLuint>(attachment.clear_value[2]),
                        static_cast<GLuint>(attachment.clear_value[3])
                    };
                    if (use_dsa) {
                        gl::clear_named_framebuffer_uiv(name, gl::Buffer::color, static_cast<GLint>(color_index), &ui[0]);
                    } else {
                        gl::clear_buffer_uiv(gl::Buffer::color, static_cast<GLint>(color_index), &ui[0]);
                    }
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
        if (use_dsa) {
            gl::clear_named_framebufferf_i(
                name,
                gl::Buffer::depth_stencil,
                0,
                static_cast<float>(m_depth_attachment.clear_value[0]),
                static_cast<GLint>(m_stencil_attachment.clear_value[0])
            );
        } else {
            gl::clear_bufferf_i(
                gl::Buffer::depth_stencil,
                0,
                static_cast<float>(m_depth_attachment.clear_value[0]),
                static_cast<GLint>(m_stencil_attachment.clear_value[0])
            );
        }
    } else {
        if (clear_depth) {
            const GLfloat f[4] = {
                static_cast<GLfloat>(m_depth_attachment.clear_value[0]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[1]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[2]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[3])
            };
            if (use_dsa) {
                gl::clear_named_framebuffer_fv(name, gl::Buffer::depth, 0, &f[0]);
            } else {
                gl::clear_buffer_fv(gl::Buffer::depth, 0, &f[0]);
            }
        }
        if (clear_stencil) {
            const GLuint ui[4] = {
                static_cast<GLuint>(m_stencil_attachment.clear_value[0]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[1]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[2]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[3])
            };
            if (use_dsa) {
                gl::clear_named_framebuffer_uiv(name, gl::Buffer::stencil, 0, &ui[0]);
            } else {
                gl::clear_buffer_uiv(gl::Buffer::stencil, 0, &ui[0]);
            }
        }
    }

}

void Render_pass_impl::end_render_pass()
{
    ERHE_VERIFY(s_active_render_pass == this);
    s_active_render_pass = nullptr;

    ERHE_VERIFY(m_is_active);
    m_is_active = false;

    auto end_debug_group = std::make_unique<Scoped_debug_group>(m_end_debug_group_name);

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

    const bool use_dsa = m_device.get_info().use_direct_state_access;

    if (m_uses_multisample_resolve) {
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
#if !defined(NDEBUG)
        {
            gl::Framebuffer_status status;
            if (use_dsa) {
                status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::read_framebuffer);
            } else {
                status = gl::check_framebuffer_status(gl::Framebuffer_target::read_framebuffer);
            }
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

        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name());
#if !defined(NDEBUG)
        {
            gl::Framebuffer_status status;
            if (use_dsa) {
                status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
            } else {
                status = gl::check_framebuffer_status(gl::Framebuffer_target::draw_framebuffer);
            }
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
                if (use_dsa) {
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
                } else {
                    gl::read_buffer(static_cast<gl::Read_buffer_mode>(color_buffer));
                    const gl::Draw_buffer_mode draw_buf = static_cast<gl::Draw_buffer_mode>(color_buffer);
                    gl::draw_buffers(1, &draw_buf);
                    gl::blit_framebuffer(
                        0, 0, blit_width, blit_height,
                        0, 0, blit_width, blit_height,
                        gl::Clear_buffer_mask::color_buffer_bit,
                        gl::Blit_framebuffer_filter::linear
                    );
                }
            }
        }
        // Restore draw buffer state
        if (use_dsa) {
            gl::named_framebuffer_draw_buffers(gl_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
            gl::named_framebuffer_read_buffer (gl_name(), gl::Color_buffer::color_attachment0);
            gl::named_framebuffer_draw_buffers(gl_multisample_resolve_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
            gl::named_framebuffer_read_buffer (gl_multisample_resolve_name(), gl::Color_buffer::color_attachment0);
        } else {
            // Bind the main FBO to draw, restore its draw buffers
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
            gl::draw_buffers(static_cast<GLsizei>(m_draw_buffers.size()), reinterpret_cast<const gl::Draw_buffer_mode*>(m_draw_buffers.data()));
            gl::read_buffer(static_cast<gl::Read_buffer_mode>(gl::Color_buffer::color_attachment0));
            // Bind the resolve FBO to draw, restore its draw buffers
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name());
            gl::draw_buffers(static_cast<GLsizei>(m_draw_buffers.size()), reinterpret_cast<const gl::Draw_buffer_mode*>(m_draw_buffers.data()));
            gl::read_buffer(static_cast<gl::Read_buffer_mode>(gl::Color_buffer::color_attachment0));
            // Re-bind for depth/stencil blit: read=main, draw=resolve
            gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_multisample_resolve_name());
        }

        // NOTE: Depth/stencil blit does not involve draw buffers
        if (check_multisample_resolve(m_depth_attachment, blit_width, blit_height)) {
            if (use_dsa) {
                gl::blit_named_framebuffer(
                    gl_name(),
                    gl_multisample_resolve_name(),
                    0, 0, blit_width, blit_width,
                    0, 0, blit_width, blit_height,
                    gl::Clear_buffer_mask::depth_buffer_bit,
                    gl::Blit_framebuffer_filter::linear
                );
            } else {
                gl::blit_framebuffer(
                    0, 0, blit_width, blit_width,
                    0, 0, blit_width, blit_height,
                    gl::Clear_buffer_mask::depth_buffer_bit,
                    gl::Blit_framebuffer_filter::linear
                );
            }
        }
        if (check_multisample_resolve(m_stencil_attachment, blit_width, blit_height)) {
            if (use_dsa) {
                gl::blit_named_framebuffer(
                    gl_name(),
                    gl_multisample_resolve_name(),
                    0, 0, blit_width, blit_width,
                    0, 0, blit_width, blit_height,
                    gl::Clear_buffer_mask::stencil_buffer_bit,
                    gl::Blit_framebuffer_filter::linear
                );
            } else {
                gl::blit_framebuffer(
                    0, 0, blit_width, blit_width,
                    0, 0, blit_width, blit_height,
                    gl::Clear_buffer_mask::stencil_buffer_bit,
                    gl::Blit_framebuffer_filter::linear
                );
            }
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
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
        //gl::invalidate_framebuffer(gl::Framebuffer_target::draw_framebuffer, invalidate_attachments_count, invalidate_attachments.data());
    }

    // TODO Strictly speaking this is redundant, but might be useful for debugging
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

    end_debug_group.reset();
    m_outer_debug_group.reset();
}

} // namespace erhe::graphics
