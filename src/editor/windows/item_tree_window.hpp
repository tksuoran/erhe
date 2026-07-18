#pragma once

#include "operations/compound_operation.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_item/item.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

namespace erhe {
    class Hierarchy;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace editor {

class App_scenes;
class App_settings;
class Brush;
class Icon_set;
class Operation;
class Operation_stack;
class Scene_root;
class Selection_tool;

enum class Placement : unsigned int {
    Before_anchor = 0, // Place node before anchor
    After_anchor,      // Place node after anchor
    Last               // Place node as last child of parent, ignore anchor
};

enum class Selection_usage : unsigned int {
    Selection_ignored = 0,
    Selection_used
};

class Item_tree
{
public:
    Item_tree(App_context& context);

    // Callback to render custom context menu items before the generic
    // Cut/Copy/Paste/Duplicate/Delete section.  Set `close` to true
    // when an item is selected to close the popup.
    using Context_menu_callback = std::function<void(
        const std::shared_ptr<erhe::Item_base>& item,
        std::vector<std::function<void()>>&     deferred_operations,
        bool&                                   close
    )>;

    void set_root                       (const std::shared_ptr<erhe::Hierarchy>& root);
    [[nodiscard]] auto get_root         () const -> const std::shared_ptr<erhe::Hierarchy>&;
    // Optional item flattened before the root, at indent 0 (a sibling of the
    // root's rows). Used by the scene hierarchy window to show a selectable
    // Scene item with the Content Library nested under it (issue #240); left
    // null by other windows, which are then unaffected.
    void set_header_item                (const std::shared_ptr<erhe::Item_base>& item);
    void set_item_filter                (const erhe::Item_filter& filter);
    void clear_cached_rows              ();
    void set_item_callback              (std::function<bool(const std::shared_ptr<erhe::Item_base>&)> fun);
    void set_hover_callback             (std::function<void()> fun);
    void add_item_context_menu_callback (Context_menu_callback fun);

    auto drag_and_drop_target(const std::shared_ptr<erhe::Item_base>& node) -> bool;

    void imgui_tree(float ui_scale);

    ERHE_PROFILE_MUTEX(std::mutex, item_tree_mutex);

protected:
    [[nodiscard]] auto get_hovered_item() const -> const std::shared_ptr<erhe::Item_base>&;

    App_context& m_context;

private:
    void set_item_selection_terminator(const std::shared_ptr<erhe::Item_base>& item);
    void set_item_selection           (const std::shared_ptr<erhe::Item_base>& item, bool selected);
    void clear_selection              ();
    void recursive_add_to_selection   (const std::shared_ptr<erhe::Item_base>& node);
    void select_all                   ();
    void move_selection               (const std::shared_ptr<erhe::Item_base>& target, erhe::Item_base* payload_item, Placement placement);
    void attach_selection_to          (const std::shared_ptr<erhe::Item_base>& target_node, erhe::Item_base* payload_item);
    void root_popup_menu              ();
    void item_popup_menu              (const std::shared_ptr<erhe::Item_base>& item);

    // A resolved icon glyph ready to draw: no type queries or measurement at render time
    class Row_icon
    {
    public:
        ImFont*          font      {nullptr};
        const char*      code      {nullptr}; // nullptr = no icon
        glm::vec4        color     {1.0f, 1.0f, 1.0f, 1.0f};
        const glm::vec3* live_color{nullptr}; // overrides color at draw time (light / material tint)
        float            x_offset  {0.0f};    // from the right-icons block start
    };

    // One entry per currently visible tree row, fully resolved and laid out so
    // that rendering needs no casts, no icon resolution and no text
    // measurement. Rendering submits only the on-screen range through
    // ImGuiListClipper. Cached across frames; rebuilt when the item mutation
    // serial moves or when this tree's own inputs (open/close state, filters,
    // root, fonts, layout) change.
    class Flat_row
    {
    public:
        static constexpr std::size_t max_right_icon_count = 6;

        std::shared_ptr<erhe::Item_base>           item;
        std::shared_ptr<Brush>                     brush;              // brush thumbnail rows (content library)
        erhe::utility::Debug_label                 debug_label;        // "<name>##<id>" - tree node id
        std::string_view                           label_text;         // points into the item name; cache is rebuilt on rename
        ImGuiTreeNodeFlags                         tree_node_flags{0}; // everything except Selected (queried live)
        float                                      indent         {0.0f};
        Row_icon                                   primary_icon;
        float                                      label_x_offset {0.0f}; // from row content start
        float                                      label_width    {0.0f};
        std::array<Row_icon, max_right_icon_count> right_icons;
        std::size_t                                right_icon_count {0};
        float                                      right_icons_width{0.0f};
        // R5.8 reference badge: content-library REFERENCE entries show a dim
        // suffix naming the defining container (filename) after the label,
        // plus a link glyph in right_icons. Empty for every other row.
        // Allocates only when the row cache is rebuilt, never per frame.
        std::string                                reference_suffix;
        float                                      reference_suffix_width{0.0f};
    };

    void imgui_row                    (const Flat_row& row);
    void item_icon_and_text           (const std::shared_ptr<erhe::Item_base>& item); // drag payload preview
    void item_update_selection        (const std::shared_ptr<erhe::Item_base>& item);
    void flatten_visible_rows         (const std::shared_ptr<erhe::Item_base>& item, float indent);

    enum class Show_mode : unsigned int {
        Hide          = 0,
        Show          = 1,
        Show_expanded = 2
    };

    [[nodiscard]] auto should_show(const std::shared_ptr<erhe::Item_base>& item) -> Show_mode;

    void try_add_to_attach(
        Compound_operation::Parameters&         compound_parameters,
        const std::shared_ptr<erhe::Item_base>& target_node,
        const std::shared_ptr<erhe::Item_base>& node,
        Selection_usage                         selection_usage
    );

    void reposition(
        Compound_operation::Parameters&         compound_parameters,
        const std::shared_ptr<erhe::Item_base>& anchor_node,
        const std::shared_ptr<erhe::Item_base>& child_node,
        Placement                               placement,
        Selection_usage                         selection_usage
    );
    void drag_and_drop_source(const std::shared_ptr<erhe::Item_base>& node);

    erhe::Item_filter                                            m_filter;
    ImGuiTextFilter                                              m_text_filter;
    std::shared_ptr<erhe::Hierarchy>                             m_root;
    std::shared_ptr<erhe::Item_base>                             m_header_item;
    std::function<bool(const std::shared_ptr<erhe::Item_base>&)> m_item_callback;
    std::function<void()>                                        m_hover_callback;
    std::vector<Context_menu_callback>                           m_item_context_menu_callbacks;

    std::shared_ptr<Operation>         m_operation;
    std::vector<std::function<void()>> m_operations;

    bool                               m_toggled_open{false};
    std::weak_ptr<erhe::Item_base>     m_last_focus_item;
    std::shared_ptr<erhe::Item_base>   m_hovered_item;
    std::shared_ptr<erhe::Item_base>   m_popup_item;
    std::string                        m_popup_id_string;
    unsigned int                       m_popup_id{0};
    bool                               m_shift_down_range_selection_started{false};
    float                              m_ui_scale{1.0f};

    std::vector<Flat_row>              m_flat_rows;
    bool                               m_flat_rows_dirty{true};
    uint64_t                           m_last_mutation_serial{0};
    erhe::Item_filter                  m_cached_filter{};
    bool                               m_cached_expand_attachments{false};
    float                              m_cached_indent_spacing{0.0f};
    float                              m_cached_font_size{0.0f};
    float                              m_cached_icon_font_size{0.0f};
    bool                               m_range_selection_edited{false};

    // Row layout constants derived from font and style at rebuild time
    float                              m_icon_x_offset {0.0f}; // primary icon, from row content start
    float                              m_icon_y_offset {0.0f}; // icon glyphs, from row top
    float                              m_label_y_offset{0.0f}; // label text, from row top
};

class Item_tree_window : public erhe::imgui::Imgui_window, public Item_tree
{
public:
    Item_tree_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        std::string_view             window_title,
        std::string_view             ini_label
    );

    // Marks this tree window as a scene-hierarchy browser (as opposed to the
    // generic content-library / tools tree windows built from the same class).
    // Used by window placement (#258) to dock a new Hierarchy window with an
    // existing one.
    void set_scene_hierarchy(bool value);
    [[nodiscard]] auto is_scene_hierarchy() const -> bool;

    // Stable per-window slot (1, 2, ...) carried in the "Scene Hierarchy [N]"
    // title and the "Hierarchy_window N" ini label. The lowest free slot is
    // reused after a Hierarchy window is destroyed, so window layout and open
    // state persist across sessions independent of scene names and open order
    // (issue #265). 0 for tree windows that are not scene-hierarchy browsers.
    void set_scene_hierarchy_slot(int slot) { m_scene_hierarchy_slot = slot; }
    [[nodiscard]] auto get_scene_hierarchy_slot() const -> int { return m_scene_hierarchy_slot; }

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;
    void hidden  () override;

private:
    // The Scene_root this window browses; nullptr for non-scene-hierarchy
    // trees (content library, tools scene browser).
    [[nodiscard]] auto get_tree_scene_root() const -> Scene_root*;

    bool m_is_scene_hierarchy{false};
    int  m_scene_hierarchy_slot{0};
    int  m_active_scene_tint_count{0};
    bool m_was_focused{false};
};

}
