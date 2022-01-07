#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>


namespace erhe::geometry
{

const char* c_str(const glm::vec3::length_type axis)
{
    switch (axis)
    {
        case glm::vec3::length_type{0}: return "X";
        case glm::vec3::length_type{1}: return "Y";
        case glm::vec3::length_type{2}: return "Z";
        default: return "?";
    }
}

template<typename T>
class Pair_entries
{
public:
    class Entry
    {
    public:
        Entry(T primary, T secondary)
            : primary  {primary}
            , secondary{secondary}
        {
        }
        void swap(T lhs, T rhs)
        {
            if (primary == lhs)
            {
                primary = rhs;
            }
            else if (primary == rhs)
            {
                primary = lhs;
            }
            if (secondary == lhs)
            {
                secondary = rhs;
            }
            else if (secondary == rhs)
            {
                secondary = lhs;
            }
        }

        T primary;
        T secondary;
    };

    auto find_primary(const T primary) const -> Entry*
    {
        for (auto& i : entries)
        {
            if (i.primary == primary)
            {
                return &i;
            }
        }
        return nullptr;
    }
    auto find_secondary(const T secondary) -> Entry*
    {
        for (auto& i : entries)
        {
            if (i.secondary == secondary)
            {
                return &i;
            }
        }
        return nullptr;
    }

    void insert(const T primary, const T secondary)
    {
        entries.emplace_back(primary, secondary);
    }

    auto size() const -> size_t
    {
        return entries.size();
    }

    void swap(T lhs, T rhs)
    {
        for (auto& entry : entries)
        {
            entry.swap(lhs, rhs);
        }
    }
    std::vector<Entry> entries;
};

template<typename T>
class Remapper
{
public:
    Remapper(const T size)
        : old_size{size}
        , new_size{size}
        , new_end {size}
    {
        old_from_new.resize(size);
        new_from_old.resize(size);
        old_used.resize(size);

        for (T new_id = 0; new_id < size; ++new_id)
        {
            old_from_new[new_id] = new_id;
        }

        std::fill(old_used.begin(), old_used.end(), false);
    }

    void create_new_from_old_mapping()
    {
        for (T new_id = 0; new_id < new_size; ++new_id)
        {
            const T old_id = old_from_new[new_id];
            new_from_old[old_id] = new_id;
        }
    }

    void dump()
    {
        bool error = false;
        const erhe::log::Indenter scoped_indent;
        for (T old_id = 0; old_id < old_size; ++old_id)
        {
            const T new_id = new_from_old[old_id];
            log_weld.trace("{:2}", new_id);
            if (is_bijection && (old_id != std::numeric_limits<T>::max()) && (old_from_new[new_id] != old_id))
            {
                error = true;
                log_weld.trace("!");
            }
            else
            {
                log_weld.trace(" ");
            }
        }
        log_weld.trace("  < new from old\n");
        for (T old_id = 0; old_id < old_size; ++old_id)
        {
            log_weld.trace("{:2} ", old_id);
        }
        log_weld.trace("  < old\n");
        log_weld.trace("\n");
        log_weld.trace("    \\/  \\/  \\/  \\/  \\/  \\/  \\/  \\/\n");
        log_weld.trace("    /\\  /\\  /\\  /\\  /\\  /\\  /\\  /\\\n");
        log_weld.trace("\n");

        for (T new_id = 0; new_id < old_size; ++new_id)
        {
            log_weld.trace("{:2} ", new_id);
        }
        log_weld.trace("  < new\n");
        for (T new_id = 0; new_id < new_size; ++new_id)
        {
            const T old_id = old_from_new[new_id];
            log_weld.trace("{:2}", old_id);
            if (is_bijection && (old_id != std::numeric_limits<T>::max()) && (new_from_old[old_id] != new_id))
            {
                error = true;
                log_weld.trace("!");
            }
            else
            {
                log_weld.trace(" ");
            }
        }
        log_weld.trace("  < old from new\n");
        if (error)
        {
            log_weld.trace("Errors detected\n");
        }
    }

    auto old_id(const T new_id) const -> T
    {
        return old_from_new[new_id];
    }

    auto new_id(const T old_id) const -> T
    {
        return new_from_old[old_id];
    }

    void swap(const T secondary_new_id, const T keep_new_id)
    {
        ERHE_VERIFY(secondary_new_id != keep_new_id);
        const T secondary_old_id = old_from_new[secondary_new_id];
        const T keep_old_id      = old_from_new[keep_new_id];
        //log_weld.trace("New {:2} old {:2} is being removed - swapping with new {:2} old {:2}\n",
        //                secondary_new_id, secondary_old_id,
        //                keep_new_id, keep_old_id);
        std::swap(
            old_from_new[secondary_new_id],
            old_from_new[keep_new_id]
        );
        std::swap(
            new_from_old[secondary_old_id],
            new_from_old[keep_old_id]
        );

        for (size_t i = 0; i < merge.size(); ++i)
        {
            merge.entries[i].swap(keep_new_id, secondary_new_id);
        }
        for (size_t i = 0; i < eliminate.size(); ++i)
        {
            if (eliminate[i] == keep_new_id)
            {
                eliminate[i] = secondary_new_id;
            }
            else if (eliminate[i] == secondary_new_id)
            {
                eliminate[i] = keep_new_id;
            }
        }
    }

    auto get_next_end(const bool check_used = false) -> T
    {
        for (;;)
        {
            ERHE_VERIFY(new_end > 0);
            --new_end;
            if (check_used && !old_used[old_from_new[new_end]])
            {
                continue;
            }
            return new_end;
        }
    }

    void reorder_to_drop_duplicates()
    {
        log_weld.trace("Merge list:");
        for (auto entry : merge.entries)
        {
            log_weld.trace(" {}->{}", entry.secondary, entry.primary);
        }
        log_weld.trace("\n");

        log_weld.trace("Dropped due to merge:");
        for (size_t i = 0, end = merge.size(); i < end; ++i)
        {
            const auto& entry = merge.entries[i];
            const T secondary_new_id = entry.secondary;
            if (secondary_new_id >= new_end)
            {
                log_weld.trace(" {:2} -> {:2} ", secondary_new_id, secondary_new_id);
                continue;
            }
            const T keep_new_id = get_next_end();
            if (secondary_new_id == keep_new_id)
            {
                log_weld.trace(" {:2} -> {:2} ", secondary_new_id, secondary_new_id);
                continue;
            }
            log_weld.trace(" {:2} -> {:2} ", secondary_new_id, keep_new_id);
            swap(secondary_new_id, keep_new_id);
        }
        log_weld.trace("\n");

        log_weld.trace("Dropped due to eliminate:");
        for (size_t i = 0, end = eliminate.size(); i < end; ++i)
        {
            T secondary_new_id = eliminate[i];
            if (secondary_new_id >= new_end)
            {
                log_weld.trace(" {:2} -> {:2} ", secondary_new_id, secondary_new_id);
                continue;
            }
            T keep_new_id = get_next_end();
            if (secondary_new_id == keep_new_id)
            {
                log_weld.trace(" {:2} -> {:2} ", secondary_new_id, secondary_new_id);
                continue;
            }
            log_weld.trace(" {:2} -> {:2} ", secondary_new_id, keep_new_id);
            swap(secondary_new_id, keep_new_id);
        }
        log_weld.trace("\n");
    }

    void reorder_to_drop_unused()
    {
        log_weld.trace("Usage:\n");
        new_end = 0;
        for (T new_id = 0, end = new_size; new_id < end; ++new_id)
        {
            const T old_id = old_from_new[new_id];
            log_weld.trace("new {:2} old {:2} : {}\n", new_id, old_id, (old_used[old_id] ? "true" : "false"));
            if (old_used[old_id])
            {
                new_end = new_id + 1;
            }
        }

        for (T new_id = 0, end = new_size; new_id < end; ++new_id)
        {
            const T old_id = old_from_new[new_id];
            if (!old_used[old_id])
            {
                T secondary_new_id = new_id;
                if (secondary_new_id >= new_end)
                {
                    log_weld.trace("Dropping unused, end {:2} new {:2} old {:2}\n", new_end, secondary_new_id, old_id);
                    continue;
                }
                T keep_new_id = get_next_end(true);
                if (secondary_new_id == keep_new_id)
                {
                    log_weld.trace("Dropping unused, end {:2} new {:2} old {:2}\n", new_end, secondary_new_id, old_id);
                    continue;
                }
                log_weld.trace(
                    "Dropping unused, end {:2} new {:2} old {:2} -> new {:2} old {:2}\n",
                    new_end,
                    secondary_new_id,
                    old_id,
                    keep_new_id,
                    old_from_new[keep_new_id]
                );
                swap(secondary_new_id, keep_new_id);
            }
        }
        log_weld.trace("\n");
    }

    void for_each_primary_new(
        const T primary_new_id,
        std::function<void(
            const T /*primary_new_id*/,   const T /*primary_old_id*/,
            const T /*secondary_new_id*/, const T /*secondary_old_id*/)
        > callback
    )
    {
        if (!callback)
        {
            return;
        }
        for (size_t i = 0, end = merge.size(); i < end; ++i)
        {
            const auto& entry = merge.entries[i];
            if (entry.primary != primary_new_id)
            {
                continue;
            }
            //const T primary_new_id   = entry.primary;
            ERHE_VERIFY(primary_new_id == entry.primary);
            const T primary_old_id   = old_from_new[primary_new_id];
            const T secondary_new_id = entry.secondary;
            const T secondary_old_id = old_from_new[secondary_new_id];
            callback(primary_new_id, primary_old_id, secondary_new_id, secondary_old_id);
        }
    }

    void merge_pass(
        std::function<
            void(
                T primary_new_id,
                T primary_old_id,
                T secondary_new_id,
                T secondary_old_id
            )
        > swap_callback
    )
    {
        if (!swap_callback)
        {
            return;
        }
        for (size_t i = 0, end = merge.size(); i < end; ++i)
        {
            const auto& entry = merge.entries[i];
            const T primary_new_id   = entry.primary;
            const T primary_old_id   = old_from_new[primary_new_id];
            const T secondary_new_id = entry.secondary;
            const T secondary_old_id = old_from_new[secondary_new_id];
            swap_callback(primary_new_id, primary_old_id, secondary_new_id, secondary_old_id);
        }
    }

    void update_secondary_new_from_old()
    {
        for (size_t i = 0, end = merge.size(); i < end; ++i)
        {
            const auto& entry = merge.entries[i];
            const T primary_new_id   = entry.primary;
            //const T primary_old_id   = old_from_new[primary_new_id];
            const T secondary_new_id = entry.secondary;
            const T secondary_old_id = old_from_new[secondary_new_id];
            new_from_old[secondary_old_id] = primary_new_id;
        }
        is_bijection = false;
    }

    void trim(std::function<void(const T new_id, const T old_id)> remove_callback)
    {
        if (remove_callback)
        {
            for (T new_id = new_end; new_id < old_size; ++new_id)
            {
                //log_weld.trace("Removing new {} old {}\n", new_id, old_id(new_id));
                //for (T keep_id : new_from_old) // old may may not be mapped to new which is being deleted
                //{
                //    ERHE_VERIFY(new_id != keep_id);
                //}
                remove_callback(new_id, old_id(new_id));
            }
        }
        trim();
    }

    void trim()
    {
        //for (T new_id = merge_end; new_id < old_size; ++new_id)
        //{
        //    log_weld.trace("Removing new {} old {}\n", new_id, old_id(new_id));
        //    for (T keep_id : new_from_old) // old may may not be mapped to new which is being deleted
        //    {
        //        ERHE_VERIFY(new_id != keep_id);
        //    }
        //}
        is_bijection = false;
        new_size = new_end;
    }

    void use_old(const T old_id)
    {
        old_used[old_id] = true;
    }

    T                 old_size{0};
    T                 new_size{0};
    T                 new_end{0};
    bool              is_bijection{true};
    std::vector<bool> old_used;
    std::vector<T>    old_from_new;
    std::vector<T>    new_from_old;
    Pair_entries<T>   merge;
    std::vector<T>    eliminate;
};

void Geometry::weld(const Weld_settings& weld_settings)
{
    using namespace glm;

    ERHE_PROFILE_FUNCTION

    class Point_attribute_maps
    {
    public:
        Property_map<Point_id, glm::vec3>* locations {nullptr};
        Property_map<Point_id, glm::vec3>* normals   {nullptr};
        Property_map<Point_id, glm::vec3>* tangents  {nullptr};
        Property_map<Point_id, glm::vec3>* bitangents{nullptr};
        Property_map<Point_id, glm::vec2>* texcoords {nullptr};
    };
    const Point_attribute_maps point_attribute_maps
    {
        .locations  = point_attributes().find<vec3>(c_point_locations),
        .normals    = point_attributes().find<vec3>(c_point_normals),
        .tangents   = point_attributes().find<vec3>(c_point_tangents),
        .bitangents = point_attributes().find<vec3>(c_point_bitangents),
        .texcoords  = point_attributes().find<vec2>(c_point_texcoords)
    };

    class Polygon_attribute_maps
    {
    public:
        Property_map<Polygon_id, glm::vec3>* centroids{nullptr};
        Property_map<Polygon_id, glm::vec3>* normals  {nullptr};
    };

    const Polygon_attribute_maps polygon_attribute_maps{
        .centroids  = polygon_attributes().find<vec3>(c_polygon_centroids),
        .normals    = polygon_attributes().find<vec3>(c_polygon_normals)
    };

    glm::vec3 min_corner(std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max());
    glm::vec3 max_corner(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    //log_weld.trace("Points before sort:\n");
    for (Point_id point_id = 0; point_id < m_next_point_id; ++point_id)
    {
        if (!point_attribute_maps.locations->has(point_id))
        {
            continue;
        }
        const vec3 position = point_attribute_maps.locations->get(point_id);
        //log_weld.trace("    {:2}: {}\n", point_id, position);
        min_corner = glm::min(min_corner, position);
        max_corner = glm::max(max_corner, position);
    }

    const glm::vec3 bounding_box_size0 = max_corner - min_corner;
    const auto      axis0 = erhe::toolkit::max_axis_index(bounding_box_size0);
    glm::vec3 bounding_box_size1 = bounding_box_size0;
    bounding_box_size1[axis0] = 0.0f;
    const auto axis1 = erhe::toolkit::max_axis_index(bounding_box_size1);
    glm::vec3 bounding_box_size2 = bounding_box_size1;
    bounding_box_size2[axis1] = 0.0f;
    const auto axis2 = erhe::toolkit::max_axis_index(bounding_box_size2);

    log_weld.trace("Primary   axis = {} {}\n", axis0, c_str(axis0));
    log_weld.trace("Secondary axis = {} {}\n", axis1, c_str(axis1));
    log_weld.trace("Tertiary  axis = {} {}\n", axis2, c_str(axis2));

    //debug_trace();
    sanity_check();

    log_weld.trace("Polygon processing:\n");
    {
        const erhe::log::Indenter scope_indent;

        Remapper<Polygon_id> polygon_remapper{m_next_polygon_id};

        std::sort(
            polygon_remapper.old_from_new.begin(),
            polygon_remapper.old_from_new.end(),
            [axis0, axis1, axis2, polygon_attribute_maps](const Polygon_id& lhs, const Polygon_id& rhs)
            {
                if (!polygon_attribute_maps.centroids->has(lhs) || !polygon_attribute_maps.centroids->has(rhs))
                {
                    return false;
                }
                const glm::vec3 position_lhs = polygon_attribute_maps.centroids->get(lhs);
                const glm::vec3 position_rhs = polygon_attribute_maps.centroids->get(rhs);
                if (position_lhs[axis0] != position_rhs[axis0])
                {
                    return position_lhs[axis0] < position_rhs[axis0];;
                }
                if (position_lhs[axis1] != position_rhs[axis1])
                {
                    return position_lhs[axis1] < position_rhs[axis1];;
                }
                return position_lhs[axis2] < position_rhs[axis2];;
            }
        );

        polygon_remapper.create_new_from_old_mapping();

        //log_weld.trace("Sorted polygon centroids:\n");
        for (Polygon_id new_polygon_id = 0; new_polygon_id < m_next_polygon_id; ++new_polygon_id)
        {
            const Polygon_id old_polygon_id = polygon_remapper.old_id(new_polygon_id);
            if (!polygon_attribute_maps.centroids->has(old_polygon_id))
            {
                continue;
            }
            //const vec3 centroid = polygon_attribute_maps.centroids->get(old_polygon_id);
            //log_weld.trace("    {:2} (old {:2}: centroid {}", new_polygon_id, old_polygon_id, centroid);
            if (polygon_attribute_maps.normals->has(old_polygon_id))
            {
                //const vec3 normal = polygon_attribute_maps.normals->get(old_polygon_id);
                //log_weld.trace(" normal {}", normal);
            }
            //log_weld.trace("\n");
        }

        //log_weld.trace("Polygon remapping before merging / eliminating:\n");
        //polygon_remapper.dump();

        // Scan for polygon merge/eliminate
        class Polygon_data
        {
        public:
            Polygon_data(Polygon_id old_id, const Polygon_attribute_maps& attribute_maps)
            {
                ERHE_VERIFY((attribute_maps.centroids != nullptr) && attribute_maps.centroids->has(old_id));
                ERHE_VERIFY((attribute_maps.normals   != nullptr) && attribute_maps.normals  ->has(old_id));
                centroid = attribute_maps.centroids->get(old_id);
                normal   = attribute_maps.normals  ->get(old_id);
            }
            glm::vec3 centroid{0.0f, 0.0f, 0.0f};
            glm::vec3 normal  {0.0f, 0.0f, 0.0f};
        };
        for (
            Polygon_id primary_new_id = 0;
            primary_new_id < m_next_polygon_id;
            ++primary_new_id
        )
        {
            const Polygon_id primary_old_id = polygon_remapper.old_id(primary_new_id);
            if (polygon_remapper.merge.find_secondary(primary_new_id) != nullptr)
            {
                continue;
            }
            const Polygon_data primary_attributes(primary_old_id, polygon_attribute_maps);
            //log_weld.trace(
            //    "primary   new {:2} old {:2} centroid {} normal {}\n",
            //    primary_new_id,
            //    primary_old_id,
            //    primary_attributes.centroid,
            //    primary_attributes.normal
            //);
            for (
                Polygon_id secondary_new_id = primary_new_id + 1;
                secondary_new_id < m_next_polygon_id;
                ++secondary_new_id
            )
            {
                if (polygon_remapper.merge.find_secondary(secondary_new_id) != nullptr)
                {
                    continue;
                }
                const Polygon_id   secondary_old_id = polygon_remapper.old_id(secondary_new_id);
                const Polygon_data secondary_attributes(secondary_old_id, polygon_attribute_maps);
                //log_weld.trace(
                //    "secondary new {:2} old {:2} centroid {} normal {}\n",
                //    secondary_new_id,
                //    secondary_old_id,
                //    secondary_attributes.centroid,
                //    secondary_attributes.normal
                //);

                const float diff = std::abs(primary_attributes.centroid[axis0] - secondary_attributes.centroid[axis0]);
                if (diff > 0.001f)
                {
                    //log_weld.trace(
                    //    "group end: primary new {:2} old {:2} secondary new {:2} old {:2} diff {}\n",
                    //    primary_new_id, primary_old_id, secondary_new_id, secondary_old_id, diff
                    //);
                    break;
                }

                const float distance = glm::distance(primary_attributes.centroid, secondary_attributes.centroid);
                if (distance > weld_settings.max_point_distance)
                {
                    //log_weld.trace(
                    //    "primary new {:2} old {:2} secondary new {:2} old {:2} centroid distance {}\n",
                    //    primary_new_id, primary_old_id, secondary_new_id, secondary_old_id, distance
                    //);
                    continue;
                }
                const float dot_product = glm::dot(primary_attributes.normal, secondary_attributes.normal);
                if (std::abs(dot_product) < weld_settings.min_normal_dot_product)
                {
                    //log_weld.trace(
                    //    "primary new {:2} old {:2} secondary new {:2} old {:2} normal dot product {}\n",
                    //    primary_new_id, primary_old_id, secondary_new_id, secondary_old_id, dot_product
                    //);
                    continue;
                }

                // Merge case: identical polygons
                // TODO check polygons have matching corners
                if (dot_product >= weld_settings.min_normal_dot_product)
                {
                    //log_weld.trace(
                    //    "merging new polygon {:2} old polygon {:2} to new polygon {:2} old polygon {:2}\n",
                    //    secondary_new_id, secondary_old_id, primary_new_id, primary_old_id
                    //);
                    polygon_remapper.merge.insert(primary_new_id, secondary_new_id);
                }

                // Elimination case: opposite polygons
                // TODO check polygons have matching corners
                if (dot_product <= -weld_settings.min_normal_dot_product)
                {
                    //log_weld.trace(
                    //    "eliminate polygons new {:2} old polygon {:2} and new polygon {:2} old polygon {:2}\n",
                    //    secondary_new_id, secondary_old_id, primary_new_id, primary_old_id
                    //);
                    polygon_remapper.eliminate.push_back(primary_new_id);
                    polygon_remapper.eliminate.push_back(secondary_new_id);
                    //{
                    //    const erhe::log::Indenter scoped_indent;
                    //    log_weld.trace("centroid {} vs {}\n", primary_attributes.centroid, current.centroid);
                    //    log_weld.trace("normal {} vs {}\n", primary_attributes.normal, current.normal);
                    //}
                }
            }
        }

        auto old_polygons = polygons;

        //log_weld.trace("Merged {} polygons\n", polygon_remapper.merge.size());
        //log_weld.trace("Polygon remapping after merge, before removing duplicates:\n");
        //polygon_remapper.dump();
        polygon_remapper.reorder_to_drop_duplicates();
        //log_weld.trace("Polygon remapping after reorder_to_drop_duplicates:\n");
        //polygon_remapper.dump();
        polygon_remapper.update_secondary_new_from_old();
        //log_weld.trace("Polygon remapping after update_secondary_new_from_old:\n");
        //polygon_remapper.dump();
        polygon_remapper.trim(
            [/*this*/](Point_id new_id, Point_id old_id)
            {
#if 0 // Disable trace logging
                log_weld.trace("Dropping polygon new {:2} old {:2} corners:", new_id, old_id);
                const Polygon& polygon = polygons[old_id];
                for (
                    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
                    end = polygon.first_polygon_corner_id + polygon.corner_count;
                    polygon_corner_id < end;
                    ++polygon_corner_id)
                {
                    const Corner_id corner_id = polygon_corners[polygon_corner_id];
                    log_weld.trace(" {:2}", corner_id);
                }
                log_weld.trace("\n");
#else
                static_cast<void>(new_id);
                static_cast<void>(old_id);
#endif
            }
        );
        m_next_polygon_id = polygon_remapper.new_size;

        // Remap polygons
        auto old_polygon_corners = polygon_corners;
        uint32_t next_polygon_corner = 0;
        for (
            Polygon_id new_polygon_id = 0, new_polygon_id_end = get_polygon_count();
            new_polygon_id < new_polygon_id_end;
            ++new_polygon_id
        )
        {
            const Polygon_id old_polygon_id = polygon_remapper.old_id(new_polygon_id);
            //log_weld.trace("Polygon new {:2} from old {:2} corners:", new_polygon_id, old_polygon_id);
            Polygon& old_polygon = old_polygons[old_polygon_id];
            Polygon& new_polygon = polygons[new_polygon_id];
            polygons[new_polygon_id].first_polygon_corner_id = next_polygon_corner;
            polygons[new_polygon_id].corner_count            = 0;
            for (uint32_t i = 0, end = old_polygon.corner_count; i < end; ++i)
            {
                const Polygon_corner_id old_polygon_corner_id = old_polygon.first_polygon_corner_id + i;                
                const Polygon_corner_id new_polygon_corner_id = new_polygon.first_polygon_corner_id + new_polygon.corner_count;
                const Corner_id         corner_id             = old_polygon_corners[old_polygon_corner_id];
                polygon_corners[new_polygon_corner_id] = corner_id;
                ++new_polygon.corner_count;
                ++next_polygon_corner;
                //log_weld.trace(" {}", corner_id);
            }
            polygon_remapper.for_each_primary_new(
                new_polygon_id,
                [this, new_polygon_id, &old_polygons, &old_polygon_corners, &next_polygon_corner]
                (
                    Polygon_id primary_new_id,
                    Polygon_id /*primary_old_id*/, 
                    Polygon_id /*secondary_new_id*/,
                    Polygon_id secondary_old_id
                )
                {
                    ERHE_VERIFY(new_polygon_id == primary_new_id);
                    Polygon& primary_new   = polygons    [primary_new_id];
                    Polygon& secondary_old = old_polygons[secondary_old_id];
                    ERHE_VERIFY(secondary_old.corner_count > 0);
                    for (uint32_t i = 0, end = secondary_old.corner_count; i < end; ++i)
                    {
                        Polygon_corner_id old_polygon_corner_id = secondary_old.first_polygon_corner_id + i;
                        Polygon_corner_id new_polygon_corner_id = primary_new.first_polygon_corner_id + primary_new.corner_count;
                        ERHE_VERIFY(new_polygon_corner_id == next_polygon_corner);
                        Corner_id corner_id = old_polygon_corners[old_polygon_corner_id];
                        polygon_corners[new_polygon_corner_id] = corner_id;
                        ++primary_new.corner_count;
                        ++next_polygon_corner;
                        //log_weld.trace(" {}", corner_id);
                    }
                });
            //log_weld.trace("\n");
        }
        m_next_polygon_corner_id = next_polygon_corner;
        polygon_corners.resize(m_next_polygon_corner_id);

        // Remap corner polygons
        for (
            Corner_id corner_id = 0, corner_id_end = get_corner_count();
            corner_id < corner_id_end;
            ++corner_id
        )
        {
            Corner& corner = corners[corner_id];
            Polygon_id old_polygon_id = corner.polygon_id;
            ERHE_VERIFY(old_polygon_id != std::numeric_limits<Polygon_id>::max());
            Polygon_id new_polygon_id = polygon_remapper.new_id(old_polygon_id);
            corner.polygon_id = new_polygon_id;
            //log_weld.trace("Corner {:2} polygon {:2} -> {:2}\n", corner_id, old_polygon_id, new_polygon_id);
        }

        polygon_attributes().remap_keys(polygon_remapper.old_from_new);
        polygon_attributes().trim(get_polygon_count());
    }

    //debug_trace();
    //sanity_check();

    //log_weld.trace("Point processing:\n");
    {
        const erhe::log::Indenter scope_indent;

        Remapper<Point_id> point_remapper{m_next_point_id};
        std::sort(
            point_remapper.old_from_new.begin(),
            point_remapper.old_from_new.end(),
            [axis0, axis1, axis2, point_attribute_maps](const Point_id& lhs, const Point_id& rhs)
            {
                if (!point_attribute_maps.locations->has(lhs) || !point_attribute_maps.locations->has(rhs))
                {
                    return false;
                }
                const glm::vec3 position_lhs = point_attribute_maps.locations->get(lhs);
                const glm::vec3 position_rhs = point_attribute_maps.locations->get(rhs);
                if (position_lhs[axis0] != position_rhs[axis0])
                {
                    const bool is_less = position_lhs[axis0] < position_rhs[axis0];
                    //log_weld.trace(
                    //    "{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                    //    lhs, rhs, position_lhs, axis0, is_less ? "< " : ">=", position_rhs, axis0
                    //);
                    return is_less;
                }
                const bool is_not_same = position_lhs[axis1] != position_rhs[axis1];
                //log_weld.trace(
                //   "position_lhs[axis1] = {}, position_rhs[axis1] = {}, is not same = {}\n",
                //   position_lhs[axis1], position_rhs[axis1], is_not_same
                //);
                if (is_not_same)
                {
                    const bool is_less = position_lhs[axis1] < position_rhs[axis1];
                    //log_weld.trace(
                    //    "{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                    //    lhs, rhs, position_lhs, axis1, is_less ? "< " : ">=", position_rhs, axis1
                    //);
                    return is_less;
                }
                const bool is_less = position_lhs[axis2] < position_rhs[axis2];
                //log_weld.trace(
                //    "{:2} vs {:2} {} [ {} ] {} {} [ {} ]\n",
                //    lhs, rhs, position_lhs, axis2, is_less ? "< " : ">=", position_rhs, axis2
                //);
                return is_less;
            }
        );

        // log_weld.trace("Points after sort:\n");
        // for (Point_id new_point_id = 0; new_point_id < m_next_point_id; ++new_point_id)
        // {
        //     Point_id old_point_id = new_to_old_point_id[new_point_id];
        //     if (!point_attribute_maps.locations->has(old_point_id))
        //     {
        //         continue;
        //     }
        //     vec3 position = point_attribute_maps.locations->get(old_point_id);
        //     log_weld.trace("    new {:2} old {:2}: {}\n", new_point_id, old_point_id, position);
        // }
        point_remapper.create_new_from_old_mapping();

        //log_weld.trace("Point remapping after sort, before merging:\n");
        //point_remapper.dump();

        // Scan for mergable points
        class Point_data
        {
        public:
            Point_data(const Point_id old_id, const Point_attribute_maps& attribute_maps)
            {
                if ((attribute_maps.locations  != nullptr) && attribute_maps.locations ->has(old_id)) position  = attribute_maps.locations ->get(old_id);
                if ((attribute_maps.normals    != nullptr) && attribute_maps.normals   ->has(old_id)) normal    = attribute_maps.normals   ->get(old_id);
                if ((attribute_maps.tangents   != nullptr) && attribute_maps.tangents  ->has(old_id)) tangent   = attribute_maps.tangents  ->get(old_id); 
                if ((attribute_maps.bitangents != nullptr) && attribute_maps.bitangents->has(old_id)) bitangent = attribute_maps.bitangents->get(old_id);  
                if ((attribute_maps.texcoords  != nullptr) && attribute_maps.texcoords ->has(old_id)) texcoord  = attribute_maps.texcoords ->get(old_id); 
            }
            std::optional<glm::vec3> position;
            std::optional<glm::vec3> normal;
            std::optional<glm::vec3> tangent;
            std::optional<glm::vec3> bitangent;
            std::optional<glm::vec2> texcoord;
        };

        for (Point_id primary_new_id = 0; primary_new_id < m_next_point_id; ++primary_new_id)
        {
            const Point_id primary_old_id = point_remapper.old_id(primary_new_id);

            if (point_remapper.merge.find_secondary(primary_new_id) != nullptr)
            {
                //log_weld.trace(
                //    "Span start {} old point {} has already been removed/merged\n",
                //    span_start, old_reference_point_id
                //);
                continue;
            }

            const Point_data primary_attributes(primary_old_id, point_attribute_maps);
            //log_weld.trace("span start: new {:2} old {:2} position {}\n", span_start, new_to_old_point_id[span_start], reference.position.value());
            for (
                Point_id secondary_new_id = primary_new_id + 1;
                secondary_new_id < m_next_point_id;
                ++secondary_new_id
            )
            {
                const Point_id secondary_old_id = point_remapper.old_id(secondary_new_id);
                if (point_remapper.merge.find_secondary(secondary_new_id) != nullptr)
                {
                    //log_weld.trace(
                    //    "New point {} old point {} has already been removed/merged\n",
                    //    secondary_new_id, secondary_old_id
                    //);
                    continue;
                }
                const Point_data secondary_attributes(secondary_old_id, point_attribute_maps);
                //log_weld.trace("new {:2} old {:2} current position {}\n", secondary_new_id, secondary_old_id, secondary_attributes.position.value());

                const float diff = std::abs(primary_attributes.position.value()[axis0] - secondary_attributes.position.value()[axis0]);
                if (diff > 0.001f)
                {
                    //log_weld.trace("span {:2} .. {:2}  {} .. {}\n", span_start, new_point_id, reference.position.value(), current.position.value());
                    //log_weld.trace(
                    //    "primary axis diff: span {:2} new point {:2} - {} vs. {}, diff {}\n",
                    //    span_start, new_point_id, reference.position.value(), current.position.value(), diff
                    //);
                    break;
                }

                if (primary_attributes.position.has_value() && secondary_attributes.position.has_value())
                {
                    const float distance = glm::distance(primary_attributes.position.value(), secondary_attributes.position.value());
                    if (distance > weld_settings.max_point_distance)
                    {
                        //log_weld.trace(
                        //    "position distance too large for span {:2} point {:2} - {} vs. {}, distance {}\n",
                        //    span_start, new_point_id, reference.position.value(), current.position.value(), distance
                        //);
                        continue;
                    }
                }

#if 0
                if (primary_attributes.normal.has_value() && secondary_attributes.normal.has_value())
                {
                    float dot_product = glm::dot(primary_attributes.normal.value(), secondary_attributes.normal.value());
                    if (dot_product < weld_settings.min_normal_dot_product)
                    {
                        //log_weld.trace("normal dot product too small for span {:2} point {:2} - {} vs. {}, dot product {}\n",
                        //               span_start, new_point_id, reference.normal.value(), current.normal.value(), dot_product);
                        continue;
                    }
                }
                if (primary_attributes.tangent.has_value() && secondary_attributes.tangent.has_value())
                {
                    float dot_product = glm::dot(primary_attributes.tangent.value(), secondary_attributes.tangent.value());
                    if (dot_product < weld_settings.min_normal_dot_product)
                    {
                        //log_weld.trace("tangent dot product too small for span {:2} point {:2} - {} vs. {}, dot product {}\n",
                        //               span_start, new_point_id, reference.tangent.value(), current.tangent.value(), dot_product);
                        continue;
                    }
                }
                if (primary_attributes.bitangent.has_value() && secondary_attributes.bitangent.has_value())
                {
                    float dot_product = glm::dot(primary_attributes.bitangent.value(), secondary_attributes.bitangent.value());
                    if (dot_product < weld_settings.min_normal_dot_product)
                    {
                        //log_weld.trace("bitangent dot product too small for span {:2} point {:2} - {} vs. {}, dot product {}\n",
                        //               span_start, new_point_id, reference.bitangent.value(), current.bitangent.value(), dot_product);
                        continue;
                    }
                }
                if (primary_attributes.texcoord.has_value() && secondary_attributes.texcoord.has_value())
                {
                    float distance = glm::distance(primary_attributes.texcoord.value(), secondary_attributes.texcoord.value());
                    if (distance > weld_settings.max_texcoord_distance)
                    {
                        //log_weld.trace("texcoord distance {} too large\n", distance);
                        continue;
                    }
                }
#endif

                log_weld.trace(
                    "merging new point {:2} old point {:2} to new point {:2} old point {:2}\n",
                    secondary_new_id, secondary_old_id, primary_new_id, primary_old_id
                );
                point_remapper.merge.insert(primary_new_id, secondary_new_id);
            }
        }

        log_weld.trace("Merged {} points\n", point_remapper.merge.size());
        //log_weld.trace("Point remapping after merge, before removing duplicate points:\n");
        //point_remapper.dump();
        //debug_trace();
        //sanity_check();

        point_remapper.reorder_to_drop_duplicates();
        //point_remapper.dump();
        //debug_trace();
        //sanity_check();

        point_remapper.update_secondary_new_from_old();
        //log_weld.trace("Points before trim:\n");
        //point_remapper.dump();
        point_remapper.trim();
        m_next_point_id = point_remapper.new_size;
        //log_weld.trace("Points after trim:\n");
        //point_remapper.dump();

        // Remap points
        auto old_points = points;
        auto old_point_corners = point_corners;
        uint32_t next_point_corner = 0;
        for (
            Point_id new_point_id = 0, new_point_end = get_point_count();
            new_point_id < new_point_end;
            ++new_point_id
        )
        {
            const Point_id old_point_id = point_remapper.old_id(new_point_id);
            log_weld.trace("Point new {:2} from old {:2} corners:", new_point_id, old_point_id);
            Point& old_point = old_points[old_point_id];
            Point& new_point = points[new_point_id];
            points[new_point_id].first_point_corner_id = next_point_corner;
            points[new_point_id].corner_count          = 0;
            for (uint32_t i = 0, i_end = old_point.corner_count; i < i_end; ++i)
            {
                const Point_corner_id old_point_corner_id = old_point.first_point_corner_id + i;
                const Point_corner_id new_point_corner_id = new_point.first_point_corner_id + new_point.corner_count;
                ERHE_VERIFY(new_point_corner_id == next_point_corner);
                Corner_id corner_id = old_point_corners[old_point_corner_id];
                Corner&   corner    = corners[corner_id];
                if (corner.polygon_id >= m_next_polygon_id)
                {
                    //log_weld.trace(" !{:2}", corner_id);
                    continue;
                }
                point_corners[new_point_corner_id] = corner_id;
                ++new_point.corner_count;
                ++new_point.reserved_corner_count;
                ++next_point_corner;
                //log_weld.trace("  {:2}", corner_id);
            }
            point_remapper.for_each_primary_new(
                new_point_id,
                [this, new_point_id, &old_points, &old_point_corners, &next_point_corner]
                (
                    const Polygon_id primary_new_id,
                    const Polygon_id /*primary_old_id*/, 
                    const Polygon_id /*secondary_new_id*/,
                    const Polygon_id secondary_old_id
                )
                {
                    ERHE_VERIFY(new_point_id == primary_new_id);
                    Point& primary_new         = points    [primary_new_id];
                    const Point& secondary_old = old_points[secondary_old_id];
                    ERHE_VERIFY(secondary_old.corner_count > 0);
                    for (uint32_t i = 0, end = secondary_old.corner_count; i < end; ++i)
                    {
                        const Point_corner_id old_point_corner_id = secondary_old.first_point_corner_id + i;
                        const Point_corner_id new_point_corner_id = primary_new.first_point_corner_id + primary_new.corner_count;
                        const Corner_id       corner_id           = old_point_corners[old_point_corner_id];
                        const Corner&         corner              = corners[corner_id];
                        if (corner.polygon_id >= m_next_polygon_id)
                        {
                            //log_weld.trace(" !{:2}", corner_id);
                            continue;
                        }

                        point_corners[new_point_corner_id]  = corner_id;
                        ++primary_new.corner_count;
                        ++next_point_corner;
                        //log_weld.trace("  {:2}", corner_id);
                    }
                });
            //log_weld.trace("\n");
        }
        m_next_point_corner_reserve = next_point_corner;
        point_corners.resize(m_next_point_corner_reserve);

        // Remap corner points
        for (Corner_id corner_id = 0, end = get_corner_count(); corner_id < end; ++corner_id)
        {
            Corner& corner = corners[corner_id];
            const Point_id old_point_id = corner.point_id;
            ERHE_VERIFY(old_point_id != std::numeric_limits<Point_id>::max());
            const Point_id new_point_id = point_remapper.new_id(old_point_id);
            corner.point_id = new_point_id;
            //log_weld.trace("Corner {:2} point {:2} -> {:2}\n", corner_id, old_point_id, new_point_id);
        }

        point_attributes().remap_keys(point_remapper.old_from_new);
        point_attributes().trim(get_point_count());
    }

    //debug_trace();
    //sanity_check();

    // Remove unused corners
    //log_weld.trace("Corner processing:\n");
    {
        const log::Indenter scope_indent;

        Remapper<Corner_id> corner_remapper{get_corner_count()};
        for (
            Point_id point_id = 0, point_id_end = get_point_count();
            point_id < point_id_end;
            ++point_id
        )
        {
            const Point& point = points[point_id];
            for (
                Point_corner_id point_corner_id = point.first_point_corner_id,
                end = point.first_point_corner_id + point.corner_count;
                point_corner_id < end;
                ++point_corner_id
            )
            {
                const Corner_id corner_id = point_corners[point_corner_id];
                ERHE_VERIFY(corner_id != std::numeric_limits<Corner_id>::max());
                corner_remapper.use_old(corner_id);
            }
        }

        for (
            Polygon_id polygon_id = 0, polygon_id_end = get_polygon_count();
            polygon_id < polygon_id_end;
            ++polygon_id
        )
        {
            const Polygon& polygon = polygons[polygon_id];
            for (
                Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
                end = polygon.first_polygon_corner_id + polygon.corner_count;
                polygon_corner_id < end;
                ++polygon_corner_id
            )
            {
                const Corner_id corner_id = polygon_corners[polygon_corner_id];
                ERHE_VERIFY(corner_id != std::numeric_limits<Corner_id>::max());
                corner_remapper.use_old(corner_id);
            }
        }

        std::sort(
            corner_remapper.old_from_new.begin(),
            corner_remapper.old_from_new.end(),
            [&corner_remapper, this]
            (const Corner_id& lhs, const Corner_id& rhs)
            {
                // Drop corners pointing to removed polygons
                if (corner_remapper.old_used[lhs] && !corner_remapper.old_used[rhs])
                {
                    ERHE_VERIFY(corners[lhs].polygon_id < m_next_polygon_id);
                    return true;
                }
                return false;
            }
        );

        corner_remapper.create_new_from_old_mapping();
        //log_weld.trace("\nInitial corner remapping:\n");
        //corner_remapper.dump();
        corner_remapper.reorder_to_drop_unused();
        //log_weld.trace("\nCorner remapping after reorder_to_drop_unused():\n");
        //corner_remapper.dump();
        corner_remapper.trim();
        //log_weld.trace("\nCorner remapping after trim_new():\n");
        //corner_remapper.dump();
        m_next_corner_id = corner_remapper.new_size;

        // Remap corners
        //log_weld.trace("Corner renaming:\n");
        {
            const log::Indenter scope_indent_inner;

            auto old_corners = corners;
            for (
                Corner_id new_corner_id = 0, end = m_next_corner_id;
                new_corner_id < end;
                ++new_corner_id
            )
            {
                const Corner_id old_corner_id = corner_remapper.old_id(new_corner_id);
                //log_weld.trace("Corner new {:2} from old {:2}\n", new_corner_id, old_corner_id);
                corners[new_corner_id] = old_corners[old_corner_id];
            }

            corner_attributes().remap_keys(corner_remapper.old_from_new);
            corner_attributes().trim(get_corner_count());

            for (
                Point_id point_id = 0, point_id_end = get_point_count();
                point_id < point_id_end;
                ++point_id
            )
            {
                const Point& point = points[point_id];
                //log_weld.trace("Point {:2} corners:", point_id);
                const log::Indenter point_scope_indent;
                for (
                    Point_corner_id point_corner_id = point.first_point_corner_id,
                    end = point.first_point_corner_id + point.corner_count;
                    point_corner_id < end;
                    ++point_corner_id
                )
                {
                    const Corner_id old_corner_id = point_corners[point_corner_id];
                    const Corner_id new_corner_id = corner_remapper.new_id(old_corner_id);
                    //log_weld.trace(" {:2} -> {:2}", old_corner_id, new_corner_id);
                    point_corners[point_corner_id] = new_corner_id;
                }
                //log_weld.trace("\n");
            }

            for (
                Polygon_id polygon_id = 0, polygon_id_end = get_polygon_count();
                polygon_id < polygon_id_end;
                ++polygon_id
            )
            {
                const Polygon& polygon = polygons[polygon_id];
                //log_weld.trace("Polygon {:2} corners:", polygon_id);
                const log::Indenter polygon_scope_indent;
                for (
                    Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
                    end = polygon.first_polygon_corner_id + polygon.corner_count;
                    polygon_corner_id < end;
                    ++polygon_corner_id
                )
                {
                    const Corner_id old_corner_id = polygon_corners[polygon_corner_id];
                    const Corner_id new_corner_id = corner_remapper.new_id(old_corner_id);
                    //log_weld.trace(" {:2} -> {:2}", old_corner_id, new_corner_id);
                    polygon_corners[polygon_corner_id] = new_corner_id;
                }
                //log_weld.trace("\n");
            }
        }
    }

    //debug_trace();
    sort_point_corners();
    sanity_check();

    log_weld.trace("merge done\n");
}

}
