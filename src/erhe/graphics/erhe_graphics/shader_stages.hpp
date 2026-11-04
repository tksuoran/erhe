#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace glslang { class TShader; }
namespace erhe::dataformat { class Vertex_format; }

namespace erhe::graphics {

class Bind_group_layout;
class Device_impl;
class Fragment_outputs;
class Shader_resource;
class Vertex_input_state;

class Shader_stage_extension
{
public:
    Shader_type shader_stage;
    std::string extension;
};

class Shader_stage
{
public:
    // user-provided constructors are needed for vector::emplace_back()
    // Sadly, this disables using designated initializers
    Shader_stage(Shader_type type, std::string_view source);
    Shader_stage(Shader_type type, const std::filesystem::path& path);

    [[nodiscard]] auto get_description() const -> std::string;

    Shader_type                        type;
    std::string                        source;
    std::vector<std::filesystem::path> paths;
};

// The named constants of an enum-valued define; final_source() emits the
// constants and the define as the name of its value (doc/erhe/graphics.md
// "Named define values").
enum class Define_constants_kind : unsigned int
{
    value,   // the value is one constant
    bit_mask // the value is the OR of the constants of its set bits
};

class Shader_define_constants
{
public:
    std::string                                   define;
    Define_constants_kind                         kind{Define_constants_kind::value};
    std::vector<std::pair<std::string, uint32_t>> constants;
};

// The constant naming value ("(A | B)" for a bit mask; "0" for an empty mask);
// empty when value is not a number or no constant names it.
[[nodiscard]] auto get_define_value_expression(const Shader_define_constants& constants, std::string_view value) -> std::string;

class Shader_stages_create_info
{
public:
    // Adds #version, #extensions, #defines, fragment outputs, uniform blocks, samplers,
    // and source (possibly read from file).
    // struct_and_block_source is struct_and_block_source() of this create
    // info, for a caller that builds several stages from it: the text is the
    // same for every stage. nullptr: generated here.
    [[nodiscard]] auto final_source(
        Device&                             graphics_device,
        const Shader_stage&                 shader,
        std::vector<std::filesystem::path>* paths,
        std::optional<unsigned int>         gl_name = {},
        const std::string*                  struct_and_block_source = nullptr
    ) const -> std::string;

    // struct_types_source() followed by interface_blocks_source().
    [[nodiscard]] auto struct_and_block_source() const -> std::string;

    [[nodiscard]] auto attributes_source       () const -> std::string;
    [[nodiscard]] auto attribute_defines_source() const -> std::string;
    // The defines the vertex format contributes to every stage: ERHE_ATTRIBUTE_<name>
    // per attribute and the ERHE_VERTEX_*_ENCODING values.
    [[nodiscard]] auto get_vertex_format_defines() const -> std::vector<std::pair<std::string, std::string>>;
    // The program variant's identity: defines, then get_vertex_format_defines().
    [[nodiscard]] auto get_variant_defines     () const -> std::vector<std::pair<std::string, std::string>>;
    [[nodiscard]] auto fragment_outputs_source () const -> std::string;
    [[nodiscard]] auto struct_types_source     () const -> std::string;
    [[nodiscard]] auto interface_blocks_source () const -> std::string;
    [[nodiscard]] auto interface_source        () const -> std::string;

    void add_interface_block(const Shader_resource* uniform_block);

    [[nodiscard]] auto get_description() const -> std::string;

    // The define_constants entry of a define; nullptr when it has none.
    [[nodiscard]] auto find_define_constants(std::string_view define) const -> const Shader_define_constants*;

    std::string                                      name;
    erhe::utility::Debug_label                       debug_label           {};
    std::vector<std::string>                         pragmas               {};
    std::vector<std::pair<std::string, std::string>> defines               {};
    std::vector<Shader_define_constants>             define_constants      {};
    std::vector<Shader_stage_extension>              extensions            {};
    std::vector<const Shader_resource*>              struct_types          {};
    std::vector<const Shader_resource*>              interface_blocks      {};
    const Fragment_outputs*                          fragment_outputs      {nullptr};
    const erhe::dataformat::Vertex_format*           vertex_format         {nullptr};
    bool                                             no_vertex_input       {false};   // fullscreen passes: don't inject vertex attributes
    std::vector<Shader_stage>                        shaders               {};
    std::vector<std::filesystem::path>               extra_include_paths   {};
    const Bind_group_layout*                         bind_group_layout     {nullptr};
    uint32_t                                         view_count            {0};
    bool                                             dump_reflection       {false};
    bool                                             dump_interface        {false};
    bool                                             dump_final_source     {false};
    bool                                             dump_spirv_disassembly{false};
};

class Shader_stages_prototype_impl;
class Shader_stages_prototype final
{
public:
    Shader_stages_prototype (Device& device, Shader_stages_create_info&& create_info);
    Shader_stages_prototype (Device& device, const Shader_stages_create_info& create_info);
    ~Shader_stages_prototype() noexcept;
    Shader_stages_prototype (const Shader_stages_prototype&) = delete;
    void operator=          (const Shader_stages_prototype&) = delete;
    Shader_stages_prototype (Shader_stages_prototype&& old) noexcept;

    void compile_shaders() const;
    auto link_program   () const -> bool;

#if defined(ERHE_SPIRV)
    auto compile_glslang(Device& device, const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>;
#endif

    [[nodiscard]] auto name            () const -> const std::string&;
    [[nodiscard]] auto create_info     () const -> const Shader_stages_create_info&;
    [[nodiscard]] auto is_valid        () const -> bool;
    [[nodiscard]] auto get_final_source(const Shader_stage& shader, std::optional<unsigned int> gl_name) const -> std::string;
    // Every source file the stages were built from: the stage files and
    // what they include, as found while the final sources were assembled.
    [[nodiscard]] auto get_dependency_paths() const -> const std::vector<std::filesystem::path>&;
    [[nodiscard]] auto get_impl        () -> Shader_stages_prototype_impl&;
    [[nodiscard]] auto get_impl        () const -> const Shader_stages_prototype_impl&;

private:
    std::unique_ptr<Shader_stages_prototype_impl> m_impl;
};

[[nodiscard]] auto build_shader_stages(Shader_stages_prototype&& prototype) -> Shader_stages_prototype;
[[nodiscard]] auto build_shader_stages(Device& device, Shader_stages_create_info create_info) -> Shader_stages_prototype;

class Shader_stages_impl;
class Shader_stages final
{
public:
    Shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Shader_stages(Device& device, const std::string& non_functional_name);
    ~Shader_stages() noexcept;
    Shader_stages(Shader_stages&&) noexcept;
    Shader_stages& operator=(Shader_stages&&) noexcept;

    // Reloads program by consuming prototype
    void reload    (Shader_stages_prototype&& prototype) const;
    void invalidate() const;

    [[nodiscard]] auto name                 () const -> const std::string&;
    // Shader_stages_create_info::get_variant_defines() of the create info the stages were built from.
    [[nodiscard]] auto get_variant_defines  () const -> const std::vector<std::pair<std::string, std::string>>&;
    [[nodiscard]] auto is_valid             () const -> bool;
    [[nodiscard]] auto get_bind_group_layout() const -> const Bind_group_layout*;
    [[nodiscard]] auto get_impl             () -> Shader_stages_impl&;
    [[nodiscard]] auto get_impl             () const -> const Shader_stages_impl&;

private:
    std::unique_ptr<Shader_stages_impl>                      m_impl;
    mutable std::vector<std::pair<std::string, std::string>> m_defines; // replaced with the impl's modules by reload()
};

class Reloadable_shader_stages
{
public:
    Reloadable_shader_stages(Device& device, const std::string& non_functional_name);
    Reloadable_shader_stages(Device& device, Shader_stages_prototype&& prototype);
    Reloadable_shader_stages(Device& device, const Shader_stages_create_info& create_info);
    Reloadable_shader_stages(Reloadable_shader_stages&& old) noexcept;
    Reloadable_shader_stages& operator=(Reloadable_shader_stages&& old) noexcept;

    Shader_stages_create_info create_info;
    Shader_stages             shader_stages;

private:
    [[nodiscard]] auto make_prototype(Device& graphics_device) const -> Shader_stages_prototype;
};

} // namespace erhe::graphics
