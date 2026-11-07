#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using erhe::asset_store::Content_hasher;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;
using erhe::asset_store::Key_builder;
using erhe::asset_store::Key_hash;

auto bytes_of(const std::string_view text) -> std::span<const std::byte>
{
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

TEST(Asset_store_key, hex_round_trip)
{
    const Key key = Content_hasher::hash(bytes_of("erhe"));
    const std::string hex = key.to_hex();
    EXPECT_EQ(hex.size(), 32u);
    const std::optional<Key> parsed = Key::from_hex(hex);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value(), key);
    EXPECT_FALSE(Key::from_hex("0123").has_value());
    EXPECT_FALSE(Key::from_hex("zz000000000000000000000000000000").has_value());
}

TEST(Asset_store_key, xxh3_128_reference_value)
{
    // XXH3-128 of empty input with seed 0, canonical form (xxHash test vectors).
    const Key empty = Content_hasher::hash({});
    EXPECT_EQ(empty.to_hex(), "99aa06d3014798d86001c324468d497f");
}

TEST(Asset_store_key, incremental_equals_one_shot)
{
    std::vector<std::byte> data(100000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::byte>((i * 2654435761u) >> 13u);
    }
    Content_hasher hasher;
    const std::span<const std::byte> all{data};
    hasher.update(all.subspan(0, 7));
    hasher.update(all.subspan(7, 50000));
    hasher.update(all.subspan(50007));
    EXPECT_EQ(hasher.finish(), Content_hasher::hash(all));
    hasher.reset();
    EXPECT_EQ(hasher.finish(), Content_hasher::hash({}));
}

TEST(Asset_store_key, builder_is_deterministic)
{
    const Key content = Content_hasher::hash(bytes_of("mesh"));
    const Key a = Key_builder{Item_kind::local_mesh}.add_code_version(3).add_format_stamp("v1").add_content_hash(content).finish();
    const Key b = Key_builder{Item_kind::local_mesh}.add_code_version(3).add_format_stamp("v1").add_content_hash(content).finish();
    EXPECT_EQ(a, b);
}

TEST(Asset_store_key, builder_reference_value)
{
    // Keys are persisted: the byte stream of Key_builder must not change between builds or platforms.
    const Key key = Key_builder{Item_kind::local_mesh}
        .add_code_version(1)
        .add_format_stamp("stamp")
        .add_int_setting("lod", -2)
        .add_uint_setting("count", 7)
        .add_float_setting("error", 0.25)
        .add_text_setting("target", "pc")
        .add_key(Content_hasher::hash(bytes_of("other")))
        .add_content_hash(Content_hasher::hash(bytes_of("content")))
        .add_bytes(bytes_of("raw"))
        .finish();
    EXPECT_EQ(key.to_hex(), "3d715c0ea2f24ed26c0d25653dbe429a");
}

TEST(Asset_store_key, builder_separates_field_sequences)
{
    std::vector<Key> keys;
    // Field boundaries: the same concatenated text split differently.
    keys.push_back(Key_builder{Item_kind::scene}.add_format_stamp("ab").add_format_stamp("c").finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_format_stamp("a").add_format_stamp("bc").finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_format_stamp("abc").finish());
    // Setting name / value boundary.
    keys.push_back(Key_builder{Item_kind::scene}.add_text_setting("ab", "c").finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_text_setting("a", "bc").finish());
    // Field type: same bytes as a key, a content hash, raw bytes.
    const Key digest = Content_hasher::hash(bytes_of("x"));
    keys.push_back(Key_builder{Item_kind::scene}.add_key(digest).finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_content_hash(digest).finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_bytes(std::as_bytes(std::span{digest.bytes})).finish());
    // Value type: the same number as signed, unsigned and code version.
    keys.push_back(Key_builder{Item_kind::scene}.add_int_setting("n", 1).finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_uint_setting("n", 1).finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_code_version(1).finish());
    // Kind.
    keys.push_back(Key_builder{Item_kind::local_mesh}.add_code_version(1).finish());
    keys.push_back(Key_builder{Item_kind::world_mesh}.add_code_version(1).finish());
    // Order of fields.
    keys.push_back(Key_builder{Item_kind::scene}.add_code_version(1).add_format_stamp("s").finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_format_stamp("s").add_code_version(1).finish());
    // Empty field versus no field.
    keys.push_back(Key_builder{Item_kind::scene}.finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_format_stamp("").finish());
    // Float bits: zero and negative zero are different settings.
    keys.push_back(Key_builder{Item_kind::scene}.add_float_setting("f", 0.0).finish());
    keys.push_back(Key_builder{Item_kind::scene}.add_float_setting("f", -0.0).finish());

    const std::unordered_set<Key, Key_hash> unique{keys.begin(), keys.end()};
    EXPECT_EQ(unique.size(), keys.size());
}

}
