// Mcp_server tool list: name / description / input schema for every tool.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    // Query tools
    m_tool_infos.push_back({"list_scenes",         "List all scenes in the editor",                          schema_no_args()});
    m_tool_infos.push_back({"get_scene_nodes",     "List all nodes in a scene",                              schema_scene_name()});
    m_tool_infos.push_back({"get_node_details",    "Get detailed info for a node (transform, attachments)",  schema_scene_and_item("node_name", "Name of the node")});
    m_tool_infos.push_back({"get_scene_cameras",   "List all cameras in a scene",                            schema_scene_name()});
    m_tool_infos.push_back({"get_scene_lights",    "List all lights in a scene",                             schema_scene_name()});
    m_tool_infos.push_back({"get_scene_materials", "List all materials in a scene's content library",        schema_scene_name()});
    m_tool_infos.push_back({"get_material_details","Get detailed material properties",                       schema_scene_and_item("material_name", "Name of the material")});
    m_tool_infos.push_back({"get_scene_textures", "List all textures in a scene's content library",         schema_scene_name()});
    m_tool_infos.push_back({"get_scene_brushes",  "List all brushes in a scene's content library, each with its folder_path (slash-separated content-library folder hierarchy, '' = directly under the Brushes root)", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"scene_id",   {{"type", "integer"}, {"description", "Scene id (from list_scenes); use to disambiguate two scenes that share a name. Takes precedence over scene_name."}}}
        }}
    }});
    m_tool_infos.push_back({"get_viewports",        "List viewport windows: window title, bound scene name, and camera name. Use to verify which scene/camera each viewport shows (e.g. that opening a scene did not rebind pre-existing viewports).", schema_no_args()});
    m_tool_infos.push_back({"get_server_info",      "Get this editor MCP server's identity: name, version, process id (pid), build timestamp (compile time of the server), and bound port. Use it to detect a STALE editor: if the pid/build does not match the editor you just launched, another editor.exe is holding the port and your calls are hitting the wrong process.", schema_no_args()});
    m_tool_infos.push_back({"get_selection",        "Get currently selected items",                          schema_no_args()});
    m_tool_infos.push_back({"get_undo_redo_stack", "Get undo/redo operation stacks",                       schema_no_args()});
    m_tool_infos.push_back({"get_async_status",   "Get pending/running async operation counts",          schema_no_args()});
    m_tool_infos.push_back({"get_shadow_fit_debug","Dump directional shadow frustum fit debug geometry per shadow node: F_shadow planes, their bounded face quads (the truncated view-frustum faces caster AABBs are tested against), and receiver frustum corners. Needs the Shadow Fit 'Collect Debug' setting enabled.", schema_no_args()});
    m_tool_infos.push_back({"select_items",        "Select items by ID (scene nodes, materials, etc.). Mirrors UI selection semantics: replaces the selection within the target scene only (other scenes' selections persist; ids=[] clears the target scene only) and makes the target scene the active scene.",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to search for items"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Array of item IDs to select"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"get_active_scene",    "Get the active scene: the scene that commands targeting scene-hosted items act on (the transform gizmo, mesh operations, delete/cut/duplicate). Follows selection changes and window focus; null when no scene is active.", schema_no_args()});
    m_tool_infos.push_back({"set_active_scene",    "Make a scene the active scene, through the same activation path as focusing that scene's window in the UI (the gizmo rebinds to its selection, the window highlight moves, commands target it).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to activate"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"transform_selection", "Apply a Transform tool edit (translation / rotation / scale / skew) to the currently selected node(s), through the same code path as the Transform window numeric entry. space=local applies values in parent space (requires exactly one selected node); space=global applies in world (anchor) space. end_edit=true (default) records an undo operation and refreshes the edit baselines; end_edit=false keeps the edit session open like an active drag, so repeated calls re-apply against the same initial state.", {
        {"type", "object"},
        {"properties", {
            {"space",         {{"type", "string"},  {"description", "Edit space: 'local' or 'global' (default 'global')"}}},
            {"translation",   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation [x, y, z]"}}},
            {"rotation_xyzw", {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4}, {"description", "Rotation quaternion [x, y, z, w]"}}},
            {"scale",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Scale [x, y, z]"}}},
            {"skew",          {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Skew [x, y, z]"}}},
            {"end_edit",      {{"type", "boolean"}, {"description", "Record an undo operation and refresh edit baselines (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"place_brush",         "Place a brush instance in a scene at a given position", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"brush_id",      {{"type", "integer"}, {"description", "Brush ID (from get_scene_brushes)"}}},
            {"position",      {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "World position [x, y, z]"}}},
            {"material_name", {{"type", "string"},  {"description", "Material name (optional, uses first available if omitted)"}}},
            {"scale",         {{"type", "number"},  {"description", "Scale factor (optional, default 1.0)"}}},
            {"motion_mode",   {{"type", "string"},  {"description", "Physics mode: static, dynamic (optional, default dynamic)"}}}
        }},
        {"required", json::array({"scene_name", "brush_id", "position"})}
    }});

    m_tool_infos.push_back({"create_shape",        "Create a parametric shape using the same generators as the Create window, then place an instance in the scene and/or add the brush to the content library. Shape parameters: box (size [x,y,z], steps [x,y,z], power), uv_sphere (radius, slice_count, stack_count), cone (height, bottom_radius, top_radius, use_top, use_bottom, slice_count, stack_count), capsule (length, bottom_radius, top_radius, slice_count, stack_count; tapered when the radii differ, which requires length > |bottom_radius - top_radius|), torus (major_radius, minor_radius, major_steps, minor_steps).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the scene"}}},
            {"shape",          {{"type", "string"},  {"description", "Shape type: box, uv_sphere, cone, capsule or torus"}}},
            {"name",           {{"type", "string"},  {"description", "Brush / instance name (default: shape type)"}}},
            {"instance",       {{"type", "boolean"}, {"description", "Place an instance node in the scene (default true)"}}},
            {"add_brush",      {{"type", "boolean"}, {"description", "Add the brush to the content library for later place_brush use (default false)"}}},
            {"position",       {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] for the instance (default [0, 0, 0])"}}},
            {"parent_node_id", {{"type", "integer"}, {"description", "Parent node ID for the instance (default: scene root); the world position is preserved"}}},
            {"material_name",  {{"type", "string"},  {"description", "Material name (default: first available)"}}},
            {"scale",          {{"type", "number"},  {"description", "Uniform scale factor for the instance (default 1.0)"}}},
            {"motion_mode",    {{"type", "string"},  {"description", "Physics motion mode for the instance: static, kinematic, dynamic (default dynamic)"}}},
            {"size",           {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "box: size [x, y, z]"}}},
            {"steps",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "box: subdivision steps [x, y, z]"}}},
            {"power",          {{"type", "number"},  {"description", "box: power"}}},
            {"radius",         {{"type", "number"},  {"description", "uv_sphere: radius"}}},
            {"height",         {{"type", "number"},  {"description", "cone: height"}}},
            {"length",         {{"type", "number"},  {"description", "capsule: cylinder section length"}}},
            {"bottom_radius",  {{"type", "number"},  {"description", "cone / capsule: bottom radius"}}},
            {"top_radius",     {{"type", "number"},  {"description", "cone / capsule: top radius"}}},
            {"use_top",        {{"type", "boolean"}, {"description", "cone: generate top cap (default true)"}}},
            {"use_bottom",     {{"type", "boolean"}, {"description", "cone: generate bottom cap (default true)"}}},
            {"slice_count",    {{"type", "integer"}, {"description", "uv_sphere / cone / capsule: slice count"}}},
            {"stack_count",    {{"type", "integer"}, {"description", "uv_sphere / cone: stack count; capsule: hemisphere stack count"}}},
            {"major_radius",   {{"type", "number"},  {"description", "torus: major radius"}}},
            {"minor_radius",   {{"type", "number"},  {"description", "torus: minor radius"}}},
            {"major_steps",    {{"type", "integer"}, {"description", "torus: major steps"}}},
            {"minor_steps",    {{"type", "integer"}, {"description", "torus: minor steps"}}}
        }},
        {"required", json::array({"scene_name", "shape"})}
    }});
    m_tool_infos.push_back({"create_node",         "Create an empty scene node (undoable), optionally parented and positioned. Useful as a physics joint anchor: create_physics_joint captures its joint frames from the joint / connected node world transforms, so coincident anchor child nodes on the two bodies give a clean joint pivot.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new node (default 'new empty node')"}}},
            {"parent_node_id",   {{"type", "integer"}, {"description", "Parent node ID (default: scene root)"}}},
            {"parent_node_name", {{"type", "string"},  {"description", "Parent node name (alternative to parent_node_id)"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_light",        "Create a light (directional, point or spot) attached to a new scene-root node (undoable, inserted on the next frame). Point/spot lights default to range 25; directional to range 0.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "Light type (default directional)"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new light / node (default 'MCP light')"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB color [r, g, b] (default [1, 1, 1])"}}},
            {"intensity",        {{"type", "number"},  {"description", "Light intensity (default 1.0)"}}},
            {"range",            {{"type", "number"},  {"description", "Light range / far distance (default 25 for point/spot, 0 for directional)"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows (default true)"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians (spot only)"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians (spot only)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_light",          "Edit an existing light in place (by light_id or light_name). Changing 'type' re-buckets the light for shadow rendering. 'position' moves the light's node. Only the provided fields change.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"light_id",         {{"type", "integer"}, {"description", "ID of the light to edit (from get_scene_lights)"}}},
            {"light_name",       {{"type", "string"},  {"description", "Name of the light to edit (alternative to light_id)"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "New light type"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New world position [x, y, z] for the light's node"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New linear RGB color [r, g, b]"}}},
            {"intensity",        {{"type", "number"},  {"description", "New light intensity"}}},
            {"range",            {{"type", "number"},  {"description", "New light range / far distance"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_camera",         "Edit an existing camera in place (by camera_id or camera_name). 'exposure' scales scene content brightness in the standard shader; overlay meshes (transform gizmo, hotbar) deliberately ignore it. Only the provided fields change.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",   {{"type", "string"},  {"description", "Name of the scene"}}},
            {"camera_id",    {{"type", "integer"}, {"description", "ID of the camera to edit (from get_scene_cameras)"}}},
            {"camera_name",  {{"type", "string"},  {"description", "Name of the camera to edit (alternative to camera_id)"}}},
            {"exposure",     {{"type", "number"},  {"description", "New exposure value (1.0 = neutral)"}}},
            {"shadow_range", {{"type", "number"},  {"description", "New shadow range / far distance"}}},
            {"fov_y",        {{"type", "number"},  {"description", "New vertical field of view in radians (perspective_vertical cameras)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"toggle_physics",     "Toggle dynamic physics simulation on/off",              schema_no_args()});
    m_tool_infos.push_back({"add_node_attachment", "Add a new attachment to a scene node (undoable, executes on the next frame). 'type' is a catalog key: camera, light, mesh, rigid_body, joint, layout, layout_item, grid, frame_controller. All kinds except joint are single-instance (refused if the node already has one). layout_item requires the node's parent to have a layout. Verify with get_node_details.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Target node ID"}}},
            {"node_name",  {{"type", "string"},  {"description", "Target node name (alternative to node_id)"}}},
            {"type",       {{"type", "string"},  {"enum", json::array({"camera", "light", "mesh", "rigid_body", "joint", "layout", "layout_item", "grid", "frame_controller"})}, {"description", "Attachment catalog key"}}}
        }},
        {"required", json::array({"scene_name", "type"})}
    }});
    m_tool_infos.push_back({"remove_node_attachment", "Remove an attachment from a scene node (undoable pure detach, executes on the next frame). Identify the attachment by attachment_id, or by 'type' = its attachment type name as reported by get_node_details (e.g. Camera, Light, Mesh, Node_physics, Grid, Layout), case-insensitive. Any attachment is removable, including ones not in the add catalog.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "Target node ID"}}},
            {"node_name",     {{"type", "string"},  {"description", "Target node name (alternative to node_id)"}}},
            {"attachment_id", {{"type", "integer"}, {"description", "ID of the attachment to remove (from get_node_details)"}}},
            {"type",          {{"type", "string"},  {"description", "Attachment type name to remove (alternative to attachment_id)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"reparent_node",     "Set a node's parent (by node IDs)",                    {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "ID of the node to reparent"}}},
            {"parent_node_id",{{"type", "integer"}, {"description", "ID of the new parent node (0 for scene root)"}}}
        }},
        {"required", json::array({"scene_name", "node_id"})}
    }});
    m_tool_infos.push_back({"lock_items",         "Lock items by ID (prevents deletion/modification)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to lock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"unlock_items",       "Unlock items by ID",                                  {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to unlock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"add_tags",           "Add tags to items by ID",                             {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to tag"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to add"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    m_tool_infos.push_back({"remove_tags",        "Remove tags from items by ID",                        {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to remove"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    json texture_slot_schema = {
        {"type", "object"},
        {"properties", {
            {"texture",   {{"description", "Texture: string (name in content library), integer (texture id), or null to clear. Omit to keep current."}}},
            {"tex_coord", {{"type", "integer"}, {"minimum", 0},  {"description", "UV channel index (e.g. 0 for TEXCOORD_0)"}}},
            {"rotation",  {{"type", "number"}, {"description", "UV rotation in radians"}}},
            {"offset",    {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV offset [u, v]"}}},
            {"scale",     {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV scale [u, v]"}}}
        }}
    };
    m_tool_infos.push_back({"edit_material",      "Edit material properties including texture assignments (undoable). Only fields supplied are changed.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",                 {{"type", "string"},  {"description", "Name of the scene"}}},
            {"material_name",              {{"type", "string"},  {"description", "Name of the material to edit"}}},
            {"base_color",                 {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB base color [r, g, b]"}}},
            {"opacity",                    {{"type", "number"},  {"description", "Opacity in [0, 1]"}}},
            {"roughness",                  {{"description", "Roughness; either [x, y] for anisotropic or a single number applied to both."}}},
            {"metallic",                   {{"type", "number"},  {"description", "Metallic factor in [0, 1]"}}},
            {"reflectance",                {{"type", "number"},  {"description", "Dielectric reflectance in [0, 1]"}}},
            {"emissive",                   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB emissive [r, g, b]"}}},
            {"normal_texture_scale",       {{"type", "number"},  {"description", "Normal map scale"}}},
            {"occlusion_texture_strength", {{"type", "number"},  {"description", "Occlusion map strength"}}},
            {"bxdf_model",                 {{"type", "string"},  {"enum", json::array({"unlit", "isotropic_brdf", "anisotropic_brdf", "anisotropic_slope", "anisotropic_engine_ready"})}, {"description", "Selects which BxDF the standard shader applies"}}},
            {"use_circular_brushed_metal", {{"type", "boolean"}, {"description", "Enable circular brushed metal shading variant"}}},
            {"use_aniso_control",          {{"type", "boolean"}, {"description", "Enable anisotropic shading control"}}},
            {"texture_samplers",           {
                {"type", "object"},
                {"description", "Per-slot texture assignments. Textures must come from the scene's content library (use get_scene_textures to list). Each slot holds a single texture reference, so assigning or clearing a texture also replaces/clears any Graph Texture binding on that slot."},
                {"properties", {
                    {"base_color",         texture_slot_schema},
                    {"metallic_roughness", texture_slot_schema},
                    {"normal",             texture_slot_schema},
                    {"occlusion",          texture_slot_schema},
                    {"emissive",           texture_slot_schema}
                }}
            }}
        }},
        {"required", json::array({"scene_name", "material_name"})}
    }});

    m_tool_infos.push_back({"copy_library_item", "Copy a content-library item from one scene's library to another's. Copies never alias: the copy is a fresh item owned by the target scene (brushes share their expensive payload by reference). Name collisions in the target get a ' (N)' suffix. Textures and graph assets cannot be copied; a copied material keeps its texture references into the source scene's textures (they render, but do not export with the target scene). Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"item_name",    {{"type", "string"}, {"description", "Name of the item to copy (must be unique within its folder)"}}},
            {"item_type",    {{"type", "string"}, {"enum", json::array({"material", "brush", "physics_material", "collision_filter", "physics_joint"})}, {"description", "Item category; default material"}}},
            {"source_scene", {{"type", "string"}, {"description", "Scene whose library holds the item"}}},
            {"target_scene", {{"type", "string"}, {"description", "Scene whose library receives the copy"}}}
        }},
        {"required", json::array({"item_name", "source_scene", "target_scene"})}
    }});

    m_tool_infos.push_back({"get_scene_settings", "Get a scene's per-scene state: ambient light color, physics enable, and the per-scene setting overrides (#239; null when every field uses the editor-global default, else the Scene_settings object: sky, grid, physics, shadow_frustum_fit, camera_controls, clear_color, post_processing).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"set_scene_settings", "Set a scene's per-scene state: ambient light color and/or the per-scene setting overrides (#239). 'settings' REPLACES the whole Scene_settings object (omitted fields return to the editor-global default; {} clears every override). These persist in the ERHE_scene extension when the scene is saved.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ambient_light", {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 4}, {"description", "Ambient light color [r, g, b] or [r, g, b, a]"}}},
            {"settings",      {{"type", "object"}, {"description", "Scene_settings object (same shape get_scene_settings returns): optional sky, grid, physics, shadow_frustum_fit, camera_controls, clear_color ([r,g,b,a]), post_processing (bool)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"save_scene",         "Save a scene as a single erhe-authored glTF file carrying FULL editor state (render content plus the ERHE_* extensions: scene settings, physics, layouts, brushes, node graphs, collections/tags; ERHE_scene in extensionsUsed marks the file), without a file dialog. Without 'path' the scene saves back to its own source file when it was opened/loaded from one, else to res/editor/scenes/<scene name>.glb. When the written file is a loaded prefab source, the prefab is reloaded so every instance in every scene reflects the edit (this subsumed the former save_prefab tool). This is the scene persistence path; export_gltf without editor_state is the plain interchange export.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"path",       {{"type", "string"}, {"description", "Destination file path (default: the scene's source file, else res/editor/scenes/<scene name>.glb); .glb is appended when the extension is neither .glb nor .gltf (.gltf selects the text form)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"load_scene",         "Load a saved scene, without a file dialog: an erhe-authored glTF file (saved by save_scene) opens as a full scene with its saved editor state (fresh content library, browser + viewport windows; not undoable); a foreign glTF opens as a new scene via the same path as open_scene. The load is queued and completes on a following frame; discover the scene via list_scenes.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .glb/.gltf scene file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"open_scene",         "Open a FOREIGN glTF file as a new scene (same as the Asset Browser's Open context menu entry): creates a scene root + content library + browser window + a new viewport window showing the scene, and imports the file, all as a single undoable operation. Existing viewport windows are not modified. The open is queued and completes on a following frame; discover the scene via list_scenes (named after the file name). For an erhe-authored scene file, prefer load_scene, which also applies the saved editor state.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"close_scene",        "Close a scene: destroys its viewport and browser windows and unregisters it from the editor (same as the Scene row's Close context menu entry). The close is queued and completes on a following frame.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to close"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_scene",       "Create a new, empty scene with its own camera, viewport and content library (same as the Create > Scene menu command). The new scene's content library is populated with the standard brushes shared from the default scene. The creation is queued and completes on a following frame; discover the new scene name (\"Scene N\") via list_scenes.", schema_no_args()});
    m_tool_infos.push_back({"export_gltf",        "Export a scene to a glTF file, without a file dialog. By default a plain interchange export; with editor_state=true the file additionally carries the editor-domain ERHE_* extensions (ERHE_scene settings, ERHE_physics, ERHE_layout, ERHE_brushes, ERHE_node_graphs, ERHE_collections) for full scene persistence, and baked graph-mesh products are excluded (rebuilt on load).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",   {{"type", "string"},  {"description", "Name of the scene"}}},
            {"path",         {{"type", "string"},  {"description", "Destination file path"}}},
            {"binary",       {{"type", "boolean"}, {"description", "Write binary .glb instead of text .gltf (default true)"}}},
            {"editor_state", {{"type", "boolean"}, {"description", "Include editor-domain ERHE_* extensions for full scene persistence (default false = plain interchange export)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"import_gltf",        "Import a glTF file into an existing scene", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the destination scene"}}},
            {"path",       {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"instantiate_prefab", "Instantiate a glTF file as a prefab into a scene: the file is parsed once (cached app-wide) and inserted as a clone that stays a reference to the source file. Instances share GPU buffers; insertion is undoable.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the destination scene"}}},
            {"path",       {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}},
            {"position",   {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] of the instance root (default origin)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"reload_prefab",      "Re-parse a loaded prefab from its source glTF file and refresh every instance in every scene (prefabs whose templates reference it are rebuilt too, in dependency order). Carrier node transforms are preserved. Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .gltf/.glb file path of a loaded prefab"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"get_prefabs",        "List the glTF prefabs currently loaded in the app-wide prefab library (source path, name, content counts)", schema_no_args()});
    m_tool_infos.push_back({"capture_screenshot",  "Capture the current rendered frame to a PNG file and return its path. Currently supported only in the headless Vulkan configuration (emulated swapchain).", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Output PNG path (default logs/mcp_screenshot.png)"}}}
        }}
    }});
    m_tool_infos.push_back({"wake_physics_bodies", "Activate all dynamic rigid bodies in a scene (bodies enter the world deactivated)", schema_scene_name()});

    m_tool_infos.push_back({"get_physics_items", "List the shared physics content-library items (physics materials, collision filters, joint settings) with full properties", schema_scene_name()});

    const json node_ref_properties = {
        {"node_id",   {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
        {"node_name", {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
    };
    const json shape_properties = {
        {"shape",         {{"type", "string"}, {"enum", json::array({"auto", "box", "sphere", "capsule", "tapered_capsule", "cylinder", "tapered_cylinder", "convex_hull", "mesh"})}, {"description", "Collision shape; auto (default) = convex hull from the node's mesh, unit box when the node has no mesh. mesh shapes are static/kinematic only."}}},
        {"half_extents",  {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Box / cylinder half extents [x, y, z] (default [0.5, 0.5, 0.5])"}}},
        {"radius",        {{"type", "number"}, {"description", "Sphere / capsule radius (default 0.5)"}}},
        {"bottom_radius", {{"type", "number"}, {"description", "Tapered capsule / cylinder bottom radius (default 0.5)"}}},
        {"top_radius",    {{"type", "number"}, {"description", "Tapered capsule / cylinder top radius (default 0.5)"}}},
        {"length",        {{"type", "number"}, {"description", "Capsule / tapered shape axial length (default 1.0)"}}},
        {"axis",          {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}, {"description", "Shape axis (default y)"}}}
    };
    const json body_properties = {
        {"motion_mode",      {{"type", "string"}, {"enum", json::array({"static", "kinematic", "kinematic_non_physical", "dynamic"})}, {"description", "Motion mode (default dynamic; kinematic = kinematic physical)"}}},
        {"mass",             {{"type", "number"}, {"description", "Mass; 0 = spec infinite-mass convention; omitted = derived from the shape"}}},
        {"friction",         {{"type", "number"}, {"description", "Scalar friction (overridden by a physics material)"}}},
        {"restitution",      {{"type", "number"}, {"description", "Scalar restitution (overridden by a physics material)"}}},
        {"linear_damping",   {{"type", "number"}, {"description", "Linear damping"}}},
        {"angular_damping",  {{"type", "number"}, {"description", "Angular damping"}}},
        {"gravity_factor",   {{"type", "number"}, {"description", "Gravity factor (default 1.0)"}}},
        {"is_trigger",       {{"type", "boolean"}, {"description", "Create as a sensor / trigger volume"}}},
        {"linear_velocity",  {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial linear velocity [x, y, z] (world space, applied at body creation)"}}},
        {"angular_velocity", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial angular velocity [x, y, z] (world space, applied at body creation)"}}},
        {"center_of_mass",   {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Center of mass offset [x, y, z]"}}},
        {"material_name",    {{"type", "string"}, {"description", "Physics material name from the content library (empty string clears)"}}},
        {"filter_name",      {{"type", "string"}, {"description", "Collision filter name from the content library (empty string clears)"}}}
    };
    json create_body_properties = {{"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}};
    create_body_properties.update(node_ref_properties);
    create_body_properties.update(shape_properties);
    create_body_properties.update(body_properties);
    m_tool_infos.push_back({"create_physics_body", "Attach a new rigid body (Node_physics) to a scene node (undoable). One rigid body per node.", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_physics_body", "Edit the rigid body attached to a scene node. Only fields supplied are changed; shape fields replace the collision shape (recreates the body).", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});

    json joint_properties = {
        {"scene_name",          {{"type", "string"},  {"description", "Name of the scene"}}},
        {"connected_node_id",   {{"type", "integer"}, {"description", "Connected node ID (no connected node = constrain to the world)"}}},
        {"connected_node_name", {{"type", "string"},  {"description", "Connected node name"}}},
        {"settings_name",       {{"type", "string"},  {"description", "Physics joint settings name from the content library (empty = free six-dof joint)"}}},
        {"enable_collision",    {{"type", "boolean"}, {"description", "Keep collision enabled between the joined bodies (default false)"}}}
    };
    joint_properties.update(node_ref_properties);
    m_tool_infos.push_back({"create_physics_joint", "Attach a new joint (Node_joint) to a scene node (undoable): joins the nearest self-or-ancestor rigid body of the node to that of the connected node (or the world)", {
        {"type", "object"},
        {"properties", joint_properties},
        {"required", json::array({"scene_name"})}
    }});
    json edit_joint_properties = joint_properties;
    edit_joint_properties.update(json{
        {"joint_index",      {{"type", "integer"}, {"description", "Index among the node's joint attachments (default 0)"}}},
        {"connect_to_world", {{"type", "boolean"}, {"description", "Clear the connected node (constrain to the world)"}}},
        {"rebuild",          {{"type", "boolean"}, {"description", "Rebuild the constraint, re-capturing joint frames from current node transforms"}}}
    });
    m_tool_infos.push_back({"edit_physics_joint", "Edit a joint attached to a scene node. Only fields supplied are changed; changes rebuild the constraint.", {
        {"type", "object"},
        {"properties", edit_joint_properties},
        {"required", json::array({"scene_name"})}
    }});

    const json physics_material_value_properties = {
        {"static_friction",     {{"type", "number"}, {"description", "Static friction (default 0.6)"}}},
        {"dynamic_friction",    {{"type", "number"}, {"description", "Dynamic friction (default 0.6)"}}},
        {"restitution",         {{"type", "number"}, {"description", "Restitution (default 0.0)"}}},
        {"friction_combine",    {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Friction combine mode"}}},
        {"restitution_combine", {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Restitution combine mode"}}}
    };
    json create_physics_material_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new physics material (must not already exist)"}}}
    };
    create_physics_material_properties.update(physics_material_value_properties);
    m_tool_infos.push_back({"create_physics_material", "Create a shared physics material in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_physics_material_properties = create_physics_material_properties;
    edit_physics_material_properties["name"] = {{"type", "string"}, {"description", "Name of the physics material to edit"}};
    edit_physics_material_properties["new_name"] = {{"type", "string"}, {"description", "Rename the physics material"}};
    m_tool_infos.push_back({"edit_physics_material", "Edit a shared physics material; changes re-apply to all bodies using it. Only fields supplied are changed.", {
        {"type", "object"},
        {"properties", edit_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json collision_filter_value_properties = {
        {"collision_systems",        {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Systems the body belongs to"}}},
        {"collide_with_systems",     {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Non-empty = collide only with these systems (allowlist)"}}},
        {"not_collide_with_systems", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Used when collide_with_systems is empty: never collide with these"}}}
    };
    json create_collision_filter_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new collision filter (must not already exist)"}}}
    };
    create_collision_filter_properties.update(collision_filter_value_properties);
    m_tool_infos.push_back({"create_collision_filter", "Create a shared collision filter in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_collision_filter_properties = create_collision_filter_properties;
    edit_collision_filter_properties["name"] = {{"type", "string"}, {"description", "Name of the collision filter to edit"}};
    edit_collision_filter_properties["new_name"] = {{"type", "string"}, {"description", "Rename the collision filter"}};
    m_tool_infos.push_back({"edit_collision_filter", "Edit a shared collision filter; system lists supplied replace the existing lists and re-apply to all bodies using the filter", {
        {"type", "object"},
        {"properties", edit_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json joint_settings_value_properties = {
        {"limits", {
            {"type", "array"},
            {"description", "Per-axis limit entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"linear_axes",  {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation axes [x, y, z] this limit applies to"}}},
                    {"angular_axes", {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Rotation axes [x, y, z] this limit applies to"}}},
                    {"min",          {{"type", "number"}, {"description", "Lower bound (absent = unbounded)"}}},
                    {"max",          {{"type", "number"}, {"description", "Upper bound (absent = unbounded; min == max fixes the axis)"}}},
                    {"stiffness",    {{"type", "number"}, {"description", "Soft limit spring stiffness (absent = hard limit)"}}},
                    {"damping",      {{"type", "number"}, {"description", "Soft limit damping"}}}
                }}
            }}
        }},
        {"drives", {
            {"type", "array"},
            {"description", "Drive (motor) entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"type",            {{"type", "string"},  {"enum", json::array({"linear", "angular"})}}},
                    {"mode",            {{"type", "string"},  {"enum", json::array({"force", "acceleration"})}}},
                    {"axis",            {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
                    {"max_force",       {{"type", "number"},  {"description", "Maximum force (absent = unlimited)"}}},
                    {"position_target", {{"type", "number"}}},
                    {"velocity_target", {{"type", "number"}}},
                    {"stiffness",       {{"type", "number"},  {"description", "> 0 selects a position motor, else a velocity motor"}}},
                    {"damping",         {{"type", "number"}}}
                }}
            }}
        }}
    };
    json create_joint_settings_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new joint settings (must not already exist)"}}}
    };
    create_joint_settings_properties.update(joint_settings_value_properties);
    m_tool_infos.push_back({"create_physics_joint_settings", "Create shared physics joint settings (limits + drives) in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_joint_settings_properties = create_joint_settings_properties;
    edit_joint_settings_properties["name"] = {{"type", "string"}, {"description", "Name of the joint settings to edit"}};
    edit_joint_settings_properties["new_name"] = {{"type", "string"}, {"description", "Rename the joint settings"}};
    m_tool_infos.push_back({"edit_physics_joint_settings", "Edit shared physics joint settings; limits / drives arrays supplied replace the existing ones and all joints using the settings are rebuilt", {
        {"type", "object"},
        {"properties", edit_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    // Mesh component (vertex / edge / face) selection, used by Align and Add Joint.
    const json component_mode_property = {
        {"mode", {{"type", "string"}, {"enum", json::array({"object", "vertex", "edge", "face"})}, {"description", "Component granularity: object, vertex, edge or face"}}}
    };
    m_tool_infos.push_back({"set_mesh_component_mode", "Set the mesh-component selection granularity (object / vertex / edge / face). vertex/edge/face are required before select_mesh_components and Align / Add Joint.", {
        {"type", "object"},
        {"properties", component_mode_property},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"select_mesh_components", "Select sub-components (vertices / edges / faces) of a node's mesh, addressed by Geogram indices into its render geometry. extend=false (default) replaces the whole component selection first; extend=true accumulates (use it to add a second node's component for Align / Add Joint). Indices are validated against the geometry. Edges are [v0, v1] vertex-index pairs.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"mode",            {{"type", "string"},  {"enum", json::array({"object", "vertex", "edge", "face"})}, {"description", "Set the component mode before selecting (default: keep current)"}}},
            {"extend",          {{"type", "boolean"}, {"description", "Accumulate instead of replacing the whole selection (default false)"}}},
            {"vertices",        {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Vertex indices to select"}}},
            {"edges",           {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "Edges as [v0, v1] vertex-index pairs"}}},
            {"facets",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Facet (polygon) indices to select"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_mesh_component_selection", "Get the current mesh-component selection: mode plus each entry's node, primitive index, selected vertices / edges / facets, and whether it is live.", schema_no_args()});
    m_tool_infos.push_back({"grow_mesh_selection", "Blender Select More: grow the current mesh-component selection by one border ring, in the active mode (vertex / edge / face). No-op in object mode. Returns the resulting selection (same shape as get_mesh_component_selection).", schema_no_args()});
    m_tool_infos.push_back({"shrink_mesh_selection", "Blender Select Less: shrink the current mesh-component selection by dropping the components on its border, in the active mode (vertex / edge / face). No-op in object mode. Returns the resulting selection (same shape as get_mesh_component_selection).", schema_no_args()});
    m_tool_infos.push_back({"get_id_range_mapping", "Report the GPU ID-buffer range mapping from the most recently resolved region scan: for each drawn primitive, its id_offset, length (index count), triangle_count, base_vertex, and the owning mesh/node/primitive_index. A decoded pixel id in [id_offset, id_offset+length) selects that primitive and (id - id_offset) is its 0-based facet index. Run a box/paint select (or debug_region_select) first to populate it.", schema_no_args()});
    m_tool_infos.push_back({"debug_region_select", "Debug/test: drive a region face-select (box or paint brush) over an explicit viewport-pixel rectangle, bypassing the mouse. Forces Face component mode, requests a GPU id-buffer scan, and commits the visible faces a few frames later (poll get_mesh_component_selection afterwards). x,y,width,height are in viewport pixels; is_brush masks to a disk of brush_radius centered in the rect.", {
        {"type", "object"},
        {"properties", {
            {"x",            {{"type", "integer"}, {"description", "Rectangle left in viewport pixels"}}},
            {"y",            {{"type", "integer"}, {"description", "Rectangle top in viewport pixels"}}},
            {"width",        {{"type", "integer"}, {"description", "Rectangle width in viewport pixels"}}},
            {"height",       {{"type", "integer"}, {"description", "Rectangle height in viewport pixels"}}},
            {"is_brush",     {{"type", "boolean"}, {"description", "Mask the rectangle to a centered disk (paint brush), default false"}}},
            {"brush_radius", {{"type", "number"},  {"description", "Disk radius in pixels when is_brush is true"}}},
            {"replace",      {{"type", "boolean"}, {"description", "Clear the selection before adding (default true)"}}},
            {"subtract",     {{"type", "boolean"}, {"description", "Remove the scanned faces instead of adding (default false)"}}}
        }},
        {"required", json::array({"x", "y", "width", "height"})}
    }});
    m_tool_infos.push_back({"get_mesh_geometry_info", "Inspect a node's render geometry: element counts (vertices, edges, facets, corners) and, per domain (facet / vertex / corner), the list of attributes that are present (name, GEO type, and how many elements carry the attribute). Use this before get_mesh_attribute_values to learn what to query. Note: a flat-shaded mesh stores per-corner normals (corner_normal); a smooth mesh stores per-vertex normals.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_mesh_attribute_values", "Read attribute presence and values for specific mesh elements of a node's render geometry. Pick a domain (vertex / corner / facet / edge) and pass the element indices; each element reports its per-attribute {present, value}. vertex elements also report position; corner elements report their vertex + facet; facet elements report their corner + vertex lists; edge elements report endpoint vertices + adjacent facets plus edge_sharpness when present (see set_edge_sharpness). Optionally restrict to named attributes; default returns all attributes of the domain.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"domain",          {{"type", "string"},  {"enum", json::array({"vertex", "corner", "facet", "edge"})}, {"description", "Which element domain the indices address"}}},
            {"indices",         {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Element indices to read (max 4096)"}}},
            {"attributes",      {{"type", "array"},   {"items", {{"type", "string"}}}, {"description", "Optional attribute-name filter (e.g. [\"corner_normal\"]); default: all attributes in the domain"}}}
        }},
        {"required", json::array({"scene_name", "domain", "indices"})}
    }});
    m_tool_infos.push_back({"clear_mesh_component_selection", "Clear the entire mesh-component selection", schema_no_args()});
    m_tool_infos.push_back({"set_edge_sharpness", "Set (or clear) the semi-sharp crease sharpness (edge_sharpness attribute) of mesh edges; Catmull-Clark subdivision then applies the sharp rules for sharpness levels (fractional part blends, decays by ~1 per level, \"infinity\" never smooths). Targets explicit [v0, v1] edge pairs, or the current edge component selection on the geometry when edges is omitted. Undoable; queued (barrier: any query). Read values back with get_mesh_attribute_values domain=edge.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"edges",           {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "Edges as [v0, v1] vertex-index pairs; omit to use the current edge component selection"}}},
            {"sharpness",       {{"description", "Sharpness value >= 0 (number), or the string \"infinity\" for an infinitely sharp crease"}}},
            {"clear",           {{"type", "boolean"}, {"description", "Remove the sharpness values (back to smooth) instead of setting one (default false)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"align_components", "Align the two selected mesh components (of the active vertex/edge/face mode) on two distinct nodes: colocate vertices, align edges, or glue faces. apply_scale also matches scale (edge/face only). Requires exactly two components selected on two distinct nodes. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"apply_scale", {{"type", "boolean"}, {"description", "Also apply uniform scale to match the components (edge/face modes; default false)"}}}
        }}
    }});
    m_tool_infos.push_back({"add_joint", "Align the two selected mesh components, then create a physics joint between the two nodes' rigid bodies (ball for vertex, hinge for edge/face). Both nodes must already have a rigid body. Searches the joint's free rotational DOF for a non-intersecting placement; fails if none is found. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the initial-orientation search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"flip_joint", "Flip the hinge joint that the selected node is a rigid-body party of: reorients that body by a 180-degree edge-endpoints-swapped flip plus a non-intersecting roll, re-pins its joint frame, and rebuilds the constraint. Select a rigid-body party of a hinge joint first. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the roll search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"get_physics_state", "Get the live physics motion state of a node's rigid body: motion mode, active flag, world position, and linear / angular velocity (with magnitudes). Poll across frames to observe how a body reacts after an operation.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",  {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    // Geogram mesh operations - act on the object selection (set via select_items).
    m_tool_infos.push_back({"remesh", "Geogram isotropic / anisotropic remesh of the selected mesh node(s) to a target vertex count (queued, runs over subsequent frames - poll get_async_status). anisotropy=0 (default) is isotropic; anisotropy>0 (e.g. 0.04) is anisotropic. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"target_vertex_count",   {{"type", "integer"}, {"description", "Target vertex count (Geogram nb_points, default 2000)"}}},
            {"anisotropy",            {{"type", "number"},  {"description", "0 = isotropic (default); >0 = anisotropic strength (e.g. 0.04)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"decimate", "Geogram vertex-clustering decimation of the selected mesh node(s) (queued). bins is the clustering grid resolution (higher = more detail kept). Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"bins",                  {{"type", "integer"}, {"description", "Vertex-clustering grid resolution (default 50)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"smooth", "Geogram Laplacian smoothing of the selected mesh node(s) (queued). Vertex count is preserved. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"iterations",            {{"type", "integer"}, {"description", "Smoothing iterations (default 5)"}}},
            {"strength",              {{"type", "number"},  {"description", "Smoothing strength [0,1] (default 0.5)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"chamfer", "Conway chamfer of the selected mesh node(s) (queued): shrink each facet toward its centroid by bevel_ratio and replace every edge with a hexagonal face. When a FACE-mode mesh-component selection is active, only the selected facets are chamfered and the result stays welded watertight to the rest of the mesh (selected facets inset, interior shared edges become hexagons, boundary edges become bevel quads); otherwise the whole mesh is chamfered. Acts on the current object or face-component selection.", {
        {"type", "object"},
        {"properties", {
            {"bevel_ratio", {{"type", "number"}, {"description", "How much each face shrinks toward its centroid, [0,1] (default 0.25)"}}}
        }}
    }});
    m_tool_infos.push_back({"catmull_clark", "Apply one level of Catmull-Clark subdivision to the selected mesh node(s) (select_items first). Honors the per-edge edge_sharpness crease attribute (see set_edge_sharpness): sharp rules for sharpness levels, fractional blend, child edges carry decremented sharpness. Queued and undoable.", schema_no_args()});
    m_tool_infos.push_back({"merge_faces", "Merge (dissolve) the selected facets of the mesh node(s) into one polygon per edge-connected group: facets connected through a shared EDGE (not merely a shared vertex) become a single polygon spanning their boundary loop, dropping the now-interior edges and vertices. Requires an active FACE-mode mesh-component selection (set_mesh_component_mode face + select_mesh_components). A group whose boundary is not a single simple loop (encloses a hole / pinches) is left unchanged. Queued; the rest of the mesh stays watertight.", schema_no_args()});
    m_tool_infos.push_back({"generate_texture_coordinates", "Generate texture coordinates for the selected mesh node(s) via Geogram mesh_make_atlas (queued). Writes UVs into the given corner texcoord channel. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"texcoord_slot",   {{"type", "integer"}, {"description", "Target corner texcoord channel 0 or 1 (default 0)"}}},
            {"hard_angles_deg", {{"type", "number"},  {"description", "Hard-angle threshold in degrees for chart seams (default 45)"}}},
            {"parameterizer",   {{"type", "integer"}, {"description", "Atlas_parameterizer enum index (default 3 = ABF)"}}},
            {"packer",          {{"type", "integer"}, {"description", "Atlas_packer enum index (default 2 = XAtlas)"}}}
        }}
    }});

    // Transform reference frame for the transform gizmo / numeric edits.
    m_tool_infos.push_back({"set_transform_reference_mode", "Set the orientation reference frame (transform space) of the transform tool: global/world (world axes), local (selection's own orientation), reference (a chosen reference node's orientation), or selection (a frame derived from the active mesh-component selection). For reference mode, give reference_node_id or reference_node_name (searched across scenes, or within scene_name when given) - this is how you set the reference. edge_normal_blend tunes the selection-mode frame. Read the current state back with get_transform_state.", {
        {"type", "object"},
        {"properties", {
            {"mode",                {{"type", "string"}, {"enum", json::array({"global", "world", "local", "reference", "selection"})}, {"description", "Transform space / reference mode ('world' is an alias for 'global')"}}},
            {"scene_name",          {{"type", "string"},  {"description", "Scene to resolve reference_node in (optional; otherwise all scenes are searched)"}}},
            {"reference_node_id",   {{"type", "integer"}, {"description", "Reference-mode node ID (sets the reference)"}}},
            {"reference_node_name", {{"type", "string"},  {"description", "Reference-mode node name (sets the reference)"}}},
            {"edge_normal_blend",   {{"type", "number"},  {"description", "Selection mode: [0,1] blend between the two faces sharing a selected edge"}}}
        }},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"get_transform_state", "Read the transform tool's current state: reference frame / transform space (reference_mode: global/local/reference/selection), the chosen reference_node (for reference mode), edge_normal_blend (selection mode), the mesh transform_mode (move/extrude/...), whether a mesh-component selection is driving the gizmo (component_mode), the selected node count, and the resolved world-space anchor_frame (origin + orientation the gizmo and local-space numeric edits operate in). Complements set_transform_reference_mode / set_transform_mode.", schema_no_args()});
    m_tool_infos.push_back({"set_transform_mode", "Set what the transform tool does to a mesh-component (vertex/edge/face) selection when the gizmo is dragged: move (translate/rotate/scale in place), extrude (duplicate the selection boundary, bridge it with new faces, then move along the gizmo delta), extrude_group_normal (same topology, but each disjoint selection subset slides along its own average normal by an amount derived from the drag), or extrude_vertex_normal (same topology, but each vertex slides along its own normal). Persisted in editor settings; applies to subsequent component edits.", {
        {"type", "object"},
        {"properties", {
            {"mode", {{"type", "string"}, {"enum", json::array({"move", "extrude", "extrude_group_normal", "extrude_vertex_normal"})}, {"description", "Mesh transform mode"}}}
        }},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"set_gizmo_visibility", "Show or hide the transform gizmo handle sets (translate arrows, rotate rings, scale handles) and choose the scale gizmo style (basic axis/plane/uniform handles, or bounding_box face cones). This is the scriptable equivalent of activating the Move/Rotate/Scale tool from the hotbar or toggling the viewport-toolbar gizmo buttons, which are otherwise mouse-only. Omitted flags keep their current value; returns the resulting state.", {
        {"type", "object"},
        {"properties", {
            {"translate",        {{"type", "boolean"}, {"description", "Show the translate handles"}}},
            {"rotate",           {{"type", "boolean"}, {"description", "Show the rotate rings"}}},
            {"scale",            {{"type", "boolean"}, {"description", "Show the scale handles"}}},
            {"scale_gizmo_mode", {{"type", "string"}, {"enum", json::array({"basic", "bounding_box"})}, {"description", "Scale gizmo style"}}}
        }}
    }});

    // Geometry node graph (Geometry Graph window)
    m_tool_infos.push_back({"get_geometry_graph", "List the geometry node graph the Geometry Graph window currently targets: nodes with ids, type labels, input/output pins (slot, key, name, connection count), per-output payload summaries (geometry vertex/facet counts, point/instance counts, scalar values) and all links. 'selected' is true when the window has a target (a Graph Mesh asset). Waits for any background graph evaluation to finish, so the payloads reflect all previously issued mutations.", schema_no_args()});
    m_tool_infos.push_back({"set_geometry_graph_target", "Point the Geometry Graph window (and the geometry_graph_* tools) at a Graph Mesh asset by name, or clear the target with an empty/omitted name. This replaces the old 'select the asset to edit it' behaviour - the window no longer follows the global selection (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to target; empty or omitted clears the target"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_add_node", "Add a node to the geometry node graph. Returns the new node's id and pin layout.", {
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"}, {"enum", json::array({"box", "sphere", "torus", "cone", "disc", "subdivide", "conway", "transform", "triangulate", "normalize", "reverse", "repair", "join", "boolean", "float", "integer", "vector", "math", "output"})}, {"description", "Node type to create"}}}
        }},
        {"required", json::array({"type"})}
    }});
    m_tool_infos.push_back({"geometry_graph_connect", "Connect an output pin of one geometry graph node to an input pin of another (pins are addressed by node id + pin slot index; pin keys must match). Undoable; the graph re-evaluates in the background (get_geometry_graph waits for completion, get_async_status reports progress).", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_disconnect", "Disconnect a geometry graph link (addressed like geometry_graph_connect). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_remove_node", "Remove a geometry graph node (its links are removed too). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to remove"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_parameter", "Set parameters of a geometry graph node. Takes the same JSON object shape as the graph file's per-node 'parameters' (see get_geometry_graph); partial updates are allowed - omitted keys keep their current values. Undoable; the graph re-evaluates in the background (get_geometry_graph waits for completion, get_async_status reports progress).", {
        {"type", "object"},
        {"properties", {
            {"node_id",    {{"type", "integer"}, {"description", "Id of the node"}}},
            {"parameters", {{"type", "object"},  {"description", "Parameter key/values to set"}}}
        }},
        {"required", json::array({"node_id", "parameters"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_display_flags", "Set or clear a geometry graph node's display / ghost designation (Houdini display / template flags). The display node's geometry replaces the output node's wired input in the scene bake (render mesh AND physics); the ghost node's geometry additionally shows as a dim edge-lines-only mesh (no shadow, not pickable). One node each per graph: setting a flag moves it from any previous holder; false clears the flag only when this node holds it. Undoable; the graph re-bakes in the background (get_geometry_graph waits and reports display_node_id / ghost_node_id).", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to (un)designate"}}},
            {"display", {{"type", "boolean"}, {"description", "true: make this the display node; false: clear the display flag if this node holds it"}}},
            {"ghost",   {{"type", "boolean"}, {"description", "true: make this the ghost node; false: clear the ghost flag if this node holds it"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_node_previews", "Enable/disable per-node mesh preview thumbnails on the geometry graph canvas for the window's target Graph Mesh (the 'Show node previews' checkbox). Enabling forces a full background re-evaluation so every node gets a preview; previews render a few nodes per frame after evaluation completes.", {
        {"type", "object"},
        {"properties", {
            {"enabled", {{"type", "boolean"}, {"description", "true (default) to show previews, false to hide"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_set_view", "Show the Geometry Graph window and set its node-editor zoom (view scale) immediately, centered on the graph content. zoom > 1 zooms in (content drawn larger), zoom < 1 zooms out. Deterministic (no animation / no mouse input) - intended for headless zoom-quality verification (capture_screenshot on the next frame). Requires the window to have a target Graph Mesh (create_graph_mesh / set_geometry_graph_target) for it to render nodes.", {
        {"type", "object"},
        {"properties", {
            {"zoom", {{"type", "number"}, {"description", "View scale (> 0). 1.0 = native, 2.0 = 2x zoom in, 0.5 = 2x zoom out."}}}
        }},
        {"required", json::array({"zoom"})}
    }});
    m_tool_infos.push_back({"geometry_graph_select_nodes", "Set the Geometry Graph window's canvas node selection: clears the current canvas selection and selects the given node ids (empty / omitted node_ids just clears). Also shows the Geometry Graph and Node Properties windows so the selection and its properties are observable in the next capture_screenshot. Nodes must have rendered at least once (show the window first, e.g. geometry_graph_set_view).", {
        {"type", "object"},
        {"properties", {
            {"node_ids", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Ids of the nodes to select (see get_geometry_graph); empty clears the selection"}}}
        }}
    }});
    m_tool_infos.push_back({"texture_graph_select_nodes", "Set the Texture Graph window's canvas node selection: clears the current canvas selection and selects the given node ids (empty / omitted node_ids just clears). Also shows the Texture Graph and Node Properties windows so the selection and its properties are observable in the next capture_screenshot. Nodes must have rendered at least once (show the window first).", {
        {"type", "object"},
        {"properties", {
            {"node_ids", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Ids of the nodes to select (see get_texture_graph); empty clears the selection"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_set_node_layout", "Set a geometry graph node's canvas layout: position, requested width / height (canvas units; content is not scaled - the node gets more room; 0 = automatic, content taller/wider than the request wins), and which node edge the input / output pins are laid out on. All arguments except node_id are optional; omitted ones are left unchanged. Returns the resulting layout. Same knobs as the Node Properties window's Position / Size / Inputs / Outputs rows.", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node (see get_geometry_graph)"}}},
            {"position",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "Canvas position [x, y]"}}},
            {"width",       {{"type", "number"},  {"description", "Requested node width in canvas units; 0 = automatic (content-derived)"}}},
            {"height",      {{"type", "number"},  {"description", "Requested node height in canvas units; 0 = automatic (content-derived)"}}},
            {"input_edge",  {{"type", "string"},  {"description", "Edge for input pins: left (default) or right"}}},
            {"output_edge", {{"type", "string"},  {"description", "Edge for output pins: left or right (default)"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_set_node_layout", "Set a texture graph node's canvas layout: position, requested width / height (canvas units; content is not scaled - the node gets more room; 0 = automatic, content taller/wider than the request wins), and which node edge the input / output pins are laid out on. All arguments except node_id are optional; omitted ones are left unchanged. Returns the resulting layout. Same knobs as the Node Properties window's Position / Size / Inputs / Outputs rows.", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node (see get_texture_graph)"}}},
            {"position",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "Canvas position [x, y]"}}},
            {"width",       {{"type", "number"},  {"description", "Requested node width in canvas units; 0 = automatic (content-derived)"}}},
            {"height",      {{"type", "number"},  {"description", "Requested node height in canvas units; 0 = automatic (content-derived)"}}},
            {"input_edge",  {{"type", "string"},  {"description", "Edge for input pins: left (default) or right"}}},
            {"output_edge", {{"type", "string"},  {"description", "Edge for output pins: left or right (default)"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"create_graph_texture", "Create a Graph Texture asset (a procedural texture backed by a node graph) in a scene's content library and point the Texture Graph window at it (its new target). The window's target Graph Texture is what the texture_graph_* tools operate on (retarget later with set_texture_graph_target). A Material slot can then source from it (set_material_texture_source). Returns the new asset's id and name.", {
        {"type", "object"},
        {"properties", {
            {"name",       {{"type", "string"}, {"description", "Name of the new Graph Texture asset (must be unique in the scene)"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"name"})}
    }});
    m_tool_infos.push_back({"set_material_texture_source", "Bind a Material texture slot to a Graph Texture asset so the material samples the graph's baked output (the material->graph back-reference; editing the graph updates the material live). Each slot holds a single texture reference, so binding replaces any previously assigned texture. Omit or empty 'graph_texture' to clear the slot entirely.", {
        {"type", "object"},
        {"properties", {
            {"material_name", {{"type", "string"}, {"description", "Name of the material in the scene's content library"}}},
            {"slot",          {{"type", "string"}, {"description", "Texture slot: base_color (default), metallic_roughness, normal, occlusion, or emissive"}}},
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to source from; empty to clear the binding"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"material_name"})}
    }});
    m_tool_infos.push_back({"get_graph_textures", "List the Graph Texture assets in a scene's content library (or all scenes when scene_name is omitted): name, id, scene, node_count, and has_output (whether the graph currently bakes an output texture).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Scene to list (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"create_graph_mesh", "Create a Graph Mesh asset (a procedural mesh backed by a geometry node graph) in a scene's content library and point the Geometry Graph window at it (its new target). The window's target Graph Mesh is what the geometry_graph_* tools operate on (retarget later with set_geometry_graph_target). A scene Node can then source its mesh from it (set_node_graph_mesh). Returns the new asset's id and name.", {
        {"type", "object"},
        {"properties", {
            {"name",       {{"type", "string"}, {"description", "Name of the new Graph Mesh asset (must be unique in the scene)"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"name"})}
    }});
    m_tool_infos.push_back({"set_node_graph_mesh", "Bind a scene Node to a Graph Mesh asset via a Geometry Graph Mesh attachment: the node's renderable mesh (and optional physics) is sourced from the graph's baked output, and the attachment points back at the asset (the node->graph back-reference; editing the graph updates the node's mesh live). Creates the attachment when missing; omit or empty 'graph_mesh' to remove the attachment and its controlled mesh. Scene state reflects the bake after the next evaluation completes (get_geometry_graph is the barrier).", {
        {"type", "object"},
        {"properties", {
            {"node_name",  {{"type", "string"}, {"description", "Name of the scene node to bind"}}},
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to source from; empty to remove the binding"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"node_name"})}
    }});
    m_tool_infos.push_back({"get_graph_meshes", "List the Graph Mesh assets in a scene's content library (or all scenes when scene_name is omitted): name, id, scene, node_count, baked_revision and has_bake (whether the graph has published baked products).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Scene to list (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"get_texture_graph", "List the procedural texture node graph the Texture Graph window currently targets: the target asset name/id, nodes with ids, type labels, canvas positions, parameters, input pins (slot, value type, whether connected, source node id/slot) and output pins (slot, value type), plus a 'composable' flag per node, and all links. 'selected' is true when the window has a target. Texture graph evaluation is synchronous, so no wait is needed.", schema_no_args()});
    m_tool_infos.push_back({"set_texture_graph_target", "Point the Texture Graph window (and the texture_graph_* tools) at a Graph Texture asset by name, or clear the target with an empty/omitted name. Replaces the old selection-driven targeting - the window no longer follows the global selection (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to target; empty or omitted clears the target"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    // Build the node-type enum from the descriptor registry (plus the bespoke
    // "output" sink) so it never drifts as node types are added.
    json texture_node_type_enum = json::array();
    for (const erhe::texgen::Node_descriptor* descriptor : all_texture_node_descriptors()) {
        texture_node_type_enum.push_back(descriptor->name);
    }
    texture_node_type_enum.push_back("output");
    texture_node_type_enum.push_back("material_output");
    m_tool_infos.push_back({"texture_graph_add_node", "Add a node to the texture node graph. Returns the new node's id, parameters and pin layout.", {
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"}, {"enum", texture_node_type_enum}, {"description", "Node type to create"}}},
            {"position", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "Optional [x, y] canvas position; defaults to the next spawn-grid slot"}}}
        }},
        {"required", json::array({"type"})}
    }});
    m_tool_infos.push_back({"texture_graph_connect", "Connect an output pin of one texture graph node to an input pin of another (pins are addressed by node id + pin slot index; pin keys / value types must match). Undoable. Returns an error result when the pin keys differ or the link would create a cycle.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_disconnect", "Disconnect a texture graph link (addressed like texture_graph_connect). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_remove_node", "Remove a texture graph node (its links are removed too). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to remove"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_set_parameter", "Set parameters of a texture graph node. Takes the same JSON object shape as the graph file's per-node 'parameters' (see get_texture_graph); partial updates are allowed - omitted keys keep their current values. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id",    {{"type", "integer"}, {"description", "Id of the node"}}},
            {"parameters", {{"type", "object"},  {"description", "Parameter key/values to set"}}}
        }},
        {"required", json::array({"node_id", "parameters"})}
    }});
    m_tool_infos.push_back({"texture_graph_export_png", "Compose and render a texture graph node's output to a PNG file. For a generator/filter node the given output slot is rendered; for the Output node (no output pins) the connected input's subtree is rendered. Returns the written path and its width/height. Requires the graphics device (works in the headless Vulkan build).", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node whose output to render"}}},
            {"output_slot", {{"type", "integer"}, {"description", "Output pin slot index to render (default 0; ignored for the Output node)"}}},
            {"size",        {{"type", "integer"}, {"description", "Square edge length in pixels (default 256, clamped to [1, 4096])"}}},
            {"path",        {{"type", "string"},  {"description", "PNG file path to write (parent directories are created)"}}}
        }},
        {"required", json::array({"node_id", "path"})}
    }});
    m_tool_infos.push_back({"texture_graph_export_material", "Compose and render each connected PBR channel of a Material Output node to a PNG file in 'dir': <base_name>_albedo.png, _normal.png, _emissive.png, _occlusion.png, and _metallic_roughness.png (glTF ORM packing: R=occlusion, G=roughness, B=metallic). Only connected channels are written. Returns the list of written file paths. Requires the graphics device (works in the headless Vulkan build).", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the Material Output node"}}},
            {"dir",     {{"type", "string"},  {"description", "Output directory (created if missing); files are named <base_name>_<channel>.png"}}},
            {"size",    {{"type", "integer"}, {"description", "Square edge length in pixels (default 256, clamped to [1, 4096])"}}}
        }},
        {"required", json::array({"node_id", "dir"})}
    }});

    // Extra editor window instances (issue #252). Open additional Geometry
    // Graph / Texture Graph / Properties windows, each with its own target,
    // so several assets can be edited / inspected side by side. The user
    // closes them with the window X button.
    m_tool_infos.push_back({"open_geometry_graph_window", "Open a new Geometry Graph window (in addition to the primary one), optionally targeting a named Graph Mesh asset. Multiple windows can target different assets at once (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to target; empty/omitted opens an empty editor"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"open_texture_graph_window", "Open a new Texture Graph window (in addition to the primary one), optionally targeting a named Graph Texture asset. Multiple windows can target different assets at once (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to target; empty/omitted opens an empty editor"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"open_properties_window", "Open a new Properties window pinned to a named material (issue #252). A pinned Properties window shows only its target, independent of the global selection; omit 'material' to open one that follows the selection like the primary Properties window.", {
        {"type", "object"},
        {"properties", {
            {"material",   {{"type", "string"}, {"description", "Name of the material to pin the window to; empty/omitted follows the global selection"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});

    // Animation timeline / curve editor tools (issue #243). The Animation
    // window and Animation_player share one target animation;
    // set_animation_target points both at a named animation from a scene's
    // content library (glTF import fills it).
    m_tool_infos.push_back({"get_scene_animations", "List animations in scene content libraries (glTF imports land there): per animation the time range and every channel (target node, path, interpolation, keyframe count, sampler index).", schema_scene_name()});
    m_tool_infos.push_back({"set_animation_target", "Point the Animation window (curve editor) and the animation player at a named animation and show the window. Optionally restrict which channels' curves are visible (deterministic view for screenshots). Empty/omitted 'animation' clears the target.", {
        {"type", "object"},
        {"properties", {
            {"animation",        {{"type", "string"}, {"description", "Animation name; empty/omitted clears the target"}}},
            {"scene_name",       {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}},
            {"visible_channels", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Channel indices whose curves are visible (all components); other channels are hidden. Omit to keep defaults. Also re-frames the view."}}}
        }}
    }});
    m_tool_infos.push_back({"animation_playback", "Control animation playback (Animation_player): play / pause / stop, seek to a time, set speed, looping and the autokey mode. Applies the sampled pose to the target nodes immediately, so a capture_screenshot on the next frame shows it. Optionally retargets to a named animation first.", {
        {"type", "object"},
        {"properties", {
            {"animation",  {{"type", "string"},  {"description", "Animation name to target first (optional; default: current target)"}}},
            {"scene_name", {{"type", "string"},  {"description", "Scene to search when 'animation' is given (default: all scenes)"}}},
            {"action",     {{"type", "string"},  {"description", "play, pause or stop (optional)"}}},
            {"time",       {{"type", "number"},  {"description", "Seek to this absolute animation time in seconds (optional)"}}},
            {"speed",      {{"type", "number"},  {"description", "Playback speed factor; negative plays backwards (optional)"}}},
            {"looping",    {{"type", "boolean"}, {"description", "Loop at the end of the range (optional)"}}},
            {"autokey",    {{"type", "string"},  {"description", "Autokey mode: off, modified (key only edited paths) or all (key T+R+S on any edit). Applies to finished Transform tool edits, including transform_selection (optional)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_create_key", "Create keys (LightWave 'Create Key') on the targeted animation at a time: writes the nodes' current translation / rotation / scale values, creating missing channels (LINEAR) on demand. Nodes default to the current selection. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"nodes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Node names to key (default: current selection)"}}},
            {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Paths to key: translation, rotation, scale (default: all three)"}}},
            {"time",  {{"type", "number"}, {"description", "Key time in seconds (default: current play position)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_delete_key", "Delete keys (LightWave 'Delete Key') from the targeted animation: removes the keys at (approximately) a time from every channel targeting the given nodes. Nodes default to the current selection. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"nodes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Node names (default: current selection)"}}},
            {"time",  {{"type", "number"}, {"description", "Key time in seconds (default: current play position)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_edit_keyframe", "Edit animation keyframes (undoable through the operation stack, same code path as the Animation window). edit=move changes a key's time (clamped between neighbor keys) and/or one component's value; edit=insert adds a key at a time, evaluating the curve so the shape is preserved; edit=delete removes a key (all components). Times/values apply to the channel's sampler, so channels sharing a sampler are affected together.", {
        {"type", "object"},
        {"properties", {
            {"animation",     {{"type", "string"},  {"description", "Animation name (optional; default: the Animation window's target)"}}},
            {"scene_name",    {{"type", "string"},  {"description", "Scene to search when 'animation' is given (default: all scenes)"}}},
            {"edit",          {{"type", "string"},  {"description", "move, insert or delete"}}},
            {"channel_index", {{"type", "integer"}, {"description", "Channel index (from get_scene_animations)"}}},
            {"key_index",     {{"type", "integer"}, {"description", "Keyframe index (move / delete)"}}},
            {"time",          {{"type", "number"},  {"description", "move: new time for the key; insert: time of the new key"}}},
            {"value",         {{"type", "number"},  {"description", "move: new value for one component (requires 'component')"}}},
            {"component",     {{"type", "integer"}, {"description", "Component index (0=X, 1=Y, 2=Z, 3=W) for 'value'"}}}
        }},
        {"required", json::array({"edit", "channel_index"})}
    }});

    // Editor commands
    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        m_tool_infos.push_back({name, std::string{"Editor command: "} + name, schema_no_args()});
    }
}


} // namespace editor
