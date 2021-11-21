#pragma once

#include "erhe/components/component.hpp"

#include <filesystem>

namespace erhe::graphics {
    class Texture;
}

namespace editor {

class Image_transfer;

class Textures
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Textures"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Textures();
    ~Textures() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    auto load(const std::filesystem::path& path)
        -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;

private:
    Image_transfer* m_image_transfer{nullptr};
};

}
