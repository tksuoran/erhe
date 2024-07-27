#pragma once

#if defined(ERHE_GLTF_LIBRARY_FASTGLTF)
# include "gltf_fastgltf.hpp"
#elif defined(ERHE_GLTF_LIBRARY_NONE)
# include "gltf_none.hpp"
#endif
