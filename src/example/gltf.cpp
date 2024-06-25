void parse_primitive(
    const std::shared_ptr<erhe::scene::Mesh>& erhe_mesh,
    const cgltf_mesh*                         mesh,
    const cgltf_size                          primitive_index
)
{
    const cgltf_primitive* primitive = &mesh->primitives[primitive_index];

    auto name = (mesh->name != nullptr)
        ? fmt::format("{}[{}]", mesh->name, primitive_index)
        : fmt::format("primitive[{}]", primitive_index);

    log_gltf->trace("Primitive type: {}", c_str(primitive->type));

    // NOTE: If same geometry is used in multiple primitives,
    //       this naive loader will create duplicate vertex
    //       and index buffer sections for each.
    //       Parser in erhe::gltf does better job, but it goes
    //       through erhe::geometry::Geometry.
    erhe::primitive::Renderable_mesh erhe_gl_primitive;

    // Index buffer
    {
        const cgltf_accessor* const    accessor     = primitive->indices;
        const cgltf_buffer_view* const buffer_view  = accessor->buffer_view;
        const cgltf_buffer* const      buffer       = buffer_view->buffer;
        const cgltf_size               buffer_index = buffer - m_data->buffers;

        log_gltf->trace(
            "Index buffer index = {}, component type = {}, type = {}, "
            "count = {}, accessor offset = {}, buffer view_offset = {}",
            buffer_index,
            c_str(accessor->component_type),
            c_str(accessor->type),
            accessor->count,
            accessor->offset,
            buffer_view->offset
        );

        const cgltf_size  index_stride            = 4; // always 32-bit unsigned integer
        const std::size_t index_buffer_byte_count = primitive->indices->count * index_stride;

        std::vector<uint32_t> read_indices(primitive->indices->count);
        const cgltf_size unpack_count = cgltf_accessor_unpack_indices(accessor, &read_indices[0], primitive->indices->count);
        if (unpack_count != primitive->indices->count) {
            log_gltf->error(
                "cgltf_accessor_unpack_indices() failed: expected {}, got {}",
                primitive->indices->count,
                unpack_count
            );
            return;
        }

        std::vector<uint8_t> data(index_buffer_byte_count);
        memcpy(data.data(), read_indices.data(), data.size());

        erhe::primitive::Buffer_range index_range = m_context.buffer_sink.allocate_index_buffer(primitive->indices->count, index_stride);
        m_context.buffer_sink.enqueue_index_data(index_range.byte_offset, std::move(data));

        erhe_gl_primitive.index_buffer_range.byte_offset       = index_range.byte_offset;
        erhe_gl_primitive.index_buffer_range.count             = primitive->indices->count;
        erhe_gl_primitive.index_buffer_range.element_size      = index_stride;
        erhe_gl_primitive.triangle_fill_indices.primitive_type = to_erhe(primitive->type);
        erhe_gl_primitive.triangle_fill_indices.first_index    = 0;
        erhe_gl_primitive.triangle_fill_indices.index_count    = primitive->indices->count;
    }

    // Vertex buffer
    {
        const cgltf_size  vertex_count             = primitive->attributes[0].data->count;
        const std::size_t vertex_buffer_byte_count = vertex_count * m_context.vertex_format.stride();
        erhe::primitive::Buffer_range vertex_range = m_context.buffer_sink.allocate_vertex_buffer(vertex_count, m_context.vertex_format.stride());

        std::vector<uint8_t> data(vertex_buffer_byte_count);
        uint8_t* span_start = data.data();

        erhe_gl_primitive.vertex_buffer_range.byte_offset  = vertex_range.byte_offset;
        erhe_gl_primitive.vertex_buffer_range.count        = vertex_count;
        erhe_gl_primitive.vertex_buffer_range.element_size = m_context.vertex_format.stride();

        for (cgltf_size attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
            const cgltf_attribute* const attribute = &primitive->attributes[attribute_index];

            const auto erhe_usage_opt = to_erhe(attribute->type);
            if (!erhe_usage_opt.has_value()) {
                continue;
            }

            const auto erhe_usage = erhe_usage_opt.value();
            const erhe::graphics::Vertex_attribute* erhe_attribute = m_context.vertex_format.find_attribute_maybe(
                erhe_usage, attribute->index
            );
            if (erhe_attribute == nullptr) {
                continue;
            }
        
            const cgltf_accessor* accessor = attribute->data;

            log_gltf->trace(
                "Primitive attribute[{}]: name = {}, attribute type = {}[{}], "
                "component type = {}, accessor type = {}, normalized = {}, count = {}, "
                "stride = {}, accessor offset = {}",
                attribute_index,
                attribute->name,
                c_str(attribute->type), // semantics
                attribute->index,
                c_str(accessor->component_type),
                c_str(accessor->type),
                accessor->normalized != 0,
                accessor->count,
                accessor->stride,
                accessor->offset
            );

            const cgltf_size component_count = cgltf_num_components(accessor->type);

            switch (erhe_attribute->data_type.type) {
                case gl::Vertex_attrib_type::float_: {
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        cgltf_float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        cgltf_accessor_read_float(accessor, i, &value[0], component_count);
                        float* dest = reinterpret_cast<float*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                        for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                            dest[c] = value[c];
                        }
                        cgltf_accessor_read_float(
                            accessor,
                            i,
                            reinterpret_cast<float*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride()),
                            erhe_attribute->data_type.dimension
                        );
                    }
                    break;
                }

                case gl::Vertex_attrib_type::unsigned_byte: {
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        cgltf_uint value[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                        uint8_t* dest = reinterpret_cast<uint8_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                        for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                            ERHE_VERIFY((value[c] & ~0xffu) == 0);
                            dest[c] = value[c] & 0xffu;
                        }
                    }
                    break;
                }
                case gl::Vertex_attrib_type::byte: {
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        cgltf_uint value[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                        int8_t* dest = reinterpret_cast<int8_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                        for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                            ERHE_VERIFY((value[c] & ~0xffu) == 0);
                            dest[c] = value[c] & 0xffu;
                        }
                    }
                    break;
                }
                case gl::Vertex_attrib_type::unsigned_short: {
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        cgltf_uint value[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                        uint16_t* dest = reinterpret_cast<uint16_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                        for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                            ERHE_VERIFY((value[c] & ~0xffffu) == 0);
                            dest[c] = value[c] & 0xffffu;
                        }
                    }
                    break;
                }
                case gl::Vertex_attrib_type::short_: {
                    for (cgltf_size i = 0; i < accessor->count; ++i) {
                        cgltf_uint value[4] = { 0, 0, 0, 0 };
                        cgltf_accessor_read_uint(accessor, i, &value[0], component_count);
                        int16_t* dest = reinterpret_cast<int16_t*>(span_start + erhe_attribute->offset + i * m_context.vertex_format.stride());
                        for (cgltf_size c = 0; c < erhe_attribute->data_type.dimension; ++c) {
                            ERHE_VERIFY((value[c] & ~0xffffu) == 0);
                            dest[c] = value[c] & 0xffffu;
                        }
                    }
                    break;
                }
                default: {
                    // TODO
                    break;
                }
            }
        }

        m_context.buffer_sink.enqueue_vertex_data(vertex_range.byte_offset, std::move(data));
    }

    const cgltf_size material_index = primitive->material - m_data->materials;
    erhe_mesh->add_primitive(
        erhe::primitive::Primitive{
            .material           = m_materials.at(material_index),
            .geometry_primitive = std::make_shared<erhe::primitive::Geometry_primitive>(
                std::move(erhe_gl_primitive)
            )
        }
    );
}
