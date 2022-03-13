#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Gyro_operation
    : public Mesh_operation
{
public:
    explicit Gyro_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(Parameters&& context);

    auto describe() const -> std::string override;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Reverse_operation
    : public Mesh_operation
{
public:
    explicit Reverse_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Normalize_operation
    : public Mesh_operation
{
public:
    explicit Normalize_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Weld_operation
    : public Mesh_operation
{
public:
    explicit Weld_operation(Parameters&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

}
