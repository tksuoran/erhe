#include "igl/Common.h"

namespace erhe::primitive {

auto c_str(const igl::ResourceStorage resource_storage) -> const char*
{
    switch (resource_storage) {
        case igl::ResourceStorage::Invalid:    return "Invalid";
        case igl::ResourceStorage::Private:    return "Private";
        case igl::ResourceStorage::Shared:     return "Shared";
        case igl::ResourceStorage::Managed:    return "Managed";
        case igl::ResourceStorage::Memoryless: return "Memoryless";
        default: return "?";
    }
}

}