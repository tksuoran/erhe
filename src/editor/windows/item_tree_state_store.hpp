#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace editor {

// Persistent open / closed state of item tree rows, in state/item_trees.json
// (doc/editor/windows.md "Row open state"). Main thread only. An AI-driven run neither
// reads nor writes the file; the state then lives for the session only.
class Item_tree_state_store
{
public:
    Item_tree_state_store();

    [[nodiscard]] auto get_open(std::string_view scope, std::string_view key) const -> std::optional<bool>;
    // Writes the file when the stored value changes.
    void set_open(std::string_view scope, std::string_view key, bool open);

    [[nodiscard]] auto get_path() const -> const std::filesystem::path&;

private:
    void load();
    void save() const;

    using Tree_state = std::map<std::string, bool, std::less<>>;

    std::filesystem::path                          m_path{"state/item_trees.json"};
    bool                                           m_persist{true};
    std::map<std::string, Tree_state, std::less<>> m_trees;
};

} // namespace editor
