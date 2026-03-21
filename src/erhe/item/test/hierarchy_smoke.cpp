#include "hierarchy_smoke.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace erhe::item::test {

// --- Dummy hierarchy types mimicking erhe::scene patterns ---

// Intermediate node type (like erhe::scene::Node)
class Test_node : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Test_node>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Test_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::node; }
};

// Group node (like Content_library_node / Asset_folder)
class Test_group : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Test_group>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Test_group"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::scene; }
};

// Leaf node (like a simplified mesh/camera placeholder in the tree)
class Test_leaf : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Test_leaf>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Test_leaf"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::mesh; }
};

// Another leaf type for type-diversity in for_each<T>
class Test_marker : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Test_marker>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Test_marker"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::light; }
};

// --- Smoke test implementation ---

namespace {

auto make_node(std::mt19937_64& rng, std::size_t& counter) -> std::shared_ptr<erhe::Hierarchy>
{
    ++counter;
    const auto name = std::to_string(counter);
    const auto kind = rng() % 4;
    switch (kind) {
        case 0:  return std::make_shared<Test_node>(name);
        case 1:  return std::make_shared<Test_group>(name);
        case 2:  return std::make_shared<Test_leaf>(name);
        default: return std::make_shared<Test_marker>(name);
    }
}

auto pick_random(std::mt19937_64& rng, std::vector<std::shared_ptr<erhe::Hierarchy>>& nodes) -> std::shared_ptr<erhe::Hierarchy>
{
    if (nodes.empty()) {
        return {};
    }
    return nodes[rng() % nodes.size()];
}

void remove_from_list(std::vector<std::shared_ptr<erhe::Hierarchy>>& nodes, const erhe::Hierarchy* target)
{
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
            [target](const std::shared_ptr<erhe::Hierarchy>& n) { return n.get() == target; }),
        nodes.end()
    );
}

// Collect all nodes in a subtree (for removal from the flat list)
void collect_subtree(erhe::Hierarchy* node, std::vector<erhe::Hierarchy*>& out)
{
    out.push_back(node);
    for (const auto& child : node->get_children()) {
        collect_subtree(child.get(), out);
    }
}

} // namespace

auto run_hierarchy_smoke(const uint64_t seed, const double duration_seconds) -> Smoke_result
{
    Smoke_result result;
    result.seed = seed;

    std::mt19937_64 rng{seed};

    // Flat list of all live nodes (roots and non-roots)
    std::vector<std::shared_ptr<erhe::Hierarchy>> nodes;

    // Keep a few root nodes
    std::vector<std::shared_ptr<erhe::Hierarchy>> roots;

    std::size_t node_counter = 0;

    // Seed initial roots
    for (int i = 0; i < 4; ++i) {
        auto root = make_node(rng, node_counter);
        result.nodes_created++;
        roots.push_back(root);
        nodes.push_back(root);
    }

    const auto start = std::chrono::steady_clock::now();

    while (true) {
        const auto now     = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed >= duration_seconds) {
            break;
        }

        result.operations++;

        const auto op = rng() % 100;

        if (op < 30) {
            // CREATE: make a new node and attach to a random parent
            auto parent = pick_random(rng, nodes);
            auto child  = make_node(rng, node_counter);
            result.nodes_created++;
            if (parent && parent->get_depth() < 20) {
                child->set_parent(parent);
            } else {
                roots.push_back(child);
            }
            nodes.push_back(child);

        } else if (op < 50) {
            // REPARENT: move a random node to a different parent
            auto node       = pick_random(rng, nodes);
            auto new_parent = pick_random(rng, nodes);
            if (node && new_parent && node.get() != new_parent.get() &&
                !new_parent->is_ancestor(node.get()) &&
                new_parent->get_depth() < 20)
            {
                // Remove from roots if it was a root
                remove_from_list(roots, node.get());
                node->set_parent(new_parent);
                result.reparents++;
            }

        } else if (op < 60) {
            // REMOVE LEAF: pick a node with no children and remove it
            auto node = pick_random(rng, nodes);
            if (node && node->get_child_count() == 0 && nodes.size() > 4) {
                node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
                remove_from_list(roots, node.get());
                remove_from_list(nodes, node.get());
                result.removes++;
            }

        } else if (op < 65) {
            // RECURSIVE REMOVE: remove a subtree
            auto node = pick_random(rng, nodes);
            if (node && nodes.size() > 8) {
                std::vector<erhe::Hierarchy*> subtree;
                collect_subtree(node.get(), subtree);
                node->recursive_remove();
                for (auto* n : subtree) {
                    remove_from_list(nodes, n);
                    remove_from_list(roots, n);
                }
                result.removes++;
            }

        } else if (op < 70) {
            // REMOVE (splice): remove a node, reparenting its children to its parent
            auto node = pick_random(rng, nodes);
            if (node && nodes.size() > 4) {
                auto parent = node->get_parent().lock();
                node->remove();
                remove_from_list(nodes, node.get());
                remove_from_list(roots, node.get());
                // Children are now under the old parent (or orphaned)
                result.removes++;
            }

        } else if (op < 80) {
            // ITERATE: for_each with different types
            auto node = pick_random(rng, nodes);
            if (node) {
                std::size_t count = 0;
                const auto type_pick = rng() % 4;
                switch (type_pick) {
                    case 0:
                        node->for_each<Test_node>([&](Test_node&) -> bool { ++count; return true; });
                        break;
                    case 1:
                        node->for_each<Test_group>([&](Test_group&) -> bool { ++count; return true; });
                        break;
                    case 2:
                        node->for_each<Test_leaf>([&](Test_leaf&) -> bool { ++count; return true; });
                        break;
                    default:
                        node->for_each<Test_marker>([&](Test_marker&) -> bool { ++count; return true; });
                        break;
                }
                result.iterations++;
            }

        } else if (op < 85) {
            // CLONE: clone a subtree
            auto node = pick_random(rng, nodes);
            if (node && node->get_child_count() > 0 && node->get_child_count() < 50) {
                auto cloned = std::dynamic_pointer_cast<erhe::Hierarchy>(node->clone());
                if (cloned) {
                    cloned->adopt_orphan_children();
                    roots.push_back(cloned);
                    // Add cloned subtree to flat list
                    cloned->for_each<erhe::Hierarchy>([&](erhe::Hierarchy& h) -> bool {
                        nodes.push_back(h.shared_hierarchy_from_this());
                        return true;
                    });
                    result.clones++;
                }
            }

        } else if (op < 90) {
            // FLAG OPERATIONS: toggle flags randomly
            auto node = pick_random(rng, nodes);
            if (node) {
                const auto flag = uint64_t{1} << (rng() % erhe::Item_flags::count);
                if (rng() % 2 == 0) {
                    node->enable_flag_bits(flag);
                } else {
                    node->disable_flag_bits(flag);
                }
            }

        } else if (op < 95) {
            // FILTERED CHILD COUNT
            auto node = pick_random(rng, nodes);
            if (node) {
                erhe::Item_filter filter;
                filter.require_all_bits_set = erhe::Item_flags::visible;
                static_cast<void>(node->get_child_count(filter));
            }

        } else {
            // SANITY CHECK: verify tree invariants
            auto node = pick_random(rng, roots);
            if (node) {
                node->hierarchy_sanity_check();
                result.sanity_checks++;
            }
        }
    }

    // Final sanity check on all roots
    for (const auto& root : roots) {
        root->hierarchy_sanity_check();
        result.sanity_checks++;
    }

    return result;
}

} // namespace erhe::item::test
