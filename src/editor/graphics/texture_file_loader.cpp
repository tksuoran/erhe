#include "graphics/texture_file_loader.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_task/task.hpp"
#include "erhe_profile/profile.hpp"

#include <taskflow/taskflow.hpp>

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>

namespace editor {

namespace {

// GPU upload allowance per frame for previews and texture imports. Small:
// this path serves hover previews and the occasional single-file import, and
// it shares the device staging ring with the asset loader.
constexpr std::size_t s_upload_budget_bytes_per_frame = 16 * 1024 * 1024;

// Preview cache bounds. Previews are full-resolution decodes of the source
// file (they are also what the material slots would sample), so the byte cap
// matters more than the entry count.
constexpr std::size_t s_max_cache_entries = 64;
constexpr std::size_t s_max_cache_bytes   = 256 * 1024 * 1024;

[[nodiscard]] auto to_lower(std::string text) -> std::string
{
    std::transform(
        text.begin(), text.end(), text.begin(),
        [](const unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return text;
}

[[nodiscard]] auto estimate_texture_byte_count(const erhe::graphics::Texture& texture) -> std::size_t
{
    return erhe::dataformat::get_mip_chain_byte_count(
        texture.get_pixelformat(),
        static_cast<std::size_t>(texture.get_width()),
        static_cast<std::size_t>(texture.get_height()),
        static_cast<std::size_t>(texture.get_level_count())
    );
}

} // anonymous namespace

auto is_texture_file_extension(const std::filesystem::path& path) -> bool
{
    const std::string extension = to_lower(erhe::file::to_string(path.extension()));
    return
        (extension == ".png" ) ||
        (extension == ".jpg" ) ||
        (extension == ".jpeg") ||
        (extension == ".ktx2") ||
        (extension == ".dds" );
}

// One decode in flight. The worker fills info / pixels / ok / error and sets
// finished; everything else - the Texture, the upload, the callback - happens
// on the main thread in tick().
class Texture_file_loader::Request
{
public:
    std::filesystem::path                                               path;
    std::string                                                         name;
    std::atomic<bool>                                                   finished{false};
    bool                                                                ok{false};
    std::string                                                         error;
    erhe::graphics::Image_info                                          info{};
    std::vector<std::uint8_t>                                           pixels;
    // Filled in by whoever started the request: writes the result into the
    // cache entry, or hands it to a load_async() caller. Runs on the main
    // thread from tick().
    std::function<void(const std::shared_ptr<erhe::graphics::Texture>&, const std::string&)> on_ready;
};

Texture_file_loader::Texture_file_loader(App_context& context)
    : m_context{context}
{
}

Texture_file_loader::~Texture_file_loader() noexcept = default;

auto Texture_file_loader::start_request(const std::filesystem::path& path) -> std::shared_ptr<Request>
{
    auto request = std::make_shared<Request>();
    request->path = path;
    request->name = erhe::file::to_string(path.filename());

    // Transcode preference is a device property (KTX2 / Basis sources), so it
    // must be sampled here on the main thread, not on the worker.
    erhe::graphics::Transcode_format_preference transcode_format_preference =
        erhe::graphics::Transcode_format_preference::rgba8;
    if (m_context.graphics_device != nullptr) {
        transcode_format_preference = erhe::gltf::query_gltf_device_options(*m_context.graphics_device).transcode_format_preference;
    }

    const auto decode = [request, transcode_format_preference]() {
        ERHE_PROFILE_SCOPE("texture file decode");
        erhe::graphics::Image_loader loader;
        // A standalone image file carries no usage information, so it is
        // decoded as sRGB color data - the common case for the textures a
        // user browses. A normal / ORM map imported this way needs its
        // material slot to treat it as non-color data.
        const bool linear = false;
        if (!loader.open(request->path, request->info, linear, transcode_format_preference)) {
            request->error = "could not decode image";
            request->finished.store(true, std::memory_order_release);
            return;
        }
        const erhe::graphics::Image_info& info = request->info;
        std::size_t byte_count = 0;
        if (erhe::dataformat::is_block_compressed(info.format) || (info.level_count > 1)) {
            // Container formats (KTX2 / DDS) carry a tightly packed mip chain
            byte_count = erhe::dataformat::get_mip_chain_byte_count(
                info.format,
                static_cast<std::size_t>(info.width),
                static_cast<std::size_t>(info.height),
                static_cast<std::size_t>(info.level_count)
            );
        } else {
            byte_count = static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height);
        }
        if (byte_count == 0) {
            request->error = "image is empty";
            loader.close();
            request->finished.store(true, std::memory_order_release);
            return;
        }
        request->pixels.resize(byte_count);
        request->ok = loader.load(std::span<std::uint8_t>{request->pixels.data(), request->pixels.size()});
        loader.close();
        if (!request->ok) {
            request->error = "could not read image pixels";
            request->pixels = std::vector<std::uint8_t>{};
        }
        request->finished.store(true, std::memory_order_release);
    };

    if (m_context.executor != nullptr) {
        erhe::task::spawn(*m_context.executor, decode);
    } else {
        decode();
    }
    m_pending.push_back(request);
    return request;
}

auto Texture_file_loader::get_preview(const std::filesystem::path& path) -> Preview
{
    const std::string key = path.generic_string();
    ++m_use_counter;

    for (Cache_entry& entry : m_cache) {
        if (entry.key != key) {
            continue;
        }
        entry.last_use = m_use_counter;
        return Preview{
            .texture = entry.texture,
            .pending = static_cast<bool>(entry.request),
            .error   = entry.error
        };
    }

    Cache_entry entry;
    entry.key      = key;
    entry.last_use = m_use_counter;
    entry.request  = start_request(path);
    // Look the entry up by key at completion: m_cache reallocates.
    Texture_file_loader* self = this;
    entry.request->on_ready = [self, key](const std::shared_ptr<erhe::graphics::Texture>& texture, const std::string& error) {
        for (Cache_entry& cache_entry : self->m_cache) {
            if (cache_entry.key == key) {
                cache_entry.texture = texture;
                cache_entry.error   = error;
                cache_entry.request.reset();
                break;
            }
        }
        self->evict_if_needed();
    };
    m_cache.push_back(std::move(entry));
    return Preview{.pending = true};
}

void Texture_file_loader::load_async(
    const std::filesystem::path&                                        path,
    std::function<void(const std::shared_ptr<erhe::graphics::Texture>&)> callback
)
{
    std::shared_ptr<Request> request = start_request(path);
    request->on_ready = [callback = std::move(callback)](
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const std::string&                             /*error*/
    ) {
        callback(texture);
    };
}

void Texture_file_loader::publish(
    Request&                        request,
    erhe::graphics::Command_buffer& command_buffer,
    std::size_t&                    remaining_budget_bytes
)
{
    if (!request.ok) {
        log_asset_browser->warn("Texture '{}': {}", erhe::file::to_string(request.path), request.error);
        if (request.on_ready) {
            request.on_ready({}, request.error);
        }
        return;
    }

    const erhe::graphics::Image_info& info = request.info;
    erhe::graphics::Texture_create_info texture_create_info{
        .device      = *m_context.graphics_device,
        .usage_mask  =
            erhe::graphics::Image_usage_flag_bit_mask::sampled |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
        .pixelformat = info.format,
        .use_mipmaps = true,
        .width       = info.width,
        .height      = info.height,
        .depth       = info.depth,
        .level_count = info.level_count,
        .row_stride  = info.row_stride,
        .debug_label = erhe::utility::Debug_label{request.name}
    };
    const int  mipmap_count = texture_create_info.get_texture_level_count();
    // Mipmap generation blits level to level, which cannot write
    // block-compressed levels; a compressed image samples only the levels its
    // container provides.
    const bool generate_mipmap =
        (mipmap_count != info.level_count) &&
        !erhe::dataformat::is_block_compressed(info.format);
    if (generate_mipmap) {
        texture_create_info.usage_mask |= erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        texture_create_info.level_count = mipmap_count;
    }

    auto texture = std::make_shared<erhe::graphics::Texture>(*m_context.graphics_device, texture_create_info);
    texture->set_name(request.name);
    texture->set_source_path(request.path);
    texture->set_two_component_normal(info.two_component_normal);

    const erhe::gltf::Image_upload_result result = m_image_transfer->upload_into_frame(
        command_buffer,
        info,
        std::span<const std::uint8_t>{request.pixels.data(), request.pixels.size()},
        *texture.get(),
        generate_mipmap,
        remaining_budget_bytes
    );
    if (result == erhe::gltf::Image_upload_result::budget_exhausted) {
        // Nothing was recorded, so dropping the texture is safe; the request
        // stays pending and is retried next frame.
        return;
    }

    request.pixels = std::vector<std::uint8_t>{};
    if (request.on_ready) {
        request.on_ready(texture, {});
    }
}

void Texture_file_loader::tick(erhe::graphics::Command_buffer& command_buffer)
{
    if (m_pending.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();

    if (!m_image_transfer) {
        // frame_recording: staging comes from the device ring and the copies
        // go into the frame's command buffer, so this allocates nothing.
        m_image_transfer = std::make_unique<erhe::gltf::Image_transfer>(
            *m_context.graphics_device,
            erhe::gltf::Image_transfer_mode::frame_recording
        );
    }

    std::size_t remaining_budget_bytes = s_upload_budget_bytes_per_frame;
    for (std::size_t i = 0; i < m_pending.size();) {
        const std::shared_ptr<Request>& request = m_pending[i];
        if (!request->finished.load(std::memory_order_acquire)) {
            ++i;
            continue;
        }
        // A finished decode that finds no budget left publishes next frame;
        // publish() leaves it untouched in that case.
        const bool was_pixels_pending = request->ok && !request->pixels.empty();
        publish(*request, command_buffer, remaining_budget_bytes);
        if (was_pixels_pending && !request->pixels.empty()) {
            break; // out of upload budget this frame
        }
        m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void Texture_file_loader::evict_if_needed()
{
    std::size_t byte_count = 0;
    for (const Cache_entry& entry : m_cache) {
        if (entry.texture) {
            byte_count += estimate_texture_byte_count(*entry.texture);
        }
    }
    while ((m_cache.size() > s_max_cache_entries) || (byte_count > s_max_cache_bytes)) {
        // Evict the least recently used RESIDENT entry; a loading entry has
        // nothing to reclaim and dropping it would strand its request.
        auto victim = m_cache.end();
        for (auto i = m_cache.begin(); i != m_cache.end(); ++i) {
            if (!i->texture) {
                continue;
            }
            if ((victim == m_cache.end()) || (i->last_use < victim->last_use)) {
                victim = i;
            }
        }
        if (victim == m_cache.end()) {
            break;
        }
        byte_count -= std::min(byte_count, estimate_texture_byte_count(*victim->texture));
        // The ImGui renderer retains the texture reference for as long as it
        // is in draw data, so releasing it here cannot strand a draw.
        m_cache.erase(victim);
    }
}

void draw_texture_preview(App_context& context, const std::shared_ptr<erhe::graphics::Texture>& texture, const float max_size)
{
    if (!texture || (context.imgui_renderer == nullptr)) {
        return;
    }
    const float width  = static_cast<float>(texture->get_width());
    const float height = static_cast<float>(texture->get_height());
    if ((width <= 0.0f) || (height <= 0.0f)) {
        return;
    }
    const float scale = std::min(1.0f, max_size / std::max(width, height));
    context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = texture,
            .width             = static_cast<int>(width  * scale),
            .height            = static_cast<int>(height * scale),
            // Uploaded image content: identity UVs, not the render-target UVs
            .uv0               = glm::vec2{0.0f, 0.0f},
            .uv1               = glm::vec2{1.0f, 1.0f},
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::linear,
            .debug_label       = "draw_texture_preview()"
        }
    );
    ImGui::Text(
        "%d x %d %s",
        texture->get_width(),
        texture->get_height(),
        erhe::dataformat::c_str(texture->get_pixelformat())
    );
}

} // namespace editor
