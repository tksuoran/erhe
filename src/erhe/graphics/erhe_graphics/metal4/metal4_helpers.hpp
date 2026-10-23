#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

[[nodiscard]] auto to_mtl_pixel_format          (erhe::dataformat::Format format) -> MTL::PixelFormat;
[[nodiscard]] auto to_mtl_texture_type          (Texture_type type, int sample_count) -> MTL::TextureType;
[[nodiscard]] auto to_mtl_vertex_format         (erhe::dataformat::Format format) -> MTL::VertexFormat;
[[nodiscard]] auto to_mtl_compare_function      (Compare_operation op) -> MTL::CompareFunction;
[[nodiscard]] auto to_mtl_blend_factor          (Blending_factor factor) -> MTL::BlendFactor;
[[nodiscard]] auto to_mtl_blend_operation       (Blend_equation_mode mode) -> MTL::BlendOperation;
[[nodiscard]] auto to_mtl_stencil_operation     (Stencil_op op) -> MTL::StencilOperation;
[[nodiscard]] auto to_mtl_winding               (Front_face_direction direction) -> MTL::Winding;
[[nodiscard]] auto to_mtl_cull_mode             (bool face_cull_enable, Cull_face_mode mode) -> MTL::CullMode;
[[nodiscard]] auto to_mtl_triangle_fill_mode    (Polygon_mode mode) -> MTL::TriangleFillMode;
[[nodiscard]] auto to_mtl_depth_clip_mode       (bool depth_clamp_enable) -> MTL::DepthClipMode;
[[nodiscard]] auto to_mtl_depth_resolve_filter  (Resolve_mode mode) -> MTL::MultisampleDepthResolveFilter;
[[nodiscard]] auto to_mtl_stencil_resolve_filter(Resolve_mode mode) -> MTL::MultisampleStencilResolveFilter;

static constexpr NS::UInteger vertex_buffer_index_offset = 16;

} // namespace erhe::graphics
