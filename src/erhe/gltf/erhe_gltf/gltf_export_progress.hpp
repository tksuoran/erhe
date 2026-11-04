#pragma once

#include <cstddef>
#include <mutex>

namespace erhe::gltf {

enum class Gltf_export_stage : unsigned int
{
    starting = 0,
    mesh_data,
    meshes,
    scene,
    textures,
    encode
};

[[nodiscard]] auto c_str(Gltf_export_stage stage) -> const char*;

class Gltf_export_progress_state
{
public:
    Gltf_export_stage stage{Gltf_export_stage::starting};
    std::size_t       done {0};
    std::size_t       total{0};
};

// Written by export_gltf() (its thread and its executor tasks), read by
// any thread; see Gltf_export_arguments::progress.
class Gltf_export_progress
{
public:
    void begin_stage(Gltf_export_stage stage, std::size_t total);
    void advance    ();
    [[nodiscard]] auto get() const -> Gltf_export_progress_state;

private:
    mutable std::mutex         m_mutex;
    Gltf_export_progress_state m_state;
};

} // namespace erhe::gltf
