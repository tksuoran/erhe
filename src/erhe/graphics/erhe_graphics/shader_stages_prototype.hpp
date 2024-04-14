#pragma once

#include "erhe_graphics/shader_resource.hpp"

#include <igl/Shader.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace glslang {
    class TShader;
};

namespace igl {
    class IDevice;
};

namespace erhe::graphics {

class Fragment_outputs;
class Shader_resource;
class Vertex_attribute_mappings;

class Shader_stage_create_info
{
public:
    // user-provided constructors are needed for vector::emplace_back()
    // Sadly, this disables using designated initializers
    Shader_stage_create_info(igl::ShaderStage stage, const std::string_view source);
    Shader_stage_create_info(igl::ShaderStage stage, const std::filesystem::path path);

    igl::ShaderStage         stage;
    std::string              source;
    std::string              entrypoint{"main"};
    std::filesystem::path    path;
    std::vector<std::string> extensions;
};

class Shader_stages_create_info
{
public:
    // Adds #version, #extensions, #defines, fragment outputs, uniform blocks, samplers,
    // and source (possibly read from file).
    [[nodiscard]] auto final_source           (const Shader_stage_create_info& shader) const -> std::string;
    [[nodiscard]] auto attributes_source      () const -> std::string;
    [[nodiscard]] auto fragment_outputs_source() const -> std::string;
    [[nodiscard]] auto struct_types_source    () const -> std::string;
    [[nodiscard]] auto interface_blocks_source() const -> std::string;
    [[nodiscard]] auto interface_source       () const -> std::string;

    void add_interface_block(gsl::not_null<const Shader_resource*> uniform_block);

    std::string                                      name;
    std::vector<std::string>                         pragmas                  {};
    std::vector<std::pair<std::string, std::string>> defines                  {};
    std::vector<const Shader_resource*>              struct_types             {};
    std::vector<const Shader_resource*>              interface_blocks         {};
    const Vertex_attribute_mappings*                 vertex_attribute_mappings{nullptr};
    const Fragment_outputs*                          fragment_outputs         {nullptr};
    const Shader_resource*                           samplers                 {nullptr};
    std::vector<Shader_stage_create_info>            stage_create_infos       {};
    bool                                             dump_reflection          {false};
    bool                                             dump_interface           {false};
    bool                                             dump_final_source        {false};
    bool                                             build                    {false};
};

class Shader_stages_prototype final
{
public:
    Shader_stages_prototype (igl::IDevice& device, Shader_stages_create_info&& create_info);
    Shader_stages_prototype (igl::IDevice& device, const Shader_stages_create_info& create_info);
    ~Shader_stages_prototype() noexcept = default;
    Shader_stages_prototype (const Shader_stages_prototype&) = delete;
    void operator=          (const Shader_stages_prototype&) = delete;
    Shader_stages_prototype (Shader_stages_prototype&&) = default;

    [[nodiscard]] auto name       () const -> const std::string&;
    [[nodiscard]] auto create_info() const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid   () -> bool;

    auto link_program   () -> bool;
    void compile_shaders();

private:
    [[nodiscard]] auto compile(const Shader_stage_create_info& stage_create_info) -> std::shared_ptr<glslang::TShader>;

    friend class Shader_stages;

    enum class State : int
    {
        init                       = 0,
        shader_compilation_started = 1,
        program_link_started       = 2,
        ready                      = 3,
        fail                       = 4
    };

    igl::IDevice&                           m_device;
    Shader_stages_create_info               m_create_info;
    std::unique_ptr<igl::IShaderStages>     m_shader_stages;
    State                                   m_state{State::init};
    struct Shader_stage_entry{
        explicit Shader_stage_entry(std::shared_ptr<glslang::TShader> glslang_shader) : glslang_shader{glslang_shader} {}
        std::shared_ptr<glslang::TShader>   glslang_shader;
        std::vector<uint32_t>               spv_binary;
        std::shared_ptr<igl::IShaderModule> module;
    };
    std::vector<Shader_stage_entry> m_stages;
    std::map<std::string, Shader_resource, std::less<>> m_resources;
};

} // namespace erhe::graphis
