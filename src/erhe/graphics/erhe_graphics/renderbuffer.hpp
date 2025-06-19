#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <string>
#include <string_view>

namespace erhe::graphics {

class Device;

class Renderbuffer
{
public:
    Renderbuffer(
        Device&                  device,
        erhe::dataformat::Format pixelformat,
        unsigned int             width,
        unsigned int             height
    );

    Renderbuffer(
        Device&                  device,
        erhe::dataformat::Format pixelformat,
        unsigned int             sample_count,
        unsigned int             width,
        unsigned int             height
    );

    ~Renderbuffer() noexcept;

    [[nodiscard]] auto get_pixelformat () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_sample_count() const -> unsigned int;
    [[nodiscard]] auto get_width       () const -> unsigned int;
    [[nodiscard]] auto get_height      () const -> unsigned int;
    [[nodiscard]] auto gl_name         () const -> unsigned int;

    void set_debug_label(std::string_view label);

private:
    Device&                  m_device;
    Gl_renderbuffer          m_handle;
    std::string              m_debug_label;
    erhe::dataformat::Format m_pixelformat;
    unsigned int             m_sample_count{0};
    unsigned int             m_width       {0};
    unsigned int             m_height      {0};
};

class Renderbuffer_hash
{
public:
    auto operator()(const Renderbuffer& renderbuffer) const noexcept -> size_t;
};

[[nodiscard]] auto operator==(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Renderbuffer& lhs, const Renderbuffer& rhs) noexcept -> bool;

} // namespace erhe::graphics
