#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/filesystem.hpp"

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
    static constexpr uint32_t         hash  {
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Textures();
    ~Textures() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto load(const fs::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;

private:
    std::shared_ptr<Image_transfer> m_image_transfer;
};

}
