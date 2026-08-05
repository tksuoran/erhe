#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace editor {

// Thread-safe collection of lightmap pipeline failures and warnings for
// display in the Lightmap window. UV unwrap runs on tf::Executor workers
// and its exceptions used to vanish into the log (items.cpp task-boundary
// catch); every producer - unwrap, atlas layout, G-buffer, bake, tile
// persistence, streaming - records here instead so the user can see why
// a stage failed without hunting the log. UI-free by design (the baker's
// convention); the Lightmap window renders snapshot().
class Lightmap_report
{
public:
    enum class Stage : int {
        uv_unwrap = 0,
        layout,
        partition,
        gbuffer,
        bake,
        persist,
        stream
    };

    static auto c_str(Stage stage) -> const char*;

    class Entry
    {
    public:
        Stage       stage     {Stage::uv_unwrap};
        std::string subject   {}; // mesh / node / tile / file the entry is about
        std::string message   {};
        bool        is_warning{false};
    };

    void add_error  (Stage stage, std::string subject, std::string message);
    void add_warning(Stage stage, std::string subject, std::string message);

    // Copy for UI iteration - entries are appended from worker threads.
    [[nodiscard]] auto snapshot() const -> std::vector<Entry>;
    [[nodiscard]] auto empty   () const -> bool;

    void clear      ();
    void clear_stage(Stage stage);

private:
    void add(Stage stage, std::string&& subject, std::string&& message, bool is_warning);

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;

    // Cap so a pathological scene (thousands of failing meshes) cannot grow
    // the list without bound; the newest entries win.
    static constexpr std::size_t s_max_entries = 256;
    std::size_t m_dropped{0};
};

}
