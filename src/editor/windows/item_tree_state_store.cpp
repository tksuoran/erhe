#include "windows/item_tree_state_store.hpp"

#include "ai_driver.hpp"
#include "editor_log.hpp"

#include "erhe_profile/profile.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace editor {

namespace {

constexpr int c_item_tree_state_version = 1;

} // anonymous namespace

Item_tree_state_store::Item_tree_state_store()
    : m_persist{!is_ai_driver()}
{
    if (!m_persist) {
        log_tree->info("AI-driven run: {} is neither read nor written", m_path.generic_string());
        return;
    }
    load();
}

auto Item_tree_state_store::get_path() const -> const std::filesystem::path&
{
    return m_path;
}

void Item_tree_state_store::load()
{
    ERHE_PROFILE_FUNCTION();

    std::error_code error_code;
    if (!std::filesystem::exists(m_path, error_code)) {
        return;
    }
    std::ifstream stream{m_path};
    if (!stream) {
        log_tree->warn("Item tree state: cannot open {}", m_path.generic_string());
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(stream, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        log_tree->warn("Item tree state: {} is not a JSON object; starting empty", m_path.generic_string());
        return;
    }
    const int version = root.value("_version", 0);
    if (version != c_item_tree_state_version) {
        log_tree->warn("Item tree state: {} has version {}, expected {}; starting empty", m_path.generic_string(), version, c_item_tree_state_version);
        return;
    }
    const nlohmann::json::const_iterator trees = root.find("trees");
    if ((trees == root.end()) || !trees->is_object()) {
        return;
    }
    std::size_t row_count = 0;
    for (const auto& [scope, rows] : trees->items()) {
        if (!rows.is_object()) {
            continue;
        }
        Tree_state& tree_state = m_trees[scope];
        for (const auto& [key, value] : rows.items()) {
            if (value.is_boolean()) {
                tree_state[key] = value.get<bool>();
                ++row_count;
            }
        }
    }
    log_tree->info("Item tree state loaded from {} ({} trees, {} rows)", m_path.generic_string(), m_trees.size(), row_count);
}

void Item_tree_state_store::save() const
{
    ERHE_PROFILE_FUNCTION();

    nlohmann::json trees = nlohmann::json::object();
    for (const auto& [scope, tree_state] : m_trees) {
        nlohmann::json rows = nlohmann::json::object();
        for (const auto& [key, open] : tree_state) {
            rows[key] = open;
        }
        trees[scope] = std::move(rows);
    }
    const nlohmann::json root{
        {"_version", c_item_tree_state_version},
        {"trees",    std::move(trees)}
    };

    std::error_code error_code;
    if (m_path.has_parent_path()) {
        std::filesystem::create_directories(m_path.parent_path(), error_code);
    }
    // Through a temporary file, so a failed write leaves the previous file whole.
    std::filesystem::path temp_path = m_path;
    temp_path += ".tmp";
    {
        std::ofstream stream{temp_path, std::ios::trunc};
        if (!stream) {
            log_tree->warn("Item tree state: cannot write {}", temp_path.generic_string());
            return;
        }
        stream << root.dump(4) << '\n';
        if (!stream) {
            log_tree->warn("Item tree state: writing {} failed", temp_path.generic_string());
            return;
        }
    }
    std::filesystem::rename(temp_path, m_path, error_code);
    if (error_code) {
        log_tree->warn("Item tree state: cannot replace {}: {}", m_path.generic_string(), error_code.message());
    }
}

auto Item_tree_state_store::get_open(const std::string_view scope, const std::string_view key) const -> std::optional<bool>
{
    const auto tree_i = m_trees.find(scope);
    if (tree_i == m_trees.end()) {
        return std::nullopt;
    }
    const auto row_i = tree_i->second.find(key);
    if (row_i == tree_i->second.end()) {
        return std::nullopt;
    }
    return row_i->second;
}

void Item_tree_state_store::set_open(const std::string_view scope, const std::string_view key, const bool open)
{
    auto tree_i = m_trees.find(scope);
    if (tree_i == m_trees.end()) {
        tree_i = m_trees.emplace(std::string{scope}, Tree_state{}).first;
    }
    Tree_state& tree_state = tree_i->second;
    const auto row_i = tree_state.find(key);
    if (row_i != tree_state.end()) {
        if (row_i->second == open) {
            return;
        }
        row_i->second = open;
    } else {
        tree_state.emplace(std::string{key}, open);
    }
    if (m_persist) {
        save();
    }
}

} // namespace editor
