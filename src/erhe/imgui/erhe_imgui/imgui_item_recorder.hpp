#pragma once

// Dear ImGui item recorder (doc/erhe/imgui.md, "Item recorder").
//
// Dear ImGui reports every item it submits to four extern functions when
// IMGUI_ENABLE_TEST_ENGINE is defined (it is, for the imgui target, in
// src/imgui/CMakeLists.txt) and ImGuiContext::TestEngineHookItems is true.
// erhe supplies those four functions here rather than linking the
// imgui_test_engine library, which carries its own non-MIT license.
//
// One Imgui_item_recorder belongs to one ImGuiContext, and one Imgui_host owns
// one context, so the recorder is a member of Imgui_host. Recording is armed
// for exactly one frame by Imgui_host::request_item_recording(); with
// TestEngineHookItems false the whole cost is one branch per item inside Dear
// ImGui and not a single call into this file.
//
// Threading: every ImGui context erhe creates is driven from the main (tick)
// thread - Imgui_windows::begin_frame / draw_imgui_windows / end_frame run
// there for the desktop host and for every Rendertarget_imgui_host alike - so
// the context -> recorder registry below needs no synchronization.

#include <imgui/imgui.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

struct ImRect;
struct ImGuiLastItemData;

namespace erhe::imgui {

// One item Dear ImGui submitted in a recorded frame. The rectangle is in the
// context's screen pixels, which for the desktop host are editor window
// pixels - the coordinate space the input gesture MCP tools take.
class Item_record
{
public:
    static constexpr uint32_t c_no_label = 0xffffffffu;

    ImGuiID  id             {0};
    ImGuiID  window_id      {0};
    float    x0             {0.0f};
    float    y0             {0.0f};
    float    x1             {0.0f};
    float    y1             {0.0f};
    int      status_flags   {0};
    int      item_flags     {0};
    uint32_t label_offset   {c_no_label}; // into the label arena
    bool     has_item_data  {false};      // ItemAdd carried an ImGuiLastItemData
    bool     has_status     {false};      // ItemInfo filled status_flags
};

class Imgui_item_recorder
{
public:
    Imgui_item_recorder();
    ~Imgui_item_recorder() noexcept;

    Imgui_item_recorder (const Imgui_item_recorder&) = delete;
    auto operator=      (const Imgui_item_recorder&) -> Imgui_item_recorder& = delete;
    Imgui_item_recorder (Imgui_item_recorder&&) = delete;
    auto operator=      (Imgui_item_recorder&&) -> Imgui_item_recorder& = delete;

    // The context whose items this recorder receives. Called by Imgui_host
    // once its context exists, and with nullptr before the context is
    // destroyed.
    void set_context(ImGuiContext* context);

    [[nodiscard]] auto get_context() const -> ImGuiContext*;

    // Frame boundaries of a recorded frame. begin_frame() clears the records
    // keeping their capacity; end_frame() marks them readable.
    void begin_frame();
    void end_frame  ();

    [[nodiscard]] auto get_records        () const -> const std::vector<Item_record>&;
    [[nodiscard]] auto get_label          (const Item_record& record) const -> std::string_view;
    [[nodiscard]] auto has_records        () const -> bool;
    // Number of hook calls this recorder has received since it was created.
    // A frame without a recording request must not move it.
    [[nodiscard]] auto get_hook_call_count() const -> uint64_t;

    // Called by the four extern hook functions.
    void on_item_add (ImGuiID id, ImGuiID window_id, const ImRect& bb, const ImGuiLastItemData* item_data);
    void on_item_info(ImGuiID id, const char* label, int status_flags);
    void on_log      ();

    // Replaces the recorded label of the last item carrying this id; see
    // set_item_debug_label() below.
    void set_item_label(ImGuiID id, std::string_view label);

    [[nodiscard]] auto get_record_count() const -> std::size_t;

    // Names the items recorded at [first_index, get_record_count()): a single
    // named item takes 'label' itself, several take '<label>.x', '<label>.y',
    // '<label>.z', '<label>.w' and '<label>.<position>' past the fourth, so
    // one component of a vector row is addressable on its own. Items Dear
    // ImGui submits with id 0 - a group's bounding box, a text run - are left
    // unnamed because nothing can click them.
    void set_labels_from(std::size_t first_index, std::string_view label);

    [[nodiscard]] auto find_label(ImGuiID id) const -> const char*;

    [[nodiscard]] static auto find_for_context(ImGuiContext* context) -> Imgui_item_recorder*;

private:
    ImGuiContext*            m_context        {nullptr};
    std::vector<Item_record> m_records;
    std::vector<char>        m_labels;
    bool                     m_has_records    {false};
    uint64_t                 m_hook_call_count{0};
};

// Names the item Dear ImGui submitted last, for a widget whose visible text is
// not the label it was given - an item tree row draws its own name with the
// draw list and hands ImGui an empty label, an icon-only button's label is a
// glyph. Without this the item is recorded with no label and cannot be
// addressed by name. Costs one branch when no frame is being recorded.
void set_item_debug_label(std::string_view label);

// True while the current ImGui context is recording its items. Code that
// names items builds its label text only while this holds, so a frame nobody
// asked to record allocates nothing for naming.
[[nodiscard]] auto is_item_recording() -> bool;

// Number of items the current context has recorded so far in the frame being
// recorded, and 0 when no frame is. Reading it before and after a widget
// brackets the items that widget submitted, which set_recorded_item_labels()
// then names (Imgui_item_recorder::set_labels_from).
[[nodiscard]] auto get_recorded_item_count() -> std::size_t;

void set_recorded_item_labels(std::size_t first_index, std::string_view label);

} // namespace erhe::imgui
