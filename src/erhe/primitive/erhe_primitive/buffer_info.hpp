#pragma once

#include "erhe_primitive/enums.hpp"

#include "igl/Buffer.h"

namespace erhe::graphics
{
    class Vertex_format;
}

namespace erhe::primitive {

class Buffer_sink;

auto c_str(igl::ResourceStorage resource_storage) -> const char*;

class Buffer_info
{
public:
    igl::ResourceStorage                 usage        {igl::ResourceStorage::Private};
    Normal_style                         normal_style {Normal_style::corner_normals};
    igl::IndexFormat                     index_type   {igl::IndexFormat::UInt16};
    const erhe::graphics::Vertex_format& vertex_format;
    Buffer_sink&                         buffer_sink;
};


}
