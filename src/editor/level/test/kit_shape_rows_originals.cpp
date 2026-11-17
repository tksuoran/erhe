#include "kit_shape_test_support.hpp"

// The original shapes (doc/editor/level_tools.md "Shapes").
namespace editor::kit_test {

auto get_originals_rows() -> Kit_shape_rows
{
    Kit_shape_rows rows;
    rows.shapes = {
        // The 14 shapes of R4.1, then the built shapes of doc/plans/level_kit_more_shapes.md.
        "basic.block", "basic.wedge", "basic.cylinder", "basic.stairs", "basic.floor_slab",
        "architecture.wall", "architecture.door_wall", "architecture.pillar", "architecture.arch",
        "nature.rock", "nature.land_mass", "nature.hill", "nature.tree", "nature.bush",
        "architecture.column", "architecture.entablature", "architecture.torii", "architecture.battlement", "architecture.tower",
        "nature.cliff", "nature.shore", "nature.water", "nature.palm", "nature.leaf_plant",
        "nature.rock_spire", "nature.basalt_columns", "architecture.roof", "nature.palm_clump", "nature.palm_shrub",
        "nature.dune", "nature.ground_patch"
    };
    rows.recreated_originals = {};
    rows.seeded_shapes = {
        "nature.rock", "nature.land_mass", "nature.hill", "nature.tree", "nature.bush", "nature.cliff", "nature.shore",
        "nature.palm", "nature.leaf_plant", "nature.rock_spire", "nature.basalt_columns", "nature.palm_clump",
        "nature.palm_shrub", "nature.dune", "nature.ground_patch"
    };
    rows.enum_shapes = {
        "architecture.pillar", "nature.tree", "nature.rock", "nature.cliff", "nature.palm", "architecture.column",
        "architecture.entablature", "architecture.battlement", "architecture.tower", "nature.leaf_plant",
        "nature.rock_spire", "architecture.roof", "nature.palm_clump", "nature.palm_shrub"
    };
    // The palms and the leaf plant are trunks and one closed part per leaf or leaf half, the basalt one part per column;
    // the column, entablature, torii and roof are closed parts that overlap (base, shaft, capital; profile, triglyphs;
    // posts, beams; slab, body, tile courses, caps); the tree is trunk and canopy, the tower its walls and the lintel.
    rows.components = {
        {"nature.palm", Kit_component_rule::more_than, 10}, {"nature.leaf_plant", Kit_component_rule::more_than, 10},
        {"nature.palm_clump", Kit_component_rule::more_than, 10}, {"nature.palm_shrub", Kit_component_rule::more_than, 10},
        {"nature.basalt_columns", Kit_component_rule::more_than, 10},
        {"architecture.column", Kit_component_rule::more_than, 3}, {"architecture.entablature", Kit_component_rule::more_than, 3},
        {"architecture.torii", Kit_component_rule::more_than, 3}, {"architecture.roof", Kit_component_rule::more_than, 3},
        {"nature.tree", Kit_component_rule::exactly, 2}, {"architecture.tower", Kit_component_rule::exactly, 2}
    };
    // Built shapes whose derive ranges scale with the lengths (doc/plans/open_items.md "Derived walkability at other level lengths").
    rows.walkability_open = {"basic.wedge", "basic.stairs", "basic.floor_slab", "architecture.arch", "nature.land_mass", "nature.shore"};
    rows.triangle_counts = {
        {"basic.block", 12}, {"basic.wedge", 8}, {"basic.cylinder", 60}, {"basic.stairs", 124}, {"basic.floor_slab", 12},
        {"architecture.wall", 12}, {"architecture.door_wall", 44}, {"architecture.pillar", 12}, {"architecture.arch", 100},
        {"nature.rock", 112}, {"nature.land_mass", 128}, {"nature.hill", 128}, {"nature.tree", 68}, {"nature.bush", 48},
        {"nature.cliff", 428}, {"nature.shore", 238}, {"nature.water", 2}, {"nature.palm", 1510},
        {"architecture.column", 724}, {"architecture.entablature", 540}, {"architecture.torii", 400}, {"architecture.battlement", 44},
        {"architecture.tower", 676}, {"nature.leaf_plant", 620}, {"nature.rock_spire", 720}, {"nature.basalt_columns", 740},
        {"architecture.roof", 508}, {"nature.palm_clump", 1912}, {"nature.palm_shrub", 584}, {"nature.dune", 284}, {"nature.ground_patch", 144}
    };
    // R2.6 where the triangle maxima lie at mixed values, not at the all-maximum corner.
    rows.worst_cases = {
        {"architecture.column",      R"({"order":"corinthian","height":"0x1p-1","width":"0x1.99999ap-4","flutes":24,"taper":"0x0p+0","seed":1,"segments":20})"},
        {"architecture.entablature", R"({"length":"0x1p+3","height":"0x1.99999ap-3","depth":"0x1.99999ap-3","cornice":"0x0p+0","frieze":"triglyph","seed":1})"},
        {"architecture.entablature", R"({"length":"0x1p+1","height":"0x1.99999ap-3","depth":"0x1p-1","cornice":"0x1.333334p-3","frieze":"triglyph","seed":1})"},
        {"architecture.torii",       R"({"width":"0x1p+0","height":"0x1p+1","curve":"0x0p+0","post_width":"0x1.99999ap-4","lean":"0x0p+0","seed":1,"segments":24})"},
        {"architecture.battlement",  R"({"length":"0x1.8p+4","height":"0x1.99999ap-3","thickness":"0x1.99999ap-5","merlon_height":"0x1.99999ap-3","merlon_width":"0x1.99999ap-2","crenel_width":"0x1.333334p-2","merlon":"swallowtail","seed":1})"},
        {"architecture.tower",       R"({"profile":"round","width":"0x1p+4","height":"0x1.ep+4","batter":"0x0p+0","wall":"0x1.99999ap-3","opening_width":"0x1.99999ap-1","opening_height":"0x1p+1","top":"battlement","slit_rows":2,"seed":1})"},
        {"architecture.tower",       R"({"profile":"round","width":"0x1p+4","height":"0x1.cp+1","batter":"0x0p+0","wall":"0x1.99999ap-3","opening_width":"0x1.99999ap-1","opening_height":"0x1p+1","top":"battlement","slit_rows":2,"seed":1})"},
        {"nature.leaf_plant",        R"({"form":"banana","height":"0x1.333334p-2","leaves":14,"droop":"0x0p+0","leaf_width":"0x1.99999ap-4","seed":1})"},
        {"nature.palm",              R"({"species":"coconut","height":"0x1.8p+0","bend":"0x0p+0","fronds":24,"frond_length":"0x1.333334p-2","droop":"0x0p+0","seed":1,"segments":12})"},
        {"nature.rock_spire",        R"({"form":"pinnacle","height":"0x1.ep+5","radius":"0x1.99999ap-3","taper":"0x0p+0","lean":"0x0p+0","roughness":"0x0p+0","seed":1,"segments":20})"},
        {"nature.basalt_columns",    R"({"radius":"0x1p+4","column_width":"0x1.333334p-3","height":"0x1.99999ap-3","height_spread":"0x0p+0","step":"0x1.99999ap-5","seed":1})"},
        {"architecture.roof",        R"({"form":"hip","width":"0x1.9cf408p+3","depth":"0x1.76ffb6p+4","pitch":"0x1.6b7964p+5","overhang":"0x1.a87f5ep-4","curve":"0x1.175114p-4","seed":81079})"},
        {"nature.palm_clump",        R"({"species":"areca","stems":9,"height":"0x1p-1","spread":"0x0p+0","fronds":10,"seed":1})"},
        {"nature.palm_shrub",        R"({"species":"cycad","size":"0x1.333334p-2","fronds":16,"seed":1})"},
        {"nature.dune",              R"({"length":"0x1p+2","width":"0x1p+1","height":"0x1.99999ap-5","asymmetry":"0x0p+0","seed":1,"segments":48})"},
        {"nature.ground_patch",      R"({"size":"0x1.99999ap-3","thickness":"0x1.47ae14p-7","irregularity":"0x0p+0","dome":"0x0p+0","seed":1,"segments":48})"},
        {"nature.cliff",             R"({"length":"0x1.ddc0f6p+5","height":"0x1.91c9fcp+3","depth":"0x1.ee5558p+3","strata":"layered","overhang":"0x1.c484fp-5","roughness":"0x1.9873fp-1","ledges":3,"seed":261})"}
    };
    rows.pins = {
        {"basic.block 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 6.00000f},
        {"basic.wedge 1", 6, 5, 18, 0x2d401a55eec16520ull, {-0.50000f, 0.00000f, -1.00000f}, {0.50000f, 1.00000f, 1.00000f}, {0.00000f, 0.33333f, -0.33333f}, 7.23607f},
        {"basic.cylinder 1", 32, 18, 96, 0x8cc58f15ad955627ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 4.65218f},
        {"basic.stairs 1", 64, 92, 308, 0xcb65e93be76ff389ull, {-0.50000f, 0.00000f, -2.25000f}, {0.50000f, 3.00000f, 2.25000f}, {0.00000f, 1.50000f, 0.00000f}, 29.40000f},
        {"basic.floor_slab 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.20000f, 1.00000f}, {0.00000f, 0.10000f, 0.00000f}, 9.60000f},
        {"architecture.wall 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.50000f, 0.00000f}, 27.50000f},
        {"architecture.door_wall 1", 24, 22, 88, 0x0cdadef454be63e3ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.73333f, 0.00000f}, 23.32000f},
        {"architecture.pillar 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.25000f, 0.00000f, -0.25000f}, {0.25000f, 3.00000f, 0.25000f}, {0.00000f, 1.50000f, 0.00000f}, 6.50000f},
        {"architecture.arch 1", 52, 50, 200, 0x8d700e0ae6ddc407ull, {-1.30000f, 0.00000f, -0.25000f}, {1.30000f, 2.70000f, 0.25000f}, {0.00000f, 1.48994f, 0.00000f}, 9.75364f},
        {"architecture.column 1", 370, 454, 1632, 0x0087a9af94c808e8ull, {-0.31500f, 0.00000f, -0.31500f}, {0.31500f, 3.00000f, 0.31500f}, {0.00000f, 1.47232f, 0.00000f}, 6.95078f},
        {"architecture.entablature 1", 344, 274, 1088, 0x841c1415a8ad577cull, {-1.00000f, 0.00000f, -0.44000f}, {1.00000f, 0.75000f, 0.44000f}, {0.00000f, 0.35913f, 0.00000f}, 9.52515f},
        {"architecture.torii 1", 216, 168, 736, 0x4afb59e2a3a2aeddull, {-2.55000f, 0.00000f, -0.18750f}, {2.55000f, 3.78000f, 0.18750f}, {0.00000f, 2.32201f, 0.00000f}, 20.62952f},
        {"architecture.battlement 1", 24, 22, 88, 0x0cdadef454be63e3ull, {-1.00000f, 0.00000f, -0.12500f}, {1.00000f, 1.80000f, 0.12500f}, {0.00000f, 0.90000f, 0.00000f}, 8.11000f},
        {"architecture.tower 1", 334, 427, 1530, 0x73bb4286a4031605ull, {-1.66083f, 0.00000f, -1.63560f}, {1.66083f, 6.40000f, 1.72083f}, {0.00000f, 3.57305f, 0.01988f}, 125.54085f},
        {"architecture.roof 1", 296, 254, 1016, 0xf10a827156626e1bull, {-2.33000f, 0.00000f, -2.30000f}, {2.33000f, 1.83248f, 2.30000f}, {0.00000f, 0.83722f, 0.00000f}, 156.34335f},
        {"nature.rock 1", 58, 97, 306, 0xac804b820e4fe984ull, {-0.21238f, 0.00000f, -0.30518f}, {0.23571f, 0.35739f, 0.26512f}, {0.01986f, 0.12991f, -0.01321f}, 0.67238f},
        {"nature.rock 3", 52, 89, 278, 0xab55ed54ea11293cull, {-0.42355f, 0.00000f, -0.52139f}, {0.53290f, 0.57794f, 0.56602f}, {-0.01152f, 0.21975f, -0.02118f}, 2.47714f},
        {"nature.rock 17", 57, 99, 308, 0xec75a392babb62a6ull, {-0.53616f, 0.00000f, -0.41667f}, {0.53399f, 0.53632f, 0.47814f}, {-0.00520f, 0.21338f, 0.01204f}, 2.21727f},
        {"nature.land_mass 1", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-1.85458f, 0.00000f, -2.14295f}, {2.03018f, 1.00000f, 2.16942f}, {0.02663f, 0.49913f, -0.01562f}, 29.45218f},
        {"nature.land_mass 3", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.18004f, 0.00000f, -1.92235f}, {2.10604f, 1.00000f, 1.96032f}, {-0.03651f, 0.49683f, 0.00018f}, 29.59030f},
        {"nature.land_mass 17", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.12133f, 0.00000f, -2.04869f}, {2.29766f, 1.00000f, 2.03226f}, {0.00202f, 0.49771f, -0.00784f}, 30.76304f},
        {"nature.hill 1", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-2.83959f, 0.00000f, -3.13385f}, {2.99969f, 0.65000f, 3.16990f}, {0.02293f, 0.24571f, -0.01162f}, 56.45112f},
        {"nature.hill 3", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-3.12943f, 0.00000f, -2.82175f}, {3.05097f, 1.15000f, 2.90477f}, {-0.02655f, 0.43359f, 0.00287f}, 56.71866f},
        {"nature.hill 17", 66, 128, 384, 0x4a7b7aaab8ac5c75ull, {-3.06270f, 0.00000f, -2.97855f}, {3.32143f, 0.70000f, 3.02918f}, {0.00343f, 0.26353f, -0.00586f}, 57.12008f},
        {"nature.tree 1", 38, 56, 180, 0xa19deeea55812f9dull, {-0.94027f, 0.00000f, -0.89835f}, {0.80734f, 2.97000f, 0.93816f}, {-0.01557f, 1.79453f, -0.00565f}, 9.78669f},
        {"nature.tree 3", 38, 56, 180, 0xa19deeea55812f9dull, {-1.33725f, 0.00000f, -1.40188f}, {1.23756f, 4.71000f, 1.59014f}, {-0.02287f, 2.82603f, 0.01781f}, 24.08631f},
        {"nature.tree 17", 38, 56, 180, 0xa19deeea55812f9dull, {-0.98720f, 0.00000f, -1.07994f}, {1.16362f, 4.14000f, 1.06425f}, {-0.00568f, 2.66775f, 0.00670f}, 14.01106f},
        {"nature.bush 1", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.70145f, 0.00000f, -0.65352f}, {0.54069f, 0.78000f, 0.68968f}, {-0.02239f, 0.28680f, -0.00804f}, 3.62053f},
        {"nature.bush 3", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.55467f, 0.00000f, -0.59916f}, {0.48508f, 0.75000f, 0.72354f}, {-0.02115f, 0.27234f, 0.01868f}, 3.05131f},
        {"nature.bush 17", 26, 48, 144, 0x4d7ab5fa3a724ae5ull, {-0.51817f, 0.00000f, -0.60427f}, {0.67877f, 0.69000f, 0.59587f}, {-0.00531f, 0.25871f, 0.00662f}, 2.91611f},
        {"nature.cliff 1", 216, 395, 1218, 0xc108f4e24607d651ull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 6.90000f, 1.99953f}, {0.00022f, 3.45461f, 0.80411f}, 101.90959f},
        {"nature.cliff 3", 144, 251, 786, 0x9500dab4b480d18eull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 3.30000f, 1.43399f}, {0.00098f, 1.65618f, 0.60090f}, 55.39624f},
        {"nature.cliff 17", 168, 299, 930, 0x644c8f3b707981b1ull, {-2.00000f, 0.00000f, -1.00000f}, {2.00000f, 3.90000f, 1.31200f}, {-0.00100f, 1.95505f, 0.39021f}, 62.68912f},
        {"nature.shore 1", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.10450f, 3.00000f}, {0.00000f, 0.67512f, 0.00000f}, 62.04102f},
        {"nature.shore 3", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.10131f, 3.00000f}, {0.00000f, 0.69196f, 0.00000f}, 62.01799f},
        {"nature.shore 17", 121, 237, 712, 0x29c0ffce82052448ull, {-2.00000f, 0.00000f, -3.00000f}, {2.00000f, 1.12084f, 3.00000f}, {0.00000f, 0.68936f, 0.00000f}, 62.01485f},
        {"nature.water 1", 4, 1, 4, 0xad2aca7747985764ull, {-4.00000f, 0.00000f, -4.00000f}, {4.00000f, 0.00000f, 4.00000f}, {0.00000f, 0.00000f, 0.00000f}, 64.00000f},
        {"nature.dune 1", 144, 270, 824, 0x045c03b19e2880b0ull, {-4.66538f, 0.00000f, -3.37500f}, {4.66538f, 0.63700f, 3.37500f}, {-0.04318f, 0.29990f, 0.46217f}, 107.53407f},
        {"nature.dune 3", 144, 270, 824, 0x045c03b19e2880b0ull, {-5.16852f, 0.00000f, -3.25000f}, {5.16852f, 0.73500f, 3.25000f}, {-0.10119f, 0.34604f, 0.44383f}, 118.06809f},
        {"nature.dune 17", 144, 270, 824, 0x045c03b19e2880b0ull, {-5.91538f, 0.00000f, -3.37500f}, {5.91538f, 0.68600f, 3.37500f}, {0.14362f, 0.32297f, 0.46524f}, 141.90427f},
        {"nature.ground_patch 1", 74, 144, 432, 0x4a263b301c082585ull, {-2.87980f, 0.00000f, -2.64799f}, {2.60800f, 0.20750f, 2.34955f}, {0.00000f, 0.07099f, 0.00000f}, 43.67327f},
        {"nature.ground_patch 3", 74, 144, 432, 0x4a263b301c082585ull, {-2.15619f, 0.00000f, -2.70255f}, {2.05114f, 0.18500f, 2.47553f}, {0.00000f, 0.06558f, -0.00000f}, 32.11528f},
        {"nature.ground_patch 17", 74, 144, 432, 0x4a263b301c082585ull, {-2.23618f, 0.00000f, -2.43645f}, {2.14958f, 0.18500f, 2.34262f}, {0.00000f, 0.06558f, 0.00000f}, 31.79795f},
        {"nature.rock_spire 1", 362, 720, 2160, 0x377e47459fa74c6full, {-3.64752f, 0.00000f, -4.11290f}, {3.52583f, 14.78250f, 3.67956f}, {0.01016f, 7.16546f, -0.19037f}, 238.56767f},
        {"nature.rock_spire 3", 170, 336, 1008, 0x42d1be1a89b1029aull, {-3.07933f, 0.00000f, -3.00723f}, {3.88463f, 4.48500f, 2.16148f}, {0.10510f, 2.03278f, 0.13275f}, 84.62290f},
        {"nature.rock_spire 17", 362, 720, 2160, 0x377e47459fa74c6full, {-2.64916f, 0.00000f, -4.44135f}, {3.23760f, 16.22400f, 3.17109f}, {0.33947f, 7.92638f, 0.28505f}, 228.92017f},
        {"nature.basalt_columns 1", 444, 296, 1332, 0x44fbdab783dc67b2ull, {-1.71299f, 0.00000f, -1.55557f}, {1.72809f, 2.70000f, 1.56255f}, {0.00000f, 1.24459f, -0.00000f}, 148.30334f},
        {"nature.basalt_columns 3", 732, 488, 2196, 0x40fce0f82c29d472ull, {-2.57168f, 0.00000f, -2.30863f}, {2.58082f, 2.70000f, 2.31266f}, {0.00000f, 1.19754f, -0.00000f}, 285.46548f},
        {"nature.basalt_columns 17", 84, 56, 252, 0x4d50163cec202f6dull, {-0.72651f, 0.00000f, -0.69813f}, {0.72253f, 2.70000f, 0.68889f}, {0.00000f, 1.32857f, -0.00000f}, 30.45187f},
        {"nature.palm 1", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.05603f, 0.00000f, -1.48759f}, {2.37736f, 12.33090f, 3.88708f}, {-0.45316f, 10.53339f, 0.92783f}, 58.33212f},
        {"nature.palm 3", 555, 983, 2964, 0x6eefdf6fe4d09787ull, {-3.48997f, 0.00000f, -2.01061f}, {1.86270f, 10.79839f, 3.08687f}, {-0.61547f, 9.04642f, 0.50534f}, 35.82668f},
        {"nature.palm 17", 843, 1495, 4500, 0x5d9ce96fdb009a31ull, {-3.22049f, 0.00000f, -4.16760f}, {2.50953f, 12.78151f, 1.69860f}, {-0.43074f, 10.89694f, -1.16128f}, 58.57428f},
        {"nature.leaf_plant 1", 348, 614, 1848, 0x2f2da5e5ed3504c9ull, {-0.53277f, 0.00000f, -0.40541f}, {0.64207f, 1.24859f, 0.47117f}, {0.01968f, 0.77218f, 0.01167f}, 1.48334f},
        {"nature.leaf_plant 3", 603, 1064, 3198, 0xc7e7e0947ff42590ull, {-0.21391f, 0.00000f, -0.17556f}, {0.15538f, 0.37633f, 0.18560f}, {-0.00717f, 0.26109f, -0.00173f}, 0.28674f},
        {"nature.leaf_plant 17", 348, 614, 1848, 0x2f2da5e5ed3504c9ull, {-0.71894f, 0.00000f, -0.64669f}, {1.00937f, 1.69909f, 0.95500f}, {0.03086f, 1.15130f, 0.08859f}, 3.90585f},
        {"nature.palm_clump 1", 1134, 1865, 5642, 0x344df1114a183a71ull, {-1.56352f, 0.00000f, -2.17078f}, {1.37917f, 4.85206f, 1.75516f}, {-0.03700f, 2.77721f, -0.01980f}, 18.72364f},
        {"nature.palm_clump 3", 670, 1097, 3322, 0x17ae610c8871716dull, {-0.39434f, 0.00000f, -0.74393f}, {0.88112f, 2.23237f, 0.61127f}, {0.11201f, 1.40232f, -0.03305f}, 2.72475f},
        {"nature.palm_clump 17", 506, 825, 2502, 0x6dfb0758e807b7f2ull, {-1.68756f, 0.00000f, -0.69025f}, {0.93280f, 4.08483f, 0.83392f}, {-0.10641f, 2.28608f, -0.02681f}, 5.82635f},
        {"nature.palm_shrub 1", 342, 470, 1524, 0x48d5df08b6c0da18ull, {-1.22554f, 0.00000f, -0.93309f}, {1.12310f, 1.30057f, 1.10304f}, {-0.00991f, 0.68600f, 0.02234f}, 15.22223f},
        {"nature.palm_shrub 3", 288, 396, 1284, 0x84a19d9cbd27a482ull, {-0.82727f, 0.00000f, -0.82216f}, {0.97043f, 1.07892f, 0.80320f}, {0.03224f, 0.52061f, -0.01719f}, 7.73327f},
        {"nature.palm_shrub 17", 396, 544, 1764, 0xfd0d30eb35555863ull, {-1.03428f, 0.00000f, -0.79639f}, {0.85171f, 1.18611f, 0.93885f}, {0.00018f, 0.60575f, 0.01251f}, 13.27199f}
    };
    return rows;
}

} // namespace editor::kit_test
