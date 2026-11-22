#include "level/foliage_system.hpp"
#include "level/kit_piece.hpp"
#include "editor_item_bits.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using editor::Foliage_index;
using editor::Foliage_system;
using editor::Kit_piece;
using editor::Editor_item_flags;

enum class Forward_flags : unsigned int { no = 0, yes };

// Forwards the foliage flag as Scene_root::on_mesh_flags_changed() does.
class Foliage_test_host : public erhe::scene::Scene_host
{
public:
    Foliage_test_host()
        : scene {"foliage test scene", this}
        , system{std::make_unique<Foliage_system>()}
    {
        scene.add_node_system(*system);
    }
    ~Foliage_test_host() noexcept override
    {
        destroy_system();
    }

    // As Scene_root's destructor: the system leaves the scene's list and is destroyed.
    void destroy_system()
    {
        if (system) {
            scene.remove_node_system(*system);
            system.reset();
        }
    }

    auto get_host_name   () const -> const char*         override { return "Foliage_test_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)         override {}

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>& mesh, const uint64_t old_flag_bits, const uint64_t new_flag_bits) override
    {
        if (mesh && system && (forward_flags == Forward_flags::yes) && (((old_flag_bits ^ new_flag_bits) & Editor_item_flags::foliage) != 0)) {
            system->on_flags_changed(*mesh);
        }
    }
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_lod_changed           (const std::shared_ptr<erhe::scene::Mesh>&, const erhe::scene::Lod_data&) override {}
    void on_mesh_bounds_changed        (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene              scene;
    std::unique_ptr<Foliage_system> system;
    Forward_flags                   forward_flags{Forward_flags::yes}; // no: a flag write the system does not hear
};

[[nodiscard]] auto make_foliage(const std::string_view name, const glm::vec3 position, const float radius) -> std::shared_ptr<erhe::scene::Mesh>
{
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, position));
    mesh->enable_flag_bits(Editor_item_flags::foliage);
    mesh->set_value(Kit_piece::foliage_radius_property, radius);
    return mesh;
}

// The index's entry of the node, applying the queue first; none when the index does not hold it.
[[nodiscard]] auto find_entry(Foliage_test_host& host, const erhe::scene::Node& node) -> std::optional<editor::Foliage_index_entry>
{
    host.scene.update_node_transforms();
    const Foliage_index& index = host.system->get_index();
    const std::optional<Foliage_index::Handle> handle = host.system->find_handle(node);
    if (!handle.has_value()) {
        return {};
    }
    return index.get_entry(handle.value());
}

void expect_entry(Foliage_test_host& host, const erhe::scene::Node& node, const glm::vec3 position, const float radius)
{
    const std::optional<editor::Foliage_index_entry> entry = find_entry(host, node);
    ASSERT_TRUE(entry.has_value()) << node.get_name();
    EXPECT_EQ(entry->position, position) << node.get_name();
    EXPECT_EQ(entry->radius, radius) << node.get_name();
    EXPECT_EQ(entry->key, node.get_id());
}

} // anonymous namespace

TEST(Foliage_system, a_node_flagged_before_or_after_it_enters_is_indexed)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> before = make_foliage("before", glm::vec3{1.0f, 0.0f, 2.0f}, 0.5f);
    before->set_parent(host.scene.get_root_node());
    expect_entry(host, *before, glm::vec3{1.0f, 0.0f, 2.0f}, 0.5f);

    const std::shared_ptr<erhe::scene::Mesh> after = std::make_shared<erhe::scene::Mesh>("after");
    after->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{-3.0f, 0.0f, 0.0f}));
    after->set_parent(host.scene.get_root_node());
    EXPECT_FALSE(find_entry(host, *after).has_value());
    after->enable_flag_bits(Editor_item_flags::foliage);
    expect_entry(host, *after, glm::vec3{-3.0f, 0.0f, 0.0f}, 0.0f);
    after->set_value(Kit_piece::foliage_radius_property, 1.25f);
    expect_entry(host, *after, glm::vec3{-3.0f, 0.0f, 0.0f}, 1.25f);
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{2});
    EXPECT_EQ(host.system->get_index().get_max_radius(), 1.25f);
}

TEST(Foliage_system, a_plain_node_and_an_unflagged_mesh_are_not_indexed)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("node");
    node->enable_flag_bits(Editor_item_flags::foliage); // a node that is no mesh
    node->set_parent(host.scene.get_root_node());
    const std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->set_value(Kit_piece::foliage_radius_property, 2.0f);
    mesh->set_parent(host.scene.get_root_node());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
}

TEST(Foliage_system, the_flag_cleared_and_set_again)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{0.0f, 1.0f, 0.0f}, 0.75f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{0.0f, 1.0f, 0.0f}, 0.75f);

    mesh->disable_flag_bits(Editor_item_flags::foliage);
    EXPECT_FALSE(find_entry(host, *mesh).has_value());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});

    // Cleared and set again before the next query: one entry.
    mesh->enable_flag_bits(Editor_item_flags::foliage);
    mesh->disable_flag_bits(Editor_item_flags::foliage);
    mesh->enable_flag_bits(Editor_item_flags::foliage);
    expect_entry(host, *mesh, glm::vec3{0.0f, 1.0f, 0.0f}, 0.75f);
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{1});
}

TEST(Foliage_system, moves_of_the_item_and_of_its_parent_reach_the_index)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Xform> parent = std::make_shared<erhe::scene::Xform>("parent");
    parent->set_parent(host.scene.get_root_node());
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(parent);
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);

    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    expect_entry(host, *mesh, glm::vec3{2.0f, 0.0f, 0.0f}, 0.5f);

    parent->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 10.0f}));
    expect_entry(host, *mesh, glm::vec3{2.0f, 0.0f, 10.0f}, 0.5f);

    // Two moves before a query: the last position.
    parent->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 20.0f}));
    host.scene.update_node_transforms();
    parent->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 4.0f, 0.0f}));
    expect_entry(host, *mesh, glm::vec3{2.0f, 4.0f, 0.0f}, 0.5f);

    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    host.system->find_nodes_within(glm::vec3{2.0f, 4.0f, 0.0f}, 0.1f, nodes);
    ASSERT_EQ(nodes.size(), std::size_t{1});
    EXPECT_EQ(nodes.front().get(), mesh.get());
    host.system->find_nodes_within(glm::vec3{2.0f, 0.0f, 10.0f}, 1.0f, nodes);
    EXPECT_TRUE(nodes.empty());
}

TEST(Foliage_system, removal_and_reinsert_and_a_node_gone_while_queued)
{
    Foliage_test_host host;
    std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{5.0f, 0.0f, 5.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{5.0f, 0.0f, 5.0f}, 0.5f);

    // Out and back in, as an undo and a redo of a delete do.
    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
    EXPECT_FALSE(find_entry(host, *mesh).has_value());
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{5.0f, 0.0f, 5.0f}, 0.5f);

    // Moved, taken out and destroyed while the move and the removal are queued.
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{6.0f, 0.0f, 5.0f}));
    host.scene.update_node_transforms();
    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
    mesh.reset();
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});

    // Added and destroyed before any query: nothing is indexed.
    std::shared_ptr<erhe::scene::Mesh> brief = make_foliage("brief", glm::vec3{0.0f}, 0.5f);
    brief->set_parent(host.scene.get_root_node());
    brief->set_parent(std::shared_ptr<erhe::scene::Node>{});
    brief.reset();
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    host.system->get_nodes(nodes);
    EXPECT_TRUE(nodes.empty());
}

TEST(Foliage_system, nodes_registered_on_another_thread_are_indexed_at_the_next_query)
{
    Foliage_test_host host;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    for (int i = 0; i < 16; ++i) {
        meshes.push_back(make_foliage("loaded_" + std::to_string(i), glm::vec3{static_cast<float>(i), 0.0f, 0.0f}, 0.25f));
    }
    std::thread worker{
        [&host, &meshes]() {
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
                mesh->set_parent(host.scene.get_root_node());
            }
        }
    };
    worker.join();
    EXPECT_EQ(host.system->get_index().get_count(), meshes.size());
    for (int i = 0; i < 16; ++i) {
        expect_entry(host, *meshes[static_cast<std::size_t>(i)], glm::vec3{static_cast<float>(i), 0.0f, 0.0f}, 0.25f);
    }
}

TEST(Foliage_system, a_radius_outside_its_range_is_refused)
{
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    mesh->set_value(Kit_piece::foliage_radius_property, 2.0f);
    mesh->set_value(Kit_piece::foliage_radius_property, -1.0f);
    mesh->set_value(Kit_piece::foliage_radius_property, 100.5f);
    EXPECT_EQ(mesh->get_value(Kit_piece::foliage_radius_property), 2.0f);
}

TEST(Foliage_system, a_position_that_is_not_finite_leaves_the_index_until_it_is_finite_again)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);

    const float infinity = std::numeric_limits<float>::infinity();
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{infinity, 0.0f, 0.0f}));
    EXPECT_FALSE(find_entry(host, *mesh).has_value());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f}));
    expect_entry(host, *mesh, glm::vec3{3.0f, 0.0f, 0.0f}, 0.5f);

    // A node that enters with a position that is not finite is indexed at its first finite one.
    const std::shared_ptr<erhe::scene::Mesh> late = make_foliage("late", glm::vec3{std::nanf(""), 0.0f, 0.0f}, 0.25f);
    late->set_parent(host.scene.get_root_node());
    EXPECT_FALSE(find_entry(host, *late).has_value());
    late->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 4.0f}));
    expect_entry(host, *late, glm::vec3{0.0f, 0.0f, 4.0f}, 0.25f);
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{2});
}

TEST(Foliage_system, the_queue_holds_one_entry_per_node_and_keeps_its_capacity)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> other = std::make_shared<erhe::scene::Mesh>("other");
    other->set_parent(host.scene.get_root_node());
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    host.scene.update_node_transforms();
    static_cast<void>(host.system->get_index());
    ASSERT_EQ(host.system->get_queue_state().pending_count, std::size_t{0});

    // A node that is no foliage queues nothing: its move and a radius written on it.
    other->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    other->set_value(Kit_piece::foliage_radius_property, 3.0f);
    EXPECT_EQ(host.system->get_queue_state().pending_count, std::size_t{0});

    // Two moves and two radius writes queue one entry for the record.
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    EXPECT_EQ(host.system->get_queue_state().pending_count, std::size_t{1});
    mesh->set_value(Kit_piece::foliage_radius_property, 0.75f);
    mesh->set_value(Kit_piece::foliage_radius_property, 1.0f);
    EXPECT_EQ(host.system->get_queue_state().pending_count, std::size_t{1});
    // The flag cleared erases the record (one removal of its index entry), set again makes a new one (one entry).
    mesh->disable_flag_bits(Editor_item_flags::foliage);
    mesh->enable_flag_bits(Editor_item_flags::foliage);
    const editor::Foliage_queue_state queued = host.system->get_queue_state();
    EXPECT_EQ(queued.pending_count, std::size_t{3});

    expect_entry(host, *mesh, glm::vec3{3.0f, 0.0f, 0.0f}, 1.0f);
    const editor::Foliage_queue_state applied = host.system->get_queue_state();
    EXPECT_EQ(applied.pending_count, std::size_t{0});
    EXPECT_EQ(applied.processing_capacity, queued.pending_capacity); // the applied list is cleared, not released
    EXPECT_GE(queued.pending_capacity, std::size_t{2});

    // The next round fills the list the previous one emptied: the capacities stay.
    mesh->set_value(Kit_piece::foliage_radius_property, 0.5f);
    const editor::Foliage_queue_state again = host.system->get_queue_state();
    EXPECT_EQ(again.pending_count, std::size_t{1});
    EXPECT_EQ(again.processing_capacity + again.pending_capacity, applied.processing_capacity + applied.pending_capacity);

    // Taken out: the record goes at once, one entry removes its index entry at the next query.
    static_cast<void>(host.system->get_index());
    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
    EXPECT_EQ(host.system->get_queue_state().pending_count, std::size_t{1});
    EXPECT_FALSE(host.system->find_handle(*mesh).has_value());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
}

TEST(Foliage_system, hooks_and_moves_from_another_thread_while_the_main_thread_queries)
{
    Foliage_test_host host;
    constexpr int count = 48;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    for (int i = 0; i < count; ++i) {
        meshes.push_back(std::make_shared<erhe::scene::Mesh>("concurrent_" + std::to_string(i)));
    }
    const auto flagged     = [](const int i) -> bool      { return (i % 4) != 3; };
    const auto taken_out   = [](const int i) -> bool      { return (i % 7) == 0; };
    const auto radius_of   = [](const int i) -> float     { return 0.125f * static_cast<float>(i % 5); };
    const auto position_of = [](const int i) -> glm::vec3 { return glm::vec3{static_cast<float>(i), static_cast<float>(i % 3), -static_cast<float>(i)}; };

    std::mutex hand_off_mutex;
    bool       done{false};
    std::thread worker{
        [&]() {
            using Host_lock = std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)>;
            for (int i = 0; i < count; ++i) {
                const std::shared_ptr<erhe::scene::Mesh>& mesh = meshes[static_cast<std::size_t>(i)];
                {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->set_parent(host.scene.get_root_node());
                }
                if (flagged(i)) {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->enable_flag_bits(Editor_item_flags::foliage);
                }
                {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->set_value(Kit_piece::foliage_radius_property, radius_of(i));
                }
                {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, position_of(i) * 0.5f));
                }
                host.scene.update_node_transforms();
                {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, position_of(i)));
                }
                host.scene.update_node_transforms();
                if (taken_out(i)) {
                    const Host_lock lock{host.item_host_mutex};
                    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
                }
            }
            const std::lock_guard<std::mutex> lock{hand_off_mutex};
            done = true;
        }
    };
    std::size_t queries = 0;
    for (;;) {
        {
            const std::lock_guard<std::mutex> lock{hand_off_mutex};
            if (done) {
                break;
            }
        }
        static_cast<void>(host.system->get_index());
        ++queries;
    }
    worker.join();
    EXPECT_GT(queries, std::size_t{0});

    std::size_t expected_count = 0;
    for (int i = 0; i < count; ++i) {
        const std::shared_ptr<erhe::scene::Mesh>& mesh = meshes[static_cast<std::size_t>(i)];
        if (flagged(i) && !taken_out(i)) {
            ++expected_count;
            expect_entry(host, *mesh, position_of(i), radius_of(i));
        } else {
            EXPECT_FALSE(find_entry(host, *mesh).has_value()) << mesh->get_name();
        }
    }
    EXPECT_EQ(host.system->get_index().get_count(), expected_count);
}

TEST(Foliage_system, a_new_node_at_a_reused_address_takes_none_of_the_old_records_entries)
{
    Foliage_test_host host;
    // Separate allocations, so the object's storage is free once the node dies.
    std::shared_ptr<erhe::scene::Mesh> old_mesh{new erhe::scene::Mesh{"old"}};
    old_mesh->enable_flag_bits(Editor_item_flags::foliage);
    old_mesh->set_value(Kit_piece::foliage_radius_property, 0.5f);
    old_mesh->set_parent(host.scene.get_root_node());
    static_cast<void>(host.system->get_index());
    const void* const address = old_mesh.get();

    // A move and a radius write queued, then the node leaves and dies.
    old_mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    old_mesh->set_value(Kit_piece::foliage_radius_property, 0.7f);
    old_mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
    old_mesh.reset();

    std::vector<std::shared_ptr<erhe::scene::Mesh>> others;
    std::shared_ptr<erhe::scene::Mesh> reused;
    for (int i = 0; (i < 256) && !reused; ++i) {
        std::shared_ptr<erhe::scene::Mesh> candidate{new erhe::scene::Mesh{"candidate"}};
        if (candidate.get() == address) {
            reused = candidate;
        } else {
            others.push_back(candidate);
        }
    }
    if (!reused) {
        GTEST_SKIP() << "the allocator gave no node the old node's address";
    }
    reused->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}));
    reused->enable_flag_bits(Editor_item_flags::foliage);
    reused->set_value(Kit_piece::foliage_radius_property, 0.25f);
    reused->set_parent(host.scene.get_root_node());
    expect_entry(host, *reused, glm::vec3{5.0f, 0.0f, 0.0f}, 0.25f);
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{1});
    EXPECT_EQ(host.system->get_index().get_max_radius(), 0.25f);
}

TEST(Foliage_system, nodes_outlive_the_system_and_move_after_it)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    // Destroyed with a move queued and the node still observed.
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    host.destroy_system();
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    mesh->set_value(Kit_piece::foliage_radius_property, 0.75f);
    mesh->disable_flag_bits(Editor_item_flags::foliage);
    EXPECT_EQ(glm::vec3{mesh->position_in_world()}, (glm::vec3{3.0f, 0.0f, 0.0f}));
}

TEST(Foliage_system, a_node_taken_out_and_put_back_with_entries_pending_ends_at_its_latest_values)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);

    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f}));
    host.scene.update_node_transforms();
    mesh->set_value(Kit_piece::foliage_radius_property, 0.7f);
    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{}); // the old record's entry stays queued, with its removal
    mesh->set_value(Kit_piece::foliage_radius_property, 0.9f);
    mesh->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 0.0f, 0.0f}));
    mesh->set_parent(host.scene.get_root_node());
    EXPECT_EQ(host.system->get_queue_state().pending_count, std::size_t{3});
    expect_entry(host, *mesh, glm::vec3{4.0f, 0.0f, 0.0f}, 0.9f);
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{1});
    EXPECT_EQ(host.system->get_index().get_max_radius(), 0.9f);
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    host.system->get_nodes(nodes);
    ASSERT_EQ(nodes.size(), std::size_t{1});
    EXPECT_EQ(nodes.front().get(), mesh.get());
}

TEST(Foliage_system, a_radius_expression_resolved_by_a_hook_read_reenters_the_hook)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    ASSERT_TRUE(mesh->set_expression(Kit_piece::foliage_radius_property, "{Later/translation.x}"));
    EXPECT_FALSE(mesh->get_expression_error(Kit_piece::foliage_radius_property).empty());
    static_cast<void>(host.system->get_index());

    // The source appears; the expression stays unresolved until a read retries it.
    const std::shared_ptr<erhe::scene::Xform> later = std::make_shared<erhe::scene::Xform>("Later");
    later->set_value(erhe::scene::Node::translation_property, glm::vec3{0.75f, 0.0f, 0.0f});
    later->set_parent(host.scene.get_root_node());
    // The flag hook reads the radius: the read evaluates the expression, whose notification calls on_values_changed
    // inside the hook's read; a query applies plain values and reads no node.
    mesh->disable_flag_bits(Editor_item_flags::foliage);
    mesh->enable_flag_bits(Editor_item_flags::foliage);
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.75f);
    EXPECT_TRUE(mesh->get_expression_error(Kit_piece::foliage_radius_property).empty());

    // A change of the source reaches the index through the expression's notification.
    later->set_value(erhe::scene::Node::translation_property, glm::vec3{1.25f, 0.0f, 0.0f});
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 1.25f);
}

TEST(Foliage_system, queries_work_while_the_caller_holds_the_scene_lock)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{host.item_host_mutex};
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{1});
    EXPECT_EQ(host.system->get_index().get_max_radius(), 0.5f);
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    host.system->find_nodes_within(glm::vec3{1.0f, 0.0f, 0.0f}, 0.1f, nodes);
    ASSERT_EQ(nodes.size(), std::size_t{1});
    EXPECT_EQ(nodes.front().get(), mesh.get());
    mesh->set_value(Kit_piece::foliage_radius_property, 0.25f);
    EXPECT_EQ(host.system->get_index().get_max_radius(), 0.25f);
}

TEST(Foliage_system, an_unregistered_node_has_no_record_even_when_its_flag_was_cleared_unheard)
{
    Foliage_test_host host;
    const std::shared_ptr<erhe::scene::Mesh> mesh = make_foliage("mesh", glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);
    mesh->set_parent(host.scene.get_root_node());
    expect_entry(host, *mesh, glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f);

    host.forward_flags = Forward_flags::no;
    mesh->disable_flag_bits(Editor_item_flags::foliage);
    host.forward_flags = Forward_flags::yes;
    EXPECT_TRUE(find_entry(host, *mesh).has_value()); // the system did not hear the write

    mesh->set_parent(std::shared_ptr<erhe::scene::Node>{});
    EXPECT_FALSE(host.system->find_handle(*mesh).has_value());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
    mesh->set_parent(host.scene.get_root_node()); // back without the flag: no record
    EXPECT_FALSE(find_entry(host, *mesh).has_value());
    EXPECT_EQ(host.system->get_index().get_count(), std::size_t{0});
}
