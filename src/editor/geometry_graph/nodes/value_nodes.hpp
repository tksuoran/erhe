#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace editor {

class Float_value_node : public Geometry_graph_node
{
public:
    Float_value_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    float m_value{0.0f};
};

class Integer_value_node : public Geometry_graph_node
{
public:
    Integer_value_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    int m_value{0};
};

class Vector_value_node : public Geometry_graph_node
{
public:
    Vector_value_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    glm::vec3 m_value{0.0f, 0.0f, 0.0f};
};

}
