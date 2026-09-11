// A Skin is shared by every Mesh it skins, and each of those meshes registers
// it with the scene, so the scene counts the uses: the skin list holds one
// entry per skin, and the entry survives until the last skinned mesh leaves.
// (usd-wg test_assets/USDZ/BrainStem: fifty-nine meshes, one Skeleton.)

#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

TEST(Shared_skin_registration, one_entry_per_skin)
{
    erhe::scene::Scene scene{"test scene"};
    const std::shared_ptr<erhe::scene::Skin> skin = std::make_shared<erhe::scene::Skin>("skin");

    EXPECT_EQ(scene.register_skin(skin), erhe::scene::Skin_registry_change::changed);
    EXPECT_EQ(scene.get_skins().size(), std::size_t{1});

    for (int mesh_index = 1; mesh_index < 59; ++mesh_index) {
        EXPECT_EQ(scene.register_skin(skin), erhe::scene::Skin_registry_change::unchanged);
    }
    EXPECT_EQ(scene.get_skins().size(), std::size_t{1});
}

TEST(Shared_skin_registration, skin_leaves_with_the_last_mesh)
{
    erhe::scene::Scene scene{"test scene"};
    const std::shared_ptr<erhe::scene::Skin> skin = std::make_shared<erhe::scene::Skin>("skin");

    scene.register_skin(skin);
    scene.register_skin(skin);
    scene.register_skin(skin);

    EXPECT_EQ(scene.unregister_skin(skin), erhe::scene::Skin_registry_change::unchanged);
    EXPECT_EQ(scene.get_skins().size(), std::size_t{1});
    EXPECT_EQ(scene.unregister_skin(skin), erhe::scene::Skin_registry_change::unchanged);
    EXPECT_EQ(scene.get_skins().size(), std::size_t{1});
    EXPECT_EQ(scene.unregister_skin(skin), erhe::scene::Skin_registry_change::changed);
    EXPECT_TRUE(scene.get_skins().empty());
}

TEST(Shared_skin_registration, two_skins_are_counted_apart)
{
    erhe::scene::Scene scene{"test scene"};
    const std::shared_ptr<erhe::scene::Skin> skin_a = std::make_shared<erhe::scene::Skin>("skin a");
    const std::shared_ptr<erhe::scene::Skin> skin_b = std::make_shared<erhe::scene::Skin>("skin b");

    scene.register_skin(skin_a);
    scene.register_skin(skin_b);
    scene.register_skin(skin_b);
    EXPECT_EQ(scene.get_skins().size(), std::size_t{2});

    EXPECT_EQ(scene.unregister_skin(skin_a), erhe::scene::Skin_registry_change::changed);
    EXPECT_EQ(scene.get_skins().size(), std::size_t{1});
    EXPECT_EQ(scene.get_skins().front(), skin_b);

    EXPECT_EQ(scene.unregister_skin(skin_b), erhe::scene::Skin_registry_change::unchanged);
    EXPECT_EQ(scene.unregister_skin(skin_b), erhe::scene::Skin_registry_change::changed);
    EXPECT_TRUE(scene.get_skins().empty());
}
