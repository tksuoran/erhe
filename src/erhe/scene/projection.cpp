#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::scene
{

void Projection::update(Transform& transform, Viewport viewport) const
{
    switch (projection_type)
    {
        case Projection::Type::perspective:
        {
            transform.set_perspective(fov_x, fov_y, z_near, z_far);
            break;
        }

        case Projection::Type::perspective_horizontal:
        {
            transform.set_perspective_horizontal(fov_x, viewport.aspect_ratio(), z_near, z_far);
            break;
        }

        case Projection::Type::perspective_vertical:
        {
            transform.set_perspective_vertical(fov_y, viewport.aspect_ratio(), z_near, z_far);
            break;
        }

        case Projection::Type::orthogonal_horizontal:
        {
            transform.set_orthographic(-0.5f * ortho_width,
                                        0.5f * ortho_width,
                                       -0.5f * ortho_width / viewport.aspect_ratio(),
                                        0.5f * ortho_width / viewport.aspect_ratio(),
                                       z_near,
                                       z_far);
            break;
        }

        case Projection::Type::orthogonal_vertical:
        {
            transform.set_orthographic(-0.5f * ortho_height / viewport.aspect_ratio(),
                                        0.5f * ortho_height / viewport.aspect_ratio(),
                                       -0.5f * ortho_height,
                                        0.5f * ortho_height,
                                       z_near,
                                       z_far);
            break;
        }

        case Projection::Type::orthogonal:
        {
            transform.set_orthographic(-0.5f * ortho_width,
                                        0.5f * ortho_width,
                                       -0.5f * ortho_height,
                                        0.5f * ortho_height,
                                       z_near,
                                       z_far);
            break;
        }

        case Projection::Type::orthogonal_rectangle:
        {
            transform.set_orthographic(ortho_left,
                                       ortho_left + ortho_width,
                                       ortho_bottom,
                                       ortho_bottom + ortho_height,
                                       z_near,
                                       z_far);
            break;
        }

        case Projection::Type::generic_frustum:
        {
            transform.set_frustum(frustum_left,
                                  frustum_right,
                                  frustum_bottom,
                                  frustum_top,
                                  z_near,
                                  z_far);
            break;
        }

        case Projection::Type::other:
            // TODO(tksuoran@gmail.com): Implement
            break;
    }
}

} // namespace erhe::scene
