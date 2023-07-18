#include "renderers/composer.hpp"
#include "renderers/renderpass.hpp"
#include "editor_log.hpp"

namespace editor
{

Composer::Composer(const std::string_view name)
    : erhe::scene::Item{name}
{
}

[[nodiscard]] auto Composer::get_static_type() -> uint64_t
{
    return erhe::scene::Item_type::composer;
}

[[nodiscard]] auto Composer::get_static_type_name() -> const char*
{
    return "composer";
}

[[nodiscard]] auto Composer::get_type() const -> uint64_t
{
    return get_static_type();
}

[[nodiscard]] auto Composer::get_type_name() const -> const char*
{
    return get_static_type_name();
}

void Composer::render(const Render_context& context) const
{
    log_frame->trace("Composer::render()");
    for (const auto& renderpass : renderpasses) {
        log_frame->trace("  rp: {}", renderpass->describe());
        renderpass->render(context);
    }
}

} // namespace editor
