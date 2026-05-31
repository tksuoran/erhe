#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace erhe::graphics::test {

namespace {

// True when n is a positive power of two.
[[nodiscard]] auto is_power_of_two(uint64_t n) -> bool
{
    return (n != 0u) && ((n & (n - 1u)) == 0u);
}

} // namespace

// Depth/stencil format queries are self-consistent. Every format the device
// reports as a supported depth/stencil format must actually carry depth or
// stencil bits (per erhe::dataformat) and report itself depth- or
// stencil-renderable (per get_format_properties); and choose_depth_stencil_format
// with a depth requirement must return a depth-renderable format with depth
// bits. The require_depth|require_stencil choice, when any stencil-capable
// format is supported, must return one carrying both depth and stencil bits.
TEST_F(Gpu_test, device_caps_depth_stencil_formats_consistent)
{
    erhe::graphics::Device& graphics_device = device();

    const std::vector<erhe::dataformat::Format> supported =
        graphics_device.get_supported_depth_stencil_formats();
    ASSERT_FALSE(supported.empty()) << "device reports no supported depth/stencil formats";

    bool any_stencil_capable = false;
    for (const erhe::dataformat::Format format : supported) {
        const std::size_t depth_bits   = erhe::dataformat::get_depth_size_bits(format);
        const std::size_t stencil_bits = erhe::dataformat::get_stencil_size_bits(format);
        // The list is depth/stencil formats: each must carry at least one of the
        // two aspects, and dataformat must classify it as a depth/stencil kind.
        EXPECT_GT(depth_bits + stencil_bits, 0u)
            << "supported format " << erhe::dataformat::c_str(format) << " has neither depth nor stencil bits";
        EXPECT_EQ(erhe::dataformat::get_format_kind(format), erhe::dataformat::Format_kind::format_kind_depth_stencil)
            << erhe::dataformat::c_str(format) << " is not classified as depth/stencil";
        EXPECT_FALSE(erhe::dataformat::has_color(format))
            << erhe::dataformat::c_str(format) << " unexpectedly reports a color aspect";

        // get_supported_depth_stencil_formats only admits formats that are
        // depth- or stencil-renderable, so get_format_properties must agree.
        const erhe::graphics::Format_properties props = graphics_device.get_format_properties(format);
        EXPECT_TRUE(props.supported) << erhe::dataformat::c_str(format) << " not marked supported by get_format_properties";
        EXPECT_TRUE(props.depth_renderable || props.stencil_renderable)
            << erhe::dataformat::c_str(format) << " is neither depth- nor stencil-renderable";
        // The renderable flags must line up with the actual aspect bits.
        if (props.depth_renderable) {
            EXPECT_GT(depth_bits, 0u) << erhe::dataformat::c_str(format) << " depth_renderable but no depth bits";
        }
        if (props.stencil_renderable) {
            EXPECT_GT(stencil_bits, 0u) << erhe::dataformat::c_str(format) << " stencil_renderable but no stencil bits";
        }
        if (stencil_bits > 0u) {
            any_stencil_capable = true;
        }
    }

    // require_depth: the chosen format must have depth bits and be depth-renderable.
    const erhe::dataformat::Format depth_choice =
        graphics_device.choose_depth_stencil_format(erhe::graphics::format_flag_require_depth, 1);
    ASSERT_NE(depth_choice, erhe::dataformat::Format::format_undefined);
    EXPECT_GT(erhe::dataformat::get_depth_size_bits(depth_choice), 0u)
        << "require_depth chose " << erhe::dataformat::c_str(depth_choice) << " which has no depth bits";
    EXPECT_TRUE(graphics_device.get_format_properties(depth_choice).depth_renderable)
        << "require_depth chose a non-depth-renderable format";

    // require_depth|require_stencil: when the device has any stencil-capable
    // depth/stencil format, the choice must carry both aspects (the selector
    // prefers combined depth+stencil formats).
    const erhe::dataformat::Format ds_choice = graphics_device.choose_depth_stencil_format(
        erhe::graphics::format_flag_require_depth | erhe::graphics::format_flag_require_stencil, 1
    );
    ASSERT_NE(ds_choice, erhe::dataformat::Format::format_undefined);
    EXPECT_GT(erhe::dataformat::get_depth_size_bits(ds_choice), 0u)
        << "depth+stencil choice " << erhe::dataformat::c_str(ds_choice) << " has no depth bits";
    if (any_stencil_capable) {
        EXPECT_GT(erhe::dataformat::get_stencil_size_bits(ds_choice), 0u)
            << "depth+stencil choice " << erhe::dataformat::c_str(ds_choice)
            << " has no stencil bits even though a stencil-capable format is supported";
        const erhe::graphics::Format_properties ds_props = graphics_device.get_format_properties(ds_choice);
        EXPECT_TRUE(ds_props.depth_renderable)   << "depth+stencil choice not depth-renderable";
        EXPECT_TRUE(ds_props.stencil_renderable) << "depth+stencil choice not stencil-renderable";
    }
}

// get_format_properties and probe_image_format_support agree with each other
// and with the formats this suite is KNOWN to render here. Color formats the
// suite renders to (8_vec4_unorm, 32_vec4_float) must be color-renderable and
// probe-supported as color attachments; the depth format the depth test uses
// (d32_sfloat) must be depth-renderable with 32 depth bits. An undefined format
// must probe as unsupported.
TEST_F(Gpu_test, device_caps_format_properties_agree)
{
    erhe::graphics::Device& graphics_device = device();

    const uint64_t color_usage =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled;

    // Color formats the suite already renders to (test_m3_triangle, test_mrt,
    // test_float32_render). color_renderable and a color-attachment probe must
    // both be true, and dataformat must agree they carry color.
    const erhe::dataformat::Format color_formats[] = {
        erhe::dataformat::Format::format_8_vec4_unorm,
        erhe::dataformat::Format::format_32_vec4_float
    };
    for (const erhe::dataformat::Format format : color_formats) {
        const erhe::graphics::Format_properties props = graphics_device.get_format_properties(format);
        EXPECT_TRUE(props.supported)        << erhe::dataformat::c_str(format) << " reported unsupported";
        EXPECT_TRUE(props.color_renderable) << erhe::dataformat::c_str(format) << " not color-renderable but the suite renders to it";
        EXPECT_TRUE(graphics_device.probe_image_format_support(format, color_usage))
            << erhe::dataformat::c_str(format) << " color-renderable but probe rejected color_attachment|sampled";
        EXPECT_TRUE(erhe::dataformat::has_color(format)) << erhe::dataformat::c_str(format) << " should carry color";
        EXPECT_EQ(erhe::dataformat::get_depth_size_bits(format),   0u);
        EXPECT_EQ(erhe::dataformat::get_stencil_size_bits(format), 0u);
    }

    // The depth format used by test_depth_readback.
    constexpr erhe::dataformat::Format depth_format = erhe::dataformat::Format::format_d32_sfloat;
    const erhe::graphics::Format_properties depth_props = graphics_device.get_format_properties(depth_format);
    EXPECT_TRUE(depth_props.supported)        << "format_d32_sfloat reported unsupported";
    EXPECT_TRUE(depth_props.depth_renderable) << "format_d32_sfloat not depth-renderable but the suite renders depth to it";
    EXPECT_EQ(erhe::dataformat::get_depth_size_bits(depth_format),   32u);
    EXPECT_EQ(erhe::dataformat::get_stencil_size_bits(depth_format),  0u);
    EXPECT_FALSE(erhe::dataformat::has_color(depth_format)) << "depth format must not carry color";

    // A depth probe with a depth-stencil-attachment usage must also succeed.
    EXPECT_TRUE(graphics_device.probe_image_format_support(
        depth_format,
        erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled
    )) << "format_d32_sfloat depth-renderable but probe rejected depth_stencil_attachment|sampled";

    // The undefined format must never probe as supported.
    EXPECT_FALSE(graphics_device.probe_image_format_support(
        erhe::dataformat::Format::format_undefined, color_usage
    )) << "format_undefined must not probe as a usable image format";
}

// Device_info booleans/limits are internally consistent with the features the
// suite already exercises, and sample-count / alignment fields are sane.
TEST_F(Gpu_test, device_caps_device_info_self_consistent)
{
    const erhe::graphics::Device_info& info = device().get_info();

    EXPECT_FALSE(info.api_info.empty()) << "device must report a backend API string";

    // If compute is advertised, the compute limit fields must at least hold
    // their sane floor (>= 1) in every dimension. NOTE: the Vulkan backend does
    // not currently populate these max_compute_* fields from
    // VkPhysicalDeviceLimits (unlike the GL/Metal/null backends), so they keep
    // their default of 1; the actual local_size_x = 64 the compute tests use is
    // validated by those tests dispatching successfully, not by this field. Do
    // not tighten this to the real hardware limit until the Vulkan backend fills
    // these fields.
    if (info.use_compute_shader) {
        EXPECT_GE(info.max_compute_work_group_invocations, 1);
        for (int dim = 0; dim < 3; ++dim) {
            EXPECT_GE(info.max_compute_workgroup_size[dim], 1)  << "compute on but workgroup size dim " << dim << " < 1";
            EXPECT_GE(info.max_compute_workgroup_count[dim], 1) << "compute on but workgroup count dim " << dim << " < 1";
        }
    }

    // SSBOs are used by every compute test; if advertised there must be at
    // least one storage-buffer binding available.
    if (info.use_shader_storage_buffers) {
        EXPECT_GE(info.max_shader_storage_buffer_bindings, 1)
            << "storage buffers advertised but no SSBO bindings";
    }

    // Buffer offset alignments are device limits expressed in bytes; Vulkan
    // guarantees they are powers of two and non-zero.
    EXPECT_TRUE(is_power_of_two(info.uniform_buffer_offset_alignment))
        << "uniform_buffer_offset_alignment " << info.uniform_buffer_offset_alignment << " is not a power of two";
    EXPECT_TRUE(is_power_of_two(info.shader_storage_buffer_offset_alignment))
        << "shader_storage_buffer_offset_alignment " << info.shader_storage_buffer_offset_alignment << " is not a power of two";

    EXPECT_GE(info.max_texture_size, 1);

    // Sample counts reported per color format must form a sane ascending set of
    // powers of two that always includes 1x (single-sample is universal). Use a
    // format the suite renders to.
    const std::vector<int>& counts =
        device().get_format_properties(erhe::dataformat::Format::format_8_vec4_unorm).texture_2d_sample_counts;
    ASSERT_FALSE(counts.empty()) << "color-renderable format reports no sample counts";
    EXPECT_NE(std::find(counts.begin(), counts.end(), 1), counts.end())
        << "sample-count set does not include 1x";
    for (std::size_t i = 0; i < counts.size(); ++i) {
        EXPECT_TRUE(is_power_of_two(static_cast<uint64_t>(counts[i])))
            << "sample count " << counts[i] << " is not a power of two";
        if (i > 0) {
            EXPECT_GT(counts[i], counts[i - 1]) << "sample counts not strictly ascending (no duplicates)";
        }
    }
}

} // namespace erhe::graphics::test
