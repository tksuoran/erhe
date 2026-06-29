#pragma once

#include "erhe_item/item.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace editor {

class Render_context;
class Composition_pass;

class Composer : public erhe::Item<erhe::Item_base, erhe::Item_base, Composer>
{
public:
    Composer(const Composer&);
    Composer& operator=(const Composer&);
    Composer(Composer&& old) noexcept;
    Composer& operator=(Composer&& old) noexcept;
    ~Composer() noexcept override;

    explicit Composer(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composer"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::composer; }

    // Public API
    // Renders the composition passes selected by phase:
    //  - include_content: passes with data.overlay == false (scene content).
    //  - include_overlay: passes with data.overlay == true (tool gizmo /
    //    rendertarget meshes). When post-processing is enabled these are drawn
    //    in a separate pass after it; otherwise both phases run in one pass.
    // See issue #230.
    void render(const Render_context& context, bool include_content, bool include_overlay);

    void imgui();

    // TODO Move to children
    ERHE_PROFILE_MUTEX(std::mutex,                 mutex);
    std::vector<std::shared_ptr<Composition_pass>> composition_passes;
};

}
