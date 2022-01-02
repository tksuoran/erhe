#include "renderers/headset_renderer.hpp"
#include "application.hpp"
#include "configuration.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "window.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/theremin_tool.hpp"
#include "windows/log_window.hpp"

#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/xr/headset.hpp"

#include <imgui.h>

namespace editor
{

using namespace erhe::graphics;

Headset_view_resources::Headset_view_resources(
    erhe::xr::Render_view& render_view,
    Editor_rendering&      rendering,
    const size_t           slot
)
{
    // log_headset.trace(
    //     "Color tex {:2} {:<20} depth texture {:2} {:<20} size {:4} x {:4}, hfov {: 6.2f}..{: 6.2f}, vfov {: 6.2f}..{: 6.2f}, pos {}\n",
    //     render_view.color_texture,
    //     gl::c_str(render_view.color_format),
    //     render_view.depth_texture,
    //     gl::c_str(render_view.depth_format),
    //     render_view.width,
    //     render_view.height,
    //     glm::degrees(render_view.fov_left ),
    //     glm::degrees(render_view.fov_right),
    //     glm::degrees(render_view.fov_up   ),
    //     glm::degrees(render_view.fov_down ),
    //     render_view.view_pose.position
    // );

    color_texture = std::make_shared<Texture>(
        Texture_create_info{
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.color_format,
            .width             = static_cast<int>(render_view.width),
            .height            = static_cast<int>(render_view.height),
            .wrap_texture_name = render_view.color_texture
        }
    );
    color_texture->set_debug_label(fmt::format("XR color {}", slot));

    depth_texture = std::make_shared<Texture>(
        Texture_create_info{
            .target            = gl::Texture_target::texture_2d,
            .internal_format   = render_view.depth_format,
            .width             = static_cast<int>(render_view.width),
            .height            = static_cast<int>(render_view.height),
            .wrap_texture_name = render_view.depth_texture,
        }
    );
    depth_texture->set_debug_label(fmt::format("XR depth {}", slot));

    // depth_stencil_renderbuffer = std::make_unique<Renderbuffer>(
    //     gl::Internal_format::depth24_stencil8,
    //     texture_create_info.sample_count,
    //     render_view.width,
    //     render_view.height
    // );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, color_texture.get());
    create_info.attach(gl::Framebuffer_attachment::depth_attachment,  depth_texture.get());
    //create_info.attach(gl::Framebuffer_attachment::stencil_attachment, depth_stencil_renderbuffer.get());
    framebuffer = std::make_unique<Framebuffer>(create_info);
    framebuffer->set_debug_label(fmt::format("XR {}", slot));

    if (!framebuffer->check_status())
    {
        log_headset.warn("Invalid framebuffer for headset - disabling headset");
        return;
    }

    gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer (framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    camera = std::make_shared<erhe::scene::Camera>("Headset Camera");

    auto* scene_root = rendering.get<Scene_root>();
    scene_root->scene().cameras.push_back(camera);

    scene_root->scene().nodes.emplace_back(camera);
    scene_root->scene().nodes_sorted = false;

    auto* view_camera = rendering.get<Fly_camera_tool>()->get_camera();
    view_camera->attach(camera);
    //camera->parent = rendering.get<Scene_builder>()->get_view_camera()->get();
    //camera_node->parent = nullptr;

    is_valid = true;
}

void Headset_view_resources::update(
    erhe::xr::Render_view& render_view,
    Editor_rendering&      /*rendering*/
)
{
    *camera->projection() = erhe::scene::Projection{
        .projection_type = erhe::scene::Projection::Type::perspective_xr,
        .z_near          = 0.03f,
        .z_far           = 200.0f,
        .fov_left        = render_view.fov_left,
        .fov_right       = render_view.fov_right,
        .fov_up          = render_view.fov_up,
        .fov_down        = render_view.fov_down,
    };

    render_view.near_depth = 0.03f;
    render_view.far_depth  = 200.0f;

    const glm::mat4 orientation = glm::mat4_cast(render_view.view_pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, render_view.view_pose.position);
    const glm::mat4 m           = translation * orientation;
    camera->set_parent_from_node(m);
}

Controller_visualization::Controller_visualization(
    Mesh_memory&       mesh_memory,
    Scene_root&        scene_root,
    erhe::scene::Node* view_root
)
{
    auto controller_material = scene_root.make_material("Controller", glm::vec4{0.1f, 0.1f, 0.2f, 1.0f});
    //constexpr float length = 0.05f;
    //constexpr float radius = 0.02f;
    auto controller_geometry = erhe::geometry::shapes::make_torus(0.05f, 0.0025f, 22, 8);
    controller_geometry.transform(erhe::toolkit::mat4_swap_yz);
    controller_geometry.reverse_polygons();

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto controller_pg = erhe::primitive::make_primitive(controller_geometry, mesh_memory.build_info_set.gl);

    erhe::primitive::Primitive primitive{
        .material              = controller_material,
        .gl_primitive_geometry = controller_pg
    };
    m_controller_mesh = std::make_shared<erhe::scene::Mesh>("Controller", primitive);
    view_root->attach(m_controller_mesh);
    scene_root.add(m_controller_mesh, scene_root.controller_layer().get());

    //m_controller_mesh = scene_root.make_mesh_node(
    //    "Controller",                           // name
    //    controller_pg,                          // primitive geometry
    //    controller_material,                    // material
    //    *scene_root.controller_layer().get(),   // layer
    //    view_root,                              // parent
    //    glm::vec3{-9999.9f, -9999.9f, -9999.9f} // position
    //);
}

void Controller_visualization::update(const erhe::xr::Pose& pose)
{
    const glm::mat4 orientation = glm::mat4_cast(pose.orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{ 1 }, pose.position);
    const glm::mat4 m           = translation * orientation;
    m_controller_mesh->set_parent_from_node(m);
}

auto Controller_visualization::get_node() const -> erhe::scene::Node*
{
    return m_controller_mesh.get();
}

Headset_renderer::Headset_renderer()
    : erhe::components::Component{c_name}
{
}

Headset_renderer::~Headset_renderer() = default;

auto Headset_renderer::get_headset_view_resources(
    erhe::xr::Render_view& render_view
) -> Headset_view_resources&
{
    auto match_color_texture = [&render_view](const auto& i)
    {
        return i.color_texture->gl_name() == render_view.color_texture;
    };
    auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end())
    {
        auto& j = m_view_resources.emplace_back(
            render_view,
            *m_editor_rendering,
            m_view_resources.size()
        );
        return j;
    }
    return *i;
}

const std::vector<XrHandJointEXT> thumb_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_THUMB_METACARPAL_EXT,
    XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
    XR_HAND_JOINT_THUMB_DISTAL_EXT,
    XR_HAND_JOINT_THUMB_TIP_EXT
};
const std::vector<XrHandJointEXT> index_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_INDEX_METACARPAL_EXT,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
    XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,
    XR_HAND_JOINT_INDEX_DISTAL_EXT,
    XR_HAND_JOINT_INDEX_TIP_EXT
};
const std::vector<XrHandJointEXT> middle_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
    XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_MIDDLE_DISTAL_EXT,
    XR_HAND_JOINT_MIDDLE_TIP_EXT
};
const std::vector<XrHandJointEXT> ring_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_RING_METACARPAL_EXT,
    XR_HAND_JOINT_RING_PROXIMAL_EXT,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
    XR_HAND_JOINT_RING_DISTAL_EXT,
    XR_HAND_JOINT_RING_TIP_EXT
};
const std::vector<XrHandJointEXT> little_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_LITTLE_METACARPAL_EXT,
    XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
    XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_LITTLE_DISTAL_EXT,
    XR_HAND_JOINT_LITTLE_TIP_EXT
};

class Hand_visualization
{
public:
    void update(erhe::xr::Headset& headset, const XrHandEXT hand)
    {
        ERHE_PROFILE_FUNCTION

        for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i)
        {
            joints[i] = headset.get_hand_tracking_joint(hand, static_cast<XrHandJointEXT>(i));
        }
    }

    auto get_closest_point_to_line(
        const glm::mat4 transform,
        const glm::vec3 p0,
        const glm::vec3 p1
    ) const -> std::optional<erhe::toolkit::Closest_points<float>>
    {
        ERHE_PROFILE_FUNCTION

        std::optional<erhe::toolkit::Closest_points<float>> result;
        float min_distance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i)
        {
            const auto joint = static_cast<XrHandJointEXT>(i);
            const bool valid = is_valid(joint);
            if (valid)
            {
                const glm::vec3 pos{
                    joints[joint].location.pose.position.x,
                    joints[joint].location.pose.position.y,
                    joints[joint].location.pose.position.z
                };
                const auto p             = glm::vec3{transform * glm::vec4{pos, 1.0f}};
                const auto closest_point = erhe::toolkit::closest_point<float>(p0, p1, p);
                if (closest_point.has_value())
                {
                    const auto  q        = closest_point.value();
                    const float distance = glm::distance(p, q);
                    if (distance < min_distance)
                    {
                        min_distance = distance;
                        result = { p, q };
                    }
                }
            }
        }
        return result;
    }

    auto is_valid(const XrHandJointEXT joint) const -> bool
    {
        if (joint >= joints.size())
        {
            return false;
        }
        return (joints[joint].location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT;
    }

    void draw_joint_line_strip(
        const glm::mat4                    transform,
        const std::vector<XrHandJointEXT>& joint_names,
        Line_renderer::Style&              line_renderer
    ) const
    {
        ERHE_PROFILE_FUNCTION

        if (joint_names.size() < 2)
        {
            return;
        }

        for (size_t i = 1; i < joint_names.size(); ++i)
        {
            const size_t joint_id_a = static_cast<size_t>(joint_names[i - 1]);
            const size_t joint_id_b = static_cast<size_t>(joint_names[i]);
            if (joint_id_a >= joints.size())
            {
                continue;
            }
            if (joint_id_b >= joints.size())
            {
                continue;
            }
            const auto& joint_a = joints[joint_id_a];
            const auto& joint_b = joints[joint_id_b];
            if ((joint_a.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT)
            {
                continue;
            }
            if ((joint_b.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT)
            {
                continue;
            }
            line_renderer.add_lines(
                transform,
                {
                    {
                        glm::vec3{joint_a.location.pose.position.x, joint_a.location.pose.position.y, joint_a.location.pose.position.z},
                        glm::vec3{joint_b.location.pose.position.x, joint_b.location.pose.position.y, joint_b.location.pose.position.z}
                    }
                },
                10.0f
            );
        }
    }

    std::array<erhe::xr::Hand_tracking_joint, XR_HAND_JOINT_COUNT_EXT> joints;
};

class Gradient_stop
{
public:
    Gradient_stop(const float t, const uint8_t r, const uint8_t g, const uint8_t b)
        : t{t}
        , color{
            static_cast<float>(r) / 255.0f,
            static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f
        }
    {
    }

    float     t;
    glm::vec3 color;
};

class Palette
{
public:
    Palette(const char* name, const std::initializer_list<Gradient_stop> stops)
        : name {name}
        , stops{stops}
    {
    }

    auto get(const float t) const -> glm::vec3
    {
        if (t < 0.0f)
        {
            return glm::vec3{1.0f, 0.0f, 0.0f};
        }
        if (t > 1.0f)
        {
            return glm::vec3{1.0f, 0.0f, 1.0f};
        }
        for (size_t i = 0; i < stops.size() - 1; ++i)
        {
            const auto& stop0 = stops[i    ];
            const auto& stop1 = stops[i + 1];
            const float t0    = stop0.t;
            const float t1    = stop1.t;
            if ((t >= t0) && (t <= t1))
            {
                const float t_length     = t1 - t0;
                const float normalized_t = (t - t0) / t_length;
                const auto  color0       = stop0.color;
                const auto  color1       = stop1.color;
                return glm::mix(color0, color1, normalized_t);
            }
        }
        return glm::vec3{0.0f};
    }

    const char*                name;
    std::vector<Gradient_stop> stops;
};


const Palette cool             { "cool",             {{0.0f, 125,  0,179},{0.130f,116,  0,218},{0.250f, 98, 74,237},{0.380f, 68,146,231},{0.500f,  0,204,197},{0.630f,  0,247,146},{0.750f,  0,255, 88},{0.880f, 40,255,  8},{1.000f,147,255,  0}}};
const Palette cool_simple      { "cool-simple",      {{0.0f,   0,255,255},{1.000f,255,  0,255}}};
const Palette spring           { "spring",           {{0.0f, 255,  0,255},{1.000f,255,255,  0}}};
const Palette summer           { "summer",           {{0.0f,   0,128,102},{1.000f,255,255,102}}};
const Palette autumn           { "autumn",           {{0.0f, 255,  0,  0},{1.000f,255,255,  0}}};
const Palette winter           { "winter",           {{0.0f,   0,  0,255},{1.000f,  0,255,128}}};
const Palette greys            { "greys",            {{0.0f,   0,  0,  0},{1.000f,255,255,255}}};
const Palette bluered          { "bluered",          {{0.0f,   0,  0,255},{1.000f,255,  0,  0}}};
const Palette copper           { "copper",           {{0.0f,   0,  0,  0},{0.804f,255,160,102},{1.000f,255,199,127}}};
const Palette hot              { "hot",              {{0.0f,   0,  0,  0},{0.300f,230,  0,  0},{0.600f,255,210,  0},{1.000f,255,255,255}}};
const Palette bone             { "bone",             {{0.0f,   0,  0,  0},{0.376f, 84, 84,116},{0.753f,169,200,200},{1.000f,255,255,255}}};
const Palette blackbody        { "blackbody",        {{0.0f,   0,  0,  0},{0.200f,230,  0,  0},{0.400f,230,210,  0},{0.700f,255,255,255},{1.000f,160,200,255}}};
const Palette portland         { "portland",         {{0.0f,  12, 51,131},{0.250f, 10,136,186},{0.500f,242,211, 56},{0.750f,242,143, 56},{1.000f,217, 30, 30}}};
const Palette electric         { "electric",         {{0.0f,   0,  0,  0},{0.150f, 30,  0,100},{0.400f,120,  0,100},{0.600f,160, 90,  0},{0.800f,230,200,  0},{1.000f,255,250,220}}};
const Palette earth            { "earth",            {{0.0f,   0,  0,130},{0.100f,  0,180,180},{0.200f, 40,210, 40},{0.400f,230,230, 50},{0.600f,120, 70, 20},{1.000f,255,255,255}}};
const Palette jet              { "jet",              {{0.0f,   0,  0,131},{0.125f,  0, 60,170},{0.375f,  5,255,255},{0.625f,255,255,  0},{0.875f,250,  0,  0},{1.000f,128,  0,  0}}};
const Palette rdbu             { "rdbu",             {{0.0f,   5, 10,172},{0.350f,106,137,247},{0.500f,190,190,190},{0.600f,220,170,132},{0.700f,230,145, 90},{1.000f,178, 10, 28}}};
const Palette yignbu           { "yignbu",           {{0.0f,   8, 29, 88},{0.125f, 37, 52,148},{0.250f, 34, 94,168},{0.375f, 29,145,192},{0.500f, 65,182,196},{0.625f,127,205,187},{0.750f,199,233,180},{0.875f,237,248,217},{1.000f,255,255,217}}};
const Palette greens           { "greens",           {{0.0f,   0, 68, 27},{0.125f,  0,109, 44},{0.250f, 35,139, 69},{0.375f, 65,171, 93},{0.500f,116,196,118},{0.625f,161,217,155},{0.750f,199,233,192},{0.875f,229,245,224},{1.000f,247,252,245}}};
const Palette yiorrd           { "yiorrd",           {{0.0f, 128,  0, 38},{0.125f,189,  0, 38},{0.250f,227, 26, 28},{0.375f,252, 78, 42},{0.500f,253,141, 60},{0.625f,254,178, 76},{0.750f,254,217,118},{0.875f,255,237,160},{1.000f,255,255,204}}};
const Palette rainbow          { "rainbow",          {{0.0f, 150,  0, 90},{0.125f,  0,  0,200},{0.250f,  0, 25,255},{0.375f,  0,152,255},{0.500f, 44,255,150},{0.625f,151,255,  0},{0.750f,255,234,  0},{0.875f,255,111,  0},{1.000f,255,  0,  0}}};
const Palette viridis          { "viridis",          {{0.0f,  68,  1, 84},{0.130f, 71, 44,122},{0.250f, 59, 81,139},{0.380f, 44,113,142},{0.500f, 33,144,141},{0.630f, 39,173,129},{0.750f, 92,200, 99},{0.880f,170,220, 50},{1.000f,253,231, 37}}};
const Palette inferno          { "inferno",          {{0.0f,   0,  0,  4},{0.130f, 31, 12, 72},{0.250f, 85, 15,109},{0.380f,136, 34,106},{0.500f,186, 54, 85},{0.630f,227, 89, 51},{0.750f,249,140, 10},{0.880f,249,201, 50},{1.000f,252,255,164}}};
const Palette magma            { "magma",            {{0.0f,   0,  0,  4},{0.130f, 28, 16, 68},{0.250f, 79, 18,123},{0.380f,129, 37,129},{0.500f,181, 54,122},{0.630f,229, 80,100},{0.750f,251,135, 97},{0.880f,254,194,135},{1.000f,252,253,191}}};
const Palette plasma           { "plasma",           {{0.0f,  13,  8,135},{0.130f, 75,  3,161},{0.250f,125,  3,168},{0.380f,168, 34,150},{0.500f,203, 70,121},{0.630f,229,107, 93},{0.750f,248,148, 65},{0.880f,253,195, 40},{1.000f,240,249, 33}}};
const Palette warm             { "warm",             {{0.0f, 125,  0,179},{0.130f,172,  0,187},{0.250f,219,  0,170},{0.380f,255,  0,130},{0.500f,255, 63, 74},{0.630f,255,123,  0},{0.750f,234,176,  0},{0.880f,190,228,  0},{1.000f,147,255,  0}}};
const Palette bathymetry       { "bathymetry",       {{0.0f,  40, 26, 44},{0.130f, 59, 49, 90},{0.250f, 64, 76,139},{0.380f, 63,110,151},{0.500f, 72,142,158},{0.630f, 85,174,163},{0.750f,120,206,163},{0.880f,187,230,172},{1.000f,253,254,204}}};
const Palette cdom             { "cdom",             {{0.0f,  47, 15, 62},{0.130f, 87, 23, 86},{0.250f,130, 28, 99},{0.380f,171, 41, 96},{0.500f,206, 67, 86},{0.630f,230,106, 84},{0.750f,242,149,103},{0.880f,249,193,135},{1.000f,254,237,176}}};
const Palette chlorophyll      { "chlorophyll",      {{0.0f,  18, 36, 20},{0.130f, 25, 63, 41},{0.250f, 24, 91, 59},{0.380f, 13,119, 72},{0.500f, 18,148, 80},{0.630f, 80,173, 89},{0.750f,132,196,122},{0.880f,175,221,162},{1.000f,215,249,208}}};
const Palette density          { "density",          {{0.0f,  54, 14, 36},{0.130f, 89, 23, 80},{0.250f,110, 45,132},{0.380f,120, 77,178},{0.500f,120,113,213},{0.630f,115,151,228},{0.750f,134,185,227},{0.880f,177,214,227},{1.000f,230,241,241}}};
const Palette freesurface_blue { "freesurface-blue", {{0.0f,  30,  4,110},{0.130f, 47, 14,176},{0.250f, 41, 45,236},{0.380f, 25, 99,212},{0.500f, 68,131,200},{0.630f,114,156,197},{0.750f,157,181,203},{0.880f,200,208,216},{1.000f,241,237,236}}};
const Palette freesurface_red  { "freesurface-red",  {{0.0f,  60,  9, 18},{0.130f,100, 17, 27},{0.250f,142, 20, 29},{0.380f,177, 43, 27},{0.500f,192, 87, 63},{0.630f,205,125,105},{0.750f,216,162,148},{0.880f,227,199,193},{1.000f,241,237,236}}};
const Palette oxygen           { "oxygen",           {{0.0f,  64,  5,  5},{0.130f,106,  6, 15},{0.250f,144, 26,  7},{0.380f,168, 64,  3},{0.500f,188,100,  4},{0.630f,206,136, 11},{0.750f,220,174, 25},{0.880f,231,215, 44},{1.000f,248,254,105}}};
const Palette par              { "par",              {{0.0f,  51, 20, 24},{0.130f, 90, 32, 35},{0.250f,129, 44, 34},{0.380f,159, 68, 25},{0.500f,182, 99, 19},{0.630f,199,134, 22},{0.750f,212,171, 35},{0.880f,221,210, 54},{1.000f,225,253, 75}}};
const Palette phase            { "phase",            {{0.0f, 145,105, 18},{0.130f,184, 71, 38},{0.250f,186, 58,115},{0.380f,160, 71,185},{0.500f,110, 97,218},{0.630f, 50,123,164},{0.750f, 31,131,110},{0.880f, 77,129, 34},{1.000f,145,105, 18}}};
const Palette salinity         { "salinity",         {{0.0f,  42, 24,108},{0.130f, 33, 50,162},{0.250f, 15, 90,145},{0.380f, 40,118,137},{0.500f, 59,146,135},{0.630f, 79,175,126},{0.750f,120,203,104},{0.880f,193,221,100},{1.000f,253,239,154}}};
const Palette temperature      { "temperature",      {{0.0f,   4, 35, 51},{0.130f, 23, 51,122},{0.250f, 85, 59,157},{0.380f,129, 79,143},{0.500f,175, 95,130},{0.630f,222,112,101},{0.750f,249,146, 66},{0.880f,249,196, 65},{1.000f,232,250, 91}}};
const Palette turbidity        { "turbidity",        {{0.0f,  34, 31, 27},{0.130f, 65, 50, 41},{0.250f, 98, 69, 52},{0.380f,131, 89, 57},{0.500f,161,112, 59},{0.630f,185,140, 66},{0.750f,202,174, 88},{0.880f,216,209,126},{1.000f,233,246,171}}};
const Palette velocity_blue    { "velocity-blue",    {{0.0f,  17, 32, 64},{0.130f, 35, 52,116},{0.250f, 29, 81,156},{0.380f, 31,113,162},{0.500f, 50,144,169},{0.630f, 87,173,176},{0.750f,149,196,189},{0.880f,203,221,211},{1.000f,254,251,230}}};
const Palette velocity_green   { "velocity-green",   {{0.0f,  23, 35, 19},{0.130f, 24, 64, 38},{0.250f, 11, 95, 45},{0.380f, 39,123, 35},{0.500f, 95,146, 12},{0.630f,152,165, 18},{0.750f,201,186, 69},{0.880f,233,216,137},{1.000f,255,253,205}}};
const Palette picnic           { "picnic",           {{0.0f,   0,  0,255},{0.100f, 51,153,255},{0.200f,102,204,255},{0.300f,153,204,255},{0.400f,204,204,255},{0.500f,255,255,255},{0.600f,255,204,255},{0.700f,255,153,255},{0.800f,255,102,204},{0.900f,255,102,102},{1.00f,255,  0,  0}}};
const Palette rainbow_soft     { "rainbow-soft",     {{0.0f, 125,  0,179},{0.100f,199,  0,180},{0.200f,255,  0,121},{0.300f,255,108,  0},{0.400f,222,194,  0},{0.500f,150,255,  0},{0.600f,  0,255, 55},{0.700f,  0,246,150},{0.800f, 50,167,222},{0.900f,103, 51,235},{1.00f,124,  0,186}}};
const Palette hsv              { "hsv",              {{0.0f, 255,  0,  0},{0.169f,253,255,  2},{0.173f,247,255,  2},{0.337f,  0,252,  4},{0.341f,  0,252, 10},{0.506f,  1,249,255},{0.671f,  2,  0,253},{0.675f,  8,  0,253},{0.839f,255,  0,251},{0.843f,255,  0,245},{1.00f,255,  0,  6}}};
const Palette cubehelix        { "cubehelix",        {{0.0f,   0,  0,  0},{0.070f, 22,  5, 59},{0.130f, 60,  4,105},{0.200f,109,  1,135},{0.270f,161,  0,147},{0.330f,210,  2,142},{0.400f,251, 11,123},{0.470f,255, 29, 97},{0.530f,255, 54, 69},{0.600f,255, 85, 46},{0.67f,255,120, 34},{0.73f,255,157, 37},{0.8f,241,191, 57},{0.87f,224,220, 93},{0.93f,218,241,142},{1.0f,227,253,198}}};

//const Palette alpha            { "alpha",            {{0, 255,255,255,0},{0,  255,255,255,1}}};

void Headset_renderer::render()
{
    ERHE_PROFILE_FUNCTION

    if (!m_headset)
    {
        return;
    }

    auto frame_timing = m_headset->begin_frame();
    if (frame_timing.should_render)
    {
        struct Context
        {
            erhe::graphics::OpenGL_state_tracker* pipeline_state_tracker;
            Configuration*                        configuration;
            Scene_builder*                        scene_builder;
            Line_renderer*                        line_renderer;
            Text_renderer*                        text_renderer;
        };
        Context context
        {
            get<erhe::graphics::OpenGL_state_tracker>(),
            get<Configuration>(),
            get<Scene_builder>(),
            get<Line_renderer>(),
            get<Text_renderer>()
        };

#if 0
        {
            ERHE_PROFILE_SCOPE("headset lines");

            auto* theremin_tool = get<Theremin_tool>();
            auto& line_renderer = context.line_renderer->hidden;
            constexpr uint32_t red       = 0xff0000ffu;
            constexpr uint32_t green     = 0xff00ff00u;
            constexpr uint32_t blue      = 0xffff0000u;
            constexpr float    thickness = 70.0f;
            auto* controller_node      = m_controller_visualization->get_node();
            auto  controller_position  = controller_node->position_in_world();
            auto  controller_direction = controller_node->direction_in_world();
            auto  end_position         = controller_position - m_headset->trigger_value() * 2.0f * controller_direction;
            const glm::vec3 origo {0.0f, 0.0f, 0.0f};
            const glm::vec3 unit_x{1.0f, 0.0f, 0.0f};
            const glm::vec3 unit_y{0.0f, 1.0f, 0.0f};
            const glm::vec3 unit_z{0.0f, 0.0f, 1.0f};
            line_renderer.set_line_color(red);
            line_renderer.add_lines({{origo, unit_x}}, thickness);
            line_renderer.set_line_color(green);
            line_renderer.add_lines({{origo, unit_y}}, thickness);
            line_renderer.set_line_color(blue);
            line_renderer.add_lines({{origo, unit_z}}, thickness);
            line_renderer.set_line_color(green);
            line_renderer.add_lines({{controller_position, end_position }}, thickness);

            auto*      view_camera  = get<Fly_camera_tool>()->camera();
            const auto transform    = view_camera->world_from_node();
            auto*      log_window   = get<Log_window>();
            const bool left_active  = m_headset->get_hand_tracking_active(XR_HAND_LEFT_EXT);
            const bool right_active = m_headset->get_hand_tracking_active(XR_HAND_RIGHT_EXT);
            log_window->frame_log("Hand tracking left - {}", left_active ? "active" : "inactive");

            std::optional<erhe::toolkit::Closest_points<float>> left_closest_point;
            std::optional<erhe::toolkit::Closest_points<float>> right_closest_point;
            if (left_active)
            {
                Hand_visualization left_hand;
                const glm::vec3 p0{-0.15f, 0.0f, 0.0f};
                const glm::vec3 p1{-0.15f, 2.0f, 0.0f};
                left_hand.update(*m_headset.get(), XR_HAND_LEFT_EXT);
                left_closest_point = left_hand.get_closest_point_to_line(transform, p0, p1);
                line_renderer.set_line_color(0xff8888ff);
                line_renderer.add_lines( { { p0, p1 } }, thickness );
                if (left_closest_point.has_value())
                {
                    const auto  P = left_closest_point.value().P;
                    const auto  Q = left_closest_point.value().Q;
                    const float d = glm::distance(P, Q);
                    const float s = 0.085f / d;
                    line_renderer.set_line_color(viridis.get(s));
                    line_renderer.add_lines( { { P, Q } }, thickness );
                    if (theremin_tool != nullptr)
                    {
                        theremin_tool->set_left_distance(d);
                    }
                }

                line_renderer.set_line_color(0xff0088ff);
                left_hand.draw_joint_line_strip(transform, thumb_joints,  line_renderer);
                left_hand.draw_joint_line_strip(transform, index_joints,  line_renderer);
                left_hand.draw_joint_line_strip(transform, middle_joints, line_renderer);
                left_hand.draw_joint_line_strip(transform, ring_joints,   line_renderer);
                left_hand.draw_joint_line_strip(transform, little_joints, line_renderer);
            }

            log_window->frame_log("Hand tracking right - {}", right_active ? "active" : "inactive");
            if (right_active)
            {
                Hand_visualization right_hand;
                const glm::vec3 p0{0.15f, 0.0f, 0.0f};
                const glm::vec3 p1{0.15f, 2.0f, 0.0f};
                right_hand.update(*m_headset.get(), XR_HAND_RIGHT_EXT);
                right_closest_point = right_hand.get_closest_point_to_line(transform, p0, p1);
                line_renderer.set_line_color(0xffff8888);
                line_renderer.add_lines( { { p0, p1 } }, thickness );
                if (right_closest_point.has_value())
                {
                    const auto  P = right_closest_point.value().P;
                    const auto  Q = right_closest_point.value().Q;
                    const float d = glm::distance(P, Q);
                    const float s = 0.085f / d;
                    line_renderer.set_line_color(temperature.get(s));
                    line_renderer.add_lines( { { P, Q } }, thickness );
                    if (theremin_tool != nullptr)
                    {
                        theremin_tool->set_right_distance(d);
                    }
                }

                line_renderer.set_line_color(0xffff8800);
                right_hand.draw_joint_line_strip(transform, thumb_joints,  line_renderer);
                right_hand.draw_joint_line_strip(transform, index_joints,  line_renderer);
                right_hand.draw_joint_line_strip(transform, middle_joints, line_renderer);
                right_hand.draw_joint_line_strip(transform, ring_joints,   line_renderer);
                right_hand.draw_joint_line_strip(transform, little_joints, line_renderer);
            }
        }
#endif

        auto callback = [this, &context](erhe::xr::Render_view& render_view) -> bool
        {
            auto& view_resources = get_headset_view_resources(render_view);
            if (!view_resources.is_valid)
            {
                return false;
            }
            view_resources.update(render_view, *m_editor_rendering);
            auto* framebuffer = view_resources.framebuffer.get();
            gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, framebuffer->gl_name());
            auto status = gl::check_named_framebuffer_status(framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
            if (status != gl::Framebuffer_status::framebuffer_complete)
            {
                log_headset.error("view framebuffer status = {}\n", c_str(status));
            }


            const erhe::scene::Viewport viewport
            {
                .x             = 0,
                .y             = 0,
                .width         = static_cast<int>(render_view.width),
                .height        = static_cast<int>(render_view.height),
                .reverse_depth = get<Configuration>()->reverse_depth
            };
            gl::viewport(0, 0, render_view.width, render_view.height);

            context.pipeline_state_tracker->shader_stages.reset();
            context.pipeline_state_tracker->color_blend.execute(&Color_blend_state::color_blend_disabled);
            gl::viewport(
                viewport.x,
                viewport.y,
                viewport.width,
                viewport.height
            );
            gl::enable(gl::Enable_cap::framebuffer_srgb);

            {
                ERHE_PROFILE_GPU_SCOPE("Headset clear")

                gl::clear_color(0.0f, 0.0f, 0.0f, 0.9f);
                gl::clear_depth_f(*context.configuration->depth_clear_value_pointer());
                gl::clear(
                    gl::Clear_buffer_mask::color_buffer_bit |
                    gl::Clear_buffer_mask::depth_buffer_bit
                );
            }

            if (!m_headset->squeeze_click())
            {
                Viewport_config viewport_config;
                Render_context render_context {
                    .window          = nullptr,
                    .viewport_config = &viewport_config,
                    .camera          = as_icamera(view_resources.camera.get()),
                    .viewport        = viewport
                };
                m_editor_rendering->render_content(render_context);

                if (m_line_renderer) // && m_headset->trigger_value() > 0.0f)
                {
                    ERHE_PROFILE_GPU_SCOPE("Headset line render")
                    m_line_renderer->render(viewport, *render_context.camera);
                }
            }

            return true;
        };
        m_headset->render(callback);
    }
    m_headset->end_frame();
}

void Headset_renderer::connect()
{
    m_application      = get<Application>();
    m_editor_rendering = get<Editor_rendering>();
    m_line_renderer    = get<Line_renderer   >();
    m_scene_builder    = require<Scene_builder>();
    m_scene_root       = require<Scene_root   >();

    require<Window>();
    require<Configuration>();
}

void Headset_renderer::initialize_component()
{
    if (!get<Configuration>()->openxr)
    {
        return;
    }

    m_headset = std::make_unique<erhe::xr::Headset>(get<Window>()->get_context_window());

    auto* mesh_memory = get<Mesh_memory>();
    auto* view_root   = get<Fly_camera_tool>()->get_camera();

    m_controller_visualization = std::make_unique<Controller_visualization>(
        *mesh_memory,
        *m_scene_root,
        view_root
    );
}

void Headset_renderer::begin_frame()
{
    if (m_controller_visualization && m_headset)
    {
        m_controller_visualization->update(m_headset->controller_pose());
    }
}

}