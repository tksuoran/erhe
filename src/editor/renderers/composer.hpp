#pragma once

#include "erhe_item/item.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace editor
{

class Render_context;
class Renderpass;

class Composer
    : public erhe::Item
{
public:
    explicit Composer(const std::string_view name);

    // Implements Item
    static constexpr std::string_view static_type_name{"Composer"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::composer; }
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    void render(const Render_context& context) const;

    std::vector<std::shared_ptr<Renderpass>> renderpasses;
};

} // namespace editor
