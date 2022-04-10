#include <nlohmann/json.hpp>

#include "game.hpp"

#include "file_util.hpp"
#include "globals.hpp"

namespace power_battle
{

using json = nlohmann::json;

#if 0
struct OriginalTerrainType
{
    std::string name;
    uint8_t Color   {0U};
    uint8_t MAllow  {0U};
    uint8_t MCost   {0U};
    uint8_t Threat  {0U};
    uint8_t ADamage {0U};
    uint8_t DBonus  {0U};
    uint8_t CSize   {0U};
    uint8_t Strength{0U};
    uint8_t Mult    {0U};
    uint8_t VChg    {0U};
    uint8_t HNum    {0U};
};

void Game::load_terrain_defs()
{
    std::vector<unsigned char> def_data = read_file("data/definitions/terrain_types.def");
    constexpr size_t hex_types = 55U;
    constexpr size_t load_string_length = 12U;
    size_t pos = 0U;

    std::vector<OriginalTerrainType> original_terrain_types;
    // +1 because original data has one too many records
    for (size_t i = 0U; i < hex_types + 1U; ++i)
    {
        OriginalTerrainType type;
        type.Color    = def_data[pos++];
        type.MAllow   = def_data[pos++];
        type.MCost    = def_data[pos++];
        type.Threat   = def_data[pos++];
        type.ADamage  = def_data[pos++];
        type.DBonus   = def_data[pos++];
        type.CSize    = def_data[pos++];
        type.Strength = def_data[pos++];
        type.Mult     = def_data[pos++];
        type.VChg     = def_data[pos++];
        type.HNum     = def_data[pos++];
        original_terrain_types.push_back(type);
        // printf("\nTerrain %d:\n", i);
        // printf("Color    = %u\n", type.Color);
        // printf("MAllow   = %u\n", type.MAllow);
        // printf("MCost    = %u\n", type.MCost);
        // printf("Threat   = %u\n", type.Threat);
        // printf("ADamage  = %u\n", type.ADamage);
        // printf("DBonus   = %u\n", type.DBonus);
        // printf("CSize    = %u\n", type.CSize);
        // printf("Strength = %u\n", type.Strength);
        // printf("Mult     = %u\n", type.Mult);
        // printf("VChg     = %u\n", type.VChg);
        // printf("HNum     = %u\n", type.HNum);
    }

    // +1 because original data has one too many records
    for (size_t i = 0U; i < hex_types + 1U; ++i)
    {
        std::string value = get_string(def_data, pos, load_string_length);

        // remove trailing spaces
        value.erase(std::find_if(value.rbegin(), value.rend(), std::bind1st(std::not_equal_to<char>(), ' ')).base(), value.end());

        pos += load_string_length;
        // printf("Terrain %d Name  = %s\n", i, name.c_str());
        if (i < original_terrain_types.size())
        {
            original_terrain_types[i].name = value;
        }
    }

    constexpr size_t def_count = 11U;
    for (size_t i = 0U; i < def_count; ++i)
    {
        std::string name = get_string(def_data, pos,  load_string_length);
        pos += load_string_length;
        // printf("Field %d  Name  = %s\n", i, name.c_str());
    }

    json j;
    auto j_terrain_types = json::array();
    for (auto &ot : original_terrain_types)
    {
        json t;
        t["Name"    ] = ot.name;
        t["Color"   ] = ot.Color;
        t["MAllow"  ] = ot.MAllow;
        t["MCost"   ] = ot.MCost;
        t["Threat"  ] = ot.Threat;
        t["ADamage" ] = ot.ADamage;
        t["DBonus"  ] = ot.DBonus;
        t["CSize"   ] = ot.CSize;
        t["Strength"] = ot.Strength;
        t["Mult"    ] = ot.Mult;
        t["VChg"    ] = ot.VChg;
        t["HNum"    ] = ot.HNum;
        j_terrain_types.push_back(t);
    }
    j["terrain_types"] = j_terrain_types;

    std::string json_string = j.dump();
    write_file("data/definitions/terrain_types.json", json_string);
}
#else
void tiles::load_terrain_defs()
{
    const std::string def_data = read_file_string("data/definitions/terrain_types.json");
    const auto  j = json::parse(def_data);
    const auto& terrain_types = j["terrain_types"];
    m_terrain_types.resize(terrain_types.size());
    size_t index = 0u;
    for (const auto& t : terrain_types)
    {
        m_terrain_types[index].name = t["Name"];
        index++;
    }
    //constexpr size_t hex_types = 55U;
}
#endif

#if 0
struct OriginalUnitType
{
    std::string name;
    uint8_t MType   {0U};    // Movement Type Bits
    uint8_t Moves[2]{0U, 0U};   // Movement Points per Turn
    uint8_t TDef    {0U}; // Threat Defence
    uint8_t Fuel    {0U}; // Fuel Tank Size
    uint8_t Refuel  {0U}; // Refuel Points per Turn
    uint8_t CType   {0U}; // Cargo Type Bits
    uint8_t Cargo   {0U}; // Cargo Space
    uint8_t Loading {0U}; // Loadings/Unloadings per Turn
    uint8_t Tech    {0U}; // Required Techlevel for Production
    uint8_t PTime   {0U}; // Production Time
    uint8_t Cost    {0U}; // Reserved
    uint8_t Hits    {0U}; // Maximum HitPoints
    uint8_t Repair  {0U}; // Repair Points per Turn
    uint8_t Levels  {0U}; // Reserved
    uint8_t BType   {0U}; // Battle Type #
    uint8_t Attack [4]{0U, 0U, 0U, 0U}; // Attack Points against BattleTypes
    uint8_t Defence[4]{0U, 0U, 0U, 0U}; // Defence  -  "  -
    uint8_t LgtGndMod;            // Light Ground Modifier
    uint8_t Range[2]{0U, 0U};   // Artillery Attack Range
    uint8_t RAttack {0U}; // Ranged Attack Modifier
    uint8_t Ammo    {0U}; // Ammunitation Storage
    uint8_t Shots   {0U}; // Artillery Attacks per Turn
    uint8_t LAtt    {0U}; // Attack LevelModifier
    uint8_t LDef    {0U}; // Defence LevelModifier
    uint8_t CAttack {0U}; // City Attack Modifier
    uint8_t Vision[2]{0U, 0U}; // Vision Range
    uint8_t VFrom [2]{0U, 0U}; // Visible From -Range
    uint8_t LUI     {0U}; // Leveled Unit Icon #
    uint8_t fff     {0U}; // Reserved
    uint8_t ggg     {0U}; // Reserved
    uint8_t hhh     {0U}; // Reserved
    uint8_t Sfx     {0U}; // Sound Effect #
    uint8_t Frequ   {0U}; // Sound Effect Frequency
    uint8_t Specials{0U}; // Special Abilities
    uint8_t Undefd  {0U}; // Reserved
    uint8_t L       {0U}; // Reserved
};

void Game::load_unit_defs()
{
    std::vector<unsigned char> def_data = read_file("data/definitions/unit_types.def");
    constexpr size_t types = 42U;
    constexpr size_t load_string_length = 12U;
    size_t pos = 0U;
    std::vector<OriginalUnitType> original_unit_types;
    for (size_t i = 0U; i < types + 1U; ++i)
    {
        OriginalUnitType type;
        type.MType      = def_data[pos++];  // Movement Type Bits
        type.Moves[0]   = def_data[pos++];  // Movement Points per Turn
        type.Moves[1]   = def_data[pos++];  // Movement Points per Turn
        type.TDef       = def_data[pos++];  // Threat Defence
        type.Fuel       = def_data[pos++];  // Fuel Tank Size
        type.Refuel     = def_data[pos++];  // Refuel Points per Turn
        type.CType      = def_data[pos++];  // Cargo Type Bits
        type.Cargo      = def_data[pos++];  // Cargo Space
        type.Loading    = def_data[pos++];  // Loadings/Unloadings per Turn
        type.Tech       = def_data[pos++];  // Required Techlevel for Production
        type.PTime      = def_data[pos++];  // Production Time
        type.Cost       = def_data[pos++];  // Reserved
        type.Hits       = def_data[pos++];  // Maximum HitPoints
        type.Repair     = def_data[pos++];  // Repair Points per Turn
        type.Levels     = def_data[pos++];  // Reserved
        type.BType      = def_data[pos++];  // Battle Type #
        type.Attack[0]  = def_data[pos++];  // Attack Points against BattleTypes
        type.Attack[1]  = def_data[pos++];  // Attack Points against BattleTypes
        type.Attack[2]  = def_data[pos++];  // Attack Points against BattleTypes
        type.Attack[3]  = def_data[pos++];  // Attack Points against BattleTypes
        type.Defence[0] = def_data[pos++];  // Defence  -  "  -
        type.Defence[1] = def_data[pos++];  // Defence  -  "  -
        type.Defence[2] = def_data[pos++];  // Defence  -  "  -
        type.Defence[3] = def_data[pos++];  // Defence  -  "  -
        type.LgtGndMod  = def_data[pos++];  // Light Ground Modifier
        type.Range[0]   = def_data[pos++];  // Artillery Attack Range
        type.Range[1]   = def_data[pos++];  // Artillery Attack Range
        type.RAttack    = def_data[pos++];  // Ranged Attack Modifier
        type.Ammo       = def_data[pos++];  // Ammunitation Storage
        type.Shots      = def_data[pos++];  // Artillery Attacks per Turn
        type.LAtt       = def_data[pos++];  // Attack LevelModifier
        type.LDef       = def_data[pos++];  // Defence LevelModifier
        type.CAttack    = def_data[pos++];  // City Attack Modifier
        type.Vision[0]  = def_data[pos++];  // Vision Range
        type.Vision[1]  = def_data[pos++];  // Vision Range
        type.VFrom[0]   = def_data[pos++];  // Visible From -Range
        type.VFrom[1]   = def_data[pos++];  // Visible From -Range
        type.LUI        = def_data[pos++];  // Leveled Unit Icon #
        type.fff        = def_data[pos++];  // Reserved
        type.ggg        = def_data[pos++];  // Reserved
        type.hhh        = def_data[pos++];  // Reserved
        type.Sfx        = def_data[pos++];  // Sound Effect #
        type.Frequ      = def_data[pos++];  // Sound Effect Frequency
        type.Specials   = def_data[pos++];  // Special Abilities
        type.Undefd     = def_data[pos++];  // Reserved
        type.L          = def_data[pos++];  // Reserved
        original_unit_types.push_back(type);
        // printf("\nUnit type %d:\n", i);
        // printf("MType      = %u\n", type.MType);
        // printf("Moves[0]   = %u\n", type.Moves[0]);
        // printf("Moves[1]   = %u\n", type.Moves[1]);
        // printf("TDef       = %u\n", type.TDef);
        // printf("Fuel       = %u\n", type.Fuel);
        // printf("Refuel     = %u\n", type.Refuel);
        // printf("CType      = %u\n", type.CType);
        // printf("Cargo      = %u\n", type.Cargo);
        // printf("Loading    = %u\n", type.Loading);
        // printf("Tech       = %u\n", type.Tech);
        // printf("PTime      = %u\n", type.PTime);
        // printf("Cost       = %u\n", type.Cost);
        // printf("Hits       = %u\n", type.Hits);
        // printf("Repair     = %u\n", type.Repair);
        // printf("Levels     = %u\n", type.Levels);
        // printf("BType      = %u\n", type.BType);
        // printf("Attack[0]  = %u\n", type.Attack[0]);
        // printf("Attack[1]  = %u\n", type.Attack[1]);
        // printf("Attack[2]  = %u\n", type.Attack[2]);
        // printf("Attack[3]  = %u\n", type.Attack[3]);
        // printf("Defence[0] = %u\n", type.Defence[0]);
        // printf("Defence[1] = %u\n", type.Defence[1]);
        // printf("Defence[2] = %u\n", type.Defence[2]);
        // printf("Defence[3] = %u\n", type.Defence[3]);
        // printf("LgtGndMod  = %u\n", type.LgtGndMod);
        // printf("Range[0]   = %u\n", type.Range[0]);
        // printf("Range[1]   = %u\n", type.Range[1]);
        // printf("RAttack    = %u\n", type.RAttack);
        // printf("Ammo       = %u\n", type.Ammo);
        // printf("Shots      = %u\n", type.Shots);
        // printf("LAtt       = %u\n", type.LAtt);
        // printf("LDef       = %u\n", type.LDef);
        // printf("CAttack    = %u\n", type.CAttack);
        // printf("Vision[0]  = %u\n", type.Vision[0]);
        // printf("Vision[1]  = %u\n", type.Vision[1]);
        // printf("VFrom[0]   = %u\n", type.VFrom[0]);
        // printf("VFrom[1]   = %u\n", type.VFrom[1]);
        // printf("LUI        = %u\n", type.LUI);
        // printf("fff        = %u\n", type.fff);
        // printf("ggg        = %u\n", type.ggg);
        // printf("hhh        = %u\n", type.hhh);
        // printf("Sfx        = %u\n", type.Sfx);
        // printf("Frequ      = %u\n", type.Frequ);
        // printf("Specials   = %u\n", type.Specials);
        // printf("Undefd     = %u\n", type.Undefd);
        // printf("L          = %u\n", type.L);
    }
    for (size_t i = 0U; i < types + 1U; ++i)
    {
        std::string name = get_string(def_data, pos, load_string_length);
        pos += load_string_length;
        // printf("Unit %d Name = %s\n", i, name.c_str());
        if (i < original_unit_types.size())
        {
            original_unit_types[i].name = name;
        }
    }

    constexpr size_t def_count = 36U;
    for (size_t i = 0U; i < def_count; ++i)
    {
        std::string name = get_string(def_data, pos, load_string_length);
        pos += load_string_length;
        // printf("Field %d Name  = %s\n", i, name.c_str());
    }


    json j;
    auto j_unit_types = json::array();
    for (auto &ot : original_unit_types)
    {
        json t;
        t["Name"      ] = ot.name      ;  // Movement Type Bits
        t["MType"     ] = ot.MType     ;  // Movement Type Bits
        t["Moves"     ] = ot.Moves     ;  // Movement Points per Turn
        t["TDef"      ] = ot.TDef      ;  // Threat Defence
        t["Fuel"      ] = ot.Fuel      ;  // Fuel Tank Size
        t["Refuel"    ] = ot.Refuel    ;  // Refuel Points per Turn
        t["CType"     ] = ot.CType     ;  // Cargo Type Bits
        t["Cargo"     ] = ot.Cargo     ;  // Cargo Space
        t["Loading"   ] = ot.Loading   ;  // Loadings/Unloadings per Turn
        t["Tech"      ] = ot.Tech      ;  // Required Techlevel for Production
        t["PTime"     ] = ot.PTime     ;  // Production Time
        t["Cost"      ] = ot.Cost      ;  // Reserved
        t["Hits"      ] = ot.Hits      ;  // Maximum HitPoints
        t["Repair"    ] = ot.Repair    ;  // Repair Points per Turn
        t["Levels"    ] = ot.Levels    ;  // Reserved
        t["BType"     ] = ot.BType     ;  // Battle Type #
        t["Attack"    ] = ot.Attack    ;  // Attack Points against BattleTypes
        t["Defence"   ] = ot.Defence   ;  // Defence  -  "  -
        t["LgtGndMod" ] = ot.LgtGndMod ;  // Light Ground Modifier
        t["Range"     ] = ot.Range     ;  // Artillery Attack Range
        t["RAttack"   ] = ot.RAttack   ;  // Ranged Attack Modifier
        t["Ammo"      ] = ot.Ammo      ;  // Ammunitation Storage
        t["Shots"     ] = ot.Shots     ;  // Artillery Attacks per Turn
        t["LAtt"      ] = ot.LAtt      ;  // Attack LevelModifier
        t["LDef"      ] = ot.LDef      ;  // Defence LevelModifier
        t["CAttack"   ] = ot.CAttack   ;  // City Attack Modifier
        t["Vision"    ] = ot.Vision    ;  // Vision Range
        t["VFrom"     ] = ot.VFrom     ;  // Visible From -Range
        t["LUI"       ] = ot.LUI       ;  // Leveled Unit Icon #
        t["fff"       ] = ot.fff       ;  // Reserved
        t["ggg"       ] = ot.ggg       ;  // Reserved
        t["hhh"       ] = ot.hhh       ;  // Reserved
        t["Sfx"       ] = ot.Sfx       ;  // Sound Effect #
        t["Frequ"     ] = ot.Frequ     ;  // Sound Effect Frequency
        t["Specials"  ] = ot.Specials  ;  // Special Abilities
        t["Undefd"    ] = ot.Undefd    ;  // Reserved
        t["L"         ] = ot.L         ;  // Reserved
        j_unit_types.push_back(t);
    }
    j["unit_types"] = j_unit_types;

    std::string json_string = j.dump();
    write_file("data/definitions/unit_types.json", json_string);

}
#else
void Game::load_unit_defs()
{
    const std::string def_data = read_file_string("data/definitions/unit_types.json");
    const auto j = json::parse(def_data);
    const auto& unit_types = j["unit_types"];
    m_unit_types.resize(unit_types.size());
    size_t index = 0U;
    for (auto& t : unit_types)
    {
        auto& u = m_unit_types[index];
        u.move_type_bits      = t["MType"];
        u.moves[0]            = t["Moves"][0];
        u.moves[1]            = t["Moves"][1];
        u.fuel                = t["Fuel"];
        u.refuel_per_turn     = t["Refuel"];
        u.cargo_type          = t["CType"];
        u.load_count_per_turn = t["Loading"];
        u.production_time     = t["PTime"];
        u.hit_points          = t["Hits"];
        u.repair_per_turn     = t["Repair"];
        u.levels              = t["Levels"]; // reserved
        u.battle_type         = t["BType"];
        u.attack[0]           = t["Attack"][0];
        u.attack[1]           = t["Attack"][1];
        u.attack[2]           = t["Attack"][2];
        u.attack[3]           = t["Attack"][3];
        u.defence[0]          = t["Defence"][0];
        u.defence[1]          = t["Defence"][1];
        u.defence[2]          = t["Defence"][2];
        u.defence[3]          = t["Defence"][3];
        u.name                = t["Name"];
        index++;
    }
}
#endif

}
