#include "erhe_imgui/imgui_item_recorder.hpp"

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace erhe::imgui {

namespace {

// Every ImGui context erhe creates is driven from the main thread (see the
// threading note in the header), so a plain vector is the whole registry. It
// holds one entry per live Imgui_host, so it stays a handful of entries and a
// linear scan is cheaper than a map lookup.
std::vector<Imgui_item_recorder*>& get_registry()
{
    static std::vector<Imgui_item_recorder*> registry;
    return registry;
}

} // anonymous namespace

Imgui_item_recorder::Imgui_item_recorder() = default;

Imgui_item_recorder::~Imgui_item_recorder() noexcept
{
    set_context(nullptr);
}

void Imgui_item_recorder::set_context(ImGuiContext* context)
{
    std::vector<Imgui_item_recorder*>& registry = get_registry();
    if (m_context != nullptr) {
        const auto i = std::find(registry.begin(), registry.end(), this);
        if (i != registry.end()) {
            registry.erase(i);
        }
    }
    m_context = context;
    if (m_context != nullptr) {
        registry.push_back(this);
    }
}

auto Imgui_item_recorder::get_context() const -> ImGuiContext*
{
    return m_context;
}

auto Imgui_item_recorder::find_for_context(ImGuiContext* context) -> Imgui_item_recorder*
{
    if (context == nullptr) {
        return nullptr;
    }
    for (Imgui_item_recorder* recorder : get_registry()) {
        if (recorder->m_context == context) {
            return recorder;
        }
    }
    return nullptr;
}

void Imgui_item_recorder::begin_frame()
{
    m_records.clear(); // capacity kept
    m_labels .clear(); // capacity kept
    m_has_records = false;
}

void Imgui_item_recorder::end_frame()
{
    m_has_records = true;
}

auto Imgui_item_recorder::get_records() const -> const std::vector<Item_record>&
{
    return m_records;
}

auto Imgui_item_recorder::get_label(const Item_record& record) const -> std::string_view
{
    if ((record.label_offset == Item_record::c_no_label) || (record.label_offset >= m_labels.size())) {
        return std::string_view{};
    }
    return std::string_view{m_labels.data() + record.label_offset};
}

auto Imgui_item_recorder::has_records() const -> bool
{
    return m_has_records;
}

auto Imgui_item_recorder::get_hook_call_count() const -> uint64_t
{
    return m_hook_call_count;
}

void Imgui_item_recorder::on_item_add(
    const ImGuiID            id,
    const ImGuiID            window_id,
    const ImRect&            bb,
    const ImGuiLastItemData* item_data
)
{
    ++m_hook_call_count;
    Item_record record;
    record.id            = id;
    record.window_id     = window_id;
    record.x0            = bb.Min.x;
    record.y0            = bb.Min.y;
    record.x1            = bb.Max.x;
    record.y1            = bb.Max.y;
    record.has_item_data = (item_data != nullptr);
    if (item_data != nullptr) {
        record.status_flags = static_cast<int>(item_data->StatusFlags);
        record.item_flags   = static_cast<int>(item_data->ItemFlags);
    }
    m_records.push_back(record);
}

void Imgui_item_recorder::on_item_info(const ImGuiID id, const char* label, const int status_flags)
{
    ++m_hook_call_count;
    // ItemInfo follows the ItemAdd of the same item, so the record to fill is
    // the last one carrying that id.
    for (std::size_t i = m_records.size(); i > 0; --i) {
        Item_record& record = m_records[i - 1];
        if (record.id != id) {
            continue;
        }
        record.status_flags = status_flags;
        record.has_status   = true;
        // The first non-empty label wins. A widget built out of another one
        // reports twice: a menu is a Selectable("") that reports an empty
        // label, and BeginMenu() then reports the menu's name for the same id
        // - so an already recorded empty label must give way, and a name that
        // is there must not.
        if ((label != nullptr) && (label[0] != '\0') && get_label(record).empty()) {
            const std::size_t length = std::strlen(label);
            record.label_offset = static_cast<uint32_t>(m_labels.size());
            m_labels.insert(m_labels.end(), label, label + length);
            m_labels.push_back('\0');
        }
        return;
    }
}

void Imgui_item_recorder::on_log()
{
    ++m_hook_call_count;
}

void Imgui_item_recorder::set_item_label(const ImGuiID id, const std::string_view label)
{
    for (std::size_t i = m_records.size(); i > 0; --i) {
        Item_record& record = m_records[i - 1];
        if (record.id != id) {
            continue;
        }
        record.label_offset = static_cast<uint32_t>(m_labels.size());
        m_labels.insert(m_labels.end(), label.begin(), label.end());
        m_labels.push_back('\0');
        return;
    }
}

auto Imgui_item_recorder::get_record_count() const -> std::size_t
{
    return m_records.size();
}

void Imgui_item_recorder::set_labels_from(const std::size_t first_index, const std::string_view label)
{
    static constexpr std::string_view c_component_suffixes[4] = {".x", ".y", ".z", ".w"};

    std::size_t named_count = 0;
    for (std::size_t i = first_index; i < m_records.size(); ++i) {
        if (m_records[i].id != 0) {
            ++named_count;
        }
    }
    if (named_count == 0) {
        return;
    }

    std::size_t component = 0;
    for (std::size_t i = first_index; i < m_records.size(); ++i) {
        Item_record& record = m_records[i];
        if (record.id == 0) {
            continue;
        }
        record.label_offset = static_cast<uint32_t>(m_labels.size());
        m_labels.insert(m_labels.end(), label.begin(), label.end());
        if (named_count > 1) {
            if (component < 4) {
                const std::string_view suffix = c_component_suffixes[component];
                m_labels.insert(m_labels.end(), suffix.begin(), suffix.end());
            } else {
                const std::string suffix = "." + std::to_string(component);
                m_labels.insert(m_labels.end(), suffix.begin(), suffix.end());
            }
        }
        m_labels.push_back('\0');
        ++component;
    }
}

namespace {

// The recorder of the context currently being recorded, or nullptr when the
// current frame records nothing. This is the one branch every naming call
// costs in an ordinary frame.
[[nodiscard]] auto get_recording_recorder() -> Imgui_item_recorder*
{
    ImGuiContext* const context = ImGui::GetCurrentContext();
    if ((context == nullptr) || !context->TestEngineHookItems) {
        return nullptr;
    }
    return Imgui_item_recorder::find_for_context(context);
}

} // anonymous namespace

void set_item_debug_label(const std::string_view label)
{
    Imgui_item_recorder* const recorder = get_recording_recorder();
    if (recorder == nullptr) {
        return;
    }
    recorder->set_item_label(ImGui::GetCurrentContext()->LastItemData.ID, label);
}

auto is_item_recording() -> bool
{
    return get_recording_recorder() != nullptr;
}

auto get_recorded_item_count() -> std::size_t
{
    const Imgui_item_recorder* const recorder = get_recording_recorder();
    return (recorder != nullptr) ? recorder->get_record_count() : 0;
}

void set_recorded_item_labels(const std::size_t first_index, const std::string_view label)
{
    Imgui_item_recorder* const recorder = get_recording_recorder();
    if (recorder == nullptr) {
        return;
    }
    recorder->set_labels_from(first_index, label);
}

auto Imgui_item_recorder::find_label(const ImGuiID id) const -> const char*
{
    for (std::size_t i = m_records.size(); i > 0; --i) {
        const Item_record& record = m_records[i - 1];
        if ((record.id == id) && (record.label_offset != Item_record::c_no_label)) {
            return m_labels.data() + record.label_offset;
        }
    }
    return nullptr;
}

} // namespace erhe::imgui

// The four functions Dear ImGui declares extern in imgui_internal.h
// ("Test Engine specific hooks"). Dear ImGui only calls the first three while
// ImGuiContext::TestEngineHookItems is true; the fourth is called from the ID
// Stack Tool and the item-path debug tools whenever they need a label.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data)
{
    erhe::imgui::Imgui_item_recorder* const recorder = erhe::imgui::Imgui_item_recorder::find_for_context(ctx);
    if (recorder == nullptr) {
        return;
    }
    const ImGuiID window_id = (ctx->CurrentWindow != nullptr) ? ctx->CurrentWindow->ID : 0;
    recorder->on_item_add(id, window_id, bb, item_data);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
    erhe::imgui::Imgui_item_recorder* const recorder = erhe::imgui::Imgui_item_recorder::find_for_context(ctx);
    if (recorder == nullptr) {
        return;
    }
    recorder->on_item_info(id, label, static_cast<int>(flags));
}

void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...)
{
    // erhe keeps no test-engine log: Dear ImGui's own debug log already holds
    // these lines (this hook is fed from AddDebugLog) and erhe logging goes
    // through the erhe::imgui log categories.
    static_cast<void>(fmt);
    erhe::imgui::Imgui_item_recorder* const recorder = erhe::imgui::Imgui_item_recorder::find_for_context(ctx);
    if (recorder != nullptr) {
        recorder->on_log();
    }
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id)
{
    const erhe::imgui::Imgui_item_recorder* const recorder = erhe::imgui::Imgui_item_recorder::find_for_context(ctx);
    if (recorder == nullptr) {
        return nullptr;
    }
    return recorder->find_label(id);
}
