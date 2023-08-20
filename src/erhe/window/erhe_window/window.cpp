#include "erhe_window/window.hpp"

#include <fmt/format.h>

namespace erhe::window {

auto format_window_title(const char* window_name) -> std::string
{
    const char* month_name[] = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December"
    };

    const time_t now = time(0);
    tm* l = localtime(&now);
    return fmt::format(
        "{} {}. {}",
        window_name,
        month_name[l->tm_mon],
        l->tm_mday,
        1900 + l->tm_year
    );
}

//void flush_clear_to_gray()
//{
//    for (size_t i = 0; i < 3; ++i) {
//        gl::clear_color(0.3f, 0.3f, 0.3f, 1.0f);
//        gl::clear(gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
//        m_context_window->swap_buffers();
//    }
//}

}
