#include "tiles.hpp"

#include "hextiles.hpp"
#include "hextiles_log.hpp"
#include "file_util.hpp"
#include "map.hpp"
#include "tile_shape.hpp"
#include "types.hpp"

#include "erhe_verify/verify.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <sstream>

namespace hextiles
{

Tiles::Tiles()
{
    // Multishape groups
    load_terrain_defs();
    load_terrain_group_defs();
    load_terrain_replacement_rule_defs();
    load_unit_defs();
}

using json = nlohmann::json;

auto Tiles::get_terrain_type(terrain_t terrain) const -> const Terrain_type&
{
    ERHE_VERIFY(terrain < m_terrain_types.size());
    return m_terrain_types[terrain];
}

auto Tiles::get_terrain_type(terrain_t terrain) -> Terrain_type&
{
    ERHE_VERIFY(terrain < m_terrain_types.size());
    return m_terrain_types[terrain];
}

auto Tiles::get_terrain_type_count() const -> size_t
{
    return m_terrain_types.size();
}

auto Tiles::get_terrain_group_count() const -> size_t
{
    return m_terrain_groups.size();
}

auto Tiles::get_terrain_replacement_rule_count() const -> size_t
{
    return m_terrain_replacement_rules.size();
}

auto Tiles::get_unit_type_count() const -> size_t
{
    return m_unit_types.size();
}

auto Tiles::get_unit_type(unit_t unit) const -> const Unit_type&
{
    ERHE_VERIFY(unit < m_unit_types.size());
    return m_unit_types[unit];
}

auto Tiles::get_unit_type(unit_t unit) -> Unit_type&
{
    ERHE_VERIFY(unit < m_unit_types.size());
    return m_unit_types[unit];
}

auto Tiles::get_city_unit_type(int city_size) const -> unit_t
{
    for (unit_t t = 1; t < m_unit_types.size(); ++t) {
        if (m_unit_types[t].city_size == city_size) {
            return t;
        }
    }
    return 0;
}

auto Tiles::get_terrain_group(size_t group) const -> const Terrain_group&
{
    ERHE_VERIFY(group < m_terrain_groups.size());
    return m_terrain_groups.at(group);
}

auto Tiles::get_terrain_group(size_t group) -> Terrain_group&
{
    ERHE_VERIFY(group < m_terrain_groups.size());
    return m_terrain_groups.at(group);
}

auto Tiles::get_terrain_replacement_rule(size_t replacement_rule) const -> const Terrain_replacement_rule&
{
    ERHE_VERIFY(replacement_rule < m_terrain_replacement_rules.size());
    return m_terrain_replacement_rules.at(replacement_rule);
}

auto Tiles::get_terrain_replacement_rule(size_t replacement_rule) -> Terrain_replacement_rule&
{
    ERHE_VERIFY(replacement_rule < m_terrain_replacement_rules.size());
    return m_terrain_replacement_rules.at(replacement_rule);
}

auto Tiles::get_terrain_from_tile(terrain_tile_t terrain_tile) const -> terrain_t
{
    if (terrain_tile < Base_tiles::count) {
        return static_cast<terrain_t>(terrain_tile);
    }

    const int group_id = (terrain_tile - Base_tiles::count) / (Tile_group::width * Tile_group::height);
    ERHE_VERIFY(group_id < Tile_group::count);
    ERHE_VERIFY(group_id < m_terrain_groups.size());

    const auto&     terrain_group      = m_terrain_groups.at(group_id);
    const terrain_t base_terrain       = terrain_group.base_terrain_type;
    const auto&     base_terrain_type  = m_terrain_types[base_terrain];
    const int       base_terrain_group = base_terrain_type.group;
    if (base_terrain_group != group_id) {
        log_tiles->error("base terrain group != group");
        //ERHE_VERIFY(m_terrain_types[multishape.base_terrain_type].group == group);
    }
    return terrain_group.base_terrain_type;
}

auto Tiles::get_terrain_tile_from_terrain(terrain_t terrain) const -> terrain_tile_t
{
    return static_cast<terrain_tile_t>(terrain);
}

auto Tiles::get_terrain_group_tile(int group, unsigned int neighbor_mask) const -> terrain_tile_t
{
    ERHE_VERIFY(group < m_terrain_groups.size());
    const auto shape_group = m_terrain_groups[group].shape_group;
    ERHE_VERIFY(shape_group < Tile_group::count);
    return static_cast<terrain_tile_t>(
        Base_tiles::count +
        shape_group * Tile_group::width * Tile_group::height + neighbor_mask
    );
}

#pragma region Terrain type IO
namespace {

auto convert_city_size(const int csize) -> int
{
    switch (csize) {
        case 16: return 4;
        case 14: return 3;
        case 12: return 2;
        case 10: return 1;
        default: return 0;
    }
}

}

void Tiles::load_terrain_defs()
{
    //load_terrain_defs_v4();
    //save_terrain_defs_v5();
    load_terrain_defs_v5();
}

void Tiles::save_terrain_defs()
{
    save_terrain_defs_v5();
}

void Tiles::load_terrain_defs_v4()
{
}

void Tiles::save_terrain_defs_v5()
{
    json j;
    auto json_terrain_types = json::array();
    for (auto& terrain_type : m_terrain_types) {
        json json_terrain_type;
        json_terrain_type["move_type_allow_mask"    ] = terrain_type.move_type_allow_mask;
        json_terrain_type["move_cost"               ] = terrain_type.move_cost;
        json_terrain_type["threat"                  ] = terrain_type.threat;
        json_terrain_type["damaged_version"         ] = terrain_type.damaged_version;
        json_terrain_type["defence_bonus"           ] = terrain_type.defence_bonus;
        json_terrain_type["city_size"               ] = terrain_type.city_size;
        json_terrain_type["strength"                ] = terrain_type.strength;
        json_terrain_type["move_type_level_mask"    ] = terrain_type.move_type_level_mask;
        json_terrain_type["flags"                   ] = terrain_type.flags;
        json_terrain_type["generate_elevation"      ] = terrain_type.generate_elevation;
        json_terrain_type["generate_priority"       ] = terrain_type.generate_priority;
        json_terrain_type["generate_base"           ] = terrain_type.generate_base;
        json_terrain_type["generate_min_temperature"] = terrain_type.generate_min_temperature;
        json_terrain_type["generate_max_temperature"] = terrain_type.generate_max_temperature;
        json_terrain_type["generate_min_humidity"   ] = terrain_type.generate_min_humidity;
        json_terrain_type["generate_max_humidity"   ] = terrain_type.generate_max_humidity;
        json_terrain_type["generate_ratio"          ] = terrain_type.generate_ratio;
        json_terrain_type["group"                   ] = terrain_type.group;
        json_terrain_type["name"                    ] = terrain_type.name;
        json_terrain_types.push_back(json_terrain_type);
    }
    j["terrain_types"] = json_terrain_types;

    std::string json_string = j.dump();
    write_file("res/hextiles/terrain_types_v5.json", json_string);
}

void Tiles::load_terrain_defs_v5()
{
    const std::string def_data = read_file_string("res/hextiles/terrain_types_v5.json");
    if (def_data.empty()) {
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& json_terrain_types = j["terrain_types"];
    m_terrain_types.clear();
    for (const auto& json_terrain_type : json_terrain_types) {
        Terrain_type terrain_type;
        terrain_type.move_type_allow_mask     = json_terrain_type["move_type_allow_mask"    ];
        terrain_type.move_cost                = json_terrain_type["move_cost"               ];
        terrain_type.threat                   = json_terrain_type["threat"                  ];
        terrain_type.damaged_version          = json_terrain_type["damaged_version"         ];
        terrain_type.defence_bonus            = json_terrain_type["defence_bonus"           ];
        terrain_type.city_size                = json_terrain_type["city_size"               ];
        terrain_type.strength                 = json_terrain_type["strength"                ];
        terrain_type.move_type_level_mask     = json_terrain_type["move_type_level_mask"    ];
        terrain_type.flags                    = json_terrain_type["flags"                   ];
        terrain_type.generate_elevation       = json_terrain_type["generate_elevation"      ];
        terrain_type.generate_priority        = json_terrain_type["generate_priority"       ];
        terrain_type.generate_base            = json_terrain_type["generate_base"           ];
        terrain_type.generate_min_temperature = json_terrain_type["generate_min_temperature"];
        terrain_type.generate_max_temperature = json_terrain_type["generate_max_temperature"];
        terrain_type.generate_min_humidity    = json_terrain_type["generate_min_humidity"   ];
        terrain_type.generate_max_humidity    = json_terrain_type["generate_max_humidity"   ];
        terrain_type.generate_ratio           = json_terrain_type["generate_ratio"          ];
        terrain_type.group                    = json_terrain_type["group"                   ];
        std::string name                      = json_terrain_type["name"                    ];;
        terrain_type.name.clear();
        std::copy(name.begin(), name.end(), terrain_type.name.begin());
        m_terrain_types.push_back(terrain_type);
    }
}
#pragma endregion Terrain type IO
#pragma region Terrain group IO
void Tiles::update_terrain_groups()
{
    for (auto& terrain_type : m_terrain_types) {
        terrain_type.group = -1;
    }
    for (int group_id = 0; group_id < m_terrain_groups.size(); ++group_id) {
        Terrain_group& group = m_terrain_groups[group_id];
        ERHE_VERIFY(group.base_terrain_type < Base_tiles::count);
        m_terrain_types[group.base_terrain_type].group = group_id;
    }
}

void Tiles::load_terrain_group_defs()
{
#if 0
    terrain_t Terrain_water_deep       { 1u};
    terrain_t Terrain_water_deep_medium{ 2u};
    terrain_t Terrain_water_medium     { 3u};
    terrain_t Terrain_water_medium_low { 4u};
    terrain_t Terrain_water_low        { 5u};
    terrain_t Terrain_water_low_plain  { 6u};
    terrain_t Terrain_road             {20u};
    terrain_t Terrain_peak             {28u};
    terrain_t Terrain_forest           {41u};
    terrain_t Terrain_mountain         {45u};
    terrain_t t0{0};
    terrain_t t11{11};
    terrain_t t18{18};
    terrain_t t22{22};
    terrain_t t29{29};
    terrain_t t44{44};
    terrain_t t49{49};

    m_terrain_groups.clear();
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 0,
            .base_terrain_type = Terrain_road,
            .link_first        = { t11, t0, t0 },
            .link_last         = { t22, t0, t0 },
            .link_group        = { -1, -1, -1 },
            .promoted          = -1,
            .demoted           = -1
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 1,
            .base_terrain_type = Terrain_peak,
            .link_first        = { Terrain_peak, t0, t0 },
            .link_last         = { t29,          t0, t0 },
            .link_group        = { -1, -1, -1 },
            .promoted          = -1,
            .demoted           = 3
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 2,
            .base_terrain_type = Terrain_forest,
            .link_first        = { Terrain_forest, t0, t0 },
            .link_last         = { t44,            t0, t0 },
            .link_group        = { -1, -1, -1 },
            .promoted          = -1,
            .demoted           = -1
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 3,
            .base_terrain_type = Terrain_mountain,
            .link_first        = { Terrain_mountain, Terrain_peak, t0 },
            .link_last         = { t49, t29, t0 },
            .link_group        = { 1, -1, -1 },
            .promoted          =  1,
            .demoted           = -1
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 4,
            .base_terrain_type = Terrain_water_low_plain,
            .link_first        = { Terrain_water_low, t0, t0 },
            .link_last         = { t18,               t0, t0 },
            .link_group        = {  5,  6, -1 },
            .promoted          = -1,
            .demoted           = -1
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 5,
            .base_terrain_type = Terrain_water_medium_low,
            .link_first        = { Terrain_water_medium,     t0, t0 },
            .link_last         = { Terrain_water_medium_low, t0, t0 },
            .link_group        = { 6, -1, -1 },
            .promoted          = -1,
            .demoted           = -1
        }
    );
    m_terrain_groups.push_back(
        Terrain_group{
            .shape_group       = 6,
            .base_terrain_type = Terrain_water_deep_medium,
            .link_first        = { Terrain_water_deep,        t0, t0 },
            .link_last         = { Terrain_water_deep_medium, t0, t0 },
            .link_group        = { -1, -1, -1 },
            .promoted          = -1,
            .demoted           = -1
        }
    );

    //                             b   f0  f1  f2  l0  l1  l2  g0  g1  g2  p   d
    //m_terrain_groups.emplace_back( 20, 11, -1, -1, 22, -1, -1, -1, -1, -1, -1, -1 ); // 0 Road
    //m_terrain_groups.emplace_back( 28, 28, -1, -1, 29, -1, -1, -1, -1, -1, -1,  3 ); // 1 Peak
    //m_terrain_groups.emplace_back( 41, 41, -1, -1, 44, -1, -1, -1, -1, -1, -1, -1 ); // 2 Forest
    //m_terrain_groups.emplace_back( 45, 45, 28, -1, 49, 29, -1,  1, -1, -1,  1, -1 ); // 3 Mountain
    //m_terrain_groups.emplace_back(  6,  5, -1, -1, 18, -1, -1,  5,  6, -1, -1, -1 ); // 4 Water Low - plain (coast)
    //m_terrain_groups.emplace_back(  4,  3, -1, -1,  4, -1, -1,  6, -1, -1, -1, -1 ); // 5 Water Medium - low
    //m_terrain_groups.emplace_back(  2,  1, -1, -1,  2, -1, -1, -1, -1, -1, -1, -1 ); // 6 Water Deep - medium
    save_terrain_group_defs_v1();
#endif

    load_terrain_group_defs_v1();

    update_terrain_groups();
}

void Tiles::save_terrain_group_defs()
{
    save_terrain_group_defs_v1();
}

void Tiles::load_terrain_group_defs_v1()
{
    const std::string def_data = read_file_string("res/hextiles/terrain_groups_v1.json");
    if (def_data.empty()) {
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& json_terrain_groups = j["terrain_groups"];
    m_terrain_groups.clear();
    for (const auto& json_terrain_group : json_terrain_groups) {
        Terrain_group terrain_group;
        terrain_group.enabled           = true;
        terrain_group.shape_group       = json_terrain_group["shape_group"      ];
        terrain_group.base_terrain_type = json_terrain_group["base_terrain_type"];
        terrain_group.link_first[0]     = json_terrain_group["link_first"       ][0];
        terrain_group.link_first[1]     = json_terrain_group["link_first"       ][1];
        terrain_group.link_first[2]     = json_terrain_group["link_first"       ][2];
        terrain_group.link_last[0]      = json_terrain_group["link_last"        ][0];
        terrain_group.link_last[1]      = json_terrain_group["link_last"        ][1];
        terrain_group.link_last[2]      = json_terrain_group["link_last"        ][2];
        terrain_group.link_group[0]     = json_terrain_group["link_group"       ][0];
        terrain_group.link_group[1]     = json_terrain_group["link_group"       ][1];
        terrain_group.link_group[2]     = json_terrain_group["link_group"       ][2];
        terrain_group.promoted          = json_terrain_group["promoted"         ];
        terrain_group.demoted           = json_terrain_group["demoted"          ];
        m_terrain_groups.push_back(terrain_group);
    }
}

void Tiles::save_terrain_group_defs_v1()
{
    json j;
    auto json_terrain_groups = json::array();
    for (auto& terrain_group : m_terrain_groups) {
        json json_terrain_group;
        json_terrain_group["shape_group"      ]    = terrain_group.shape_group;
        json_terrain_group["base_terrain_type"]    = terrain_group.base_terrain_type;
        json_terrain_group["link_first"       ][0] = terrain_group.link_first[0];
        json_terrain_group["link_first"       ][1] = terrain_group.link_first[1];
        json_terrain_group["link_first"       ][2] = terrain_group.link_first[2];
        json_terrain_group["link_last"        ][0] = terrain_group.link_last[0];
        json_terrain_group["link_last"        ][1] = terrain_group.link_last[1];
        json_terrain_group["link_last"        ][2] = terrain_group.link_last[2];
        json_terrain_group["link_group"       ][0] = terrain_group.link_group[0];
        json_terrain_group["link_group"       ][1] = terrain_group.link_group[1];
        json_terrain_group["link_group"       ][2] = terrain_group.link_group[2];
        json_terrain_group["promoted"         ]    = terrain_group.promoted;
        json_terrain_group["demoted"          ]    = terrain_group.demoted;
        json_terrain_groups.push_back(json_terrain_group);
    }
    j["terrain_groups"] = json_terrain_groups;

    std::string json_string = j.dump();
    write_file("res/hextiles/terrain_groups_v1.json", json_string);
}
#pragma endregion Terrain group IO
#pragma region Terrain replacement rule IO
void Tiles::load_terrain_replacement_rule_defs()
{
#if 0
    constexpr terrain_t Terrain_peak              = 28u;
    constexpr terrain_t Terrain_mountain          = 45u;
    constexpr terrain_t Terrain_water_deep        =  1u;
    constexpr terrain_t Terrain_water_deep_medium =  2u;
    constexpr terrain_t Terrain_water_medium      =  3u;
    constexpr terrain_t Terrain_water_medium_low  =  4u;
    constexpr terrain_t Terrain_water_low         =  5u;
    constexpr terrain_t Terrain_water_low_plain   =  6u;
    constexpr terrain_t Terrain_plain             = 31u;

    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_medium,
            .secondary   = { Terrain_plain },
            .replacement = Terrain_water_low_plain
        }
    );
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_deep,
            .secondary   = { Terrain_plain },
            .replacement = Terrain_water_low_plain
        }
    );

    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_low,
            .secondary   = { Terrain_plain },
            .replacement = Terrain_water_low_plain
        }
    );

    // Replace non-low water next to plain with coast
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_plain,
            .secondary   = { Terrain_water_deep, Terrain_water_medium  },
            .replacement = Terrain_water_low_plain
        }
    );

    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_plain,
            .secondary   = { Terrain_water_deep, Terrain_water_medium },
            .replacement = Terrain_water_low_plain
        }
    );

    // Replace deep water next to low water with medium water
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_low,
            .secondary   = { Terrain_water_deep },
            .replacement = Terrain_water_medium
        }
    );

    // Replace non-ground next to plain with coast
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = false,
            .primary     = Terrain_plain,
            .secondary   = { Terrain_plain, Terrain_mountain, Terrain_peak },
            .replacement = Terrain_water_low_plain
        }
    );

    // Replace low water next to medium water with medium low water
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_medium,
            .secondary   = { Terrain_water_low },
            .replacement = Terrain_water_medium_low
        }
    );

    // Replace deep water next to medium water with deep medium water
    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_medium,
            .secondary   = { Terrain_water_deep },
            .replacement = Terrain_water_deep_medium
        }
    );

    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_low_plain,
            .secondary   = { Terrain_water_medium },
            .replacement = Terrain_water_medium_low
        }
    );

    m_terrain_replacement_rules.push_back(
        Terrain_replacement_rule{
            .equal       = true,
            .primary     = Terrain_water_deep,
            .secondary   = { Terrain_water_low_plain },
            .replacement = Terrain_water_deep_medium
        }
    );

    save_terrain_replacement_rule_defs_v1();
#endif
    load_terrain_replacement_rule_defs_v1();

    for (auto& rule : m_terrain_replacement_rules) {
        if (rule.secondary.size() > 3) {
            rule.secondary.erase(rule.secondary.begin() + 3, rule.secondary.end());
        }
        while (rule.secondary.size() < 3) {
            rule.secondary.push_back(terrain_t{0});
        }
    }
}

void Tiles::save_terrain_replacement_rule_defs()
{
    save_terrain_replacement_rule_defs_v1();
}

void Tiles::load_terrain_replacement_rule_defs_v1()
{
    const std::string def_data = read_file_string("res/hextiles/terrain_replacement_rules_v1.json");
    if (def_data.empty()) {
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& json_rules = j["terrain_replacement_rules"];
    m_terrain_replacement_rules.clear();
    for (const auto& json_rule : json_rules) {
        Terrain_replacement_rule rule;
        rule.enabled     = json_rule["enabled"   ];
        rule.equal       = json_rule["equal"     ];
        rule.primary     = json_rule["primary"   ];

        std::vector<terrain_t> secondary = json_rule["secondary" ].get<std::vector<terrain_t>>();
        rule.secondary.clear();
        std::copy(secondary.begin(), secondary.end(), rule.secondary.begin());

        rule.replacement = json_rule["replacement"];
        m_terrain_replacement_rules.push_back(rule);
    }
}

void Tiles::save_terrain_replacement_rule_defs_v1()
{
    json j;
    auto json_rules = json::array();
    for (auto& rule : m_terrain_replacement_rules) {
        json json_rule;
        json_rule["enabled"    ] = rule.enabled;
        json_rule["equal"      ] = rule.equal;
        json_rule["primary"    ] = rule.primary;
        json_rule["secondary"  ] = rule.secondary;
        json_rule["replacement"] = rule.replacement;
        json_rules.push_back(json_rule);
    }
    j["terrain_replacement_rules"] = json_rules;

    std::string json_string = j.dump();
    write_file("res/hextiles/terrain_replacement_rules_v1.json", json_string);
}
#pragma endregion Terrain replacement rule IO
#pragma region Unit type IO
namespace {

auto convert_btype(const int value) -> uint32_t
{
    switch (value) {
        case 1:  return Battle_type::bit_air;
        case 2:  return Battle_type::bit_ground;
        case 3:  return Battle_type::bit_sea;
        case 4:  return Battle_type::bit_underwater;
        case 5:  return Battle_type::bit_city;
        default: return 0;
    }
}

}

void Tiles::load_unit_defs()
{
    //load_unit_defs_v1();
    //save_unit_defs_v2();
    load_unit_defs_v2();
}

void Tiles::save_unit_defs()
{
    save_unit_defs_v2();
}

void Tiles::load_unit_defs_v1()
{
    const std::string def_data = read_file_string("res/hextiles/unit_types_v1.json");
    if (def_data.empty()) {
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& json_unit_types = j["unit_types"];
    m_unit_types.reserve(json_unit_types.size());
    for (const auto& json_unit_type : json_unit_types) {
        Unit_type unit_type;
        unit_type.name            = json_unit_type["Name" ];

        // Production
        unit_type.tech_level      = json_unit_type["Tech" ];
        unit_type.production_time = json_unit_type["PTime"];
        unit_type.city_size       = 0;

        // flags
        int levels   = json_unit_type["Levels"];
        int specials = json_unit_type["Specials"];

        static constexpr int SA_HAS_PARACH    =   1;
        static constexpr int SA_DO_PLAIN      =   2;
        static constexpr int SA_DO_FIELD      =   4;
        static constexpr int SA_DO_ROAD       =   8;
        static constexpr int SA_DO_BRIDGE     =  16;
        static constexpr int SA_DO_FORTRESS   =  32;
        static constexpr int SA_DO_NOT_CDEF   =  64;
        static constexpr int SA_NCITY_LOAD    = 128;
        static constexpr int SA_NPRCH_UNLOAD  = 256;

        if (specials & SA_HAS_PARACH) {
            unit_type.flags |= (1u << Unit_flags::bit_has_parachute);
        }
        if (specials & SA_DO_PLAIN) {
            unit_type.flags |= (1u << Unit_flags::bit_can_build_plain);
        }
        if (specials & SA_DO_FIELD) {
            unit_type.flags |= (1u << Unit_flags::bit_can_build_field);
        }
        if (specials & SA_DO_ROAD) {
            unit_type.flags |= (1u << Unit_flags::bit_can_build_road);
        }
        if (specials & SA_DO_BRIDGE) {
            unit_type.flags |= (1u << Unit_flags::bit_can_build_bridge);
        }
        if (specials & SA_DO_FORTRESS) {
            unit_type.flags |= (1u << Unit_flags::bit_can_build_fortress);
        }
        if ((specials & SA_DO_NOT_CDEF) == 0) {
            unit_type.flags |= (1u << Unit_flags::bit_city_defense);
        }
        if ((specials & SA_NCITY_LOAD) == 0) {
            unit_type.flags |= (1u << Unit_flags::bit_city_load);
        }
        if ((specials & SA_NPRCH_UNLOAD) == 0) {
            unit_type.flags |= (1u << Unit_flags::bit_airdrop_unload);
        }

        if (levels > 0) {
            unit_type.flags |= (1u << Unit_flags::bit_has_stealth_mode);
        }

        unit_type.hit_points                   = json_unit_type["Hits"     ];
        unit_type.repair_per_turn              = json_unit_type["Repair"   ];
        unit_type.move_type_bits               = json_unit_type["MType"    ];
        unit_type.move_points[0]               = json_unit_type["Moves"    ][0];
        unit_type.move_points[1]               = json_unit_type["Moves"    ][1];
        unit_type.terrain_defense              = json_unit_type["TDef"     ];
        unit_type.fuel                         = json_unit_type["Fuel"     ];
        unit_type.refuel_per_turn              = json_unit_type["Refuel"   ];
        unit_type.cargo_types                  = json_unit_type["CType"    ];
        unit_type.cargo_space                  = json_unit_type["Cargo"    ];
        unit_type.cargo_load_count_per_turn    = json_unit_type["Loading"  ];
        unit_type.battle_type                  = convert_btype(json_unit_type["BType"]);
        unit_type.attack[0]                    = json_unit_type["Attack"   ][0];
        unit_type.attack[1]                    = json_unit_type["Attack"   ][1];
        unit_type.attack[2]                    = json_unit_type["Attack"   ][2];
        unit_type.attack[3]                    = json_unit_type["Attack"   ][3];
        unit_type.defense[0]                   = json_unit_type["Defence"  ][0];
        unit_type.defense[1]                   = json_unit_type["Defence"  ][1];
        unit_type.defense[2]                   = json_unit_type["Defence"  ][2];
        unit_type.defense[3]                   = json_unit_type["Defence"  ][3];
        unit_type.city_light_ground_modifier   = json_unit_type["LgtGndMod"];
        unit_type.stealth_version              = json_unit_type["LUI"    ];
        unit_type.stealth_attack               = json_unit_type["LAtt"   ];
        unit_type.stealth_defense              = json_unit_type["LDef"   ];
        unit_type.vision_range[0]              = json_unit_type["Vision" ][0];
        unit_type.vision_range[1]              = json_unit_type["Vision" ][1];
        unit_type.visible_from_range[0]        = json_unit_type["VFrom"  ][0];
        unit_type.visible_from_range[1]        = json_unit_type["VFrom"  ][1];
        unit_type.range[0]                     = json_unit_type["Range"  ][0];
        unit_type.range[1]                     = json_unit_type["Range"  ][1];
        unit_type.ranged_attack_modifier       = json_unit_type["RAttack"];
        unit_type.ranged_attack_ammo           = json_unit_type["Ammo"   ];
        unit_type.ranged_attack_count_per_turn = json_unit_type["Shots"  ];
        unit_type.audio_loop                   = json_unit_type["Sfx"    ];
        unit_type.audio_frequency              = json_unit_type["Frequ"  ];
        m_unit_types.push_back(unit_type);
    }

    while (m_unit_types.size() < 63) {
        Unit_type placeholder;
        m_unit_types.push_back(placeholder);
    }

    if (m_unit_types.size() > 64) {
        m_unit_types.erase(m_unit_types.begin() + 64);
    }
}

void Tiles::save_unit_defs_v2()
{
    json j;
    auto json_unit_types = json::array();
    for (auto& unit_type : m_unit_types) {
        json json_unit_type;

        json_unit_type["name"                        ]    = unit_type.name;
        json_unit_type["tech_level"                  ]    = unit_type.tech_level;
        json_unit_type["production_time"             ]    = unit_type.production_time;
        json_unit_type["city_size"                   ]    = unit_type.city_size;
        json_unit_type["flags"                       ]    = unit_type.flags;
        json_unit_type["hitpoints"                   ]    = unit_type.hit_points;
        json_unit_type["repair_per_turn"             ]    = unit_type.repair_per_turn;
        json_unit_type["move_type_bits"              ]    = unit_type.move_type_bits;
        json_unit_type["moves"                       ][0] = unit_type.move_points[0];
        json_unit_type["moves"                       ][1] = unit_type.move_points[1];
        json_unit_type["terrain_defense"             ]    = unit_type.terrain_defense;
        json_unit_type["fuel"                        ]    = unit_type.fuel;
        json_unit_type["refuel_per_turn"             ]    = unit_type.refuel_per_turn;
        json_unit_type["cargo_types"                 ]    = unit_type.cargo_types;
        json_unit_type["cargo_space"                 ]    = unit_type.cargo_space;
        json_unit_type["cargo_load_count_per_turn"   ]    = unit_type.cargo_load_count_per_turn;
        json_unit_type["battle_type"                 ]    = unit_type.battle_type;
        json_unit_type["attack"                      ][0] = unit_type.attack[0];
        json_unit_type["attack"                      ][1] = unit_type.attack[1];
        json_unit_type["attack"                      ][2] = unit_type.attack[2];
        json_unit_type["attack"                      ][3] = unit_type.attack[3];
        json_unit_type["defense"                     ][0] = unit_type.defense[0];
        json_unit_type["defense"                     ][1] = unit_type.defense[1];
        json_unit_type["defense"                     ][2] = unit_type.defense[2];
        json_unit_type["defense"                     ][3] = unit_type.defense[3];
        json_unit_type["city_light_ground_modifier"  ]    = unit_type.city_light_ground_modifier;
        json_unit_type["stealth_version"             ]    = unit_type.stealth_version;
        json_unit_type["vision_range"                ][0] = unit_type.vision_range[0];
        json_unit_type["vision_range"                ][1] = unit_type.vision_range[1];
        json_unit_type["visible_from"                ][0] = unit_type.visible_from_range[0];
        json_unit_type["visible_from"                ][1] = unit_type.visible_from_range[1];
        json_unit_type["range"                       ][0] = unit_type.range[0];
        json_unit_type["range"                       ][1] = unit_type.range[1];
        json_unit_type["ranged_attack_modifier"      ]    = unit_type.ranged_attack_modifier;
        json_unit_type["ranged_attack_ammo"          ]    = unit_type.ranged_attack_ammo;
        json_unit_type["ranged_attack_count_per_turn"]    = unit_type.ranged_attack_count_per_turn;
        json_unit_type["Sfx"                         ]    = unit_type.audio_loop;
        json_unit_type["Frequ"                       ]    = unit_type.audio_frequency;

        json_unit_types.push_back(json_unit_type);
    }
    j["unit_types"] = json_unit_types;

    std::string json_string = j.dump();
    write_file("res/hextiles/unit_types_v2.json", json_string);
}

void Tiles::load_unit_defs_v2()
{
    const std::string def_data = read_file_string("res/hextiles/unit_types_v2.json");
    if (def_data.empty())
{
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& json_unit_types = j["unit_types"];
    m_unit_types.clear();
    for (const auto& json_unit_type : json_unit_types) {
        Unit_type unit_type;
        std::string name                       = json_unit_type["name"                        ];
        unit_type.name.clear();
        std::copy(name.begin(), name.end(), unit_type.name.begin());
        unit_type.tech_level                   = json_unit_type["tech_level"                  ];
        unit_type.production_time              = json_unit_type["production_time"             ];
        unit_type.city_size                    = json_unit_type["city_size"                   ];
        unit_type.flags                        = json_unit_type["flags"                       ];
        unit_type.hit_points                   = json_unit_type["hitpoints"                   ];
        unit_type.repair_per_turn              = json_unit_type["repair_per_turn"             ];
        unit_type.move_type_bits               = json_unit_type["move_type_bits"              ];
        unit_type.move_points[0]               = json_unit_type["moves"                       ][0];
        unit_type.move_points[1]               = json_unit_type["moves"                       ][1];
        unit_type.terrain_defense              = json_unit_type["terrain_defense"             ];
        unit_type.fuel                         = json_unit_type["fuel"                        ];
        unit_type.refuel_per_turn              = json_unit_type["refuel_per_turn"             ];
        unit_type.cargo_types                  = json_unit_type["cargo_types"                 ];
        unit_type.cargo_space                  = json_unit_type["cargo_space"                 ];
        unit_type.cargo_load_count_per_turn    = json_unit_type["cargo_load_count_per_turn"   ];
        //unit_type.battle_type                  = get_bit_position(json_unit_type["battle_type"                 ]);
        unit_type.battle_type                  = json_unit_type["battle_type"                 ];
        unit_type.attack[0]                    = json_unit_type["attack"                      ][0];
        unit_type.attack[1]                    = json_unit_type["attack"                      ][1];
        unit_type.attack[2]                    = json_unit_type["attack"                      ][2];
        unit_type.attack[3]                    = json_unit_type["attack"                      ][3];
        unit_type.defense[0]                   = json_unit_type["defense"                     ][0];
        unit_type.defense[1]                   = json_unit_type["defense"                     ][1];
        unit_type.defense[2]                   = json_unit_type["defense"                     ][2];
        unit_type.defense[3]                   = json_unit_type["defense"                     ][3];
        unit_type.city_light_ground_modifier   = json_unit_type["city_light_ground_modifier"  ];
        unit_type.stealth_version              = json_unit_type["stealth_version"             ];
        unit_type.vision_range[0]              = json_unit_type["vision_range"                ][0];
        unit_type.vision_range[1]              = json_unit_type["vision_range"                ][1];
        unit_type.visible_from_range[0]        = json_unit_type["visible_from"                ][0];
        unit_type.visible_from_range[1]        = json_unit_type["visible_from"                ][1];
        unit_type.range[0]                     = json_unit_type["range"                       ][0];
        unit_type.range[1]                     = json_unit_type["range"                       ][1];
        unit_type.ranged_attack_modifier       = json_unit_type["ranged_attack_modifier"      ];
        unit_type.ranged_attack_ammo           = json_unit_type["ranged_attack_ammo"          ];
        unit_type.ranged_attack_count_per_turn = json_unit_type["ranged_attack_count_per_turn"];
        unit_type.audio_loop                   = json_unit_type["Sfx"                         ];
        unit_type.audio_frequency              = json_unit_type["Frequ"                       ];
        m_unit_types.push_back(unit_type);
    }

    if (m_unit_types.size() > 56) {
        m_unit_types.erase(m_unit_types.begin() + 56, m_unit_types.end());
    }
}
#pragma endregion Unit type IO

auto Tiles::remove_terrain_group(int group_id) -> bool
{
    if (
        (group_id < 0) ||
        (group_id >= m_terrain_groups.size())
    ) {
        return false;
    }

    m_terrain_groups.erase(m_terrain_groups.begin() + group_id);
    return true;
}

auto Tiles::remove_terrain_replacement_rule(int rule_id) -> bool
{
    if (
        (rule_id < 0) ||
        (rule_id >= m_terrain_replacement_rules.size())
    ) {
        return false;
    }

    m_terrain_replacement_rules.erase(m_terrain_replacement_rules.begin() + rule_id);
    return true;
}

void Tiles::add_terrain_group()
{
    Terrain_group group{};
    m_terrain_groups.push_back(group);
}

void Tiles::add_terrain_replacement_rule()
{
    Terrain_replacement_rule rule{};
    rule.secondary.push_back(terrain_t{0});
    rule.secondary.push_back(terrain_t{0});
    rule.secondary.push_back(terrain_t{0});
    m_terrain_replacement_rules.push_back(rule);
}

void update_group_terrain(
    Tiles&          tiles,
    Map&            map,
    Tile_coordinate position
)
{
    const terrain_tile_t terrain_tile = map.get_terrain_tile(position);
    const terrain_t      terrain      = tiles.get_terrain_from_tile(terrain_tile);
    const auto&          terrain_type = tiles.get_terrain_type(terrain);
    int                  group        = terrain_type.group;
    if (group < 0) {
        return;
    }

    uint32_t neighbor_mask;
    bool promote;
    bool demote;
    size_t counter = 0u;
    do {
        demote  = false;
        const Terrain_group& terrain_group = tiles.get_terrain_group(group);

        neighbor_mask = 0u;
        for (
            direction_t direction = direction_first;
            direction <= direction_last;
            ++direction
        ) {
            const Tile_coordinate neighbor_position     = map.neighbor(position, direction);
            const terrain_tile_t  neighbor_terrain_tile = map.get_terrain_tile(neighbor_position);
            const terrain_t       neighbor_terrain      = tiles.get_terrain_from_tile(neighbor_terrain_tile);
            const int neighbor_group = tiles.get_terrain_type(neighbor_terrain).group;
            if (
                (neighbor_group == group) ||
                (
                    (terrain_group.link_group[0] >= 0) &&
                    (neighbor_group == terrain_group.link_group[0])
                ) ||
                (
                    (terrain_group.link_group[1] >= 0) &&
                    (neighbor_group == terrain_group.link_group[1])
                ) ||
                (
                    (terrain_group.link_first[0] > 0) &&
                    (terrain_group.link_last [0] > 0) &&
                    (neighbor_terrain >= terrain_group.link_first[0]) &&
                    (neighbor_terrain <= terrain_group.link_last [0])
                ) ||
                (
                    (terrain_group.link_first[1] > 0) &&
                    (terrain_group.link_last [1] > 0) &&
                    (neighbor_terrain >= terrain_group.link_first[1]) &&
                    (neighbor_terrain <= terrain_group.link_last [1])
                )
            ) {
                neighbor_mask = neighbor_mask | (1u << direction);
            }
            if (
                (terrain_group.demoted >= 0) &&
                (neighbor_group != group) &&
                (neighbor_group != terrain_group.demoted)
            ) {
                demote = true;
            }
        }
        promote = (neighbor_mask == direction_mask_all) && (terrain_group.promoted > 0);
        ERHE_VERIFY((promote && demote) == false);
        if (promote) {
            group = terrain_group.promoted;
        } else if (demote) {
            group = terrain_group.demoted;
        }
    } while (
        (promote || demote) &&
        (++counter < 2U)
    );

    const terrain_tile_t updated_terrain_tile = tiles.get_terrain_group_tile(group, neighbor_mask);
    map.set_terrain_tile(position, updated_terrain_tile);
}

} // namespace hextiles
