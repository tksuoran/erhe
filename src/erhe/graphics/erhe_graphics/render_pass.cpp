#include "erhe_graphics/render_pass.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <thread>

namespace erhe::graphics {

namespace {

void dump_fbo_attachment(int fbo_name, gl::Framebuffer_attachment attachment)
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
        GLsizei length{0};
        gl::Object_identifier gl_type = static_cast<gl::Object_identifier>(type);
        gl::get_object_label(gl_type, name, 0, &length, nullptr);
        if (length > 0) {
            debug_label.resize(length + 1);
            gl::get_object_label(gl_type, name, length + 1, nullptr, debug_label.data());
            debug_label.resize(length);
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

void dump_fbo(int fbo_name)
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

    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::color_attachment0);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::depth_attachment);
    dump_fbo_attachment(fbo_name, gl::Framebuffer_attachment::stencil_attachment);
}

}

Render_pass_attachment_descriptor::Render_pass_attachment_descriptor()
    : clear_value{0.0, 0.0, 0.0, 0.0}
{
}

Render_pass_attachment_descriptor::~Render_pass_attachment_descriptor() = default;

auto Render_pass_attachment_descriptor::is_defined() const -> bool
{
    if (texture != nullptr) {
        return true;
    }
    if (renderbuffer != nullptr) {
        return true;
    }
    if (use_default_framebuffer) {
        return true;
    }
    return false;
}

auto Render_pass_attachment_descriptor::get_pixelformat() const -> erhe::dataformat::Format
{
    if (texture != nullptr) {
        return texture->get_pixelformat();
    }
    if (renderbuffer != nullptr) {
        return renderbuffer->get_pixelformat();
    }
    return {};
}

Render_pass_descriptor::Render_pass_descriptor()
{
}

Render_pass_descriptor::~Render_pass_descriptor()
{
}

// TODO move to graphics::Device?
ERHE_PROFILE_MUTEX(std::mutex, Render_pass::s_mutex);
std::vector<Render_pass*>      Render_pass::s_all_framebuffers{};
Render_pass*                   Render_pass::s_active_render_pass = nullptr;


Render_pass::Render_pass(Device& device, const Render_pass_descriptor& descriptor)
    : m_device              {device}
    , m_color_attachments   {descriptor.color_attachments}
    , m_depth_attachment    {descriptor.depth_attachment}
    , m_stencil_attachment  {descriptor.stencil_attachment}
    , m_render_target_width {descriptor.render_target_width}
    , m_render_target_height{descriptor.render_target_height}
    , m_debug_label         {descriptor.debug_label}
    , m_debug_group_name    {fmt::format("Render pass: {}", descriptor.debug_label)}
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
        if (attachment.use_default_framebuffer) {
            m_uses_default_framebuffer = true;
        } else {
            ERHE_VERIFY(!m_uses_default_framebuffer);
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

Render_pass::~Render_pass() noexcept
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

void Render_pass::on_thread_enter()
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard lock{s_mutex};

    for (auto* framebuffer : s_all_framebuffers) {
        if (framebuffer->m_owner_thread == std::thread::id{}) {
            framebuffer->create();
        }
    }
}

void Render_pass::on_thread_exit()
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

void Render_pass::reset()
{
    m_owner_thread = {};
    m_draw_buffers.clear();
    m_gl_framebuffer.reset();
    m_gl_multisample_resolve_framebuffer.reset();
}

void Render_pass::create()
{
    ERHE_PROFILE_FUNCTION();

    m_draw_buffers.clear();
    if (m_uses_default_framebuffer) {
        m_draw_buffers.push_back(gl::Color_buffer::back);
        return;
    }

    if (m_gl_framebuffer.has_value()) {
        ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        return;
    }

    m_owner_thread = std::this_thread::get_id();
    m_gl_framebuffer.emplace(Gl_framebuffer{m_device});

    if (m_uses_multisample_resolve) {
        m_gl_multisample_resolve_framebuffer.emplace(Gl_framebuffer{m_device});
    }

    auto process_attachment = [this](gl::Framebuffer_attachment attachment_point, Render_pass_attachment_descriptor& attachment) -> bool {
        if (attachment.texture != nullptr) {
            ERHE_VERIFY(attachment.texture->get_width() >= 1);
            ERHE_VERIFY(attachment.texture->get_height() >= 1);
            if (attachment.texture->is_layered()) {
                gl::named_framebuffer_texture_layer(
                    gl_name(),
                    attachment_point,
                    attachment.texture->gl_name(),
                    attachment.texture_level,
                    attachment.texture_layer
                );
            } else {
                gl::named_framebuffer_texture(
                    gl_name(),
                    attachment_point,
                    attachment.texture->gl_name(),
                    attachment.texture_level
                );
            }
            return true;
        } else if (attachment.renderbuffer != nullptr) {
            ERHE_VERIFY(attachment.renderbuffer->get_width() >= 1);
            ERHE_VERIFY(attachment.renderbuffer->get_height() >= 1);
            gl::named_framebuffer_renderbuffer(
                gl_name(),
                attachment_point,
                gl::Renderbuffer_target::renderbuffer,
                attachment.renderbuffer->gl_name()
            );
            return true;
        }
        return false;
    };

    auto process_multisample_resolve_attachment = [this](gl::Framebuffer_attachment attachment_point, Render_pass_attachment_descriptor& attachment) {
        if (attachment.resolve_texture != nullptr) {
            ERHE_VERIFY(attachment.resolve_texture->get_width() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_height() >= 1);
            ERHE_VERIFY(attachment.resolve_texture->get_sample_count() <= 1);
            if (attachment.resolve_texture->is_layered()) {
                gl::named_framebuffer_texture_layer(
                    gl_multisample_resolve_name(),
                    attachment_point,
                    attachment.resolve_texture->gl_name(),
                    attachment.resolve_level,
                    attachment.resolve_layer
                );
            } else {
                gl::named_framebuffer_texture(
                    gl_multisample_resolve_name(),
                    attachment_point,
                    attachment.resolve_texture->gl_name(),
                    attachment.resolve_level
                );
            }
        }
    };

    m_draw_buffers.clear();
    {
        unsigned int color_index = 0;
        for (auto& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(static_cast<unsigned int>(gl::Framebuffer_attachment::color_attachment0) + color_index);
            bool has_attachment = process_attachment(attachment_point, attachment);
            if (has_attachment) {
                const gl::Color_buffer color_buffer = static_cast<gl::Color_buffer>(static_cast<unsigned int>(gl::Color_buffer::color_attachment0) + color_index);
                m_draw_buffers.push_back(color_buffer);
            }
            ++color_index;
        }
    }
    process_attachment(gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
    process_attachment(gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);
    
    if (!m_draw_buffers.empty()) {
        gl::named_framebuffer_draw_buffers(gl_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer(gl_name(), m_draw_buffers.front());
    }

    const std::string debug_label = fmt::format("(F:{}) {}", gl_name(), m_debug_label);
    gl::object_label(gl::Object_identifier::framebuffer, gl_name(), -1, debug_label.c_str());

    if (m_uses_multisample_resolve) {
        unsigned int color_index = 0;
        for (auto& attachment : m_color_attachments) {
            const gl::Framebuffer_attachment attachment_point = static_cast<gl::Framebuffer_attachment>(static_cast<unsigned int>(gl::Framebuffer_attachment::color_attachment0) + color_index);
            process_multisample_resolve_attachment(attachment_point, attachment);
        }
        process_multisample_resolve_attachment(gl::Framebuffer_attachment::depth_attachment,   m_depth_attachment);
        process_multisample_resolve_attachment(gl::Framebuffer_attachment::stencil_attachment, m_stencil_attachment);

        const std::string multisample_resolve_debug_label = fmt::format("(F:{}) {} Multisample Resolve", gl_multisample_resolve_name(), m_debug_label);
        gl::object_label(gl::Object_identifier::framebuffer, gl_multisample_resolve_name(), -1, multisample_resolve_debug_label.c_str());
    }

    ERHE_VERIFY(check_status());
}

auto Render_pass::get_sample_count() const -> unsigned int
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
        if (attachment.renderbuffer != nullptr) {
            update_sample_count(attachment.renderbuffer->get_sample_count());
        }
    }
    if (m_depth_attachment.texture != nullptr) {
        update_sample_count(m_depth_attachment.texture->get_sample_count());
    }
    if (m_stencil_attachment.texture != nullptr) {
        update_sample_count(m_stencil_attachment.texture->get_sample_count());
    }
    return sample_count.has_value() ? sample_count.value() : 0;
}

auto Render_pass::check_status() const -> bool
{
#if !defined(NDEBUG)
    gl::Framebuffer_status status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
    if (status != gl::Framebuffer_status::framebuffer_complete) {
        log_render_pass->warn("Render_pass {} FBO {} not complete: {}", get_debug_label(), gl_name(), gl::c_str(status));
        dump_fbo(gl_name());
        return false;
    }
    if (status != gl::Framebuffer_status::framebuffer_complete) {
        return false;
    }

    if (m_uses_multisample_resolve) {
        status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
        if (status != gl::Framebuffer_status::framebuffer_complete) {
            log_render_pass->warn("Render_pass {} multisample resolve FBO {} not complete: {}", get_debug_label(), gl_multisample_resolve_name(), gl::c_str(status));
            dump_fbo(gl_multisample_resolve_name());
            return false;
        }
        if (status != gl::Framebuffer_status::framebuffer_complete) {
            return false;
        }
    }
#endif
    return true;
}

auto Render_pass::gl_name() const -> unsigned int
{
    return m_gl_framebuffer.has_value() ? m_gl_framebuffer.value().gl_name() : 0;
}

auto Render_pass::gl_multisample_resolve_name() const -> unsigned int
{
    return m_gl_multisample_resolve_framebuffer.has_value() ? m_gl_multisample_resolve_framebuffer.value().gl_name() : 0;
}

auto Render_pass::get_render_target_width() const -> int
{
    return m_render_target_width;
}

auto Render_pass::get_render_target_height() const -> int
{
    return m_render_target_height;
}

auto Render_pass::get_debug_label() const -> const std::string&
{
    return m_debug_label;
}

void Render_pass::start_render_pass()
{
    ERHE_VERIFY(s_active_render_pass == nullptr);
    s_active_render_pass = this;
    ERHE_VERIFY(!m_is_active);
    m_is_active = true;

    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(m_debug_group_name.length() + 1),
        m_debug_group_name.data()
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());

    if ((m_render_target_width > 0) && (m_render_target_height > 0)) {
        gl::viewport(0, 0, m_render_target_width, m_render_target_height);
    }

    std::string begin_debug_group_name = fmt::format("Render_pass::start_render_pass() {}", m_debug_group_name);
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(begin_debug_group_name.length() + 1),
        begin_debug_group_name.c_str()
    );

#if !defined(NDEBUG)
    if (!m_uses_default_framebuffer) {
        const auto status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::draw_framebuffer);
        ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
    }
#endif

    const GLint name = gl_name();
    for (size_t color_index = 0; color_index < m_color_attachments.size(); ++color_index) {
        const Render_pass_attachment_descriptor& attachment = m_color_attachments[color_index];
        if (!attachment.is_defined()) {
            continue;
        }
        if (attachment.load_action == Load_action::Clear) {
            const erhe::dataformat::Format      pixelformat = attachment.get_pixelformat();
            const erhe::dataformat::Format_kind format_kind = attachment.use_default_framebuffer
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
                    GLfloat f[4] = {
                        static_cast<GLfloat>(attachment.clear_value[0]),
                        static_cast<GLfloat>(attachment.clear_value[1]),
                        static_cast<GLfloat>(attachment.clear_value[2]),
                        static_cast<GLfloat>(attachment.clear_value[3])
                    };

                    // TODO Figure out why gl::clear_named_framebuffer_fv() does not always work
                    if (attachment.texture != nullptr) {
                        gl::clear_tex_image(attachment.texture->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &f[0]);
                    } else
                    {
                        gl::clear_named_framebuffer_fv(name, gl::Buffer::color, static_cast<GLint>(color_index), &f[0]);
                    }
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                    GLint i[4] = {
                        static_cast<GLint>(attachment.clear_value[0]),
                        static_cast<GLint>(attachment.clear_value[1]),
                        static_cast<GLint>(attachment.clear_value[2]),
                        static_cast<GLint>(attachment.clear_value[3])
                    };
                    gl::clear_named_framebuffer_iv(name, gl::Buffer::color, static_cast<GLint>(color_index), &i[0]);
                    break;
                }
                case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                    GLuint ui[4] = {
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
    bool clear_depth   = m_depth_attachment  .is_defined() && (m_depth_attachment.load_action == Load_action::Clear);
    bool clear_stencil = m_stencil_attachment.is_defined() && (m_stencil_attachment.load_action == Load_action::Clear);
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
            GLfloat f[4] = {
                static_cast<GLfloat>(m_depth_attachment.clear_value[0]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[1]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[2]),
                static_cast<GLfloat>(m_depth_attachment.clear_value[3])
            };
            gl::clear_named_framebuffer_fv(name, gl::Buffer::depth, 0, &f[0]);
        }
        if (clear_stencil) {
            GLuint ui[4] = {
                static_cast<GLuint>(m_stencil_attachment.clear_value[0]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[1]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[2]),
                static_cast<GLuint>(m_stencil_attachment.clear_value[3])
            };
            gl::clear_named_framebuffer_uiv(name, gl::Buffer::stencil, 0, &ui[0]);
        }
    }

    gl::pop_debug_group();
}

void Render_pass::end_render_pass()
{
    ERHE_VERIFY(s_active_render_pass == this);
    s_active_render_pass = nullptr;

    ERHE_VERIFY(m_is_active);
    m_is_active = false;

    std::string end_debug_group_name = fmt::format("Render_pass::end_render_pass() {}", m_debug_group_name);
    gl::push_debug_group(
        gl::Debug_source::debug_source_application,
        0,
        static_cast<GLsizei>(end_debug_group_name.length() + 1),
        end_debug_group_name.c_str()
    );

    std::array<gl::Framebuffer_attachment, 4> color_attachment_points = {
        gl::Framebuffer_attachment::color_attachment0,
        gl::Framebuffer_attachment::color_attachment1,
        gl::Framebuffer_attachment::color_attachment2,
        gl::Framebuffer_attachment::color_attachment3
    };
    std::vector<gl::Invalidate_framebuffer_attachment> invalidate_attachments;

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
        const int src_width  = (attachment.texture != nullptr) ? attachment.texture->get_width (attachment.texture_level) : attachment.renderbuffer->get_width();
        const int src_height = (attachment.texture != nullptr) ? attachment.texture->get_height(attachment.texture_level) : attachment.renderbuffer->get_height();
        const int dst_width  = attachment.resolve_texture->get_width (attachment.texture_level);
        const int dst_height = attachment.resolve_texture->get_height(attachment.texture_level);
        blit_width  = std::min(src_width, dst_width);
        blit_height = std::min(src_height, dst_height);
        return (blit_width > 0) && (blit_height > 0);
    };

    if (m_uses_multisample_resolve) {
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, gl_name());
#if !defined(NDEBUG)
        {
            const auto status = gl::check_named_framebuffer_status(gl_name(), gl::Framebuffer_target::read_framebuffer);
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
            const auto status = gl::check_named_framebuffer_status(gl_multisample_resolve_name(), gl::Framebuffer_target::draw_framebuffer);
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
            const Render_pass_attachment_descriptor& attachment = m_color_attachments[color_index];
            if (check_multisample_resolve(attachment, blit_width, blit_height)) {
                const gl::Color_buffer color_buffer = static_cast<gl::Color_buffer>(static_cast<unsigned int>(gl::Color_buffer::color_attachment0) + color_index);
                gl::named_framebuffer_read_buffer(gl_name(), color_buffer);
                gl::named_framebuffer_draw_buffers(gl_multisample_resolve_name(), 1, &color_buffer);
                gl::blit_named_framebuffer(
                    gl_name(), // read framebuffer
                    gl_multisample_resolve_name(), // draw framebuffer
                    0,                                        // src X0
                    0,                                        // src Y1
                    blit_width,                               // src X0
                    blit_height,                              // src Y1
                    0,                                        // dst X0
                    0,                                        // dst Y1
                    blit_width,                               // dst X0
                    blit_height,                              // dst Y1
                    gl::Clear_buffer_mask::color_buffer_bit,  // mask
                    gl::Blit_framebuffer_filter::linear       // filter
                );
            }
        }
        // Restore draw buffer state
        gl::named_framebuffer_draw_buffers(gl_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer(gl_name(), gl::Color_buffer::color_attachment0);
        gl::named_framebuffer_draw_buffers(gl_multisample_resolve_name(), static_cast<GLsizei>(m_draw_buffers.size()), m_draw_buffers.data());
        gl::named_framebuffer_read_buffer(gl_multisample_resolve_name(), gl::Color_buffer::color_attachment0);

        // NOTE: Depth/stencil blit does not involve draw buffers
        if (check_multisample_resolve(m_depth_attachment, blit_width, blit_height)) {
            gl::blit_named_framebuffer(
                gl_name(),
                gl_multisample_resolve_name(),
                0,                                        // src X0
                0,                                        // src Y1
                blit_width,                               // src X0
                blit_width,                               // src Y1
                0,                                        // dst X0
                0,                                        // dst Y1
                blit_width,                               // dst X0
                blit_height,                              // dst Y1
                gl::Clear_buffer_mask::depth_buffer_bit,  // mask
                gl::Blit_framebuffer_filter::linear       // filter
            );
        }
        if (check_multisample_resolve(m_stencil_attachment, blit_width, blit_height)) {
            gl::blit_named_framebuffer(
                gl_name(),
                gl_multisample_resolve_name(),
                0,                                          // src X0
                0,                                          // src Y1
                blit_width,                                 // src X0
                blit_width,                                 // src Y1
                0,                                          // dst X0
                0,                                          // dst Y1
                blit_width,                                 // dst X0
                blit_height,                                // dst Y1
                gl::Clear_buffer_mask::stencil_buffer_bit,  // mask
                gl::Blit_framebuffer_filter::linear         // filter
            );
        }
    }

    auto check_invalidate_attachment = [&invalidate_attachments](const Render_pass_attachment_descriptor& attachment, gl::Framebuffer_attachment attachment_point)
    {
        if ((attachment.texture != nullptr) || (attachment.renderbuffer != nullptr)){
            if (attachment.store_action == Store_action::Dont_care) {
                invalidate_attachments.push_back(static_cast<gl::Invalidate_framebuffer_attachment>(attachment_point));
            }
        }
    };

    for (size_t color_index = 0; color_index < m_color_attachments.size(); ++color_index) {
        check_invalidate_attachment(m_color_attachments[color_index], color_attachment_points[color_index]);
    }
    check_invalidate_attachment(m_depth_attachment,   gl::Framebuffer_attachment::depth_attachment);
    check_invalidate_attachment(m_stencil_attachment, gl::Framebuffer_attachment::stencil_attachment);

    if (!invalidate_attachments.empty()) {
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, gl_name());
        gl::invalidate_framebuffer(gl::Framebuffer_target::draw_framebuffer, static_cast<GLsizei>(invalidate_attachments.size()), invalidate_attachments.data());
    }

    // TODO Strictly speaking this is redundant, but might be useful for debugging
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

    gl::pop_debug_group();
    gl::pop_debug_group();
}

} // namespace erhe::graphics
