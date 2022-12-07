#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

namespace erhe::scene
{

[[nodiscard]] auto Scene_item_flags::to_string(uint64_t mask) -> std::string
{
    if (mask == none) return std::string{"no flags "};
    std::stringstream ss;
    if (mask & no_message               ) ss << "no_message ";
    if (mask & show_debug_visualizations) ss << "show_debug_visualizations ";
    if (mask & shadow_cast              ) ss << "shadow_cast ";
    if (mask & selected                 ) ss << "selected ";
    if (mask & visible                  ) ss << "visible ";
    if (mask & render_wireframe         ) ss << "render_wireframe ";
    if (mask & render_bounding_box      ) ss << "render_bounding_box ";
    if (mask & node                     ) ss << "node ";
    if (mask & attachment               ) ss << "attachment ";
    if (mask & physics                  ) ss << "physics ";
    if (mask & raytrace                 ) ss << "raytrace ";
    if (mask & frame_controller         ) ss << "frame_controller ";
    if (mask & grid                     ) ss << "grid ";
    if (mask & light                    ) ss << "light ";
    if (mask & camera                   ) ss << "camera ";
    if (mask & mesh                     ) ss << "mesh ";
    if (mask & rendertarget             ) ss << "rendertarget ";
    if (mask & controller               ) ss << "controller ";
    if (mask & content                  ) ss << "content ";
    if (mask & id                       ) ss << "id ";
    if (mask & tool                     ) ss << "tool ";
    if (mask & brush                    ) ss << "brush ";
    return ss.str();
}

uint64_t Node_transforms::s_global_update_serial = 0;

auto Node_transforms::get_current_serial() -> uint64_t
{
    return s_global_update_serial;
}

auto Node_transforms::get_next_serial() -> uint64_t
{
    return ++s_global_update_serial;
}

auto Scene_item_filter::operator()(const uint64_t visibility_mask) const -> bool
{
    if ((visibility_mask & require_all_bits_set) != require_all_bits_set)
    {
        return false;
    }
    if (require_at_least_one_bit_set != 0u)
    {
        if ((visibility_mask & require_at_least_one_bit_set) == 0u)
        {
            return false;
        }
    }
    if ((visibility_mask & require_all_bits_clear) != 0u)
    {
        return false;
    }
    if (require_at_least_one_bit_clear != 0u)
    {
        if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear)
        {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------

Scene_item::Scene_item()
{
    m_label = fmt::format("##Node{}", m_id.get_id());
}

Scene_item::Scene_item(const std::string_view name)
    : m_name{name}
{
    m_label = fmt::format("{}##Node{}", name, m_id.get_id());
}

Scene_item::~Scene_item() noexcept = default;

auto Scene_item::get_name() const -> const std::string&
{
    return m_name;
}

void Scene_item::set_name(const std::string_view name)
{
    m_name = name;
    m_label = fmt::format("{}##Node{}", name, m_id.get_id());
}

auto Scene_item::get_label() const -> const std::string&
{
    return m_label;
}

auto Scene_item::get_scene_host() const -> Scene_host*
{
    return nullptr;
}

auto Scene_item::type_name() const -> const char*
{
    return "Scene_item";
}

auto Scene_item::get_flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

void Scene_item::set_flag_bits(const uint64_t mask, const bool value)
{
    const auto old_flag_bits = m_flag_bits;
    if (value)
    {
        m_flag_bits = m_flag_bits | mask;
    }
    else
    {
        m_flag_bits = m_flag_bits & ~mask;
    }

    if (m_flag_bits != old_flag_bits)
    {
        handle_flag_bits_update(old_flag_bits, m_flag_bits);
    }
}

void Scene_item::enable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, true);
}

void Scene_item::disable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, false);
}

auto Scene_item::get_id() const -> erhe::toolkit::Unique_id<Scene_item>::id_type
{
    return m_id.get_id();
}

auto Scene_item::is_selected() const -> bool
{
    return (m_flag_bits & Scene_item_flags::selected) == Scene_item_flags::selected;
}

void Scene_item::set_selected(const bool selected)
{
    set_flag_bits(Scene_item_flags::selected, selected);
}

void Scene_item::set_visible(const bool value)
{
    set_flag_bits(Scene_item_flags::visible, value);
}

void Scene_item::show()
{
    set_flag_bits(Scene_item_flags::visible, true);
}

void Scene_item::hide()
{
    set_flag_bits(Scene_item_flags::visible, false);
}

auto Scene_item::is_visible() const -> bool
{
    return (m_flag_bits & Scene_item_flags::visible) == Scene_item_flags::visible;
}

auto Scene_item::is_shown_in_ui() const -> bool
{
    return (m_flag_bits & Scene_item_flags::show_in_ui) == Scene_item_flags::show_in_ui;
}

auto Scene_item::is_hidden() const -> bool
{
    return !is_visible();
}

auto Scene_item::describe() -> std::string
{
    return fmt::format(
        "type = {}, name = {}, id = {}, flags = {}",
        type_name(),
        get_name(),
        get_id(),
        Scene_item_flags::to_string(get_flag_bits())
    );
}

void Scene_item::set_wireframe_color(const glm::vec4& color)
{
    m_wireframe_color = color;
}

 auto Scene_item::get_wireframe_color() const -> glm::vec4
{
    return m_wireframe_color;
}

// -----------------------------------------------------------------------------

Node_attachment::Node_attachment()
{
    enable_flag_bits(Scene_item_flags::attachment);
}

Node_attachment::Node_attachment(const std::string_view name)
    : Scene_item{name}
{
    enable_flag_bits(Scene_item_flags::attachment);
}

Node_attachment::~Node_attachment() noexcept
{
    Node* const host_node = get_node();
    if (host_node != nullptr)
    {
        host_node->detach(this);
    }
}

auto Node_attachment::get_node() -> Node*
{
    return m_node;
}

auto Node_attachment::get_node() const -> const Node*
{
    return m_node;
}

auto Node_attachment::get_scene_host() const -> Scene_host*
{
    if (m_node == nullptr)
    {
        return nullptr;
    }
    return m_node->get_scene_host();
}

void Node_attachment::handle_node_update(
    Node* const old_node,
    Node* const new_node
)
{
    const uint64_t old_flag_bits = old_node ? old_node->get_flag_bits() : 0;
    const uint64_t new_flag_bits = new_node ? new_node->get_flag_bits() : 0;
    const bool     visible       = (new_flag_bits & Scene_item_flags::visible) == Scene_item_flags::visible;
    if (old_flag_bits != new_flag_bits)
    {
        handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
    set_visible(visible);
}

void Node_attachment::handle_node_flag_bits_update(
    const uint64_t old_node_flag_bits,
    const uint64_t new_node_flag_bits
)
{
    static_cast<void>(old_node_flag_bits);
    const bool visible = (new_node_flag_bits & Scene_item_flags::visible) == Scene_item_flags::visible;
    set_visible(visible);
};

void Node_attachment::set_node(Node* const node)
{
    Node* const old_node = m_node;
    Scene_host* const old_host = (m_node != nullptr) ? m_node->get_scene_host() : nullptr;
    m_node = node;
    Scene_host* const new_host = (m_node != nullptr) ? m_node->get_scene_host() : nullptr;
    if (m_node != old_node)
    {
        handle_node_update(old_node, node);
        if (new_host != old_host)
        {
            handle_node_scene_host_update(old_host, new_host);
            if (m_node != nullptr)
            {
                handle_node_transform_update();
            }
        }
    }
};

// -----------------------------------------------------------------------------

Node::Node()
{
    enable_flag_bits(Scene_item_flags::node);
}

Node::Node(const std::string_view name)
    : Scene_item{name}
{
    enable_flag_bits(Scene_item_flags::node);
}

Node::~Node() noexcept
{
    sanity_check();

    std::shared_ptr<Node> parent = node_data.parent.lock();
    set_parent({});
    for (auto& child : node_data.children)
    {
        child->set_parent(parent);
    }
    for (auto& attachment : node_data.attachments)
    {
        attachment->set_node(nullptr);
    }
    sanity_check();
}

auto Node::type_name() const -> const char*
{
    return "Node";
}

void Node::attach(const std::shared_ptr<Node_attachment>& attachment)
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(attachment);

    log->trace(
        "{} (attach({} {})",
        get_name(),
        attachment->type_name(),
        attachment->get_name()
    );

#ifndef NDEBUG
    const auto i = std::find(node_data.attachments.begin(), node_data.attachments.end(), attachment);
    if (i != node_data.attachments.end())
    {
        log->error("Attachment {} already attached to {}", attachment->type_name(), get_name());
        return;
    }
#endif

    node_data.attachments.push_back(attachment);
    attachment->set_node(this);
    sanity_check();
}

auto Node::detach(Node_attachment* attachment) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!attachment)
    {
        log->warn("empty attachment, cannot detach");
        return false;
    }

    log->trace(
        "{} (detach({} {})",
        get_name(),
        attachment->type_name(),
        attachment->get_name()
    );

    auto* node = attachment->get_node();
    if (node != this)
    {
        log->warn(
            "Attachment {} {} node {} != this {}",
            attachment->type_name(),
            attachment->get_name(),
            node
                ? node->get_name()
                : "(none)",
            get_name()
        );
        return false;
    }

    const auto i = std::remove_if(
        node_data.attachments.begin(),
        node_data.attachments.end(),
        [attachment](const std::shared_ptr<Node_attachment>& node_attachment)
        {
            return node_attachment.get() == attachment;
        }
    );
    if (i != node_data.attachments.end())
    {
        log->trace(
            "Removing {} {} attachment from node",
            attachment->type_name(),
            get_name()
        );
        node_data.attachments.erase(i, node_data.attachments.end());
        attachment->set_node(nullptr);
        sanity_check();
        return true;
    }

    log->warn(
        "Detaching {} {} from node {} failed - was not attached",
        attachment->type_name(),
        attachment->get_name(),
        get_name()
    );

    return false;
}

auto Node::child_count() const -> std::size_t
{
    return node_data.children.size();
}

auto Node::child_count(const Scene_item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& child : node_data.children)
    {
        if (filter(child->get_flag_bits()))
        {
            ++result;
        }
    }
    return result;
}

auto Node::attachment_count(const Scene_item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& attachment : node_data.attachments)
    {
        if (filter(attachment->get_flag_bits()))
        {
            ++result;
        }
    }
    return result;
}

auto Node::get_index_in_parent() const -> std::size_t
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        const auto index = current_parent->get_index_of_child(this);
        return index.has_value() ? index.value() : 0;
    }
    return 0;
}

auto Node::get_index_of_child(const Node* child) const -> std::optional<std::size_t>
{
    for (
        std::size_t i = 0, end = node_data.children.size();
        i < end;
        ++i
    )
    {
        if (node_data.children[i].get() == child)
        {
            return i;
        }
    }
    return {};
}

auto Node::is_ancestor(const Node* ancestor_candidate) const -> bool
{
    const auto& current_parent = parent().lock();
    if (!current_parent)
    {
        return false;
    }
    if (current_parent.get() == ancestor_candidate)
    {
        return true;
    }
    return current_parent->is_ancestor(ancestor_candidate);
}

auto Node::get_scene_host() const -> Scene_host*
{
    return node_data.host;
}

auto Node::get_scene() const -> Scene*
{
    return (node_data.host != nullptr)
        ? node_data.host->get_scene()
        : nullptr;
}

void Node::handle_add_child(
    const std::shared_ptr<Node>& child_node,
    const std::size_t            position
)
{
    ERHE_VERIFY(child_node);

#ifndef NDEBUG
    const auto i = std::find(node_data.children.begin(), node_data.children.end(), child_node);
    if (i != node_data.children.end())
    {
        log->error("Node {} already has child {}", get_name(), child_node->get_name());
        return;
    }
#endif

    node_data.children.insert(node_data.children.begin() + position, child_node);
}

void Node::handle_remove_child(
    Node* const child_node
)
{
    ERHE_VERIFY(child_node != nullptr);

    const auto i = std::remove_if(
        node_data.children.begin(),
        node_data.children.end(),
        [child_node](const std::shared_ptr<Node>& node)
        {
            return node.get() == child_node;
        }
    );
    if (i != node_data.children.end())
    {
        node_data.children.erase(i, node_data.children.end());
    }
    else
    {
        log->error(
            "child node {} cannot be removed from parent node {}: child not found",
            child_node->get_name(),
            get_name()
        );
    }
}

void Node::set_parent(
    const std::shared_ptr<Node>& new_parent_node,
    const std::size_t            position
)
{
    const auto& world_from_node = world_from_node_transform();

    Node* old_parent = node_data.parent.lock().get();
    node_data.parent = new_parent_node;
    Node* new_parent = node_data.parent.lock().get();

    if (old_parent == new_parent)
    {
        return;
    }

    if (old_parent)
    {
        old_parent->handle_remove_child(this);
    }

    if (new_parent)
    {
        new_parent->handle_add_child(
            std::static_pointer_cast<Node>(shared_from_this()),
            position
        );
    }

    set_world_from_node(world_from_node);
    set_depth_recursive(
        new_parent
            ? new_parent->get_depth() + 1
            : 0
    );
    handle_parent_update(old_parent, new_parent);
    sanity_check();
}

void Node::set_parent(
    Node* const       new_parent_node,
    const std::size_t position
)
{
    if (new_parent_node != nullptr)
    {
        set_parent(
            std::static_pointer_cast<Node>(
                new_parent_node->shared_from_this()
            ),
            position
        );
    }
    else
    {
        set_parent(std::shared_ptr<Node>{}, position);
    }
}

void Node::set_depth_recursive(const std::size_t depth)
{
    ERHE_PROFILE_FUNCTION

    if (node_data.depth == depth)
    {
        return;
    }
    node_data.depth = depth;
    for (const auto& child : node_data.children)
    {
        child->set_depth_recursive(depth + 1);
    }
}

void Node::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
}

void Node::handle_parent_update(
    Node* const old_parent,
    Node* const new_parent)
{
    ERHE_VERIFY(old_parent != new_parent);
    Scene_host* old_scene_host = old_parent != nullptr ? old_parent->get_scene_host() : nullptr;
    Scene_host* new_scene_host = new_parent != nullptr ? new_parent->get_scene_host() : nullptr;
    if (old_scene_host != new_scene_host)
    {
        handle_scene_host_update(old_scene_host, new_scene_host);
    }
    sanity_check();
}

void Node::handle_scene_host_update(
    Scene_host* const old_scene_host,
    Scene_host* const new_scene_host
)
{
    ERHE_VERIFY(old_scene_host != new_scene_host);

    if (old_scene_host != nullptr)
    {
        Scene* old_scene = old_scene_host->get_scene();
        if (old_scene != nullptr)
        {
            old_scene->unregister_node(
                std::static_pointer_cast<Node>(
                    shared_from_this()
                )
            );
        }
    }
    if (new_scene_host != nullptr)
    {
        Scene* new_scene = new_scene_host->get_scene();
        if (new_scene != nullptr)
        {
            new_scene->register_node(
                std::static_pointer_cast<Node>(
                    shared_from_this()
                )
            );
        }
    }

    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_scene_host_update(old_scene_host, new_scene_host);
    }
}

void Node::handle_transform_update(const uint64_t serial) const
{
    ERHE_PROFILE_FUNCTION

    const uint64_t effective_serial = (serial > 0)
        ? serial
        : Node_transforms::get_next_serial();

    node_data.transforms.update_serial = effective_serial;
    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_transform_update();
    }
}

auto Node::root() -> std::weak_ptr<Node>
{
    const auto& current_parent = parent().lock();
    if (!current_parent)
    {
        return std::static_pointer_cast<Node>(
            shared_from_this()
        );
    }
    return current_parent->root();
}

void Node::update_transform(uint64_t serial) const
{
    ERHE_PROFILE_FUNCTION

    const auto& current_parent = parent().lock();
    if (!current_parent) {
        return;
    }

    serial = std::max(serial, current_parent->node_data.transforms.update_serial);

    if (node_data.transforms.update_serial >= serial) {
        return;
    }

    node_data.transforms.world_from_node.set(
        current_parent->world_from_node() * parent_from_node(),
        node_from_parent() * current_parent->node_from_world()
    );
    handle_transform_update(serial);
}

void Node::update_world_from_node()
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        node_data.transforms.world_from_node.set(
            current_parent->world_from_node() * parent_from_node(),
            node_from_parent() * current_parent->node_from_world()
        );
    }
    else
    {
        node_data.transforms.world_from_node.set(
            parent_from_node(),
            node_from_parent()
        );
    }
}

void Node::sanity_check() const
{
#if 1
    sanity_check_root_path(this);

    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        bool child_found_in_parent = false;
        for (const auto& child : current_parent->children())
        {
            if (child.get() == this)
            {
                child_found_in_parent = true;
                break;
            }
        }
        if (!child_found_in_parent)
        {
            log->error(
                "Node {} parent {} does not have node as child",
                get_name(),
                current_parent->get_name()
            );
        }
    }

    for (const auto& child : node_data.children)
    {
        if (child->parent().lock().get() != this)
        {
            log->error(
                "Node {} child {} parent == {}",
                get_name(),
                child->get_name(),
                (child->parent().lock())
                    ? child->parent().lock()->get_name()
                    : "(none)"
            );
        }
        if (child->get_depth() != get_depth() + 1)
        {
            log->error(
                "Node {} depth = {}, child {} depth = {}",
                get_name(),
                get_depth(),
                child->get_name(),
                child->get_depth()
            );
        }
        if (child->get_scene_host() != get_scene_host())
        {
            log->error(
                "Scene host mismatch: parent node = {}, child node = {}",
                get_name(),
                child->get_name()
            );
        }
        child->sanity_check();
    }

    for (const auto& attachment : node_data.attachments)
    {
        auto* node = attachment->get_node();
        if (node != this)
        {
            log->error(
                "Node {} attachment {} {} node == {}",
                get_name(),
                attachment->type_name(),
                attachment->get_name(),
                (node != nullptr)
                    ? node->get_name()
                    : "(none)"
            );
        }
    }
#endif
}

void Node::sanity_check_root_path(const Node* node) const
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        if (current_parent.get() == node)
        {
            log->error(
                "Node {} has itself as an ancestor",
                node->get_name()
            );
        }
        current_parent->sanity_check_root_path(node);
    }
}

auto Node::parent() const -> std::weak_ptr<Node>
{
    return node_data.parent;
}

auto Node::get_depth() const -> std::size_t
{
    return node_data.depth;
}

auto Node::children() const -> const std::vector<std::shared_ptr<Node>>&
{
    return node_data.children;
}

auto Node::mutable_children() -> std::vector<std::shared_ptr<Node>>&
{
    return node_data.children;
}

auto Node::attachments() const -> const std::vector<std::shared_ptr<Node_attachment>>&
{
    return node_data.attachments;
}

auto Node::parent_from_node_transform() const -> const Transform&
{
    return node_data.transforms.parent_from_node;
}

auto Node::node_from_parent_transform() const -> const Transform
{
    return Transform::inverse(node_data.transforms.parent_from_node);
}

auto Node::parent_from_node() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.matrix();
}

auto Node::world_from_node_transform() const -> const Transform&
{
    return node_data.transforms.world_from_node;
}

auto Node::node_from_world_transform() const -> const Transform
{
    return Transform::inverse(node_data.transforms.world_from_node);
}

auto Node::world_from_node() const -> glm::mat4
{
    return node_data.transforms.world_from_node.matrix();
}

auto Node::node_from_parent() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.inverse_matrix();
}

auto Node::node_from_world() const -> glm::mat4
{
    return node_data.transforms.world_from_node.inverse_matrix();
}

auto Node::world_from_parent() const -> glm::mat4
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        return current_parent->world_from_node();
    }
    return glm::mat4{1};
}

auto Node::position_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
}

auto Node::direction_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
}

[[nodiscard]] auto Node::look_at(const Node& target) const -> glm::mat4
{
    const glm::vec3 eye_position    = position_in_world();
    const glm::vec3 target_position = target.position_in_world();
    const glm::vec3 up_direction    = world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    return erhe::toolkit::create_look_at(eye_position, target_position, up_direction);
}

auto Node::transform_point_from_world_to_local(const glm::vec3 p) const -> glm::vec3
{
    // Does not homogenize
    return glm::vec3{node_from_world() * glm::vec4{p, 1.0f}};
}

auto Node::transform_direction_from_world_to_local(const glm::vec3 direction) const -> glm::vec3
{
    return glm::vec3{node_from_world() * glm::vec4{direction, 0.0f}};
}

void Node::set_parent_from_node(const glm::mat4 m)
{
    node_data.transforms.parent_from_node.set(m);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_parent_from_node(const Transform& transform)
{
    ERHE_PROFILE_FUNCTION

    node_data.transforms.parent_from_node = transform;
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const glm::mat4 matrix)
{
    node_data.transforms.parent_from_node.set(glm::inverse(matrix), matrix);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const Transform& transform)
{
    node_data.transforms.parent_from_node = Transform::inverse(transform);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_world_from_node(const glm::mat4 matrix)
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        set_parent_from_node(current_parent->node_from_world() * matrix);
    }
    else
    {
        set_parent_from_node(matrix);
    }
}

void Node::set_world_from_node(const Transform& transform)
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        set_parent_from_node(current_parent->node_from_world_transform() * transform);
    }
    else
    {
        set_parent_from_node(transform);
    }
}

auto Node_data::diff_mask(
    const Node_data& lhs,
    const Node_data& rhs
) -> unsigned int
{
    unsigned int mask{0};

    if (lhs.transforms.parent_from_node != rhs.transforms.parent_from_node) mask |= Node_data::bit_transform;
    if (lhs.transforms.world_from_node  != rhs.transforms.world_from_node ) mask |= Node_data::bit_transform;
    if (lhs.host                        != rhs.host                       ) mask |= Node_data::bit_host;
    if (lhs.depth                       != rhs.depth                      ) mask |= Node_data::bit_depth;
    return mask;
}

auto is_node(const Scene_item* const scene_item) -> bool
{
    if (scene_item == nullptr)
    {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(
        scene_item->get_flag_bits(),
        Scene_item_flags::node
    );
}

auto is_node(
    const std::shared_ptr<Scene_item>& scene_item
) -> bool
{
    return is_node(scene_item.get());
}

auto as_node(Scene_item* const scene_item) -> Node*
{
    if (scene_item == nullptr)
    {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_flag_bits(),
            Scene_item_flags::mesh
        )
    )
    {
        return nullptr;
    }
    return reinterpret_cast<Node*>(scene_item);
}

auto as_node(
    const std::shared_ptr<Scene_item>& scene_item
) -> std::shared_ptr<Node>
{
    if (!scene_item)
    {
        return {};
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_flag_bits(),
            Scene_item_flags::node
        )
    )
    {
        return {};
    }
    return std::static_pointer_cast<Node>(scene_item);
}

auto as_node_attachment(
    const std::shared_ptr<Scene_item>& scene_item
) -> std::shared_ptr<Node_attachment>
{
    if (!scene_item)
    {
        return {};
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_flag_bits(),
            Scene_item_flags::attachment
        )
    )
    {
        return {};
    }
    return std::static_pointer_cast<Node_attachment>(scene_item);
}


} // namespace erhe::scene
