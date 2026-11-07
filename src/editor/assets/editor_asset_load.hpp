#pragma once

#include "assets/editor_asset.hpp"

#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_hash/content_hash.hpp"
#include "erhe_primitive/lod.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace erhe::primitive { class Primitive; class Primitive_render_shape; }
namespace tf              { class Executor; }

namespace editor {

class Load_item_reader;

// What the build jobs of one source open did with the file's editor asset
// (doc/editor/prepared_assets.md "The editor's own loading").
class Editor_asset_load_counts
{
public:
    std::size_t from_items       {0}; // shapes loaded from a present Local-space mesh item
    std::size_t derived_stored   {0}; // shapes prepared at the load, their mesh item stored
    std::size_t bvh_from_items   {0}; // BVHs read from a present Raytrace BVH item
    std::size_t bvh_built_stored {0}; // BVHs built at the load, their item stored
    std::size_t built_as_before  {0}; // shapes built without items (geometry-normative, refused, failed)
    std::size_t items_written    {0};
    std::size_t bytes_written    {0};
    std::size_t transactions     {0};
    std::map<std::string, std::size_t> reasons; // why a shape was built without items
};

class Editor_asset_load_create_info
{
public:
    std::shared_ptr<Editor_asset>        editor_asset;
    std::shared_ptr<const Item_catalog>  catalog;
    erhe::gltf::Gltf_gpu_vertex_formats  gpu_vertex_formats;
    std::string                          mesh_stamp;
    std::string                          bvh_stamp;
    bool                                 bvh_items{false}; // the raytrace backend exports BVHs
    tf::Executor*                        executor {nullptr};
    std::size_t                          job_count{0};
    // The LOD request of a primitive without its own: the scene's export
    // settings', as Export asks (get_export_lod_settings()); none without settings.
    std::optional<erhe::primitive::Lod_settings> default_lod_settings;
};

// How a build job builds its primitive (Editor_asset_load::prepare_job()).
enum class Editor_asset_job_kind : unsigned int
{
    as_before = 0, // no item: built as a load without an editor asset builds it
    item           // item_primitive is built and then installed in place of the source shape
};

class Editor_asset_job
{
public:
    Editor_asset_job_kind                                     kind{Editor_asset_job_kind::as_before};
    std::string                                               reason; // as_before
    std::shared_ptr<erhe::primitive::Primitive>               item_primitive;
    std::shared_ptr<erhe::primitive::Primitive_render_shape>  source_shape;
    std::optional<erhe::hash::Content_hash>                   content_hash;
    erhe::asset_store::Key                                    mesh_key;
    erhe::asset_store::Key                                    bvh_key;
    bool                                                      mesh_present{false};
    bool                                                      bvh_present {false};
    bool                                                      derived_mesh{false}; // the mesh item was made by this job
    bool                                                      built_bvh   {false}; // the BVH item was made by this job
    bool                                                      bvh_item    {false}; // the item primitive reads a BVH item
};

// The build jobs of one source open read the items of its editor asset and
// store what they derive (R8.1 - R8.3); the GPU builds are the caller's
// (Gltf_load_task). Shared by the jobs; thread-safe.
class Editor_asset_load
{
public:
    explicit Editor_asset_load(Editor_asset_load_create_info&& create_info);
    ~Editor_asset_load() noexcept;
    Editor_asset_load(const Editor_asset_load&) = delete;
    auto operator=(const Editor_asset_load&) -> Editor_asset_load& = delete;

    // Worker, once per job, before the build: loads the primitive's payload,
    // keys a soup and loads its mesh item, or makes and queues its items and
    // loads them (the preparation runs once).
    [[nodiscard]] auto prepare_job(erhe::primitive::Primitive& primitive) -> Editor_asset_job;
    // Worker, after the item primitive was built: it takes the place of the
    // source shape in `primitive`, keyed as its source content, with the
    // source soup as its authoring soup.
    void install(erhe::primitive::Primitive& primitive, const Editor_asset_job& job);
    // Worker: the job was built without items.
    void count_as_before(const std::string& reason);
    // Worker, at the end of every job; the last job writes the items left.
    void finish_job();

    // Main thread, after every job: Primitives that share a shape a job replaced get the replacement.
    void share_replaced_shapes(const erhe::gltf::Gltf_data& gltf_data);

    [[nodiscard]] auto get_counts      () const -> Editor_asset_load_counts;
    [[nodiscard]] auto get_editor_asset() const -> const std::shared_ptr<Editor_asset>&;

private:
    class Pending_item
    {
    public:
        erhe::asset_store::Key                        key;
        erhe::asset_store::Item_kind                  kind{erhe::asset_store::Item_kind::local_mesh};
        std::string                                   stamp;
        std::string                                   meta;
        std::shared_ptr<const std::vector<std::byte>> payload;
        std::shared_ptr<Load_item_reader>             reader;
    };
    class Replaced_shape
    {
    public:
        // Expired when no Primitive holds the replaced shape any more (its address may then be reused).
        std::weak_ptr<erhe::primitive::Primitive_render_shape>    source;
        erhe::primitive::Primitive*                               primitive{nullptr};
        std::shared_ptr<erhe::primitive::Primitive_render_shape>  render_shape;
        std::shared_ptr<erhe::primitive::Primitive_render_shape>  optimized_render_shape;
    };
    enum class Flush : unsigned int { when_full = 0, everything };

    [[nodiscard]] auto make_reader(const erhe::asset_store::Key& key) const -> std::shared_ptr<Load_item_reader>;
    [[nodiscard]] auto is_present (const erhe::asset_store::Key& key) const -> bool;
    auto queue_payload(const erhe::asset_store::Key& key, erhe::asset_store::Item_kind kind, const std::string& stamp, std::string&& meta, std::vector<std::byte>&& payload) -> std::shared_ptr<Load_item_reader>;
    [[nodiscard]] auto load_item_primitive(
        const erhe::asset_store::Key&            mesh_key,
        const std::string&                       mesh_meta,
        const std::shared_ptr<Load_item_reader>& mesh_reader,
        const erhe::asset_store::Key&            bvh_key,
        const std::string&                       bvh_meta,
        const std::shared_ptr<Load_item_reader>& bvh_reader,
        std::string&                             out_error
    ) const -> std::shared_ptr<erhe::primitive::Primitive>;
    void flush(Flush flush);
    [[nodiscard]] auto get_lod_settings(const erhe::primitive::Primitive& primitive) const -> std::optional<erhe::primitive::Lod_settings>;

    Editor_asset_load_create_info m_create_info;

    mutable std::mutex           m_mutex;
    std::vector<Pending_item>    m_pending;          // under m_mutex
    std::size_t                  m_pending_bytes{0}; // under m_mutex
    std::size_t                  m_finished_jobs{0}; // under m_mutex
    Editor_asset_load_counts     m_counts;           // under m_mutex
    std::vector<Replaced_shape>  m_replaced;         // under m_mutex
};

}
