#pragma once

#include "erhe_item/item.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace editor {

class Render_context;
class Renderpass;

class Composer : public erhe::Item<erhe::Item_base, erhe::Item_base, Composer>
{
public:
    explicit Composer(const Composer&);
    Composer& operator=(const Composer&);
    ~Composer() noexcept override;

    explicit Composer(const std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composer"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::composer; }
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Public API
    void render(const Render_context& context) const;

    void imgui();

    // TODO Move to children
    std::vector<std::shared_ptr<Renderpass>> renderpasses;
};

} // namespace editor
