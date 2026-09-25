#include "rig/bone_hierarchy.hpp"
#include "rig/bone_naming.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"

#include <algorithm>
#include <functional>
#include <string>

namespace editor {

namespace {

[[nodiscard]] auto get_bone_parent(const erhe::scene::Node& bone) -> std::shared_ptr<erhe::scene::Node>
{
    std::shared_ptr<erhe::scene::Node> parent = bone.get_parent_node();
    if (parent && erhe::scene::is_bone(parent.get())) {
        return parent;
    }
    return {};
}

[[nodiscard]] auto count_bone_children(const erhe::scene::Node& node) -> std::size_t
{
    std::size_t count = 0;
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const erhe::scene::Node* const child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
        if ((child_node != nullptr) && erhe::scene::is_bone(child_node)) {
            ++count;
        }
    }
    return count;
}

void append_unique(std::vector<std::shared_ptr<erhe::scene::Node>>& out, const std::shared_ptr<erhe::scene::Node>& node)
{
    if (std::find(out.begin(), out.end(), node) == out.end()) {
        out.push_back(node);
    }
}

void collect_bone_descendants(const erhe::scene::Node& node, std::vector<std::shared_ptr<erhe::scene::Node>>& out)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node && erhe::scene::is_bone(child_node.get())) {
            append_unique(out, child_node);
            collect_bone_descendants(*child_node, out);
        }
    }
}

} // anonymous namespace

auto get_skeleton_root(const std::shared_ptr<erhe::scene::Node>& bone) -> std::shared_ptr<erhe::scene::Node>
{
    if (!bone || !erhe::scene::is_bone(bone.get())) {
        return {};
    }
    std::shared_ptr<erhe::scene::Node> root = bone;
    for (;;) {
        std::shared_ptr<erhe::scene::Node> parent = get_bone_parent(*root);
        if (!parent) {
            return root;
        }
        root = std::move(parent);
    }
}

void collect_bone_children(const erhe::scene::Node& node, std::vector<std::shared_ptr<erhe::scene::Node>>& out)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node && erhe::scene::is_bone(child_node.get())) {
            out.push_back(child_node);
        }
    }
}

void collect_bone_chain(const std::shared_ptr<erhe::scene::Node>& bone, std::vector<std::shared_ptr<erhe::scene::Node>>& out)
{
    if (!bone || !erhe::scene::is_bone(bone.get())) {
        return;
    }

    // Up: the top of the chain.
    std::shared_ptr<erhe::scene::Node> top = bone;
    for (;;) {
        std::shared_ptr<erhe::scene::Node> parent = get_bone_parent(*top);
        if (!parent || (count_bone_children(*parent) != 1)) {
            break;
        }
        top = std::move(parent);
    }

    // Down from the top, while there is exactly one bone child.
    std::shared_ptr<erhe::scene::Node> current = top;
    for (;;) {
        append_unique(out, current);
        std::shared_ptr<erhe::scene::Node> only_child{};
        std::size_t bone_child_count = 0;
        for (const std::shared_ptr<erhe::Hierarchy>& child : current->get_children()) {
            std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (child_node && erhe::scene::is_bone(child_node.get())) {
                ++bone_child_count;
                only_child = std::move(child_node);
            }
        }
        if (bone_child_count != 1) {
            break;
        }
        current = std::move(only_child);
    }
}

auto find_mirror_bone(const std::shared_ptr<erhe::scene::Node>& bone) -> std::shared_ptr<erhe::scene::Node>
{
    if (!bone || !erhe::scene::is_bone(bone.get())) {
        return {};
    }
    const std::string& name = bone->get_name();
    if (bone_side(name) == Bone_side::none) {
        return {};
    }
    return find_skeleton_bone(get_skeleton_root(bone), flip_side_name(name));
}

auto find_skeleton_bone(const std::shared_ptr<erhe::scene::Node>& root, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    if (!root || !erhe::scene::is_bone(root.get())) {
        return {};
    }
    const std::function<std::shared_ptr<erhe::scene::Node>(const std::shared_ptr<erhe::scene::Node>&)> search =
        [&](const std::shared_ptr<erhe::scene::Node>& node) -> std::shared_ptr<erhe::scene::Node>
        {
            if (node->get_name() == name) {
                return node;
            }
            for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
                const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
                if (!child_node || !erhe::scene::is_bone(child_node.get())) {
                    continue;
                }
                std::shared_ptr<erhe::scene::Node> found = search(child_node);
                if (found) {
                    return found;
                }
            }
            return {};
        };
    return search(root);
}

void collect_bone_selection(
    const std::span<const std::shared_ptr<erhe::scene::Node>> targets,
    const Bone_select_mode                                    mode,
    std::vector<std::shared_ptr<erhe::scene::Node>>&          out
)
{
    out.clear();
    std::vector<std::shared_ptr<erhe::scene::Node>> children;
    for (const std::shared_ptr<erhe::scene::Node>& target : targets) {
        if (!target || !erhe::scene::is_bone(target.get())) {
            continue;
        }
        switch (mode) {
            case Bone_select_mode::parent: {
                const std::shared_ptr<erhe::scene::Node> parent = get_bone_parent(*target);
                if (parent) {
                    append_unique(out, parent);
                }
                break;
            }
            case Bone_select_mode::children: {
                children.clear();
                collect_bone_children(*target, children);
                for (const std::shared_ptr<erhe::scene::Node>& child : children) {
                    append_unique(out, child);
                }
                break;
            }
            case Bone_select_mode::children_recursive: {
                collect_bone_descendants(*target, out);
                break;
            }
            case Bone_select_mode::chain: {
                collect_bone_chain(target, out);
                break;
            }
            case Bone_select_mode::mirror: {
                const std::shared_ptr<erhe::scene::Node> mirror = find_mirror_bone(target);
                if (mirror) {
                    append_unique(out, mirror);
                }
                break;
            }
            default: {
                break;
            }
        }
    }
}

}
