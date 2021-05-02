#ifndef textures_hpp
#define textures_hpp

#include "erhe/components/component.hpp"
#include <filesystem>

namespace erhe::graphics {
    class Texture;
    class ImageTransfer;
} // namespace erhe::graphics

namespace editor {

class Textures
    : public erhe::components::Component
{
public:
    Textures()
        : erhe::components::Component{"Textures"}
    {
    }

    virtual ~Textures() = default;

    void connect(std::shared_ptr<erhe::graphics::ImageTransfer> image_transder);

    void initialize_component() override;

    auto load(const std::filesystem::path& path)
    -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background_texture;

private:
    std::shared_ptr<erhe::graphics::ImageTransfer> m_image_transfer;
};

}

#endif
