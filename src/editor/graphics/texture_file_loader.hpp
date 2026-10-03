#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Command_buffer;
    class Texture;
}
namespace erhe::gltf {
    class Image_transfer;
}

namespace editor {

class App_context;

// True for the image file extensions erhe::graphics::Image_loader can decode:
// PNG / JPEG (wuffs), KTX2 (Basis transcode) and DDS. Case-insensitive.
[[nodiscard]] auto is_texture_file_extension(const std::filesystem::path& path) -> bool;

// Loads image files (PNG / JPEG / KTX2 / DDS) into GPU textures without
// blocking the frame: the decode runs on the executor and the texture is
// created and its upload recorded from tick(), against the frame's command
// buffer. Two clients:
//
//  - get_preview(): a bounded cache of textures for asset-browser and
//    content-library hover previews. Entries are shared by every hover of the
//    same file and evicted least-recently-used.
//  - load_async(): a FRESH texture per call, for importing a file into a
//    scene's content library. Never cached: a content-library entry claims
//    its item's Item_host, so two libraries must not list the same object.
class Texture_file_loader
{
public:
    explicit Texture_file_loader(App_context& context);
    ~Texture_file_loader() noexcept;
    Texture_file_loader           (const Texture_file_loader&) = delete;
    Texture_file_loader& operator=(const Texture_file_loader&) = delete;

    class Preview
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> texture{}; // null while pending or on failure
        bool                                     pending{false};
        std::string                              error{};   // non-empty when the load failed
    };

    // Cached preview for one file. The first call starts the load and returns
    // pending; a later call picks the result up. Marks the entry used, for LRU.
    [[nodiscard]] auto get_preview(const std::filesystem::path& path) -> Preview;

    // Load one file into a fresh texture. The callback runs on the main thread
    // from tick(), with a null texture when the load failed.
    void load_async(
        const std::filesystem::path&                                     path,
        std::function<void(const std::shared_ptr<erhe::graphics::Texture>&)> callback
    );

    // Main thread, once per frame, with the frame's command buffer: creates
    // textures for finished decodes and records their uploads. Bounded per
    // frame by the same kind of byte budget the asset loader uses.
    void tick(erhe::graphics::Command_buffer& command_buffer);

private:
    class Request;

    [[nodiscard]] auto start_request(const std::filesystem::path& path) -> std::shared_ptr<Request>;
    void publish(Request& request, erhe::graphics::Command_buffer& command_buffer, std::size_t& remaining_budget_bytes);
    void evict_if_needed();

    class Cache_entry
    {
        public:
        std::string                              key;
        std::shared_ptr<Request>                 request;  // non-null while loading
        std::shared_ptr<erhe::graphics::Texture> texture;
        std::string                              error;
        uint64_t                                 last_use{0};
    };

    App_context&                                m_context;
    std::unique_ptr<erhe::gltf::Image_transfer> m_image_transfer;
    std::vector<Cache_entry>                    m_cache;
    std::vector<std::shared_ptr<Request>>       m_pending; // both preview and load_async requests
    uint64_t                                    m_use_counter{0};
};

// Draws a texture preview - the image scaled to fit max_size while keeping its
// aspect ratio, above a "<width> x <height> <format>" line. Shared by the asset
// browser file tooltip and the content library texture tooltip so both look the
// same.
void draw_texture_preview(App_context& context, const std::shared_ptr<erhe::graphics::Texture>& texture, float max_size);

} // namespace editor
