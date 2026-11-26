#include "kit_shape_test_support.hpp"

// The starter set and the first built shapes (doc/editor/level_kit_shapes.md).
namespace editor::kit_test {

auto get_starter_rows() -> Kit_shape_rows
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
    // The palms and the plants are stems and closed leaves, the basalt one part per column; the column, entablature,
    // torii, roof, battlement and tower are closed parts that overlap (base, shaft, capital; profile, triglyphs; posts,
    // beams; slab, body, tile courses, caps; breastwork, band, merlons, copings; walls, courses, surround, parapet,
    // corbels); the rock, the hill, the tree, the bush and the spire are pieces, outcrops, limbs, clumps and blocks.
    rows.components = {
        {"nature.palm", Kit_component_rule::more_than, 10}, {"nature.leaf_plant", Kit_component_rule::more_than, 8},
        {"nature.palm_clump", Kit_component_rule::more_than, 10}, {"nature.palm_shrub", Kit_component_rule::more_than, 10},
        {"nature.basalt_columns", Kit_component_rule::more_than, 10},
        {"architecture.column", Kit_component_rule::more_than, 3}, {"architecture.entablature", Kit_component_rule::more_than, 3},
        {"architecture.torii", Kit_component_rule::more_than, 3}, {"architecture.roof", Kit_component_rule::more_than, 3},
        {"architecture.battlement", Kit_component_rule::more_than, 3}, {"architecture.tower", Kit_component_rule::more_than, 10},
        {"nature.rock", Kit_component_rule::exactly, 3}, {"nature.land_mass", Kit_component_rule::exactly, 1},
        {"nature.hill", Kit_component_rule::exactly, 3}, {"nature.tree", Kit_component_rule::more_than, 5},
        {"nature.bush", Kit_component_rule::more_than, 5}, {"nature.rock_spire", Kit_component_rule::more_than, 2}
    };
    // Built shapes whose derive ranges scale with the lengths (doc/plans/open_items.md "Derived walkability at other level lengths").
    rows.walkability_open = {"basic.wedge", "basic.stairs", "basic.floor_slab", "architecture.arch", "nature.land_mass", "nature.shore"};
    rows.triangle_counts = {
        {"basic.block", 12}, {"basic.wedge", 8}, {"basic.cylinder", 60}, {"basic.stairs", 180}, {"basic.floor_slab", 12},
        {"architecture.wall", 12}, {"architecture.door_wall", 572}, {"architecture.pillar", 100}, {"architecture.arch", 516},
        {"nature.rock", 162}, {"nature.land_mass", 280}, {"nature.hill", 304}, {"nature.tree", 458}, {"nature.bush", 408},
        {"nature.cliff", 428}, {"nature.shore", 238}, {"nature.water", 2}, {"nature.palm", 1390},
        {"architecture.column", 724}, {"architecture.entablature", 540}, {"architecture.torii", 400}, {"architecture.battlement", 112},
        {"architecture.tower", 1524}, {"nature.leaf_plant", 488}, {"nature.rock_spire", 396}, {"nature.basalt_columns", 740},
        {"architecture.roof", 508}, {"nature.palm_clump", 1672}, {"nature.palm_shrub", 1246}, {"nature.dune", 284}, {"nature.ground_patch", 144}
    };
    // R2.6 where the triangle maxima lie at mixed values, not at the all-maximum corner.
    rows.worst_cases = {
        {"architecture.column",      R"({"order":"corinthian","height":"0x1p-1","width":"0x1.99999ap-4","flutes":24,"taper":"0x0p+0","seed":1,"segments":20})"},
        {"architecture.entablature", R"({"length":"0x1p+3","height":"0x1.99999ap-3","depth":"0x1.99999ap-3","cornice":"0x0p+0","frieze":"triglyph","seed":1})"},
        {"architecture.entablature", R"({"length":"0x1p+1","height":"0x1.99999ap-3","depth":"0x1p-1","cornice":"0x1.333334p-3","frieze":"triglyph","seed":1})"},
        {"architecture.torii",       R"({"width":"0x1p+0","height":"0x1p+1","curve":"0x0p+0","post_width":"0x1.99999ap-4","lean":"0x0p+0","seed":1,"segments":24})"},
        {"nature.palm",              R"({"species":"coconut","height":"0x1.8p+0","bend":"0x0p+0","fronds":24,"frond_length":"0x1.333334p-2","droop":"0x0p+0","seed":1,"segments":12})"},
        {"nature.basalt_columns",    R"({"radius":"0x1p+4","column_width":"0x1.333334p-3","height":"0x1.99999ap-3","height_spread":"0x0p+0","step":"0x1.99999ap-5","seed":1})"},
        {"architecture.roof",        R"({"form":"hip","width":"0x1.9cf408p+3","depth":"0x1.76ffb6p+4","pitch":"0x1.6b7964p+5","overhang":"0x1.a87f5ep-4","curve":"0x1.175114p-4","seed":81079})"},
        {"nature.palm_clump",        R"({"species":"areca","stems":9,"height":"0x1p-1","spread":"0x0p+0","fronds":10,"seed":1})"},
        {"nature.dune",              R"({"length":"0x1p+2","width":"0x1p+1","height":"0x1.99999ap-5","asymmetry":"0x0p+0","seed":1,"segments":48})"},
        {"nature.ground_patch",      R"({"size":"0x1.99999ap-3","thickness":"0x1.47ae14p-7","irregularity":"0x0p+0","dome":"0x0p+0","seed":1,"segments":48})"},
        {"nature.cliff",             R"({"length":"0x1.ddc0f6p+5","height":"0x1.91c9fcp+3","depth":"0x1.ee5558p+3","strata":"layered","overhang":"0x1.c484fp-5","roughness":"0x1.9873fp-1","ledges":3,"seed":261})"},
        {"architecture.battlement",  R"({"length":"0x1.5ab8b4p+4","height":"0x1.8p+1","thickness":"0x1.edc47p+1","merlon_height":"0x1.99999ap-3","merlon_width":"0x1.99999ap-2","crenel_width":"0x1.333334p-2","merlon":"swallowtail","seed":1})"},
        {"architecture.tower",       R"({"profile":"square","width":"0x1p+4","height":"0x1.ep+4","batter":"0x1.4p+3","wall":"0x1.9497dcp-2","opening_width":"0x1.99999ap-1","opening_height":"0x1.8dfeaap+1","top":"battlement","slit_rows":2,"seed":1})"},
        {"nature.leaf_plant",        R"({"form":"tree_fern","height":"0x1p+3","leaves":14,"droop":"0x1p+0","leaf_width":"0x1.333334p-1","seed":0})"},
        {"nature.leaf_plant",        R"({"form":"fern","height":"0x1.333334p-2","leaves":10,"droop":"0x1p+0","leaf_width":"0x1.99999ap-4","seed":1})"},
        {"nature.palm_shrub",        R"({"species":"cycad","size":"0x1.8p+2","fronds":16,"seed":2})"},
        {"nature.palm_shrub",        R"({"species":"cycad","size":"0x1.333334p-2","fronds":12,"seed":1})"},
        {"nature.rock_spire",        R"({"form":"hoodoo","height":"0x1.ep+5","radius":"0x1p+5","taper":"0x1p+0","lean":"0x1.ep+3","roughness":"0x1p+0","strata":16,"seed":0,"segments":20})"},
        {"nature.rock_spire",        R"({"form":"pinnacle","height":"0x1.ep+5","radius":"0x1.99999ap-3","taper":"0x1p+0","lean":"0x1.ep+3","roughness":"0x1p+0","strata":16,"seed":1,"segments":20})"}
    };
    rows.pins = {
        {"architecture.wall 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 1.50000f, 0.00000f}, 27.50000f},
        {"architecture.column 1", 370, 454, 1632, 0x0087a9af94c808e8ull, {-0.31500f, 0.00000f, -0.31500f}, {0.31500f, 3.00000f, 0.31500f}, {0.00000f, 1.47232f, 0.00000f}, 6.95078f},
        {"architecture.entablature 1", 344, 274, 1088, 0x841c1415a8ad577cull, {-1.00000f, 0.00000f, -0.44000f}, {1.00000f, 0.75000f, 0.44000f}, {0.00000f, 0.35913f, 0.00000f}, 9.52515f},
        {"architecture.torii 1", 216, 168, 736, 0x4afb59e2a3a2aeddull, {-2.55000f, 0.00000f, -0.18750f}, {2.55000f, 3.78000f, 0.18750f}, {0.00000f, 2.32201f, 0.00000f}, 20.62952f},
        {"architecture.roof 1", 296, 254, 1016, 0xf10a827156626e1bull, {-2.33000f, 0.00000f, -2.30000f}, {2.33000f, 1.83248f, 2.30000f}, {0.00000f, 0.83722f, 0.00000f}, 156.34335f},
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
        {"nature.basalt_columns 1", 444, 296, 1332, 0x44fbdab783dc67b2ull, {-1.71299f, 0.00000f, -1.55557f}, {1.72809f, 2.70000f, 1.56255f}, {0.00000f, 1.24459f, -0.00000f}, 148.30334f},
        {"nature.basalt_columns 3", 732, 488, 2196, 0x40fce0f82c29d472ull, {-2.57168f, 0.00000f, -2.30863f}, {2.58082f, 2.70000f, 2.31266f}, {0.00000f, 1.19754f, -0.00000f}, 285.46548f},
        {"nature.basalt_columns 17", 84, 56, 252, 0x4d50163cec202f6dull, {-0.72651f, 0.00000f, -0.69813f}, {0.72253f, 2.70000f, 0.68889f}, {0.00000f, 1.32857f, -0.00000f}, 30.45187f},
        {"nature.palm 1", 743, 1335, 4060, 0x52f3f1902a3f51e6ull, {-3.05531f, 0.00000f, -1.48759f}, {2.37726f, 12.33688f, 3.88702f}, {-0.45717f, 10.54681f, 0.91988f}, 56.33646f},
        {"nature.palm 3", 495, 887, 2700, 0x62f1e9a5c17e737cull, {-3.48982f, 0.00000f, -2.01060f}, {1.86162f, 10.79839f, 3.08676f}, {-0.61603f, 8.99903f, 0.49047f}, 34.77498f},
        {"nature.palm 17", 743, 1335, 4060, 0x52f3f1902a3f51e6ull, {-3.21624f, 0.00000f, -4.16758f}, {2.50948f, 12.78151f, 1.69853f}, {-0.44318f, 10.80576f, -1.14942f}, 56.67741f},
        {"nature.palm_clump 1", 934, 1545, 4762, 0x1ba67dfbadf11a30ull, {-1.55982f, 0.00000f, -2.17077f}, {1.37892f, 4.85206f, 1.75213f}, {-0.03852f, 2.71130f, -0.01742f}, 18.17366f},
        {"nature.palm_clump 3", 550, 905, 2794, 0x19035ef6fb024d2dull, {-0.39273f, 0.00000f, -0.74344f}, {0.88110f, 2.23237f, 0.61126f}, {0.10720f, 1.37321f, -0.03334f}, 2.65826f},
        {"nature.palm_clump 17", 416, 681, 2106, 0x70fa42a869cda662ull, {-1.68755f, 0.00000f, -0.68790f}, {0.93149f, 4.08483f, 0.83121f}, {-0.10010f, 2.23638f, -0.02999f}, 5.65606f},
        {"basic.block 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 6.00000f},
        {"basic.wedge 1", 6, 5, 18, 0x2d401a55eec16520ull, {-0.50000f, 0.00000f, -1.00000f}, {0.50000f, 1.00000f, 1.00000f}, {0.00000f, 0.33333f, -0.33333f}, 7.23607f},
        {"basic.cylinder 1", 32, 18, 96, 0x8cc58f15ad955627ull, {-0.50000f, 0.00000f, -0.50000f}, {0.50000f, 1.00000f, 0.50000f}, {0.00000f, 0.50000f, 0.00000f}, 4.65218f},
        {"basic.stairs 1", 92, 104, 388, 0x4c5057cd16338a9dull, {-0.50000f, 0.00000f, -2.25000f}, {0.50000f, 3.00000f, 2.25000f}, {0.00000f, 1.04348f, 0.00000f}, 29.40000f},
        {"basic.floor_slab 1", 8, 6, 24, 0x0d301e6ef1629ad3ull, {-1.00000f, 0.00000f, -1.00000f}, {1.00000f, 0.20000f, 1.00000f}, {0.00000f, 0.10000f, 0.00000f}, 9.60000f},
        {"architecture.door_wall 1", 288, 314, 1200, 0x3c3e06d85ee8c4c1ull, {-2.00000f, 0.00000f, -0.12500f}, {2.00000f, 3.00000f, 0.12500f}, {0.00000f, 2.18111f, 0.00000f}, 23.27834f},
        {"architecture.pillar 1", 52, 50, 200, 0x8d700e0ae6ddc407ull, {-0.25000f, 0.00000f, -0.25000f}, {0.25000f, 3.00000f, 0.25000f}, {0.00000f, 1.81240f, 0.00000f}, 5.52820f},
        {"architecture.arch 1", 260, 282, 1080, 0x1ba910e3898af7dfull, {-1.30000f, 0.00000f, -0.25000f}, {1.30000f, 2.70000f, 0.25000f}, {0.00000f, 1.63505f, 0.00000f}, 9.22280f},
        {"architecture.battlement 1", 62, 49, 210, 0xa75f512b36454678ull, {-1.00000f, 0.00000f, -0.16000f}, {1.00000f, 1.80000f, 0.16000f}, {0.00000f, 1.16052f, 0.00000f}, 10.21486f},
        {"architecture.tower 1", 824, 786, 3096, 0x30bc2e39165734c9ull, {-1.79000f, 0.00000f, -1.76281f}, {1.79000f, 6.40000f, 1.76281f}, {0.00000f, 3.58014f, 0.04381f}, 205.64935f},
        {"nature.rock 1", 87, 108, 378, 0xcc65a7abbd5c9859ull, {-0.38246f, 0.00000f, -0.32975f}, {0.51908f, 0.41698f, 0.34153f}, {0.03528f, 0.13556f, -0.09896f}, 1.72307f},
        {"nature.rock 3", 83, 103, 360, 0x6c8af37161e47062ull, {-1.08382f, 0.00000f, -1.18839f}, {0.83022f, 0.87129f, 0.82110f}, {-0.11019f, 0.29129f, -0.12414f}, 8.59265f},
        {"nature.rock 17", 95, 118, 414, 0x0c305fff1b75f603ull, {-0.61478f, 0.00000f, -0.95683f}, {0.58810f, 0.69334f, 0.62484f}, {-0.18276f, 0.24415f, -0.04642f}, 4.92481f},
        {"nature.land_mass 1", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.63238f, 0.00000f, -1.80724f}, {1.63864f, 1.00000f, 1.79658f}, {-0.00724f, 0.57774f, -0.00370f}, 24.12314f},
        {"nature.land_mass 3", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.47213f, 0.00000f, -1.86135f}, {1.78628f, 1.00000f, 1.65956f}, {-0.01490f, 0.58049f, -0.01205f}, 23.86616f},
        {"nature.land_mass 17", 142, 280, 840, 0x1d7d06dd136a33a5ull, {-1.50885f, 0.00000f, -1.97722f}, {1.64668f, 1.00000f, 1.64419f}, {-0.00146f, 0.57901f, -0.00286f}, 24.08301f},
        {"nature.hill 1", 158, 262, 828, 0xf8df0aae6243e623ull, {-2.61038f, 0.00000f, -2.81574f}, {2.72897f, 1.13459f, 2.82760f}, {-0.44147f, 0.46375f, 0.05415f}, 55.11851f},
        {"nature.hill 3", 146, 247, 774, 0x59399e20a8b26082ull, {-2.55359f, 0.00000f, -2.84196f}, {2.83937f, 1.08355f, 2.71595f}, {-0.50907f, 0.38240f, 0.09136f}, 53.17775f},
        {"nature.hill 17", 150, 252, 792, 0xb914525b042a8879ull, {-2.63117f, 0.00000f, -2.93051f}, {2.73480f, 0.74337f, 2.67818f}, {0.14951f, 0.26875f, -0.65042f}, 54.24910f},
        {"nature.tree 1", 249, 458, 1374, 0x749f65e5e38f5aa4ull, {-0.50365f, 0.00000f, -1.07343f}, {1.03693f, 2.87100f, 0.91544f}, {0.13823f, 1.91490f, -0.15197f}, 14.64000f},
        {"nature.tree 3", 292, 536, 1608, 0xd30a3dd2d8378012ull, {-1.37001f, 0.00000f, -1.63476f}, {1.75703f, 4.55050f, 1.66454f}, {-0.01467f, 2.98247f, 0.13749f}, 48.37881f},
        {"nature.tree 17", 249, 458, 1374, 0x749f65e5e38f5aa4ull, {-1.11667f, 0.00000f, -1.00312f}, {0.95245f, 4.02450f, 1.28052f}, {0.00166f, 2.84520f, 0.00530f}, 22.08683f},
        {"nature.bush 1", 224, 408, 1224, 0x90a9514e1cced0f2ull, {-0.60605f, 0.00000f, -0.54539f}, {0.51445f, 0.83947f, 0.50183f}, {-0.02048f, 0.37057f, -0.05127f}, 6.59186f},
        {"nature.bush 3", 224, 408, 1224, 0x90a9514e1cced0f2ull, {-0.43904f, 0.00000f, -0.57791f}, {0.61761f, 0.80404f, 0.57261f}, {0.02901f, 0.33909f, 0.01638f}, 5.71063f},
        {"nature.bush 17", 250, 456, 1368, 0x62589ed50ece6ea2ull, {-0.64403f, 0.00000f, -0.51339f}, {0.53523f, 0.74696f, 0.60100f}, {-0.01896f, 0.30260f, -0.02817f}, 6.61271f},
        {"nature.rock_spire 1", 206, 396, 1188, 0x27e6ad4a60447496ull, {-4.26199f, 0.00000f, -4.04011f}, {3.58619f, 15.93060f, 3.67141f}, {0.13269f, 5.92629f, -0.39138f}, 213.05231f},
        {"nature.rock_spire 3", 218, 416, 1248, 0x4eb0477cb4a55012ull, {-3.56880f, 0.00000f, -3.75071f}, {4.10082f, 5.01379f, 2.55549f}, {0.17434f, 1.56452f, -0.38708f}, 93.71077f},
        {"nature.rock_spire 17", 218, 416, 1248, 0x5c842db5842d76baull, {-4.08935f, 0.00000f, -3.61736f}, {4.02963f, 17.45032f, 3.06503f}, {0.18783f, 6.10117f, -0.05149f}, 222.88402f},
        {"nature.leaf_plant 1", 264, 488, 1464, 0xe4e2b22c4c6890a2ull, {-0.64295f, 0.00000f, -0.59633f}, {0.44566f, 1.36983f, 0.74172f}, {0.00322f, 0.74075f, 0.02671f}, 2.77000f},
        {"nature.leaf_plant 3", 414, 768, 2304, 0x58b4aca7963c868full, {-0.20958f, 0.00000f, -0.21502f}, {0.21721f, 0.45661f, 0.20932f}, {0.00161f, 0.25911f, 0.00638f}, 0.48745f},
        {"nature.leaf_plant 17", 246, 456, 1368, 0x0e9aa3c7bbf9ba82ull, {-0.83343f, 0.00000f, -0.82759f}, {0.88511f, 2.10041f, 0.98743f}, {-0.02775f, 1.14201f, 0.04518f}, 6.25654f},
        {"nature.palm_shrub 1", 679, 1246, 3738, 0x22ae77083aec8217ull, {-1.45501f, 0.00000f, -1.18102f}, {1.42372f, 1.49740f, 1.48621f}, {0.06650f, 0.63298f, -0.05399f}, 12.09671f},
        {"nature.palm_shrub 3", 579, 1062, 3186, 0x184b10d393ff2cfeull, {-1.11559f, 0.00000f, -0.98068f}, {0.93700f, 1.05334f, 1.12683f}, {0.01303f, 0.49561f, -0.01773f}, 6.68229f},
        {"nature.palm_shrub 17", 779, 1430, 4290, 0x1538f3f36ef66444ull, {-1.29283f, 0.00000f, -1.26377f}, {1.20507f, 1.27121f, 1.05130f}, {0.06285f, 0.58328f, -0.02918f}, 11.34427f}
    };
    return rows;
}

} // namespace editor::kit_test
