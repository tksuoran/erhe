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
    static constexpr std::string_view c_type_name{"Textures"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Textures ();
    ~Textures() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Public API
    [[nodiscard]] auto load(const fs::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;

private:
    std::shared_ptr<Image_transfer> m_image_transfer;
};

} // namespace editor
