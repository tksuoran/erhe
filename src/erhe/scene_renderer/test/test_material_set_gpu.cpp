// Material_set's GPU half (doc/draw_list_material_set_plan.md V2): the
// records it writes, the slots they land at, and the persistence rule that
// decides when a new copy is written at all.
//
// The assertions read the records back through a compute shader that binds the
// set exactly as a pass does and copies base_color out of the slot under test.
// That is deliberate: it exercises the same bind path a frame takes, so a
// record that is written correctly but bound at the wrong offset still fails.
//
// Deferred to phase 4, when the record writers actually consult a set:
// texture_handles_resolve_within_own_heap, heap_survives_clean_frames and the
// consecutive-updates-in-flight case (V2.6, 11, 12), which need a raster pass
// sampling through the heap; and the assignment group (V2.14-16), which needs
// Draw_list_scene to reference its objects' materials into its set.

#include "gpu_test_fixture.hpp"

#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/material_set.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_primitive/material.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace erhe::scene_renderer::test {

using erhe::graphics::test::Gpu_test;
using erhe::primitive::Material;
using erhe::primitive::Material_create_info;

namespace {

// Copies one slot's base_color out of the bound material buffer. slot comes
// through the readback block's own first element, written by the host before
// the dispatch, so one pipeline serves every slot.
constexpr const char* c_compute_source = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    uint slot = floatBitsToUint(Output.data[0]);
    vec4 base_color = material.materials[slot].base_color;
    Output.data[0] = base_color.x;
    Output.data[1] = base_color.y;
    Output.data[2] = base_color.z;
    Output.data[3] = float(slot);
}
)glsl";

[[nodiscard]] auto make_material(const char* name, const glm::vec3& base_color) -> std::shared_ptr<Material>
{
    Material_create_info create_info{};
    create_info.name            = name;
    create_info.values.base_color = base_color;
    return std::make_shared<Material>(create_info);
}

using Material_list = std::vector<std::shared_ptr<Material>>;

} // anonymous namespace

class Material_set_gpu_test : public Gpu_test
{
protected:
    void SetUp() override
    {
        Gpu_test::SetUp();
        build_environment();
    }

    void build_environment()
    {
        m_material_interface = std::make_unique<Material_interface>(device(), 256);

        m_readback_block = std::make_unique<erhe::graphics::Shader_resource>(
            device(),
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Output",
                .binding_point = 1,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
            }
        );
        m_readback_block->add_float("data", erhe::graphics::Shader_resource::unsized_array);

        // uses_texture_heap: Material_set::bind() binds its heap along with
        // the buffer, exactly as a pass does, so the layout has to have the
        // heap set the heap binds into.
        m_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    },
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 1u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"material set test layout"},
                .uses_texture_heap = true
            }
        );

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "material_set_readback",
            .struct_types     = { &m_material_interface->material_struct },
            .interface_blocks = { &m_material_interface->material_block, m_readback_block.get() },
            .shaders          = { { erhe::graphics::Shader_type::compute_shader, std::string_view{c_compute_source} } },
            .bind_group_layout = m_layout.get()
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "material set readback compute shader failed to compile/link";
        m_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(device(), std::move(prototype));

        m_pipeline = std::make_unique<erhe::graphics::Compute_pipeline>(
            device(),
            erhe::graphics::Compute_pipeline_data{
                .name              = "material_set_readback",
                .shader_stages     = m_shader_stages.get(),
                .bind_group_layout = m_layout.get()
            }
        );
        ASSERT_TRUE(m_pipeline->is_valid());

        m_readback = make_host_buffer(4 * sizeof(float), erhe::graphics::Buffer_usage::storage, "material set readback");

        // create_dummy_texture records its pixel upload into the supplied
        // command buffer and leaves ending + submitting to the caller, so it
        // has to run inside a recording frame (submit_and_wait), never on a
        // bare get_command_buffer() handle.
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                m_fallback_texture = device().create_dummy_texture(
                    command_buffer,
                    erhe::dataformat::Format::format_8_vec4_srgb
                );
            }
        );
        m_fallback_sampler = std::make_unique<erhe::graphics::Sampler>(
            device(),
            erhe::graphics::Sampler_create_info{.debug_label = "material set test fallback sampler"}
        );
    }

    [[nodiscard]] auto make_create_info(const char* debug_label) -> Material_set_create_info
    {
        return Material_set_create_info{
            .graphics_device        = &device(),
            .material_interface     = m_material_interface.get(),
            .bind_group_layout      = m_layout.get(),
            .fallback_texture       = m_fallback_texture.get(),
            .fallback_sampler       = m_fallback_sampler.get(),
            .max_textures           = 16,
            .initial_material_count = 4,
            .debug_label            = erhe::utility::Debug_label{debug_label}
        };
    }

    void update(Material_set& material_set)
    {
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                material_set.update(command_buffer);
            }
        );
    }

    // Binds the set and reads back the base colour the record at `slot`
    // carries. Returns {r, g, b}.
    [[nodiscard]] auto read_base_color(Material_set& material_set, const uint32_t slot) -> glm::vec3
    {
        {
            // The slot index travels in through the readback block itself.
            // begin_write/end_write rather than get_map: read_buffer() unmaps
            // after every readback, so no standing mapping survives between
            // calls.
            const std::span<std::byte> map = m_readback->begin_write(0, sizeof(float));
            const float                slot_as_float = std::bit_cast<float>(slot);
            std::memcpy(map.data(), &slot_as_float, sizeof(float));
            m_readback->end_write(0, sizeof(float));
        }
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_layout.get());
                encoder.set_compute_pipeline(*m_pipeline);
                const bool bound = material_set.bind(encoder);
                EXPECT_TRUE(bound);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, m_readback.get(), 0, 4 * sizeof(float), 1);
                encoder.dispatch_compute(1, 1, 1);
                material_set.unbind(command_buffer);
            }
        );
        const std::vector<std::byte> raw = read_buffer(*m_readback, 4 * sizeof(float));
        float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(values, raw.data(), sizeof(values));
        return glm::vec3{values[0], values[1], values[2]};
    }

    std::unique_ptr<Material_interface>                m_material_interface;
    std::unique_ptr<erhe::graphics::Shader_resource>   m_readback_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout> m_layout;
    std::unique_ptr<erhe::graphics::Shader_stages>     m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>  m_pipeline;
    std::shared_ptr<erhe::graphics::Buffer>            m_readback;
    std::shared_ptr<erhe::graphics::Texture>           m_fallback_texture;
    std::unique_ptr<erhe::graphics::Sampler>           m_fallback_sampler;
};

// V2.1. The unit-level statement of the reported bug, at the GPU: the same
// Material in two sets resolves through each set's OWN slot, and each set's
// record at that slot carries that material's colour.
TEST_F(Material_set_gpu_test, two_sets_same_material_distinct_slots)
{
    const std::shared_ptr<Material> shared_material = make_material("Gold",  glm::vec3{1.0f, 0.0f, 0.0f});
    const std::shared_ptr<Material> other_material  = make_material("Other", glm::vec3{0.0f, 1.0f, 0.0f});

    Material_set set_a{make_create_info("set a")};
    Material_set set_b{make_create_info("set b")};

    const Material_list list_a{shared_material, other_material};
    const Material_list list_b{other_material, shared_material};
    set_a.sync_library(std::span<const std::shared_ptr<Material>>{list_a});
    set_b.sync_library(std::span<const std::shared_ptr<Material>>{list_b});

    const uint32_t slot_a = set_a.get_slot(shared_material.get()).value();
    const uint32_t slot_b = set_b.get_slot(shared_material.get()).value();
    EXPECT_EQ(slot_a, 1u);
    EXPECT_EQ(slot_b, 2u);

    update(set_a);
    update(set_b);

    EXPECT_EQ(read_base_color(set_a, slot_a).r, 1.0f);
    EXPECT_EQ(read_base_color(set_b, slot_b).r, 1.0f);
}

// V2.2. The direct analogue of "the preview render clobbers the main scene":
// updating one set must not disturb another's records for the same material.
TEST_F(Material_set_gpu_test, record_survives_foreign_update)
{
    const std::shared_ptr<Material> shared_material = make_material("Gold",  glm::vec3{1.0f, 0.0f, 0.0f});
    const std::shared_ptr<Material> filler          = make_material("Filler", glm::vec3{0.0f, 0.0f, 1.0f});

    Material_set set_a{make_create_info("set a")};
    Material_set set_b{make_create_info("set b")};

    const Material_list list_a{shared_material};
    const Material_list list_b{filler, shared_material};
    set_a.sync_library(std::span<const std::shared_ptr<Material>>{list_a});
    set_b.sync_library(std::span<const std::shared_ptr<Material>>{list_b});

    update(set_a);
    update(set_b);

    EXPECT_EQ(read_base_color(set_a, 2u).r, 1.0f);
}

// V2.3. R2: an addition never renumbers, and the first material's record stays
// where the cached records already say it is.
TEST_F(Material_set_gpu_test, stable_slot_after_material_added)
{
    const std::shared_ptr<Material> first  = make_material("First",  glm::vec3{1.0f, 0.0f, 0.0f});
    const std::shared_ptr<Material> second = make_material("Second", glm::vec3{0.0f, 1.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list list_1{first};
    set.sync_library(std::span<const std::shared_ptr<Material>>{list_1});
    update(set);
    EXPECT_EQ(read_base_color(set, 2u).r, 1.0f);

    const Material_list list_2{first, second};
    set.sync_library(std::span<const std::shared_ptr<Material>>{list_2});
    update(set);

    EXPECT_EQ(set.get_slot(first.get()).value(), 1u);
    EXPECT_EQ(read_base_color(set, 2u).r, 1.0f);
    EXPECT_EQ(read_base_color(set, 3u).g, 1.0f);
}

// V2.4. R3: membership is by reference, not by library listing. A material
// reachable only through an object reference must be written at its slot
// rather than left as a zero-filled hole - this is the assign-to-an-already-
// registered-mesh case the content library never covers.
TEST_F(Material_set_gpu_test, referenced_non_library_material_is_written)
{
    const std::shared_ptr<Material> library_material = make_material("Library",    glm::vec3{1.0f, 0.0f, 0.0f});
    const std::shared_ptr<Material> object_material  = make_material("ObjectOnly", glm::vec3{0.0f, 1.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list library{library_material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    const Material_slot_id id = set.add_ref(object_material);
    ASSERT_TRUE(id.is_valid());

    update(set);
    EXPECT_EQ(read_base_color(set, id.index).g, 1.0f);
}

// V2.7. R10: a clean update writes no new copy, and the copy every later frame
// binds is the one already there.
TEST_F(Material_set_gpu_test, clean_update_writes_nothing)
{
    const std::shared_ptr<Material> material = make_material("Material", glm::vec3{1.0f, 0.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});

    update(set);
    const std::size_t after_first = set.get_write_count();
    EXPECT_GT(after_first, 0u);

    update(set);
    update(set);
    EXPECT_EQ(set.get_write_count(), after_first);

    // And what is still bound is the record from the first write.
    EXPECT_EQ(read_base_color(set, 2u).r, 1.0f);
}

// V2.8. R5, and the reason invalidation is a content hash rather than a
// version counter: this edit goes straight through the Material object with no
// notification of any kind, exactly as a colour-picker drag and the MCP
// edit_material tool do. A version-counter implementation passes every other
// test here and fails this one.
TEST_F(Material_set_gpu_test, material_data_edit_dirties_the_set)
{
    const std::shared_ptr<Material> material = make_material("Material", glm::vec3{1.0f, 0.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    update(set);
    const std::size_t after_first = set.get_write_count();

    material->set_base_color(glm::vec3{0.0f, 0.0f, 1.0f});

    update(set);
    EXPECT_GT(set.get_write_count(), after_first);
    const glm::vec3 base_color = read_base_color(set, 2u);
    EXPECT_EQ(base_color.r, 0.0f);
    EXPECT_EQ(base_color.b, 1.0f);
}

// V2.9. The Graph_texture re-bake shape: the material is untouched, but the
// texture its reference resolves to is a different object. The hash reads the
// resolved texture pointer, so the set dirties without the texture graph
// knowing material state exists.
TEST_F(Material_set_gpu_test, texture_rebake_dirties_the_set)
{
    const std::shared_ptr<Material> material = make_material("Material", glm::vec3{1.0f, 0.0f, 0.0f});

    std::shared_ptr<erhe::graphics::Texture> texture_a;
    std::shared_ptr<erhe::graphics::Texture> texture_b;
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            texture_a = device().create_dummy_texture(command_buffer, erhe::dataformat::Format::format_8_vec4_srgb);
            texture_b = device().create_dummy_texture(command_buffer, erhe::dataformat::Format::format_8_vec4_srgb);
        }
    );
    // Texture is itself a Texture_reference that returns itself, which is the
    // plain case; a Graph_texture is the one that returns a different object
    // after a re-bake, and swapping the reference is that shape.
    material->data.texture_samplers.base_color.texture_reference = texture_a;

    Material_set set{make_create_info("set")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    update(set);
    const std::size_t after_first = set.get_write_count();

    material->data.texture_samplers.base_color.texture_reference = texture_b;

    update(set);
    EXPECT_GT(set.get_write_count(), after_first);
}

// V2.10. The membership dirty edge reaches the GPU write in both directions.
TEST_F(Material_set_gpu_test, membership_change_dirties_the_set)
{
    const std::shared_ptr<Material> first  = make_material("First",  glm::vec3{1.0f, 0.0f, 0.0f});
    const std::shared_ptr<Material> second = make_material("Second", glm::vec3{0.0f, 1.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list list_1{first};
    set.sync_library(std::span<const std::shared_ptr<Material>>{list_1});
    update(set);
    const std::size_t after_first = set.get_write_count();

    const Material_list list_2{first, second};
    set.sync_library(std::span<const std::shared_ptr<Material>>{list_2});
    update(set);
    const std::size_t after_add = set.get_write_count();
    EXPECT_GT(after_add, after_first);

    set.sync_library(std::span<const std::shared_ptr<Material>>{list_1});
    update(set);
    EXPECT_GT(set.get_write_count(), after_add);
}

// V2.13. R7, and the standing check on D0's dependency rule: a Material_set
// used with no scene, no draw list and no registered objects anywhere - the
// BRDF slice / example / empty-set configuration. This translation unit
// includes material_set.hpp and material_buffer.hpp and no scene or draw list
// header at all, so a dependency creeping back into the material layer breaks
// the build rather than a review.
TEST_F(Material_set_gpu_test, material_set_alone_updates_and_binds)
{
    const std::shared_ptr<Material> material = make_material("Only", glm::vec3{0.25f, 0.5f, 0.75f});

    Material_set set{make_create_info("standalone")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    update(set);

    const glm::vec3 base_color = read_base_color(set, 2u);
    EXPECT_FLOAT_EQ(base_color.r, 0.25f);
    EXPECT_FLOAT_EQ(base_color.g, 0.5f);
    EXPECT_FLOAT_EQ(base_color.b, 0.75f);
}

// invalidate() forces a rewrite for the changes no content hash can see.
TEST_F(Material_set_gpu_test, invalidate_forces_a_rewrite)
{
    const std::shared_ptr<Material> material = make_material("Material", glm::vec3{1.0f, 0.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    update(set);
    const std::size_t after_first = set.get_write_count();

    update(set);
    EXPECT_EQ(set.get_write_count(), after_first);

    set.invalidate();
    update(set);
    EXPECT_GT(set.get_write_count(), after_first);
}

// The record every primitive with no material of its own reads: the reserved
// slot carries erhe's default look (UsdPreviewSurface's unbound 0.18 grey),
// not a zeroed record and not the first library material.
TEST_F(Material_set_gpu_test, default_slot_carries_the_default_material)
{
    const std::shared_ptr<Material> material = make_material("Library", glm::vec3{1.0f, 0.0f, 0.0f});

    Material_set set{make_create_info("set")};
    const Material_list library{material};
    set.sync_library(std::span<const std::shared_ptr<Material>>{library});
    update(set);

    const glm::vec3 base_color = read_base_color(set, Material_set::default_material_slot_index);
    EXPECT_FLOAT_EQ(base_color.r, 0.18f);
    EXPECT_FLOAT_EQ(base_color.g, 0.18f);
    EXPECT_FLOAT_EQ(base_color.b, 0.18f);
}

// A set with no members at all still writes the reserved record, which is what
// a file that binds no material anywhere (an unbound-mesh USD scene) renders
// through.
TEST_F(Material_set_gpu_test, default_slot_is_written_for_an_empty_set)
{
    Material_set set{make_create_info("empty")};
    update(set);

    const glm::vec3 base_color = read_base_color(set, Material_set::default_material_slot_index);
    EXPECT_FLOAT_EQ(base_color.r, 0.18f);
}

// The second reserved slot: an unbound primitive whose mesh authored vertex
// colors reads a white base color, so its vertex colors are its albedo.
TEST_F(Material_set_gpu_test, vertex_colored_default_slot_carries_a_white_base_color)
{
    Material_set set{make_create_info("vertex_colored")};
    update(set);

    const glm::vec3 base_color = read_base_color(set, Material_set::vertex_colored_default_material_slot_index);
    EXPECT_FLOAT_EQ(base_color.r, 1.0f);
    EXPECT_FLOAT_EQ(base_color.g, 1.0f);
    EXPECT_FLOAT_EQ(base_color.b, 1.0f);
}

} // namespace erhe::scene_renderer::test
