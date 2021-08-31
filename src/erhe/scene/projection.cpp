#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::scene
{

void Projection::update(Transform& transform, const Viewport viewport) const
{
    const auto clip_range = viewport.reverse_depth ? Clip_range{z_far, z_near} : Clip_range{z_near, z_far};

    switch (projection_type)
    {
        case Projection::Type::perspective:
        {
            transform.set_perspective(clip_range, fov_x, fov_y);
            break;
        }

        case Projection::Type::perspective_xr:
        {
            transform.set_perspective_xr(clip_range, fov_left, fov_right, fov_up, fov_down);
            break;
        }

        case Projection::Type::perspective_horizontal:
        {
            transform.set_perspective_horizontal(clip_range, fov_x, viewport.aspect_ratio());
            break;
        }

        case Projection::Type::perspective_vertical:
        {
            transform.set_perspective_vertical(clip_range, fov_y, viewport.aspect_ratio());
            break;
        }

        case Projection::Type::orthogonal_horizontal:
        {
            transform.set_orthographic(clip_range,
                                       -0.5f * ortho_width,
                                        0.5f * ortho_width,
                                       -0.5f * ortho_width / viewport.aspect_ratio(),
                                        0.5f * ortho_width / viewport.aspect_ratio());
            break;
        }

        case Projection::Type::orthogonal_vertical:
        {
            transform.set_orthographic(clip_range,
                                       -0.5f * ortho_height / viewport.aspect_ratio(),
                                        0.5f * ortho_height / viewport.aspect_ratio(),
                                       -0.5f * ortho_height,
                                        0.5f * ortho_height);
            break;
        }

        case Projection::Type::orthogonal:
        {
            transform.set_orthographic(clip_range,
                                       -0.5f * ortho_width,
                                        0.5f * ortho_width,
                                       -0.5f * ortho_height,
                                        0.5f * ortho_height);
            break;
        }

        case Projection::Type::orthogonal_rectangle:
        {
            transform.set_orthographic(clip_range,
                                       ortho_left,
                                       ortho_left + ortho_width,
                                       ortho_bottom,
                                       ortho_bottom + ortho_height);
            break;
        }

        case Projection::Type::generic_frustum:
        {
            transform.set_frustum(clip_range,
                                  frustum_left,
                                  frustum_right,
                                  frustum_bottom,
                                  frustum_top);
            break;
        }

        case Projection::Type::other:
            // TODO(tksuoran@gmail.com): Implement
            break;
    }
}

auto Projection::get_fov_sides(const Viewport viewport) const -> Fov_sides
{
    switch (projection_type)
    {
        case Projection::Type::perspective:
        {
            return Fov_sides{ -0.5f * fov_x,
                               0.5f * fov_x,
                               0.5f * fov_y,
                              -0.5f * fov_y };
        }

        case Projection::Type::perspective_xr:
        {
            return Fov_sides{ fov_left,
                              fov_right,
                              fov_up,
                              fov_down };
        }

        case Projection::Type::perspective_horizontal:
        {
            return Fov_sides{ -0.5f * fov_x,
                               0.5f * fov_x,
                               0.5f * fov_x / viewport.aspect_ratio(),
                              -0.5f * fov_x / viewport.aspect_ratio() };
        }

        case Projection::Type::perspective_vertical:
        {
            return Fov_sides{ -0.5f * fov_y * viewport.aspect_ratio(),
                               0.5f * fov_y * viewport.aspect_ratio(),
                               0.5f * fov_y,
                              -0.5f * fov_y };
        }

        case Projection::Type::orthogonal_horizontal:
        {
            return Fov_sides{ -0.5f * ortho_width,
                               0.5f * ortho_width,
                               0.5f * ortho_width / viewport.aspect_ratio(),
                              -0.5f * ortho_width / viewport.aspect_ratio() };
        }

        case Projection::Type::orthogonal_vertical:
        {
            return Fov_sides{ -0.5f * ortho_height / viewport.aspect_ratio(),
                               0.5f * ortho_height / viewport.aspect_ratio(),
                               0.5f * ortho_height,
                              -0.5f * ortho_height };
        }

        case Projection::Type::orthogonal:
        {
            return Fov_sides{ -0.5f * ortho_width,
                               0.5f * ortho_width,
                               0.5f * ortho_height,
                              -0.5f * ortho_height };
        }

        case Projection::Type::orthogonal_rectangle:
        {
            return Fov_sides{ ortho_left,
                              ortho_left   + ortho_width,
                              ortho_bottom + ortho_height,
                              ortho_bottom };
        }

        case Projection::Type::generic_frustum:
        {
            return Fov_sides{ frustum_left,
                              frustum_right,
                              frustum_top,
                              frustum_bottom };
        }

        case Projection::Type::other:
        {
            // The projection is externally updated - do nothing here.
            break;
        }
    }

    return Fov_sides{ 0.0f, 0.0f, 0.0f, 0.0f };
}

} // namespace erhe::scene
