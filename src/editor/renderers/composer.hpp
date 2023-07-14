#pragma once

#include "erhe/scene/item.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace editor
{

class Render_context;
class Renderpass;

class Composer
    : public erhe::scene::Item
{
public:
    explicit Composer(const std::string_view name);

    // Implements Item
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;

    void render(const Render_context& context) const;

    std::vector<std::shared_ptr<Renderpass>> renderpasses;
};

} // namespace editor
