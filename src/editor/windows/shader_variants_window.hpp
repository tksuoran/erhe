#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Shader_variant_report;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

// The shader variant browser of a browser directory as a window
// (doc/editor/editor.md "Shader variant browser"). Main thread except request_reload().
class Shader_variants_window : public erhe::imgui::Imgui_window
{
public:
    Shader_variants_window(
        erhe::imgui::Imgui_renderer&           imgui_renderer,
        erhe::imgui::Imgui_windows&            imgui_windows,
        erhe::graphics::Shader_variant_report* report
    );
    ~Shader_variants_window() noexcept override;

    // Any thread: the directory's files changed; reloaded at the next draw.
    void request_reload();
    [[nodiscard]] auto get_directory() const -> const std::filesystem::path& { return m_directory; }

    // Implements Imgui_window
    void imgui() override;

private:
    enum class Bad_rule : unsigned int { none, below_100, above_0, has_error };
    enum class Panes    : unsigned int { both, table, details };

    class Column
    {
    public:
        std::string label;
        std::string tooltip;
        bool        numeric{true};
        Bad_rule    bad    {Bad_rule::none};
    };
    class Cell
    {
    public:
        double      number{0.0};
        bool        has_number{false};
        bool        bad{false};
        std::string text; // shown text
    };
    class Define
    {
    public:
        std::string name;
        std::string value;
        std::string value_name;
    };
    class Stage
    {
    public:
        std::string type; // "vert", "frag", ...
        std::string main_name;
        std::string source;          // file names under variants/; empty when not recorded
        std::string preamble;
        std::string spirv;
        std::string optimized_spirv;
        std::size_t spirv_bytes{0};
        std::size_t optimized_spirv_bytes{0};
        std::size_t instruction_count{0};
        std::size_t optimized_instruction_count{0};
    };
    class Key_slot
    {
    public:
        std::string label;   // padded to the axis width, "." when off
        bool        set{false};
    };
    class Program
    {
    public:
        std::string              id;       // variants/<id>.html, analytics.js key
        std::size_t              index{0}; // recording order
        std::string              name;
        std::vector<Define>      defines;
        std::vector<Stage>       stages;
        std::vector<std::string> includes;
        std::string              fragments_key; // JSON [name, defines]
        std::string              key_text;
        std::string              key_full;
        std::vector<Key_slot>    key_slots;
        std::string              filter_text;   // lower case
        std::vector<Cell>        cells;         // one per column
    };
    class Column_group
    {
    public:
        std::string tag;           // "G310 ", "A740 ", or empty for fragments
        std::size_t offset{0};
        std::size_t sum_a{0};
        std::size_t sum_b{0};
        std::string sum_label;
        bool        fragments{false};
    };
    class Text_block
    {
    public:
        void set(std::string text);
        void imgui(const char* label, bool default_open) const;

        std::string                text;
        std::vector<std::uint32_t> line_starts;
    };
    class Stage_detail
    {
    public:
        Text_block source;
        Text_block preamble;
        Text_block before;
        Text_block after;
    };
    class Include_detail
    {
    public:
        std::string path;
        Text_block  text;
    };

    void reload            ();
    auto load_manifest     (std::string& error) -> bool;
    void load_sidecars     ();
    void build_key_slots   ();
    void build_cells       ();
    void update_shown      ();
    void select            (std::size_t program_index);
    void bar_imgui         ();
    void table_imgui       ();
    void detail_imgui      ();
    void key_tab_imgui     (const Program& program);
    void sources_tab_imgui (const Program& program);
    void spirv_tab_imgui   (const Program& program, bool optimized);
    void mali_core_imgui   (const nlohmann::ordered_json& core, const nlohmann::ordered_json& program);

    erhe::graphics::Shader_variant_report* m_report{nullptr};

    std::mutex m_reload_mutex;
    bool       m_reload_requested{true};

    std::filesystem::path     m_directory;
    std::string               m_directory_input;
    std::string               m_load_error;
    nlohmann::ordered_json    m_analytics;   // window.erhe_shader_analytics, or null
    nlohmann::ordered_json    m_fragments;   // window.erhe_shader_fragments, or null
    std::string               m_mali_core;
    std::string               m_analytics_info;
    std::string               m_fragments_info;
    std::vector<Program>      m_programs;
    std::vector<Column>       m_columns;
    std::vector<Column_group> m_groups;
    std::vector<std::size_t>  m_shown;        // indices into m_programs, filtered and sorted
    std::string               m_summary;
    std::string               m_filter;
    int                       m_sort_column{0};
    bool                      m_sort_ascending{true};
    bool                      m_sort_dirty{true};
    Panes                     m_panes{Panes::both};
    float                     m_table_width{0.0f};

    std::size_t                 m_selected{static_cast<std::size_t>(-1)}; // index into m_programs
    std::vector<Stage_detail>   m_stage_details;
    std::vector<Include_detail> m_include_details;
};

} // namespace editor
