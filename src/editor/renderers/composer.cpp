#include "renderers/composer.hpp"
#include "editor_log.hpp"
#include "renderers/renderpass.hpp"

namespace editor
{

// TODO Do deep copy instead / use Hierarchy

Composer::Composer(const Composer&)            = default;
Composer& Composer::operator=(const Composer&) = default;
Composer::~Composer() noexcept                 = default;

Composer::Composer(const std::string_view name)
    : Item{name}
{
}

[[nodiscard]] auto Composer::get_type() const -> uint64_t
{
    return get_static_type();
}

[[nodiscard]] auto Composer::get_type_name() const -> std::string_view
{
    return static_type_name;
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
