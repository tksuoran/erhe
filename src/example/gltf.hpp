#pragma once

#include <filesystem>
#include <vector>

namespace erhe::graphics
{
    class Buffer;
    class Instance;
    class Sampler;
    class Texture;
    class Vertex_format;
};

namespace erhe::primitive
{
    class Gl_buffer_sink;
    class Material;
};

namespace erhe::scene
{
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Scene;
    class Skin;
};

namespace example {

class Image_transfer;

class Parse_context
{
public:
    erhe::graphics::Instance&                               graphics_instance;
    erhe::primitive::Gl_buffer_sink&                        buffer_sink;
    const erhe::graphics::Vertex_format&                    vertex_format;
    erhe::scene::Scene&                                     scene;
    Image_transfer&                                         image_transfer;
    std::filesystem::path                                   path;

    std::vector<std::shared_ptr<erhe::scene::Animation>>    animations;
    std::vector<std::shared_ptr<erhe::scene::Camera>>       cameras;
    std::vector<std::shared_ptr<erhe::scene::Light>>        lights;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>         meshes;
    std::vector<std::shared_ptr<erhe::scene::Skin>>         skins;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<std::shared_ptr<erhe::graphics::Texture>>   images;
    std::vector<std::shared_ptr<erhe::graphics::Sampler>>   samplers;
};

void parse_gltf(Parse_context& context);

}
