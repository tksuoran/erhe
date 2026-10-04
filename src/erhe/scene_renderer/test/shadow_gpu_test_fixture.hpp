#pragma once

// Shared fixture of the shadow GPU tests (doc/erhe/shadows.md "Shadow sampling
// GPU tests").
// Sets up what Shadow_renderer and Forward_renderer need beyond the Device -
// Mesh_memory, Program_interface, Shader_variant_cache, Material_set,
// Scene_pass_resources, Light_set, Light_projections - plus a small analytic
// scene: a floor box whose top face is the plane y = 0, optional caster boxes,
// one shadow-casting light straight above the origin, and a top-down
// orthographic view camera covering x, z in [-1, 1] whose depth range clips
// everything above y = 0.2, so the view shows the plane and never a caster.
//
// The fixture renders three things: the shadow map (Shadow_renderer), a
// forward pass with Shader_debug::shadow_visibility (Forward_renderer), and the
// Shadow_tie fragment pass (shaders/shadow_tie.frag through
// Forward_renderer::draw_primitives), which evaluates sample_light_visibility()
// on the plane with the reference depth offset by -N .. +N float ulps.

#include "gpu_test_fixture.hpp"

#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/light_set.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_pass_resources.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace erhe::scene_renderer::test {

enum class Shadow_light_kind : unsigned int
{
    directional,
    spot
};

[[nodiscard]] auto c_str(Shadow_light_kind light_kind) -> const char*;

// The ERHE_SHADOW_TECHNIQUE axis (values as erhe_light.glsl defines them):
// depth compares light-space depth against the depth map; distance stores
// each texel's caster plane distance on the texel's centre ray in an R32F
// map and compares the receiver plane's distance on the same ray
// (doc/erhe/shadows.md "The distance technique").
enum class Shadow_technique_case : unsigned int
{
    depth    = 0,
    distance = 1
};

[[nodiscard]] auto c_str(Shadow_technique_case technique) -> const char*;
[[nodiscard]] auto get_shadow_technique_cases() -> const std::vector<Shadow_technique_case>&;

// One receiver-side sampling configuration: the ERHE_SHADOW_FILTER and
// ERHE_SHADOW_BIAS variant axes (the bias axis only matters for the wide
// filters).
class Shadow_filter_case
{
public:
    const char* name;
    uint32_t    filter; // PCF kernel width, 0 = hard
    uint32_t    bias;   // 0 = slope_scaled, 1 = receiver_plane
};

// hard, pcf_2x2, and pcf_4x4 / pcf_6x6 with each receiver bias mode.
[[nodiscard]] auto get_shadow_filter_cases() -> const std::vector<Shadow_filter_case>&;

// Light pose, head-on to the plane in every case. The station (floor, casters,
// light, view camera) is built in station-local coordinates, where the plane
// is y = 0, and placed in the world by a rigid frame: frame_angle radians
// about frame_axis, then frame_translation. The identity frame keeps every
// matrix exact; a general frame gives the matrices the last-bit rounding
// noise a real scene has. In station coordinates the spot light sits at
// (0, spot_height, 0); the directional light's stable fit is anchored at the
// view camera (0, 3, 0) with radius shadow_range, so its light camera sits at
// (0, 3 + shadow_range, 0). Whether the head-on tie shows depends on where
// last-bit rounding falls (doc/erhe/shadows.md "Minimum bias", R6), so the
// tests run several poses.
class Shadow_pose
{
public:
    float     spot_height;
    float     shadow_range; // >= 3 keeps the plane inside the directional map's depth range
    glm::vec3 frame_axis;
    float     frame_angle;
    glm::vec3 frame_translation;

    [[nodiscard]] auto get_world_from_station() const -> glm::mat4;
};

[[nodiscard]] auto get_shadow_poses() -> const std::vector<Shadow_pose>&;
[[nodiscard]] auto describe(Shadow_light_kind light_kind, const Shadow_pose& pose) -> std::string;

// Caster-side settings of one shadow map render.
class Shadow_map_settings
{
public:
    erhe::dataformat::Format depth_format       {erhe::dataformat::Format::format_d32_sfloat};
    Shadow_technique_case    technique          {Shadow_technique_case::depth};
    int                      resolution         {1024};
    Shadow_cull_mode         cull_mode          {Shadow_cull_mode::cull_back};
    float                    depth_bias_constant{0.0f};
    float                    depth_bias_slope   {0.0f};
};

// Per-k failure counts of one Shadow_tie render.
class Shadow_tie_result
{
public:
    std::vector<int> failing_per_ulp;   // index k + max_ulps: pixels whose visibility is < 1
    int              pixels_per_ulp{0}; // receiver points evaluated per k
    int              max_ulps      {0};

    [[nodiscard]] auto total_failing() const -> int;
    [[nodiscard]] auto describe     () const -> std::string;
};

// Visibility image of one forward shadow_visibility render (r channel only),
// target_size x target_size, row-major from the first row the readback
// returns. station_x / station_z give the station-local plane point a pixel
// center shows.
class Visibility_image
{
public:
    std::vector<float> visibility;
    int                size  {0};
    float              extent{1.0f}; // the view covers station x, z in [-extent, extent]

    [[nodiscard]] auto at       (int x, int y) const -> float;
    [[nodiscard]] auto station_x(int x) const -> float;
    [[nodiscard]] auto station_z(int y) const -> float;
};

class Shadow_gpu_test : public erhe::graphics::test::Gpu_test
{
protected:
    // Scene constants, station-local. The floor's top face is the plane
    // y = 0; the view and the Shadow_tie receivers cover x, z in
    // [-c_view_extent, c_view_extent].
    static constexpr float c_floor_extent    = 2.0f;
    static constexpr float c_floor_thickness = 0.1f;
    static constexpr float c_view_extent     = 1.0f;
    static constexpr int   c_view_size       = 256;   // forward pass target, square
    static constexpr int   c_tie_band_width  = 128;   // Shadow_tie band width (even)
    static constexpr int   c_tie_height      = 128;
    static constexpr int   c_tie_max_ulps    = 4;     // k runs -c_tie_max_ulps .. +c_tie_max_ulps

    void SetUp   () override;
    void TearDown() override;

    // Depth formats the device offers for a shadow map: for each of 16, 24
    // and 32 depth bits one format, preferring one without stencil.
    [[nodiscard]] auto get_shadow_depth_formats() -> std::vector<erhe::dataformat::Format>;

    // Scene content. The floor exists from SetUp; add_box() adds a caster
    // (station-local corners). set_light() places the station at the pose's
    // frame and creates the light.
    void add_box   (const glm::vec3& min_corner, const glm::vec3& max_corner);
    void set_light (Shadow_light_kind light_kind, const Shadow_pose& pose);

    // Renders the shadow map of the current light and scene (and, for the
    // distance technique, its distance map). The receiver passes below sample
    // with the technique of the latest shadow map render.
    void render_shadow_map(const Shadow_map_settings& settings);

    // Forward pass with Shader_debug::shadow_visibility over the top-down view.
    [[nodiscard]] auto render_visibility(const Shadow_filter_case& filter_case) -> Visibility_image;

    // Shadow_tie fragment pass. The reference depth offset applies to the
    // light-space depth, which the distance technique does not compare: with
    // the distance technique every k band evaluates the same head-on tie.
    [[nodiscard]] auto render_tie(const Shadow_filter_case& filter_case) -> Shadow_tie_result;

private:
    [[nodiscard]] auto make_box_mesh(const char* name, const glm::vec3& min_corner, const glm::vec3& max_corner) -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto get_tie_stages(const Shadow_filter_case& filter_case) -> erhe::graphics::Shader_stages*;
    void flush_meshes();

    Mesh_memory_config                                         m_mesh_memory_config{};
    std::unique_ptr<Mesh_memory>                               m_mesh_memory;
    Program_interface_config                                   m_program_interface_config{};
    std::unique_ptr<Program_interface>                         m_program_interface;
    std::unique_ptr<Shader_variant_cache>                      m_shader_variant_cache;
    std::shared_ptr<erhe::graphics::Texture>                   m_fallback_texture;
    std::unique_ptr<erhe::graphics::Sampler>                   m_fallback_sampler;
    std::unique_ptr<Material_set>                              m_material_set;
    std::unique_ptr<Scene_pass_resources>                      m_pass_resources;
    std::unique_ptr<Forward_renderer>                          m_forward_renderer;
    std::unique_ptr<Shadow_renderer>                           m_shadow_renderer;
    std::unique_ptr<erhe::graphics::Base_render_pipeline>      m_forward_pipeline;
    std::unique_ptr<erhe::graphics::Base_render_pipeline>      m_tie_pipeline;
    std::map<std::string, std::unique_ptr<erhe::graphics::Shader_stages>> m_tie_stages;

    std::shared_ptr<erhe::primitive::Material>                 m_material;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>            m_meshes;
    std::shared_ptr<erhe::scene::Light>                        m_light;
    std::shared_ptr<erhe::scene::Camera>                       m_camera;
    glm::mat4                                                  m_world_from_station{1.0f};
    Light_set                                                  m_light_set;
    Light_projections                                          m_light_projections;

    std::shared_ptr<erhe::graphics::Texture>                   m_shadow_map;
    std::shared_ptr<erhe::graphics::Texture>                   m_distance_map;
    Shadow_technique_case                                      m_shadow_technique{Shadow_technique_case::depth};
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>>  m_shadow_render_passes;
    erhe::dataformat::Format                                   m_shadow_map_format{erhe::dataformat::Format::format_undefined};
};

} // namespace erhe::scene_renderer::test
