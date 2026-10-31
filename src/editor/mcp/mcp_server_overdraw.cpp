// The viewport overdraw visualization's MCP tools
// (doc/plans/overdraw_visualization.md O4, O5): set_overdraw_visualization
// turns a viewport's overdraw visualization on or off
// (Scene_views::set_overdraw_mode()), get_overdraw_stats reports the counts
// of the newest retired frame.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_scene_renderer/overdraw_counter.hpp"

#include <fmt/format.h>
#include <optional>
#include <nlohmann/json.hpp>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

// The viewport window titled viewport_title, the first one with a scene
// view when the title is empty; null when there is none.
[[nodiscard]] auto find_viewport_window(Scene_views* scene_views, const std::string& viewport_title) -> std::shared_ptr<Viewport_window>
{
    if (scene_views == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Viewport_window>& viewport_window : scene_views->get_viewport_windows()) {
        if (!viewport_window->viewport_scene_view()) {
            continue;
        }
        if (viewport_title.empty() || (viewport_window->get_title() == viewport_title)) {
            return viewport_window;
        }
    }
    return {};
}

constexpr const char* c_mode_names[] = { "off", "shaded", "rasterized" };

[[nodiscard]] auto get_mode_name(const Overdraw_mode mode) -> const char*
{
    return c_mode_names[static_cast<std::size_t>(mode)];
}

[[nodiscard]] auto viewport_not_found(const std::string& viewport_title) -> std::string
{
    return make_error_content(
        viewport_title.empty()
            ? std::string{"No viewport window with a scene view"}
            : fmt::format("No viewport window titled '{}' (see get_viewports)", viewport_title)
    );
}

} // anonymous namespace

auto Mcp_server::action_set_overdraw_visualization(const json& args) -> std::string
{
    if (!args.contains("mode") || !args["mode"].is_string()) {
        return make_error_content("Missing required argument: mode (\"off\", \"shaded\" or \"rasterized\")");
    }
    const std::string mode_name = args["mode"].get<std::string>();
    std::optional<Overdraw_mode> mode{};
    for (std::size_t i = 0; i < std::size(c_mode_names); ++i) {
        if (mode_name == c_mode_names[i]) {
            mode = static_cast<Overdraw_mode>(i);
        }
    }
    if (!mode.has_value()) {
        return make_error_content(fmt::format("mode '{}' is not one of \"off\", \"shaded\", \"rasterized\"", mode_name));
    }
    const std::string                      viewport_title  = args.value("viewport", std::string{});
    const std::shared_ptr<Viewport_window> viewport_window = find_viewport_window(m_context.scene_views, viewport_title);
    if (!viewport_window) {
        return viewport_not_found(viewport_title);
    }
    const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
    if ((mode.value() != Overdraw_mode::off) && !scene_view->is_overdraw_supported()) {
        return make_error_content(
            "Overdraw visualization is not supported on this device: the counting needs fragment stage buffer stores "
            "and atomics (Device_info::fragment_stores_and_atomics) and the overdraw_reduce compute pipeline"
        );
    }
    if (args.contains("max_count")) {
        if (!args["max_count"].is_number_integer() || (args["max_count"].get<int>() < 1)) {
            return make_error_content("max_count must be an integer >= 1");
        }
        scene_view->set_overdraw_max_count(static_cast<uint32_t>(args["max_count"].get<int>()));
    }
    m_context.scene_views->set_overdraw_mode(scene_view.get(), mode.value());
    return make_json_content({
        {"viewport",  viewport_window->get_title()},
        {"mode",      get_mode_name(scene_view->get_overdraw_mode())},
        {"max_count", scene_view->get_overdraw_max_count()}
    }).dump();
}

auto Mcp_server::query_overdraw_stats(const json& args) -> std::string
{
    const std::string                      viewport_title  = args.value("viewport", std::string{});
    const std::shared_ptr<Viewport_window> viewport_window = find_viewport_window(m_context.scene_views, viewport_title);
    if (!viewport_window) {
        return viewport_not_found(viewport_title);
    }
    const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
    const Overdraw_mode mode = scene_view->get_overdraw_mode();
    const erhe::scene_renderer::Overdraw_statistics statistics = scene_view->get_overdraw_statistics();
    json result = {
        {"viewport", viewport_window->get_title()},
        {"mode",     get_mode_name(mode)},
        {"valid",    statistics.valid}
    };
    if (!statistics.valid) {
        result["message"] = (mode != Overdraw_mode::off)
            ? "No counted frame has retired yet; call again after a few frames"
            : "Overdraw visualization is off for this viewport (set_overdraw_visualization)";
        return make_json_content(result).dump();
    }
    const uint64_t pixel_count = static_cast<uint64_t>(statistics.width) * static_cast<uint64_t>(statistics.height);
    json histogram = json::array();
    for (const uint32_t bin : statistics.histogram) {
        histogram.push_back(bin);
    }
    result["frame_index"]         = statistics.frame_index;
    result["counted_mode"]        = get_mode_name(static_cast<Overdraw_mode>(static_cast<unsigned int>(statistics.overdraw)));
    result["width"]               = statistics.width;
    result["height"]              = statistics.height;
    result["invocations_total"]   = statistics.invocation_total;
    result["covered_pixels"]      = statistics.covered_pixel_count;
    result["mean_per_covered_pixel"] = (statistics.covered_pixel_count > 0)
        ? static_cast<double>(statistics.invocation_total) / static_cast<double>(statistics.covered_pixel_count)
        : 0.0;
    result["mean_per_pixel"]      = (pixel_count > 0)
        ? static_cast<double>(statistics.invocation_total) / static_cast<double>(pixel_count)
        : 0.0;
    result["max_count"]           = statistics.max_count;
    result["histogram"]           = histogram;
    result["histogram_last_bin"]  = erhe::scene_renderer::Overdraw_statistics::histogram_last_bin;
    return make_json_content(result).dump();
}

} // namespace editor
