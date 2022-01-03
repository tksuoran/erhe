#include "parsers/gltf.hpp"
#include "log.hpp"

#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#include <GLTFSDK/GLTF.h>
#include <GLTFSDK/GLTFResourceReader.h>
#include <GLTFSDK/GLBResourceReader.h>
#include <GLTFSDK/Deserialize.h>
#include <GLTFSDK/MeshPrimitiveUtils.h>

#include <algorithm>
#include <cctype>
#include <glm/glm.hpp>
#include <fstream>
#include <limits>
#include <string>

namespace editor {

//auto c_str(Microsoft::glTF::) -> const char*
//{
//}
//

namespace {

using namespace Microsoft::glTF;

auto c_str(BufferViewTarget value) -> const char*
{
    switch (value)
    {
        case ARRAY_BUFFER:         return "array_buffer";
        case ELEMENT_ARRAY_BUFFER: return "element_array_buffer";
        default:                   return "?";
    }
};

auto c_str(ComponentType value) -> const char*
{
    switch (value)
    {
        case COMPONENT_UNKNOWN       : return "unknown";
        case COMPONENT_BYTE          : return "byte";
        case COMPONENT_UNSIGNED_BYTE : return "unsigned_byte";
        case COMPONENT_SHORT         : return "short";
        case COMPONENT_UNSIGNED_SHORT: return "unsigned_short"; 
        case COMPONENT_UNSIGNED_INT  : return "unsigned_int";
        case COMPONENT_FLOAT         : return "float";
        default:                       return "?";
    }
};

auto c_str(AccessorType value) -> const char*
{
    switch (value)
    {
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_SCALAR:  return "scalar";
        case TYPE_VEC2:    return "vec2";
        case TYPE_VEC3:    return "vec3";
        case TYPE_VEC4:    return "vec4";
        case TYPE_MAT2:    return "mat2";
        case TYPE_MAT3:    return "mat3";
        case TYPE_MAT4:    return "mat4";
        default:           return "?";
    }
};

auto c_str(MeshMode value) -> const char*
{
    switch (value)
    {
        case MESH_POINTS:         return "points";
        case MESH_LINES:          return "lines";
        case MESH_LINE_LOOP:      return "line_loop";
        case MESH_LINE_STRIP:     return "line_strip";
        case MESH_TRIANGLES:      return "triangles";
        case MESH_TRIANGLE_STRIP: return "triangle_strip";
        case MESH_TRIANGLE_FAN:   return "triangle_fan";
        default:                  return "?";
    }
};

auto c_str(AlphaMode value) -> const char*
{
    switch (value)
    {
        case ALPHA_UNKNOWN: return "unknown";
        case ALPHA_OPAQUE:  return "opaque";
        case ALPHA_BLEND:   return "blend";
        case ALPHA_MASK:    return "mask";
        default:            return "?";
    }
};

auto c_str(TargetPath value) -> const char*
{
    switch (value)
    {
        case TARGET_UNKNOWN:     return "unknown";
        case TARGET_TRANSLATION: return "translation";
        case TARGET_ROTATION:    return "rotation";
        case TARGET_SCALE:       return "scale";
        case TARGET_WEIGHTS:     return "weights";
        default:                 return "?";
    }
};

auto c_str(InterpolationType value) -> const char*
{
    switch (value)
    {
        case INTERPOLATION_UNKNOWN:     return "unknown";
        case INTERPOLATION_LINEAR:      return "linear";
        case INTERPOLATION_STEP:        return "step";
        case INTERPOLATION_CUBICSPLINE: return "cubicspline";
        default:                        return "?";
    }
};

auto c_str(TransformationType value) -> const char*
{
    switch (value)
    {
        case TRANSFORMATION_IDENTITY: return "identity";
        case TRANSFORMATION_MATRIX:   return "matrix";
        case TRANSFORMATION_TRS:      return "trs";
        default:                      return "?";
    }
};

auto c_str(ProjectionType value) -> const char*
{
    switch (value)
    {
        case PROJECTION_PERSPECTIVE:  return "perspective";
        case PROJECTION_ORTHOGRAPHIC: return "orthographic";
        default:                      return "?";
    }
};

//constexpr const char* ACCESSOR_POSITION   = "POSITION";
//constexpr const char* ACCESSOR_NORMAL     = "NORMAL";
//constexpr const char* ACCESSOR_TANGENT    = "TANGENT";
//constexpr const char* ACCESSOR_TEXCOORD_0 = "TEXCOORD_0";
//constexpr const char* ACCESSOR_TEXCOORD_1 = "TEXCOORD_1";
//constexpr const char* ACCESSOR_COLOR_0    = "COLOR_0";
//constexpr const char* ACCESSOR_JOINTS_0   = "JOINTS_0";
//constexpr const char* ACCESSOR_WEIGHTS_0  = "WEIGHTS_0";
}

class Stream_reader 
    : public Microsoft::glTF::IStreamReader
{
public:
    explicit Stream_reader(const std::filesystem::path& path_base) 
        : m_path_base{path_base}
    {
        Expects(m_path_base.has_root_path());
    }

    auto GetInputStream(const std::string& filename) const -> std::shared_ptr<std::istream> override
    {
        //using namespace Microsoft::glTF;

        auto stream_path = m_path_base / std::filesystem::path(filename);
        auto stream = std::make_shared<std::ifstream>(stream_path, std::ios_base::binary);

        // Check if the stream has no errors and is ready for I/O operations
        if (!stream || !(*stream))
        {
            log_parsers.error(
                "Unable to create a valid input stream for uri: {}\n"
                "stream_path: {}\n"
                "Base path: {}\n",
                filename,
                stream_path.string(),
                m_path_base.string()
            );
            return {};
        }

        return stream;
    }

private:
    std::filesystem::path m_path_base;
};

using namespace glm;
using namespace erhe::geometry;


auto parse_gltf(
    const std::filesystem::path& relative_path
) -> std::vector<std::shared_ptr<erhe::geometry::Geometry>>
{
    ERHE_PROFILE_FUNCTION

    std::filesystem::path canonical_path = std::filesystem::canonical(relative_path);
    std::filesystem::path path = std::filesystem::current_path() / canonical_path;
    log_parsers.trace("path = {}\n", path.generic_string());
    // Pass the absolute path, without the filename, to the stream reader
    auto stream_reader = std::make_unique<Stream_reader>(path.parent_path());

    const std::filesystem::path path_file     = path.filename();
    const std::filesystem::path path_file_ext = path_file.extension();

    std::string manifest;

    auto make_path_ext = [](const std::string& ext)
    {
        return "." + ext;
    };

    std::unique_ptr<Microsoft::glTF::GLTFResourceReader> resource_reader;

    // If the file has a '.gltf' extension then create a GLTFResourceReader
    if (path_file_ext == make_path_ext(Microsoft::glTF::GLTF_EXTENSION))
    {
        auto gltf_stream = stream_reader->GetInputStream(path_file.string()); // Pass a UTF-8 encoded filename to GetInputString
        auto gltf_resource_reader = std::make_unique<Microsoft::glTF::GLTFResourceReader>(std::move(stream_reader));

        std::stringstream manifest_stream;

        // Read the contents of the glTF file into a string using a std::stringstream
        manifest_stream << gltf_stream->rdbuf();
        manifest = manifest_stream.str();

        resource_reader = std::move(gltf_resource_reader);
    }
    else

    // If the file has a '.glb' extension then create a GLBResourceReader. This class derives
    // from GLTFResourceReader and adds support for reading manifests from a GLB container's
    // JSON chunk and resource data from the binary chunk.
    if (path_file_ext == make_path_ext(Microsoft::glTF::GLB_EXTENSION))
    {
        auto glb_stream = stream_reader->GetInputStream(path_file.string());
        auto glb_resource_reader = std::make_unique<Microsoft::glTF::GLBResourceReader>(
            std::move(stream_reader),
            std::move(glb_stream)
        );

        manifest = glb_resource_reader->GetJson(); // Get the manifest from the JSON chunk

        resource_reader = std::move(glb_resource_reader);
    }

    if (!resource_reader)
    {
        log_parsers.error("Command line argument path filename extension must be .gltf or .glb");
        return {};
    }

    Microsoft::glTF::Document document;

    try
    {
        document = Microsoft::glTF::Deserialize(manifest);
    }
    catch (const Microsoft::glTF::GLTFException& ex)
    {
        std::stringstream ss;

        ss << "Microsoft::glTF::Deserialize failed: ";
        ss << ex.what();

        throw std::runtime_error(ss.str());
    }

    log_parsers.info("Asset Version:    {}\n", document.asset.version);
    log_parsers.info("Asset MinVersion: {}\n", document.asset.minVersion);
    log_parsers.info("Asset Generator:  {}\n", document.asset.generator);
    log_parsers.info("Asset Copyright:  {}\n", document.asset.copyright);

    // Scene Info
    log_parsers.info("Scene Count: {}\n", document.scenes.Size());

    if (document.scenes.Size() > 0U)
    {
        log_parsers.info("Default Scene Index: {}\n", document.GetDefaultScene().id);
    }

    // Entity Info
    log_parsers.info("Node Count:       {}\n", document.nodes.Size());
    log_parsers.info("Camera Count:     {}\n", document.cameras.Size());
    log_parsers.info("Material Count:   {}\n", document.materials.Size());
    log_parsers.info("Mesh Count:       {}\n", document.meshes.Size());
    log_parsers.info("Skin Count:       {}\n", document.skins.Size());
    log_parsers.info("Image Count:      {}\n", document.images.Size());
    log_parsers.info("Texture Count:    {}\n", document.textures.Size());
    log_parsers.info("Sampler Count:    {}\n", document.samplers.Size());
    log_parsers.info("Buffer Count:     {}\n", document.buffers.Size());
    log_parsers.info("BufferView Count: {}\n", document.bufferViews.Size());
    log_parsers.info("Accessor Count:   {}\n", document.accessors.Size());
    log_parsers.info("Animation Count:  {}\n", document.animations.Size());

    for (const auto& extension : document.extensionsUsed)
    {
        log_parsers.info("Extension Used: {}\n", extension);
    }

    for (const auto& extension : document.extensionsRequired)
    {
        log_parsers.info("Extension Required: {}\n", extension);
    }

    for (const auto& mesh : document.meshes.Elements())
    {
        log_parsers.info("Mesh: {}\n", mesh.id);

        for (const auto& primitive : mesh.primitives)
        {
            std::string accessor_id;

            for (const auto& attribute : primitive.attributes)
            {
                const auto accessor         = document.accessors.Get(attribute.second);
                const auto buffer_view      = document.bufferViews.Get(accessor.bufferViewId);
                const auto data             = resource_reader->ReadBinaryData<float>(document, accessor);
                const auto data_byte_length = data.size() * sizeof(float);

                log_parsers.info(
                    "Primitive attribute: {}\n",
                    attribute.first
                    //attribute.second
                );

                log_parsers.info(
                    "    Accessor: buffer view = {}, offset = {}, component type = {}, normalized = {}, count = {}, type = {}\n",
                    accessor.bufferViewId,
                    accessor.byteOffset,
                    c_str(accessor.componentType),
                    accessor.normalized,
                    accessor.count,
                    c_str(accessor.type)
                );

                log_parsers.info(
                    "    Buffer view: buffer id = {}, offset = {}, length = {}, stride = {}, target = {}, byte count = {}\n",
                    buffer_view.bufferId,
                    buffer_view.byteOffset,
                    buffer_view.byteLength,
                    (buffer_view.byteStride.HasValue() ? buffer_view.byteStride.Get() : 0),
                    (buffer_view.target.HasValue() ? c_str(buffer_view.target.Get()) : "(empty)"),
                    data_byte_length
                );
            }

            log_parsers.info(
                "Primitive material ID: {}\n",
                primitive.materialId
            );

            {
                const Microsoft::glTF::Accessor& accessor = document.accessors.Get(primitive.indicesAccessorId);
                //const auto data             = resource_reader->ReadBinaryData<float>(document, accessor);
                //const auto data_byte_length = data.size() * sizeof(float);
                log_parsers.info(
                    "Indices: offset = {}, component type = {}, normalized = {}, count = {}, type = {}\n",
                   // data_byte_length,
                    accessor.byteOffset,
                    c_str(accessor.componentType),
                    accessor.normalized,
                    accessor.count,
                    c_str(accessor.type)
                );
            }

            if (
                primitive.TryGetAttributeAccessorId(
                    Microsoft::glTF::ACCESSOR_POSITION,
                    accessor_id
                )
            )
            {
                const Microsoft::glTF::Accessor& accessor = document.accessors.Get(accessor_id);
                                                
                const auto data             = resource_reader->ReadBinaryData<float>(document, accessor);
                const auto data_byte_length = data.size() * sizeof(float);

                log_parsers.info(
                    "Primitive: {} bytes of position data\n",
                    data_byte_length
                );
            }
        }
    }

    return {};
}

} // namespace editor
