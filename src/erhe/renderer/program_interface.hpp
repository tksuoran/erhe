#pragma once

#include "erhe/renderer/camera_buffer.hpp"
#include "erhe/renderer/joint_buffer.hpp"
#include "erhe/renderer/light_buffer.hpp"
#include "erhe/renderer/material_buffer.hpp"
#include "erhe/renderer/primitive_buffer.hpp"

#include "erhe/components/components.hpp"
#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"

#include <memory>

namespace erhe::renderer
{

class Program_interface
    : public erhe::components::Component
{
public:
    class Config
    {
    public:
        int max_material_count  {256};
        int max_light_count     {256};
        int max_camera_count    {256};
        int max_primitive_count {8000}; // glTF primitives
        int max_draw_count      {8000};
        int max_joint_count     {1000}; // glTF joint nodes
    };
    Config config;

    static constexpr std::string_view c_type_name{"Program_interface"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Program_interface ();
    ~Program_interface() noexcept override;
    Program_interface (const Program_interface&) = delete;
    void operator=    (const Program_interface&) = delete;
    Program_interface (Program_interface&&)      = delete;
    void operator=    (Program_interface&&)      = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    class Shader_resources
    {
    public:
        Shader_resources(
            std::size_t max_material_count,
            std::size_t max_light_count,
            std::size_t max_camera_count,
            std::size_t max_primitive_count,
            std::size_t max_joint_count
        );

        [[nodiscard]] auto make_prototype(
            const std::filesystem::path&               shader_path,
            erhe::graphics::Shader_stages::Create_info create_info
        ) -> std::unique_ptr<erhe::graphics::Shader_stages::Prototype>;

        [[nodiscard]] auto make_program(
            erhe::graphics::Shader_stages::Prototype& prototype
        ) -> std::unique_ptr<erhe::graphics::Shader_stages>;

        erhe::graphics::Fragment_outputs          fragment_outputs;
        erhe::graphics::Vertex_attribute_mappings attribute_mappings;

        Camera_interface    camera_interface;
        Light_interface     light_interface;
        Joint_interface     joint_interface;
        Material_interface  material_interface;
        Primitive_interface primitive_interface;
    };

    std::unique_ptr<Shader_resources> shader_resources;
};

extern Program_interface* g_program_interface;

} // namespace erhe::renderer
