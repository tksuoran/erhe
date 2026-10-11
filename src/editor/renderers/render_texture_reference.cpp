#include "renderers/render_texture_reference.hpp"

#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

Render_texture_reference::Render_texture_reference(std::function<const erhe::graphics::Texture*()> resolve)
    : m_resolve{std::move(resolve)}
{
    ERHE_VERIFY(m_resolve);
}

Render_texture_reference::~Render_texture_reference() noexcept = default;

auto Render_texture_reference::get_referenced_texture() const -> const erhe::graphics::Texture*
{
    erhe::utility::Thread_roles::assert_render_thread();
    return m_resolve();
}

} // namespace editor
