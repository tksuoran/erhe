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
    Textures();
    ~Textures() override;

    // Implements Component
    void connect() override;
    void initialize_component() override;

    auto load(const std::filesystem::path& path)
        -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;
    std::shared_ptr<erhe::graphics::Texture> camera_icon;
    std::shared_ptr<erhe::graphics::Texture> light_icon;
    std::shared_ptr<erhe::graphics::Texture> mesh_icon;

private:
    std::shared_ptr<Image_transfer> m_image_transfer;
};

}
