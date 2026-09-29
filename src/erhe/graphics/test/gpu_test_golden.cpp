// Golden assertions of Gpu_test: byte-exact buffer goldens and FLIP-compared
// image goldens, with per-test artifacts and a results record for the
// results.json listener (gpu_test_results.cpp).

#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"
#include "gpu_test_flip.hpp"
#include "gpu_test_pfm.hpp"
#include "gpu_test_results.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_math/math_util.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if !defined(ERHE_GRAPHICS_TEST_GOLDEN_DIR)
#   error "ERHE_GRAPHICS_TEST_GOLDEN_DIR must be defined by the build (src/erhe/graphics/test/CMakeLists.txt)"
#endif

namespace erhe::graphics::test {

namespace {

[[nodiscard]] auto get_golden_directory() -> std::filesystem::path
{
    return std::filesystem::path{ERHE_GRAPHICS_TEST_GOLDEN_DIR};
}

// Test and golden names become directory names; keep them portable.
[[nodiscard]] auto sanitize_path_component(const std::string_view text) -> std::string
{
    std::string out{text};
    for (char& c : out) {
        const bool keep =
            ((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')) ||
            ((c >= '0') && (c <= '9')) ||
            (c == '_') || (c == '-') || (c == '.');
        if (!keep) {
            c = '_';
        }
    }
    return out;
}

class Artifact_location
{
public:
    std::filesystem::path directory; // absolute
    std::string           relative;  // relative to the results directory, forward slashes

    [[nodiscard]] auto path_of(const char* file_name) const -> std::filesystem::path
    {
        return directory / file_name;
    }
    [[nodiscard]] auto relative_of(const char* file_name) const -> std::string
    {
        return relative + "/" + file_name;
    }
};

[[nodiscard]] auto make_artifact_location(const std::string_view golden_name) -> Artifact_location
{
    const std::string test_component   = sanitize_path_component(get_current_test_name());
    const std::string golden_component = sanitize_path_component(golden_name);
    Artifact_location location{};
    location.relative  = "artifacts/" + test_component + "/" + golden_component;
    location.directory = get_results_directory() / "artifacts" / test_component / golden_component;
    std::error_code error_code;
    std::filesystem::create_directories(location.directory, error_code);
    return location;
}

[[nodiscard]] auto read_file_bytes(const std::filesystem::path& path, std::vector<std::byte>& out) -> bool
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return false;
    }
    const std::vector<char> chars{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    out.resize(chars.size());
    if (!chars.empty()) {
        std::memcpy(out.data(), chars.data(), chars.size());
    }
    return true;
}

[[nodiscard]] auto write_file_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes) -> bool
{
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

[[nodiscard]] auto copy_file_over(const std::filesystem::path& from, const std::filesystem::path& to) -> bool
{
    std::error_code error_code;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error_code);
    return !error_code;
}

[[nodiscard]] auto write_png_rgba8(const std::filesystem::path& path, const int width, const int height, const std::span<const std::byte> rgba8) -> bool
{
    const std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    return writer->write_png(path, width, height, width * 4, erhe::dataformat::Format::format_8_vec4_unorm, rgba8);
}

[[nodiscard]] auto rgba32f_to_pfm(const int width, const int height, const std::span<const float> rgba) -> Pfm_image
{
    Pfm_image image{};
    image.width  = width;
    image.height = height;
    const std::size_t texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    image.rgb.resize(texel_count * 3u);
    for (std::size_t i = 0; i < texel_count; ++i) {
        image.rgb[(i * 3u) + 0u] = rgba[(i * 4u) + 0u];
        image.rgb[(i * 3u) + 1u] = rgba[(i * 4u) + 1u];
        image.rgb[(i * 3u) + 2u] = rgba[(i * 4u) + 2u];
    }
    return image;
}

[[nodiscard]] auto rgba8_to_rgb_float(const std::span<const std::byte> rgba8, const std::size_t texel_count) -> std::vector<float>
{
    std::vector<float> rgb(texel_count * 3u);
    for (std::size_t i = 0; i < texel_count; ++i) {
        for (std::size_t c = 0; c < 3u; ++c) {
            rgb[(i * 3u) + c] = static_cast<float>(std::to_integer<unsigned int>(rgba8[(i * 4u) + c])) / 255.0f;
        }
    }
    return rgb;
}

// Rows arrive with row 0 at the device's texture origin; goldens are stored
// top-down. Rendering maps NDC +y to the image top on every backend, so a
// bottom-left origin (OpenGL) reads the image bottom first and is flipped.
[[nodiscard]] auto normalize_rows_top_down(
    const erhe::graphics::Device&    graphics_device,
    const std::span<const std::byte> bytes,
    const int                        height,
    const std::size_t                bytes_per_row
) -> std::vector<std::byte>
{
    std::vector<std::byte> out(bytes.begin(), bytes.end());
    const bool row0_is_top =
        (graphics_device.get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);
    if (row0_is_top) {
        return out;
    }
    for (int y = 0; y < height; ++y) {
        const std::size_t source_row = static_cast<std::size_t>(height - 1 - y);
        std::memcpy(out.data() + (static_cast<std::size_t>(y) * bytes_per_row), bytes.data() + (source_row * bytes_per_row), bytes_per_row);
    }
    return out;
}

} // anonymous namespace

void Gpu_test::expect_buffer_matches_golden(const std::string_view name, const std::span<const std::byte> bytes)
{
    const Artifact_location     artifacts   = make_artifact_location(name);
    const std::filesystem::path golden_path = get_golden_directory() / (std::string{name} + ".bin");

    Golden_record record{};
    record.kind            = Golden_kind::buffer;
    record.name            = std::string{name};
    record.golden_file     = golden_path.generic_string();
    record.output_size     = bytes.size();
    record.output_artifact = artifacts.relative_of("output.bin");

    if (!write_file_bytes(artifacts.path_of("output.bin"), bytes)) {
        record.output_artifact.clear();
    }

    if (Gpu_test_environment::get().is_update_goldens_mode()) {
        std::error_code error_code;
        std::filesystem::create_directories(get_golden_directory(), error_code);
        if (!write_file_bytes(golden_path, bytes)) {
            record.outcome = Golden_outcome::error;
            Gpu_test_results::get().add_golden_record(record);
            ADD_FAILURE() << "could not write golden " << golden_path.string();
            return;
        }
        record.outcome         = Golden_outcome::updated;
        record.golden_size     = bytes.size();
        record.golden_artifact = copy_file_over(golden_path, artifacts.path_of("golden.bin")) ? artifacts.relative_of("golden.bin") : std::string{};
        record.note            = "updated golden " + golden_path.generic_string();
        std::cout << "[ golden   ] " << record.note << "\n";
        Gpu_test_results::get().add_golden_record(record);
        return;
    }

    std::vector<std::byte> golden;
    if (!read_file_bytes(golden_path, golden)) {
        record.outcome = Golden_outcome::missing;
        Gpu_test_results::get().add_golden_record(record);
        ADD_FAILURE() << "golden " << golden_path.string() << " is missing; run with ERHE_GPU_TEST_UPDATE_GOLDENS=1 to create it";
        return;
    }
    record.golden_size     = golden.size();
    record.golden_artifact = copy_file_over(golden_path, artifacts.path_of("golden.bin")) ? artifacts.relative_of("golden.bin") : std::string{};

    // Every offset where the bytes differ, including the tail of the longer of
    // the two; only the first c_max_recorded_diff_offsets are listed.
    const std::size_t common_size = std::min(bytes.size(), golden.size());
    const std::size_t total_size  = std::max(bytes.size(), golden.size());
    for (std::size_t offset = 0; offset < total_size; ++offset) {
        const bool differs = (offset >= common_size) || (bytes[offset] != golden[offset]);
        if (!differs) {
            continue;
        }
        ++record.diff_count;
        if (record.diff_offsets.size() < c_max_recorded_diff_offsets) {
            record.diff_offsets.push_back(offset);
        }
    }
    Gpu_test_results::get().add_golden_record(record);

    if ((record.diff_count == 0) && (bytes.size() == golden.size())) {
        return;
    }
    std::ostringstream message;
    message << "buffer golden '" << name << "' mismatch: output " << bytes.size() << " bytes, golden " << golden.size()
            << " bytes, " << record.diff_count << " differing bytes; first offsets:";
    const std::size_t listed = std::min<std::size_t>(record.diff_offsets.size(), 16u);
    for (std::size_t i = 0; i < listed; ++i) {
        message << " " << record.diff_offsets[i];
    }
    if (record.diff_offsets.size() > listed) {
        message << " ...";
    }
    message << " (artifacts: " << artifacts.directory.string() << ")";
    ADD_FAILURE() << message.str();
}

void Gpu_test::expect_image_matches_golden(
    const std::string_view           name,
    const int                        width,
    const int                        height,
    const erhe::dataformat::Format   format,
    const std::span<const std::byte> bytes,
    const float                      threshold
)
{
    {
        const std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
        if (!writer->is_supported()) {
            GTEST_SKIP() << "golden image assertions need a PNG writer, and this build has ERHE_USE_FPNG=OFF";
        }
    }

    const bool is_rgba8   = (format == erhe::dataformat::Format::format_8_vec4_unorm);
    const bool is_rgba32f = (format == erhe::dataformat::Format::format_32_vec4_float);
    if (!is_rgba8 && !is_rgba32f) {
        ADD_FAILURE() << "image golden '" << name << "': unsupported format " << erhe::dataformat::c_str(format)
                      << " (format_8_vec4_unorm or format_32_vec4_float)";
        return;
    }
    const std::size_t bytes_per_texel = is_rgba8 ? 4u : 16u;
    const std::size_t texel_count     = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    if ((width <= 0) || (height <= 0) || (bytes.size() != (texel_count * bytes_per_texel))) {
        ADD_FAILURE() << "image golden '" << name << "': " << bytes.size() << " bytes do not hold a " << width << "x" << height
                      << " " << erhe::dataformat::c_str(format) << " image";
        return;
    }

    const Flip_range            range       = is_rgba8 ? Flip_range::ldr : Flip_range::hdr;
    const char*                 extension   = is_rgba8 ? ".png" : ".pfm";
    const char*                 output_file = is_rgba8 ? "output.png" : "output.pfm";
    const char*                 golden_file = is_rgba8 ? "golden.png" : "golden.pfm";
    const Artifact_location     artifacts   = make_artifact_location(name);
    const std::filesystem::path golden_path = get_golden_directory() / (std::string{name} + extension);

    Golden_record record{};
    record.kind        = Golden_kind::image;
    record.name        = std::string{name};
    record.golden_file = golden_path.generic_string();
    record.format      = is_rgba8 ? "rgba8" : "rgba32f";
    record.width       = width;
    record.height      = height;
    record.threshold   = threshold;

    const std::vector<std::byte> output = normalize_rows_top_down(device(), bytes, height, static_cast<std::size_t>(width) * bytes_per_texel);
    std::vector<float> output_floats;
    if (is_rgba32f) {
        output_floats.resize(texel_count * 4u);
        std::memcpy(output_floats.data(), output.data(), output.size());
    }

    // Writes the output image to path (PNG for RGBA8, PFM RGB for RGBA32F).
    const auto write_output_image = [&](const std::filesystem::path& path) -> bool {
        if (is_rgba8) {
            return write_png_rgba8(path, width, height, output);
        }
        return write_pfm(path, rgba32f_to_pfm(width, height, output_floats));
    };

    if (write_output_image(artifacts.path_of(output_file))) {
        record.output_artifact = artifacts.relative_of(output_file);
    }

    if (Gpu_test_environment::get().is_update_goldens_mode()) {
        std::error_code error_code;
        std::filesystem::create_directories(get_golden_directory(), error_code);
        if (!write_output_image(golden_path)) {
            record.outcome = Golden_outcome::error;
            Gpu_test_results::get().add_golden_record(record);
            ADD_FAILURE() << "could not write golden " << golden_path.string();
            return;
        }
        record.outcome         = Golden_outcome::updated;
        record.golden_width    = width;
        record.golden_height   = height;
        record.golden_artifact = copy_file_over(golden_path, artifacts.path_of(golden_file)) ? artifacts.relative_of(golden_file) : std::string{};
        record.note            = "updated golden " + golden_path.generic_string();
        std::cout << "[ golden   ] " << record.note << "\n";
        Gpu_test_results::get().add_golden_record(record);
        return;
    }

    if (!std::filesystem::exists(golden_path)) {
        record.outcome = Golden_outcome::missing;
        Gpu_test_results::get().add_golden_record(record);
        ADD_FAILURE() << "golden " << golden_path.string() << " is missing; run with ERHE_GPU_TEST_UPDATE_GOLDENS=1 to create it";
        return;
    }
    if (copy_file_over(golden_path, artifacts.path_of(golden_file))) {
        record.golden_artifact = artifacts.relative_of(golden_file);
    }

    // Golden as interleaved RGB floats, top-down.
    std::vector<float> golden_rgb;
    if (is_rgba8) {
        erhe::graphics::Image_info   info{};
        erhe::graphics::Image_loader loader;
        const bool opened = loader.open(golden_path, info, true, erhe::graphics::Transcode_format_preference::rgba8, erhe::graphics::Alpha_mode::straight);
        std::vector<std::uint8_t> golden_rgba8;
        bool loaded = false;
        if (opened && (info.format == erhe::dataformat::Format::format_8_vec4_unorm) && (info.row_stride == (info.width * 4))) {
            golden_rgba8.resize(static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height));
            loaded = loader.load(std::span<std::uint8_t>{golden_rgba8});
        }
        loader.close();
        if (!loaded) {
            record.outcome = Golden_outcome::error;
            Gpu_test_results::get().add_golden_record(record);
            ADD_FAILURE() << "could not read golden " << golden_path.string() << " as an RGBA8 PNG";
            return;
        }
        record.golden_width  = info.width;
        record.golden_height = info.height;
        if ((info.width == width) && (info.height == height)) {
            golden_rgb = rgba8_to_rgb_float(std::as_bytes(std::span<const std::uint8_t>{golden_rgba8}), texel_count);
        }
    } else {
        Pfm_image golden_image{};
        if (!read_pfm(golden_path, golden_image)) {
            record.outcome = Golden_outcome::error;
            Gpu_test_results::get().add_golden_record(record);
            ADD_FAILURE() << "could not read golden " << golden_path.string() << " as a PFM";
            return;
        }
        record.golden_width  = golden_image.width;
        record.golden_height = golden_image.height;
        if ((golden_image.width == width) && (golden_image.height == height)) {
            golden_rgb = std::move(golden_image.rgb);
        }
    }

    if ((record.golden_width != width) || (record.golden_height != height)) {
        Gpu_test_results::get().add_golden_record(record);
        ADD_FAILURE() << "image golden '" << name << "' size mismatch: output " << width << "x" << height
                      << ", golden " << record.golden_width << "x" << record.golden_height
                      << " (artifacts: " << artifacts.directory.string() << ")";
        return;
    }

    std::vector<float> output_rgb;
    if (is_rgba8) {
        output_rgb = rgba8_to_rgb_float(output, texel_count);
    } else {
        output_rgb = rgba32f_to_pfm(width, height, output_floats).rgb;
    }

    const Flip_result flip = evaluate_flip(golden_rgb, output_rgb, width, height, range);
    record.flip_mean = flip.mean_error;
    record.flip_max  = flip.max_error;
    if (write_png_rgba8(artifacts.path_of("flip.png"), width, height, std::as_bytes(std::span<const std::uint8_t>{flip.error_map_rgba8}))) {
        record.flip_artifact = artifacts.relative_of("flip.png");
    }
    Gpu_test_results::get().add_golden_record(record);

    EXPECT_LE(flip.mean_error, threshold)
        << "image golden '" << name << "': FLIP mean error " << flip.mean_error << " (max " << flip.max_error
        << ") exceeds threshold " << threshold << " (artifacts: " << artifacts.directory.string() << ")";
}

} // namespace erhe::graphics::test
