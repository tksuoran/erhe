#include "erhe_item/item.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/xform_op.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_time_sample_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_lines(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::ifstream            stream{path};
    std::string              line;
    while (std::getline(stream, line)) {
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto file_holds(const std::filesystem::path& path, const std::string& text) -> bool
{
    for (const std::string& line : read_lines(path)) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto find_node(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

[[nodiscard]] auto find_channel(
    const erhe::scene::Animation&      animation,
    const erhe::scene::Animation_path  path,
    const std::string&                 target_name
) -> const erhe::scene::Animation_channel*
{
    for (const erhe::scene::Animation_channel& channel : animation.channels) {
        if ((erhe::scene::get_animation_path(channel) == path) && channel.target && (channel.target->get_name() == target_name)) {
            return &channel;
        }
    }
    return nullptr;
}

// `time_samples.usda`: one prim whose translate / rotateXYZ / scale ops carry
// time samples, one whose sampled op is a matrix, one whose sampled op sits
// between a pivot pair, and one that samples nothing.
class Time_samples : public testing::Test
{
protected:
    void SetUp() override
    {
        source_path = test_data_path("time_samples.usda");
        root        = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = source_path,
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    [[nodiscard]] auto save(const char* file_name) -> std::filesystem::path
    {
        const std::filesystem::path path = temporary_path(file_name);
        erhe::usd::Usd_save_arguments save_arguments{
            .path      = path,
            .root_node = root
        };
        save_arguments.time_codes_per_second = loaded.data.time_codes.time_codes_per_second;
        const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
        EXPECT_TRUE(save_result.error.empty()) << save_result.error;
        return path;
    }

    // A save that hands the writer the animations the load built, which is
    // what the editor's save does: the writer then reconciles each sampled
    // stack with the clip's keys.
    [[nodiscard]] auto save_with_animations(const char* file_name) -> std::filesystem::path
    {
        const std::filesystem::path path = temporary_path(file_name);
        erhe::usd::Usd_save_arguments save_arguments{
            .path      = path,
            .root_node = root
        };
        save_arguments.time_codes_per_second = loaded.data.time_codes.time_codes_per_second;
        save_arguments.animations            = loaded.data.animations;
        const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
        EXPECT_TRUE(save_result.error.empty()) << save_result.error;
        return path;
    }

    [[nodiscard]] auto reload(const std::filesystem::path& path) -> erhe::usd::Usd_load_result
    {
        const std::shared_ptr<erhe::scene::Node> reload_root = std::make_shared<erhe::scene::Xform>("import_root");
        reload_roots.push_back(reload_root);
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = path,
            .root_node     = reload_root,
            .mesh_layer_id = 0
        };
        erhe::usd::Usd_load_result result = erhe::usd::load_usd(load_arguments);
        EXPECT_TRUE(result.error.empty()) << result.error;
        return result;
    }

    // The sampler of one channel of the loaded clip, which a test edits the
    // way a keyed edit in the editor does.
    [[nodiscard]] auto find_sampler(
        const erhe::scene::Animation_path path,
        const std::string&                target_name
    ) -> erhe::scene::Animation_sampler*
    {
        if (loaded.data.animations.empty()) {
            return nullptr;
        }
        erhe::scene::Animation&                  animation = *loaded.data.animations.front();
        const erhe::scene::Animation_channel*    channel   = find_channel(animation, path, target_name);
        if ((channel == nullptr) || (channel->sampler_index >= animation.samplers.size())) {
            return nullptr;
        }
        return &animation.samplers[channel->sampler_index];
    }

    std::vector<std::shared_ptr<erhe::scene::Node>> reload_roots;
    std::filesystem::path              source_path;
    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Time_samples, stage_time_codes_are_read)
{
    EXPECT_TRUE(loaded.data.time_codes.time_codes_per_second_authored);
    EXPECT_TRUE(loaded.data.time_codes.start_time_code_authored);
    EXPECT_TRUE(loaded.data.time_codes.end_time_code_authored);
    EXPECT_DOUBLE_EQ(loaded.data.time_codes.time_codes_per_second, 24.0);
    EXPECT_DOUBLE_EQ(loaded.data.time_codes.start_time_code,        0.0);
    EXPECT_DOUBLE_EQ(loaded.data.time_codes.end_time_code,         48.0);
}

TEST_F(Time_samples, sampled_ops_carry_their_samples_in_time_codes)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "animated");
    ASSERT_TRUE(node);
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_NE(stack, nullptr);
    ASSERT_EQ(stack->ops.size(), 3u);
    EXPECT_TRUE(stack->has_time_samples());
    for (const erhe::scene::Xform_op& op : stack->ops) {
        ASSERT_EQ(op.samples.size(), 3u);
        EXPECT_DOUBLE_EQ(op.samples[0].time_code,  0.0);
        EXPECT_DOUBLE_EQ(op.samples[1].time_code, 24.0);
        EXPECT_DOUBLE_EQ(op.samples[2].time_code, 48.0);
    }
    EXPECT_EQ(std::get<glm::dvec3>(stack->ops[0].samples[2].value), glm::dvec3(20.0, 0.0, 0.0));
}

// The prim's static transform is its pose at the stage's start time code, not
// the `default` the attribute also carries and not its last sample.
TEST_F(Time_samples, static_transform_is_the_pose_at_the_start_time_code)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "animated");
    ASSERT_TRUE(node);
    const glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[3][0], 0.0f, 1e-5f);
    EXPECT_NEAR(matrix[0][0], 1.0f, 1e-5f);

    // An op that carries only time samples reads its value from them too.
    const std::shared_ptr<erhe::scene::Node> matrix_node = find_node(loaded.data, "sampled_matrix");
    ASSERT_TRUE(matrix_node);
    const glm::mat4 sampled = matrix_node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(sampled[3][0], 0.0f, 1e-5f);
}

TEST_F(Time_samples, sampled_ops_become_one_animation_of_three_channels)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    const std::shared_ptr<erhe::scene::Animation>& animation = loaded.data.animations.front();
    ASSERT_TRUE(animation);
    EXPECT_EQ(animation->get_name(), "time_samples");
    // `animated` is driven op by op; the matrix op, the pivot pair and the
    // [orient, translate] stack are baked into three channels each.
    ASSERT_EQ(animation->channels.size(), 12u);
    std::size_t animated_channels = 0;
    for (const erhe::scene::Animation_channel& channel : animation->channels) {
        ASSERT_TRUE(channel.target);
        if (channel.target->get_name() == "animated") {
            ++animated_channels;
        }
    }
    EXPECT_EQ(animated_channels, 3u);
    EXPECT_NE(find_channel(*animation, erhe::scene::Animation_path::TRANSLATION, "animated"), nullptr);
    EXPECT_NE(find_channel(*animation, erhe::scene::Animation_path::ROTATION,    "animated"), nullptr);
    EXPECT_NE(find_channel(*animation, erhe::scene::Animation_path::SCALE,       "animated"), nullptr);
}

// timeCodesPerSecond is 24 and the samples are at 0, 24 and 48 time codes, so
// the channels are keyed at 0, 1 and 2 seconds.
TEST_F(Time_samples, sample_times_are_seconds)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    const erhe::scene::Animation&            animation = *loaded.data.animations.front();
    const erhe::scene::Animation_channel*    channel   = find_channel(animation, erhe::scene::Animation_path::TRANSLATION, "animated");
    ASSERT_NE(channel, nullptr);
    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel->sampler_index);
    EXPECT_EQ(sampler.interpolation_mode, erhe::scene::Animation_interpolation_mode::LINEAR);
    ASSERT_EQ(sampler.timestamps.size(), 3u);
    EXPECT_FLOAT_EQ(sampler.timestamps[0], 0.0f);
    EXPECT_FLOAT_EQ(sampler.timestamps[1], 1.0f);
    EXPECT_FLOAT_EQ(sampler.timestamps[2], 2.0f);
    ASSERT_EQ(sampler.data.size(), 9u);
    EXPECT_FLOAT_EQ(sampler.data[3], 10.0f);
    EXPECT_FLOAT_EQ(sampler.data[6], 20.0f);
    EXPECT_FLOAT_EQ(loaded.data.animations.front()->get_first_time(), 0.0f);
    EXPECT_FLOAT_EQ(loaded.data.animations.front()->get_last_time (), 2.0f);
}

// Applying the clip at its end puts the prim where the last sample says.
TEST_F(Time_samples, applying_the_clip_moves_the_prim)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    loaded.data.animations.front()->apply(2.0f);
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "animated");
    ASSERT_TRUE(node);
    const glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[3][0], 20.0f, 1e-4f);
}

// A rotation sampled as Euler angles becomes a quaternion channel that reads
// back the same rotation.
TEST_F(Time_samples, euler_samples_become_a_quaternion_channel)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    const erhe::scene::Animation&         animation = *loaded.data.animations.front();
    const erhe::scene::Animation_channel* channel   = find_channel(animation, erhe::scene::Animation_path::ROTATION, "animated");
    ASSERT_NE(channel, nullptr);
    const erhe::scene::Animation_sampler& sampler = animation.samplers.at(channel->sampler_index);
    ASSERT_EQ(sampler.data.size(), 12u);
    // Sample 0 is the identity rotation, packed x, y, z, w.
    EXPECT_NEAR(sampler.data[0], 0.0f, 1e-5f);
    EXPECT_NEAR(sampler.data[3], 1.0f, 1e-5f);
    // Sample 1 is 90 degrees about y.
    const float half_root_two = 0.70710678f;
    EXPECT_NEAR(std::abs(sampler.data[5]), half_root_two, 1e-4f);
    EXPECT_NEAR(std::abs(sampler.data[7]), half_root_two, 1e-4f);
}

// A stack the per-op channels cannot drive is baked: the whole stack is
// composed at every sample time code and decomposed into translation,
// rotation and scale channels, exact at the samples.
TEST_F(Time_samples, a_pivot_pair_and_a_matrix_op_are_baked_into_channels)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    const std::shared_ptr<erhe::scene::Animation>& animation = loaded.data.animations.front();
    EXPECT_NE(find_channel(*animation, erhe::scene::Animation_path::ROTATION,    "pivot_sampled"),  nullptr);
    EXPECT_NE(find_channel(*animation, erhe::scene::Animation_path::TRANSLATION, "sampled_matrix"), nullptr);

    // The sampled matrix op translates by 5 on x at time code 24 (1 s).
    animation->apply(1.0f);
    const std::shared_ptr<erhe::scene::Node> matrix_node = find_node(loaded.data, "sampled_matrix");
    ASSERT_TRUE(matrix_node);
    EXPECT_NEAR(matrix_node->parent_from_node_transform().get_matrix()[3][0], 5.0f, 1e-4f);

    // The pivot pair rotates 90 degrees about y around (0, 1, 0): the origin
    // stays put, so the baked translation is (0, 0, 0) and the rotation is the
    // 90 degree turn.
    const std::shared_ptr<erhe::scene::Node> pivot_node = find_node(loaded.data, "pivot_sampled");
    ASSERT_TRUE(pivot_node);
    const glm::mat4 pivot_matrix = pivot_node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(pivot_matrix[3][0], 0.0f, 1e-4f);
    EXPECT_NEAR(pivot_matrix[3][1], 0.0f, 1e-4f);
    EXPECT_NEAR(pivot_matrix[3][2], 0.0f, 1e-4f);
    EXPECT_NEAR(pivot_matrix[0][2], -1.0f, 1e-3f); // x axis maps to -z after 90 deg about y

    // The samples are still on the ops, so a save writes them back.
    const erhe::scene::Xform_op_stack* pivot_stack = pivot_node->get_xform_op_stack();
    ASSERT_NE(pivot_stack, nullptr);
    EXPECT_TRUE(pivot_stack->has_time_samples());
}

// [orient, translate] composes rotation * translation: the translate is
// rotated. The baked translation channel carries the rotated offset.
TEST_F(Time_samples, an_orient_then_translate_stack_is_baked)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    const std::shared_ptr<erhe::scene::Animation>& animation = loaded.data.animations.front();
    ASSERT_NE(find_channel(*animation, erhe::scene::Animation_path::TRANSLATION, "orient_then_translate"), nullptr);
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "orient_then_translate");
    ASSERT_TRUE(node);

    animation->apply(0.0f);
    glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[3][0], 10.0f, 1e-4f);
    EXPECT_NEAR(matrix[3][2],  0.0f, 1e-4f);

    animation->apply(1.0f);
    matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[3][0],   0.0f, 1e-3f);
    EXPECT_NEAR(matrix[3][2], -10.0f, 1e-3f);
}

TEST_F(Time_samples, a_prim_that_samples_nothing_has_no_samples)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "not_sampled");
    ASSERT_TRUE(node);
    const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
    ASSERT_NE(stack, nullptr);
    EXPECT_FALSE(stack->has_time_samples());
}

// The save writes the samples back as `timeSamples` and authors the layer's
// time coordinates, and the written file is a fixed point of the round trip.
TEST_F(Time_samples, samples_and_time_codes_are_written_back)
{
    const std::filesystem::path first_path = save("time_samples.usda");
    EXPECT_TRUE(file_holds(first_path, "timeSamples"));
    EXPECT_TRUE(file_holds(first_path, "startTimeCode = 0"));
    EXPECT_TRUE(file_holds(first_path, "endTimeCode = 48"));
    EXPECT_TRUE(file_holds(first_path, "timeCodesPerSecond = 24"));

    const std::shared_ptr<erhe::scene::Node> second_root = std::make_shared<erhe::scene::Xform>("import_root");
    const erhe::usd::Usd_load_arguments      load_arguments{
        .path          = first_path,
        .root_node     = second_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;
    ASSERT_EQ(reloaded.data.animations.size(), 1u);
    EXPECT_EQ(reloaded.data.animations.front()->channels.size(), 12u);

    const std::filesystem::path   second_path = temporary_path("time_samples_again.usda");
    erhe::usd::Usd_save_arguments save_arguments{
        .path      = second_path,
        .root_node = second_root
    };
    save_arguments.time_codes_per_second = reloaded.data.time_codes.time_codes_per_second;
    const erhe::usd::Usd_save_result save_result = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save_result.error.empty()) << save_result.error;

    EXPECT_EQ(read_lines(second_path), read_lines(first_path));
}

// A save made while an animation plays writes what the prims authored, not the
// pose the playback put them in: the animated layer (doc/erhe/property_system.md
// D5) never reaches a serializer, and stopping leaves the stage byte for byte
// where it started.
TEST_F(Time_samples, a_save_during_playback_writes_the_authored_transforms)
{
    const std::filesystem::path resting_path = save("time_samples_resting.usda");

    ASSERT_EQ(loaded.data.animations.size(), 1u);
    erhe::scene::Animation& animation = *loaded.data.animations.front();
    animation.apply(1.0f);

    const std::shared_ptr<erhe::scene::Node> node = find_node(loaded.data, "animated");
    ASSERT_TRUE(node);
    EXPECT_TRUE(node->is_local_transform_animated());
    EXPECT_NE(node->parent_from_node(), node->authored_parent_from_node_transform().get_matrix());

    const std::filesystem::path playing_path = save("time_samples_playing.usda");
    EXPECT_EQ(read_lines(playing_path), read_lines(resting_path));

    animation.clear_applied();
    EXPECT_FALSE(node->is_local_transform_animated());

    const std::filesystem::path stopped_path = save("time_samples_stopped.usda");
    EXPECT_EQ(read_lines(stopped_path), read_lines(resting_path));
}


// The op samples of one prim of a reload, by op index.
[[nodiscard]] auto find_op(
    const erhe::usd::Usd_data&       data,
    const std::string&               node_name,
    const erhe::scene::Xform_op_type type
) -> const erhe::scene::Xform_op*
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (!node || (node->get_name() != node_name)) {
            continue;
        }
        const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
        if (stack == nullptr) {
            return nullptr;
        }
        for (const erhe::scene::Xform_op& op : stack->ops) {
            if (op.type == type) {
                return &op;
            }
        }
    }
    return nullptr;
}

// A save that hands the writer the clip is the save the editor makes, and an
// unedited clip still writes the samples the file authored: the keys are the
// projection of those samples, so there is nothing to write back.
TEST_F(Time_samples, an_unedited_clip_writes_the_authored_samples)
{
    const std::filesystem::path without_clip = save("time_samples_plain.usda");
    const std::filesystem::path with_clip    = save_with_animations("time_samples_clip.usda");
    EXPECT_EQ(read_lines(with_clip), read_lines(without_clip));
}

// An edited key value reaches the op's `timeSamples`, and the keys the edit
// did not touch - of this op and of the ops beside it - stay as authored.
TEST_F(Time_samples, an_edited_key_value_is_written_back)
{
    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::TRANSLATION, "animated");
    ASSERT_NE(sampler, nullptr);
    ASSERT_EQ(sampler->data.size(), 9u);
    sampler->data[4] = 5.0f; // the y of the key at 1 s (time code 24)

    const erhe::usd::Usd_load_result reloaded = reload(save_with_animations("time_samples_edited_value.usda"));
    const erhe::scene::Xform_op*     op       = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::translate);
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->samples.size(), 3u);
    EXPECT_DOUBLE_EQ(op->samples[1].time_code, 24.0);
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[0].value), glm::dvec3(0.0, 0.0, 0.0));
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[1].value), glm::dvec3(10.0, 5.0, 0.0));
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[2].value), glm::dvec3(20.0, 0.0, 0.0));

    const erhe::scene::Xform_op* scale_op = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::scale);
    ASSERT_NE(scale_op, nullptr);
    ASSERT_EQ(scale_op->samples.size(), 3u);
    EXPECT_EQ(std::get<glm::dvec3>(scale_op->samples[1].value), glm::dvec3(2.0, 2.0, 2.0));

    // The layer's time coordinates are the ones the samples span, unchanged.
    EXPECT_DOUBLE_EQ(reloaded.data.time_codes.time_codes_per_second, 24.0);
    EXPECT_DOUBLE_EQ(reloaded.data.time_codes.start_time_code,        0.0);
    EXPECT_DOUBLE_EQ(reloaded.data.time_codes.end_time_code,         48.0);
}

// A key moved in time is written at its new time code: the samples a save
// writes are the keys, one for one.
TEST_F(Time_samples, a_moved_key_time_is_written_back)
{
    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::TRANSLATION, "animated");
    ASSERT_NE(sampler, nullptr);
    ASSERT_EQ(sampler->timestamps.size(), 3u);
    sampler->timestamps[1] = 1.5f; // time code 36

    const erhe::usd::Usd_load_result reloaded = reload(save_with_animations("time_samples_moved_key.usda"));
    const erhe::scene::Xform_op*     op       = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::translate);
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->samples.size(), 3u);
    EXPECT_DOUBLE_EQ(op->samples[0].time_code,  0.0);
    EXPECT_DOUBLE_EQ(op->samples[1].time_code, 36.0);
    EXPECT_DOUBLE_EQ(op->samples[2].time_code, 48.0);
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[1].value), glm::dvec3(10.0, 0.0, 0.0));
}

// A key added to a channel is a sample added to the op.
TEST_F(Time_samples, an_added_key_is_written_back)
{
    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::TRANSLATION, "animated");
    ASSERT_NE(sampler, nullptr);
    sampler->timestamps.insert(sampler->timestamps.begin() + 2, 1.5f);
    sampler->data.insert(sampler->data.begin() + 6, {15.0f, 3.0f, 0.0f});

    const erhe::usd::Usd_load_result reloaded = reload(save_with_animations("time_samples_added_key.usda"));
    const erhe::scene::Xform_op*     op       = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::translate);
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->samples.size(), 4u);
    EXPECT_DOUBLE_EQ(op->samples[2].time_code, 36.0);
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[2].value), glm::dvec3(15.0, 3.0, 0.0));
}

// An edited rotation key reaches the Euler op it was read from.
TEST_F(Time_samples, an_edited_rotation_key_is_written_back_as_euler)
{
    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::ROTATION, "animated");
    ASSERT_NE(sampler, nullptr);
    ASSERT_EQ(sampler->data.size(), 12u);
    // 180 degrees about y as (x, y, z, w).
    sampler->data[4] = 0.0f;
    sampler->data[5] = 1.0f;
    sampler->data[6] = 0.0f;
    sampler->data[7] = 0.0f;

    const erhe::usd::Usd_load_result reloaded = reload(save_with_animations("time_samples_edited_rotation.usda"));
    const erhe::scene::Xform_op*     op       = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::rotate_xyz);
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->samples.size(), 3u);

    // Euler angles name a rotation in more than one way, so the rotation the
    // reload plays is what the edit has to survive as: at 1 s the prim turns
    // a half turn about y, with the authored scale of 2 on its basis.
    ASSERT_EQ(reloaded.data.animations.size(), 1u);
    const std::shared_ptr<erhe::scene::Node> node = find_node(reloaded.data, "animated");
    ASSERT_TRUE(node);
    reloaded.data.animations.front()->apply(1.0f);
    const glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[0][0], -2.0f, 1e-3f);
    EXPECT_NEAR(matrix[1][1],  2.0f, 1e-3f);
    EXPECT_NEAR(matrix[2][2], -2.0f, 1e-3f);
}

// A baked stack - `[orient, translate]` composes rotation * translation, so
// no channel drives one op - writes back through its composed pose: the pose
// the edited channels ask for is solved into the ops, and reloading the file
// puts the prim where the edit put it.
TEST_F(Time_samples, an_edited_baked_stack_writes_back_through_its_pose)
{
    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::TRANSLATION, "orient_then_translate");
    ASSERT_NE(sampler, nullptr);
    ASSERT_EQ(sampler->data.size(), 6u);
    // At 1 s the baked translation is (0, 0, -10); ask for twice as far.
    sampler->data[3] =   0.0f;
    sampler->data[4] =   0.0f;
    sampler->data[5] = -20.0f;

    const erhe::usd::Usd_load_result reloaded = reload(save_with_animations("time_samples_edited_baked.usda"));
    ASSERT_EQ(reloaded.data.animations.size(), 1u);
    const std::shared_ptr<erhe::scene::Node> node = find_node(reloaded.data, "orient_then_translate");
    ASSERT_TRUE(node);

    reloaded.data.animations.front()->apply(1.0f);
    const glm::mat4 matrix = node->parent_from_node_transform().get_matrix();
    EXPECT_NEAR(matrix[3][0],   0.0f, 1e-3f);
    EXPECT_NEAR(matrix[3][1],   0.0f, 1e-3f);
    EXPECT_NEAR(matrix[3][2], -20.0f, 1e-3f);
}

// An edit made while the clip plays is an edit of the clip, not of the pose:
// the animated layer holds the pose, the keys hold the edit, and a save made
// without stopping writes the same samples a save made after stopping does.
TEST_F(Time_samples, an_edit_made_while_the_clip_plays_is_written_back)
{
    ASSERT_EQ(loaded.data.animations.size(), 1u);
    erhe::scene::Animation& animation = *loaded.data.animations.front();
    animation.apply(1.0f);

    erhe::scene::Animation_sampler* sampler = find_sampler(erhe::scene::Animation_path::TRANSLATION, "animated");
    ASSERT_NE(sampler, nullptr);
    sampler->data[5] = 7.0f; // the z of the key at 1 s

    const std::filesystem::path playing_path = save_with_animations("time_samples_edited_playing.usda");
    animation.clear_applied();
    const std::filesystem::path stopped_path = save_with_animations("time_samples_edited_stopped.usda");
    EXPECT_EQ(read_lines(playing_path), read_lines(stopped_path));

    const erhe::usd::Usd_load_result reloaded = reload(playing_path);
    const erhe::scene::Xform_op*     op       = find_op(reloaded.data, "animated", erhe::scene::Xform_op_type::translate);
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->samples.size(), 3u);
    EXPECT_EQ(std::get<glm::dvec3>(op->samples[1].value), glm::dvec3(10.0, 0.0, 7.0));
}

} // anonymous namespace
