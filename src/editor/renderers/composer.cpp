#include "renderers/composer.hpp"
#include "renderers/renderpass.hpp"
#include "editor_log.hpp"

namespace editor
{

Composer::Composer(const std::string_view name)
    : erhe::scene::Item{name}
{
}

Composer::~Composer() = default;

[[nodiscard]] auto Composer::static_type() -> uint64_t
{
    return erhe::scene::Item_type::composer;
}

[[nodiscard]] auto Composer::static_type_name() -> const char*
{
    return "composer";
}

[[nodiscard]] auto Composer::get_type() const -> uint64_t
{
    return static_type();
}

[[nodiscard]] auto Composer::type_name() const -> const char*
{
    return static_type_name();
}

void Composer::render(const Render_context& context) const
{
    log_frame->info("Composer::render()");
    for (const auto& renderpass : renderpasses) {
        log_frame->info("  rp: {}", renderpass->describe());
        renderpass->render(context);
    }
}

} // namespace editor
