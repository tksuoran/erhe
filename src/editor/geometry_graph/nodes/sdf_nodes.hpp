#pragma once

// SDF (signed distance field) geometry graph nodes over erhe::voxel::Grid
// (OpenVDB narrow-band level sets). Only compiled when
// ERHE_VOXEL_LIBRARY=openvdb; see doc/openvdb-integration-plan.md Phase 3.
//
// Grids flowing through "sdf" pins are immutable by convention (like
// geometries): operation nodes deep-copy before modifying. Grids are not
// serialized - they are re-evaluated from node parameters.

#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace editor {

// Shared voxel resolution parameter + imgui/serialization helpers for the
// nodes that create grids (sphere, capsule, voxelize). Downstream operation
// nodes inherit the resolution of their input grids.
class Sdf_create_parameters
{
public:
    // Draws the voxel size widget; returns true when edited.
    [[nodiscard]] auto imgui(float content_scale) -> bool;
    void write(nlohmann::json& out) const;
    void read (const nlohmann::json& in);

    float voxel_size{0.05f};
};

class Sdf_sphere_node : public Geometry_graph_node
{
public:
    Sdf_sphere_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    Sdf_create_parameters m_create_parameters{};
    float                 m_radius{1.0f};
    glm::vec3             m_center{0.0f};
};

class Sdf_capsule_node : public Geometry_graph_node
{
public:
    Sdf_capsule_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    Sdf_create_parameters m_create_parameters{};
    glm::vec3             m_p0     {0.0f, -1.0f, 0.0f};
    glm::vec3             m_p1     {0.0f,  1.0f, 0.0f};
    float                 m_radius0{0.5f};
    float                 m_radius1{0.5f};
};

// Voxelize: closed mesh geometry -> SDF grid
class Sdf_from_geometry_node : public Geometry_graph_node
{
public:
    Sdf_from_geometry_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    Sdf_create_parameters m_create_parameters{};
};

// Mesh: SDF grid -> quad-dominant mesh geometry
class Sdf_to_geometry_node : public Geometry_graph_node
{
public:
    Sdf_to_geometry_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_adaptivity{0.0f};
};

class Sdf_boolean_node : public Geometry_graph_node
{
public:
    enum class Boolean_operation : int {
        union_operation = 0,
        intersection    = 1,
        difference      = 2
    };

    Sdf_boolean_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    Boolean_operation m_operation{Boolean_operation::union_operation};
};

class Sdf_offset_node : public Geometry_graph_node
{
public:
    Sdf_offset_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_distance{0.1f};
};

class Sdf_smooth_node : public Geometry_graph_node
{
public:
    Sdf_smooth_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    int m_iterations{1};
};

}
