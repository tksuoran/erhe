#include "assets/editor_asset_load.hpp"

#include "assets/item_keys.hpp"
#include "assets/local_mesh_items.hpp"
#include "editor_log.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_file/file.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <cstring>
#include <span>
#include <unordered_map>

namespace editor {

using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;

namespace {

// Items are written in batches of this many, or of these bytes, whichever comes first.
constexpr std::size_t c_batch_item_count = 512;
constexpr std::size_t c_batch_byte_count = 64 * 1024 * 1024;

} // anonymous namespace

// The payload of one item the load reads: from memory until its batch is written, then from the store.
class Load_item_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    Load_item_reader(std::shared_ptr<Editor_asset> editor_asset, const Key& key, std::shared_ptr<const std::vector<std::byte>> pending)
        : m_editor_asset{std::move(editor_asset)}
        , m_key         {key}
        , m_pending     {std::move(pending)}
    {
    }

    [[nodiscard]] auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        std::shared_ptr<const std::vector<std::byte>> pending;
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            pending = m_pending;
        }
        if (!pending) {
            return m_editor_asset->get_store().read_payload(m_key, offset, out);
        }
        if ((offset > pending->size()) || (out.size() > (pending->size() - offset))) {
            return false;
        }
        std::memcpy(out.data(), pending->data() + offset, out.size());
        return true;
    }

    // The item is in the store: later reads go there.
    void on_written()
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_pending.reset();
    }

private:
    std::shared_ptr<Editor_asset>                 m_editor_asset;
    Key                                           m_key;
    mutable std::mutex                            m_mutex;
    std::shared_ptr<const std::vector<std::byte>> m_pending;
};

Editor_asset_load::Editor_asset_load(Editor_asset_load_create_info&& create_info)
    : m_create_info{std::move(create_info)}
{
}

Editor_asset_load::~Editor_asset_load() noexcept = default;

auto Editor_asset_load::get_editor_asset() const -> const std::shared_ptr<Editor_asset>&
{
    return m_create_info.editor_asset;
}

auto Editor_asset_load::get_counts() const -> Editor_asset_load_counts
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_counts;
}

auto Editor_asset_load::is_present(const Key& key) const -> bool
{
    return m_create_info.catalog && m_create_info.catalog->items.contains(key);
}

auto Editor_asset_load::get_lod_settings(const erhe::primitive::Primitive& primitive) const -> std::optional<erhe::primitive::Lod_settings>
{
    return primitive.lod_settings.has_value() ? primitive.lod_settings : m_create_info.default_lod_settings;
}

auto Editor_asset_load::make_reader(const Key& key) const -> std::shared_ptr<Load_item_reader>
{
    return std::make_shared<Load_item_reader>(m_create_info.editor_asset, key, std::shared_ptr<const std::vector<std::byte>>{});
}

auto Editor_asset_load::queue_payload(
    const Key&               key,
    const Item_kind          kind,
    const std::string&       stamp,
    std::string&&            meta,
    std::vector<std::byte>&& payload
) -> std::shared_ptr<Load_item_reader>
{
    std::shared_ptr<const std::vector<std::byte>> bytes = std::make_shared<const std::vector<std::byte>>(std::move(payload));
    std::shared_ptr<Load_item_reader> reader = std::make_shared<Load_item_reader>(m_create_info.editor_asset, key, bytes);
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_pending_bytes += bytes->size();
        m_pending.push_back(Pending_item{.key = key, .kind = kind, .stamp = stamp, .meta = std::move(meta), .payload = std::move(bytes), .reader = reader});
    }
    flush(Flush::when_full);
    return reader;
}

// Writes the pending items in one write transaction: when a batch is full, or the rest after the last job.
void Editor_asset_load::flush(const Flush flush)
{
    std::vector<Pending_item> batch;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        const bool full = (m_pending.size() >= c_batch_item_count) || (m_pending_bytes >= c_batch_byte_count);
        if (m_pending.empty() || ((flush == Flush::when_full) && !full)) {
            return;
        }
        batch.swap(m_pending);
        m_pending_bytes = 0;
    }
    ERHE_PROFILE_SCOPE("store load items");
    erhe::asset_store::Asset_store& store = m_create_info.editor_asset->get_store();
    std::size_t bytes = 0;
    bool        ok    = false;
    {
        erhe::asset_store::Write_transaction transaction{store};
        ok = transaction.is_active();
        for (const Pending_item& item : batch) {
            if (!ok) {
                break;
            }
            bytes += item.payload->size();
            ok = transaction.put(erhe::asset_store::Item_data{
                .key     = item.key,
                .kind    = item.kind,
                .target  = {},
                .stamp   = item.stamp,
                .meta    = item.meta,
                .info    = {},
                .payload = std::span<const std::byte>{*item.payload}
            });
        }
        ok = ok && transaction.commit();
    }
    if (!ok) {
        // The primitives keep reading their items from memory; the next open derives them again.
        log_parsers->error("Open: {} items were not stored in the editor asset '{}'", batch.size(), erhe::file::to_string(store.get_path()));
        return;
    }
    for (const Pending_item& item : batch) {
        item.reader->on_written();
    }
    m_create_info.editor_asset->invalidate_item_catalog();
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_counts.items_written += batch.size();
    m_counts.bytes_written += bytes;
    ++m_counts.transactions;
}

void Editor_asset_load::finish_job()
{
    bool last = false;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ++m_finished_jobs;
        last = (m_finished_jobs == m_create_info.job_count);
    }
    if (last) {
        flush(Flush::everything);
    }
}

void Editor_asset_load::count_as_before(const std::string& reason)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    ++m_counts.built_as_before;
    ++m_counts.reasons[reason];
}

auto Editor_asset_load::load_item_primitive(
    const Key&                               mesh_key,
    const std::string&                       mesh_meta,
    const std::shared_ptr<Load_item_reader>& mesh_reader,
    const Key&                               bvh_key,
    const std::string&                       bvh_meta,
    const std::shared_ptr<Load_item_reader>& bvh_reader,
    std::string&                             out_error
) const -> std::shared_ptr<erhe::primitive::Primitive>
{
    erhe::gltf::Gltf_mesh_item_reference reference{.uri = make_item_uri(mesh_key), .meta = mesh_meta};
    const std::string bvh_uri = make_item_uri(bvh_key);
    if (bvh_reader) {
        reference.raytrace_bvh = erhe::gltf::Gltf_item_reference{.uri = bvh_uri, .meta = bvh_meta};
    }
    const std::string mesh_uri = reference.uri;
    return erhe::gltf::load_mesh_item_primitive(
        erhe::gltf::Gltf_mesh_item_load_arguments{
            .executor        = *m_create_info.executor,
            .reference       = std::move(reference),
            .buffer_resolver = [mesh_uri, bvh_uri, mesh_reader, bvh_reader](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
                if (uri == mesh_uri) {
                    return mesh_reader;
                }
                if (bvh_reader && (uri == bvh_uri)) {
                    return bvh_reader;
                }
                return {};
            },
            .path                 = m_create_info.editor_asset->get_asset_path(),
            .triangle_soup_reload = erhe::gltf::Triangle_soup_reload::record,
            .mesh_payload         = erhe::gltf::Mesh_payload_load::deferred
        },
        out_error
    );
}

auto Editor_asset_load::prepare_job(erhe::primitive::Primitive& primitive) -> Editor_asset_job
{
    ERHE_PROFILE_FUNCTION();
    Editor_asset_job job{};
    job.source_shape = primitive.render_shape;
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& source_shape = job.source_shape;
    // A geometry-normative shape is built from its Geometry: an item's core is triangles its facets do not map to.
    if (!source_shape || !source_shape->is_payload_pending()) {
        job.reason = (source_shape && source_shape->has_triangle_soup()) ? "soup read by the parse" : "geometry-normative";
        return job;
    }
    if (!primitive.load_payload()) {
        job.reason = "mesh data not read";
        return job;
    }
    job.content_hash = source_shape->get_content_hash();
    if (!job.content_hash.has_value()) {
        const std::shared_ptr<erhe::primitive::Triangle_soup> soup = source_shape->get_triangle_soup();
        if (soup) {
            job.content_hash = erhe::primitive::compute_triangle_soup_content_hash(*soup);
            source_shape->set_content_hash(job.content_hash.value());
        }
    }
    if (!job.content_hash.has_value()) {
        job.reason = "no content hash";
        return job;
    }
    job.mesh_key     = make_shape_local_mesh_key(Local_mesh_source::soup, job.content_hash.value(), get_lod_settings(primitive), m_create_info.mesh_stamp);
    job.bvh_key      = make_raytrace_bvh_key(job.mesh_key, m_create_info.bvh_stamp);
    job.mesh_present = is_present(job.mesh_key);
    job.bvh_present  = m_create_info.bvh_items && is_present(job.bvh_key);

    std::string                       mesh_meta;
    std::shared_ptr<Load_item_reader> mesh_reader;
    std::string                       bvh_meta;
    std::shared_ptr<Load_item_reader> bvh_reader;
    erhe::asset_store::Asset_store&   store = m_create_info.editor_asset->get_store();
    if (job.mesh_present) {
        const std::optional<std::string> meta = store.read_meta(job.mesh_key);
        if (meta.has_value()) {
            mesh_meta   = meta.value();
            mesh_reader = make_reader(job.mesh_key);
        }
    }
    if (job.bvh_present) {
        const std::optional<std::string> meta = store.read_meta(job.bvh_key);
        if (meta.has_value()) {
            bvh_meta   = meta.value();
            bvh_reader = make_reader(job.bvh_key);
        }
    }
    if (!mesh_reader) {
        // R8.3: the mesh optimizer and the BVH build run once, and their results are stored.
        const Local_mesh_items_wanted wanted = (m_create_info.bvh_items && !bvh_reader) ? Local_mesh_items_wanted::mesh_and_bvh : Local_mesh_items_wanted::mesh;
        Local_mesh_items items = make_local_mesh_items(*source_shape, Local_mesh_source::soup, get_lod_settings(primitive), &m_create_info.gpu_vertex_formats, wanted);
        if (!items.mesh.has_value()) {
            job.reason = items.skip_reason.empty() ? std::string{"the mesh item was not made"} : items.skip_reason;
            return job;
        }
        mesh_meta        = items.mesh->meta;
        mesh_reader      = queue_payload(job.mesh_key, Item_kind::local_mesh, m_create_info.mesh_stamp, std::move(items.mesh->meta), std::move(items.mesh->payload));
        job.derived_mesh = true;
        if (items.bvh.has_value()) {
            bvh_meta      = items.bvh->meta;
            bvh_reader    = queue_payload(job.bvh_key, Item_kind::raytrace_bvh, m_create_info.bvh_stamp, std::move(items.bvh->meta), std::move(items.bvh->payload));
            job.built_bvh = true;
        }
    }

    std::string error;
    job.item_primitive = load_item_primitive(job.mesh_key, mesh_meta, mesh_reader, job.bvh_key, bvh_meta, bvh_reader, error);
    if (job.item_primitive && m_create_info.bvh_items && !bvh_reader) {
        // The mesh item is present and its BVH item is not (another BVH library): the BVH is built over its core once and stored.
        if (job.item_primitive->load_payload() && job.item_primitive->render_shape) {
            Local_mesh_items items = make_local_mesh_items(*job.item_primitive->render_shape, Local_mesh_source::soup, get_lod_settings(primitive), &m_create_info.gpu_vertex_formats, Local_mesh_items_wanted::bvh);
            if (items.bvh.has_value()) {
                bvh_meta      = items.bvh->meta;
                bvh_reader    = queue_payload(job.bvh_key, Item_kind::raytrace_bvh, m_create_info.bvh_stamp, std::move(items.bvh->meta), std::move(items.bvh->payload));
                job.built_bvh = true;
                job.item_primitive = load_item_primitive(job.mesh_key, mesh_meta, mesh_reader, job.bvh_key, bvh_meta, bvh_reader, error);
            }
        }
    }
    if (!job.item_primitive) {
        log_parsers->warn("Open: the mesh item {} does not load ({}) - its primitive is built from its soup", job.mesh_key.to_hex(), error);
        job.reason = "the mesh item does not load";
        return job;
    }
    job.bvh_item = static_cast<bool>(bvh_reader);
    job.kind     = Editor_asset_job_kind::item;
    return job;
}

void Editor_asset_load::install(erhe::primitive::Primitive& primitive, const Editor_asset_job& job)
{
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& item_shape   = job.item_primitive->render_shape;
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& source_shape = job.source_shape;
    // R3.5: the core keys as its source content; a source save writes that content (doc/erhe/primitive.md "Authoring soup").
    item_shape->set_content_hash(job.content_hash.value());
    const std::shared_ptr<const erhe::primitive::Triangle_soup_source> authoring_source = source_shape->get_triangle_soup_source();
    item_shape->set_authoring_soup(
        authoring_source ? std::shared_ptr<erhe::primitive::Triangle_soup>{} : source_shape->get_triangle_soup(),
        authoring_source
    );
    // Nothing else touches the job's Primitive while its job runs (one job per shape).
    primitive.render_shape           = item_shape;
    primitive.optimized_render_shape = job.item_primitive->optimized_render_shape;
    if (!primitive.collision_shape || (primitive.collision_shape == source_shape)) {
        primitive.collision_shape = job.item_primitive->collision_shape;
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_replaced.push_back(Replaced_shape{.source = source_shape, .primitive = &primitive, .render_shape = item_shape, .optimized_render_shape = primitive.optimized_render_shape});
    if (job.derived_mesh) {
        ++m_counts.derived_stored;
    } else {
        ++m_counts.from_items;
    }
    if (job.built_bvh) {
        ++m_counts.bvh_built_stored;
    } else if (job.bvh_item) {
        ++m_counts.bvh_from_items;
    }
}

void Editor_asset_load::share_replaced_shapes(const erhe::gltf::Gltf_data& gltf_data)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    // The replaced shapes some Primitive still holds; their addresses are unique.
    std::unordered_map<const erhe::primitive::Primitive_render_shape*, const Replaced_shape*> alive;
    std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>>                       holders;
    for (const Replaced_shape& replaced : m_replaced) {
        std::shared_ptr<erhe::primitive::Primitive_render_shape> source = replaced.source.lock();
        if (source) {
            alive.emplace(source.get(), &replaced);
            holders.push_back(std::move(source));
        }
    }
    if (!alive.empty()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : gltf_data.nodes) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = node ? erhe::scene::get_mesh(node.get()) : std::shared_ptr<erhe::scene::Mesh>{};
            if (!mesh) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                if ((primitive == nullptr) || !primitive->render_shape) {
                    continue;
                }
                const auto i = alive.find(primitive->render_shape.get());
                if ((i == alive.end()) || (i->second->primitive == primitive)) {
                    continue;
                }
                primitive->render_shape           = i->second->render_shape;
                primitive->optimized_render_shape = i->second->optimized_render_shape;
            }
        }
    }
    m_replaced.clear();
}

}
