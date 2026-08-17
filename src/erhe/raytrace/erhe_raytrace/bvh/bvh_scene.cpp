#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_raytrace/bvh/bvh_geometry.hpp"
#include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_raytrace/bvh/glm_conversions.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_executor.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <bvh/v2/default_builder.h>
#include <bvh/v2/ray.h>
#include <bvh/v2/stack.h>

#include <algorithm>

namespace erhe::raytrace {

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Bvh_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Bvh_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Bvh_scene>(debug_label);
}

auto Bvh_scene_child::get_bbox() const -> erhe::math::Aabb
{
    if (geometry != nullptr) {
        return geometry->get_bbox();
    }
    if (instance != nullptr) {
        return instance->get_bbox();
    }
    return erhe::math::Aabb{};
}

Bvh_scene::Bvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    log_scene->trace("Created Bvh_scene '{}'", debug_label);
}

Bvh_scene::~Bvh_scene() noexcept
{
    log_scene->trace("Destroyed Bvh_scene '{}'", m_debug_label);

    // Drop the back links, so that children outliving this scene do not
    // reference it, and instances instantiating this scene do not reference it.
    for (const Bvh_scene_child& child : m_children) {
        if (child.geometry != nullptr) {
            child.geometry->remove_parent_scene(this);
        } else if (child.instance != nullptr) {
            child.instance->remove_parent_scene(this);
        }
    }
    for (Bvh_instance* instance : m_referencing_instances) {
        instance->on_instanced_scene_destroyed();
    }
}

void Bvh_scene::attach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} attach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

#ifndef NDEBUG
    if (find_child(bvh_geometry) != m_children.end()) {
        log_scene->error("raytrace geometry already in scene");
        return;
    }
#endif

    m_children.push_back(
        Bvh_scene_child{
            .geometry           = bvh_geometry,
            .instance           = nullptr,
            .last_modified_tick = m_tick
        }
    );
    bvh_geometry->add_parent_scene(this);
    mark_modified();
}

void Bvh_scene::attach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} attach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

#ifndef NDEBUG
    if (find_child(bvh_instance) != m_children.end()) {
        log_scene->error("raytrace instance already in scene");
        return;
    }
#endif

    m_children.push_back(
        Bvh_scene_child{
            .geometry           = nullptr,
            .instance           = bvh_instance,
            .last_modified_tick = m_tick
        }
    );
    bvh_instance->add_parent_scene(this);
    mark_modified();
}

void Bvh_scene::detach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} detach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

    const auto i = find_child(bvh_geometry);
    if (i == m_children.end()) {
        log_scene->error("raytrace geometry not in scene");
        return;
    }
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    m_children.erase(i);
    bvh_geometry->remove_parent_scene(this);
    mark_modified();
}

void Bvh_scene::detach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} detach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

    const auto i = find_child(bvh_instance);
    if (i == m_children.end()) {
        log_scene->error("raytrace instance not in scene");
        return;
    }
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    m_children.erase(i);
    bvh_instance->remove_parent_scene(this);
    mark_modified();
}

auto Bvh_scene::find_child(const Bvh_geometry* geometry) -> std::vector<Bvh_scene_child>::iterator
{
    return std::find_if(
        m_children.begin(),
        m_children.end(),
        [geometry](const Bvh_scene_child& child) { return child.geometry == geometry; }
    );
}

auto Bvh_scene::find_child(const Bvh_instance* instance) -> std::vector<Bvh_scene_child>::iterator
{
    return std::find_if(
        m_children.begin(),
        m_children.end(),
        [instance](const Bvh_scene_child& child) { return child.instance == instance; }
    );
}

void Bvh_scene::on_child_modified(const Bvh_geometry* geometry)
{
    const auto i = find_child(geometry);
    if (i == m_children.end()) {
        return;
    }
    i->last_modified_tick = m_tick;
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    mark_modified();
}

void Bvh_scene::on_child_modified(const Bvh_instance* instance)
{
    const auto i = find_child(instance);
    if (i == m_children.end()) {
        return;
    }
    i->last_modified_tick = m_tick;
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    mark_modified();
}

void Bvh_scene::on_child_destroyed(const Bvh_geometry* geometry)
{
    const auto i = find_child(geometry);
    if (i == m_children.end()) {
        return;
    }
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    m_children.erase(i);
    mark_modified();
}

void Bvh_scene::on_child_destroyed(const Bvh_instance* instance)
{
    const auto i = find_child(instance);
    if (i == m_children.end()) {
        return;
    }
    if (i->in_pending_tlas) {
        m_build_aborted = true;
    }
    if (i->in_tlas) {
        evict_from_tlas(*i);
    }
    m_children.erase(i);
    mark_modified();
}

void Bvh_scene::add_referencing_instance(Bvh_instance* instance)
{
    ERHE_VERIFY(instance != nullptr);
    const auto i = std::find(m_referencing_instances.begin(), m_referencing_instances.end(), instance);
    if (i == m_referencing_instances.end()) {
        m_referencing_instances.push_back(instance);
    }
}

void Bvh_scene::remove_referencing_instance(Bvh_instance* instance)
{
    const auto i = std::find(m_referencing_instances.begin(), m_referencing_instances.end(), instance);
    if (i != m_referencing_instances.end()) {
        m_referencing_instances.erase(i);
    }
}

void Bvh_scene::mark_modified()
{
    // The bounds of this scene changed, which changes the bounds of every
    // instance which instantiates it. The guard makes a cyclic scene graph
    // terminate.
    if (m_in_propagation) {
        return;
    }
    m_in_propagation = true;
    for (Bvh_instance* instance : m_referencing_instances) {
        instance->notify_parents_modified();
    }
    m_in_propagation = false;
}

void Bvh_scene::commit()
{
    ERHE_PROFILE_FUNCTION();

    ++m_tick;
    update_tlas();
}

void Bvh_scene::update_tlas()
{
    collect_tlas_build();
    if (m_build_task) {
        // A build is running. Until it lands, traversal keeps using whatever
        // the scene has now: the previous BVH, or the linear path.
        return;
    }
    start_tlas_build();
}

void Bvh_scene::collect_tlas_build()
{
    if (!m_build_task || !m_build_task->done.load(std::memory_order_acquire)) {
        return;
    }

    const std::shared_ptr<Tlas_build_task> task = std::move(m_build_task);
    m_build_task.reset();
    clear_pending_build();

    if (m_build_aborted) {
        // A member was modified or detached while the build was running.
        log_scene->trace("Bvh_scene {} scene BVH build discarded", m_debug_label);
        m_build_aborted = false;
        return;
    }
    take_tlas(std::move(task->result));
    m_tlas_static_child_count = task->static_child_count;
}

void Bvh_scene::start_tlas_build()
{
    const std::size_t static_child_count = get_static_child_count(k_static_delay_ticks);
    if (static_child_count < k_min_tlas_children) {
        invalidate_tlas();
        return;
    }
    if (m_tlas_ready &&
        (static_child_count == m_tlas_static_child_count) &&
        (m_tlas_live_member_count == m_tlas_members.size())
    ) {
        return; // nothing settled, moved or was evicted since the last build
    }
    if ((m_tick - m_last_build_tick) < k_rebuild_cooldown_ticks) {
        // Rebuilding only improves traversal quality: children which are not
        // covered are traversed linearly meanwhile.
        return;
    }

    auto task = std::make_shared<Tlas_build_task>();
    task->input              = make_tlas_build_input();
    task->static_child_count = static_child_count;
    if (task->input.members.size() < k_min_tlas_children) {
        // Not enough children with a valid bounding box.
        clear_pending_build();
        invalidate_tlas();
        m_tlas_static_child_count = static_child_count;
        return;
    }

    m_build_task      = task;
    m_build_aborted   = false;
    m_last_build_tick = m_tick;

    tf::Executor* executor = get_executor();
    if (executor != nullptr) {
        executor->silent_async([task]() { build_tlas(*task); });
    } else {
        // No executor injected: build synchronously, so that tests and
        // headless tools stay deterministic.
        build_tlas(*task);
        collect_tlas_build();
    }
}

void Bvh_scene::clear_pending_build()
{
    for (Bvh_scene_child& child : m_children) {
        child.in_pending_tlas = false;
    }
}

auto Bvh_scene::make_tlas_build_input() -> Tlas_build_input
{
    ERHE_PROFILE_FUNCTION();

    Tlas_build_input input;
    for (Bvh_scene_child& child : m_children) {
        child.in_pending_tlas = false;
        if ((m_tick - child.last_modified_tick) < k_static_delay_ticks) {
            continue;
        }
        const erhe::math::Aabb bbox = child.get_bbox();
        if (!bbox.is_valid()) {
            continue;
        }
        child.in_pending_tlas = true;
        input.members.push_back(Tlas_member{.geometry = child.geometry, .instance = child.instance});
        input.bboxes .push_back(Tlas_bbox{to_bvh(bbox.min), to_bvh(bbox.max)});
        input.centers.push_back(to_bvh(bbox.center()));
    }
    return input;
}

void Bvh_scene::build_tlas(Tlas_build_task& task)
{
    ERHE_PROFILE_FUNCTION();

    typename bvh::v2::DefaultBuilder<Tlas_node>::Config config;

    // The scene level BVH is rebuilt whenever children settle or move, so build
    // time matters much more than the last bit of traversal quality.
    config.quality = bvh::v2::DefaultBuilder<Tlas_node>::Quality::Low;

    task.result.tlas    = bvh::v2::DefaultBuilder<Tlas_node>::build(task.input.bboxes, task.input.centers, config);
    task.result.members = task.input.members;
    task.done.store(true, std::memory_order_release);
}

void Bvh_scene::take_tlas(Tlas_build_result&& result)
{
    for (Bvh_scene_child& child : m_children) {
        child.in_tlas = false;
    }

    m_tlas         = std::move(result.tlas);
    m_tlas_members = std::move(result.members);
    m_tlas_ready   = true;

    for (std::size_t member_index = 0, end = m_tlas_members.size(); member_index < end; ++member_index) {
        const Tlas_member& member = m_tlas_members[member_index];
        const auto i = (member.geometry != nullptr) ? find_child(member.geometry) : find_child(member.instance);
        if (i != m_children.end()) {
            i->in_tlas    = true;
            i->tlas_index = member_index;
        } else {
            // The child went away between the snapshot and now.
            m_tlas_members[member_index] = Tlas_member{};
        }
    }

    m_tlas_live_member_count = 0;
    for (const Tlas_member& member : m_tlas_members) {
        if ((member.geometry != nullptr) || (member.instance != nullptr)) {
            ++m_tlas_live_member_count;
        }
    }
    m_last_build_tick = m_tick;

    log_scene->trace("Bvh_scene {} scene BVH built for {} children", m_debug_label, m_tlas_live_member_count);
}

void Bvh_scene::evict_from_tlas(Bvh_scene_child& child)
{
    // The child is removed from the coverage of the scene level BVH and goes
    // back to the linear path. The node bounds it left behind are stale, which
    // only costs a few wasted node tests until the next build - never a wrong
    // result. This is what keeps one moving object from throwing away the
    // BVH built for all the others.
    if (!child.in_tlas) {
        return;
    }
    child.in_tlas = false;
    m_tlas_members[child.tlas_index] = Tlas_member{};
    ERHE_VERIFY(m_tlas_live_member_count > 0);
    --m_tlas_live_member_count;
    if (m_tlas_live_member_count == 0) {
        invalidate_tlas();
    }
}

void Bvh_scene::invalidate_tlas()
{
    if (!m_tlas_ready) {
        return;
    }
    log_scene->trace("Bvh_scene {} scene BVH invalidated", m_debug_label);
    m_tlas_ready = false;
    m_tlas_members.clear();
    m_tlas = Tlas{};
    m_tlas_static_child_count = 0;
    m_tlas_live_member_count  = 0;
    for (Bvh_scene_child& child : m_children) {
        child.in_tlas = false;
    }
}

auto Bvh_scene::get_tlas_member_count() const -> std::size_t
{
    return m_tlas_ready ? m_tlas_live_member_count : 0;
}

auto Bvh_scene::get_tick() const -> uint64_t
{
    return m_tick;
}

auto Bvh_scene::get_static_child_count(const uint64_t delay_ticks) const -> std::size_t
{
    std::size_t count = 0;
    for (const Bvh_scene_child& child : m_children) {
        if ((m_tick - child.last_modified_tick) >= delay_ticks) {
            ++count;
        }
    }
    return count;
}

auto Bvh_scene::get_bbox() const -> erhe::math::Aabb
{
    if (m_in_get_bbox) {
        return erhe::math::Aabb{};
    }
    m_in_get_bbox = true;
    erhe::math::Aabb bbox{};
    for (const Bvh_scene_child& child : m_children) {
        const erhe::math::Aabb child_bbox = child.get_bbox();
        if (child_bbox.is_valid()) {
            bbox.include(child_bbox);
        }
    }
    m_in_get_bbox = false;
    return bbox;
}

auto Bvh_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    // log_frame->trace(
    //     "Bvh_scene {} intersect mask = {:04x} children = {}, ray origin = {}, direction = {}",
    //     m_debug_label, ray.mask, m_children.size(), ray.origin, ray.direction
    // );

    ERHE_PROFILE_FUNCTION();

    return intersect_children(ray, hit, nullptr);
}

auto Bvh_scene::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    return intersect_children(ray, hit, in_instance);
}

auto Bvh_scene::intersect_children(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    // The scene level BVH usually produces a near hit cheaply, which narrows
    // ray.t_far for the linear pass over the recently modified children.
    bool is_hit = m_tlas_ready && intersect_tlas(ray, hit, in_instance);

    for (const Bvh_scene_child& child : m_children) {
        if (child.in_tlas) {
            continue;
        }
        const bool child_is_hit = (child.instance != nullptr)
            ? child.instance->intersect(ray, hit)
            : child.geometry->intersect_instance(ray, hit, in_instance);
        if (child_is_hit) {
            is_hit = true;
        }
    }
    return is_hit;
}

auto Bvh_scene::intersect_tlas(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    ERHE_PROFILE_FUNCTION();

    static constexpr std::size_t stack_size           = 64;
    static constexpr bool        use_robust_traversal = false;

    bvh::v2::Ray<float, 3> bvh_ray{
        to_bvh(ray.origin),
        to_bvh(ray.direction),
        ray.t_near,
        ray.t_far
    };

    bool is_hit = false;
    bvh::v2::SmallStack<Tlas::Index, stack_size> stack;
    m_tlas.intersect<false, use_robust_traversal>(
        bvh_ray,
        m_tlas.get_root().index,
        stack,
        [&](const std::size_t begin, const std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const Tlas_member& member = m_tlas_members[m_tlas.prim_ids[i]];
                if ((member.geometry == nullptr) && (member.instance == nullptr)) {
                    continue; // evicted: traversed linearly instead
                }
                const bool member_is_hit = (member.instance != nullptr)
                    ? member.instance->intersect(ray, hit)
                    : member.geometry->intersect_instance(ray, hit, in_instance);
                if (member_is_hit) {
                    is_hit = true;
                    // Children narrow ray.t_far; feeding that back into the
                    // traversal ray is what makes node culling tighten.
                    bvh_ray.tmax = ray.t_far;
                }
            }
            return false; // closest hit: never stop early
        }
    );
    return is_hit;
}

auto Bvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
