#ifndef ERHE_CAMERA_VIEW_GLSL
#define ERHE_CAMERA_VIEW_GLSL

// c_view_index: integer used to pick the per-view entry from
// camera.cameras[]. With multiview enabled (ERHE_MULTIVIEW), this is
// gl_ViewIndex, supplied by VK_KHR_multiview / GL_EXT_multiview. With
// multiview disabled, it is 0u so single-view code paths read the only
// camera entry. Shaders that read camera data should index as
//     camera.cameras[c_view_index].clip_from_world
// instead of hard-coding camera.cameras[0].

#ifdef ERHE_MULTIVIEW
    #define c_view_index uint(gl_ViewIndex)
#else
    #define c_view_index 0u
#endif

#endif // ERHE_CAMERA_VIEW_GLSL
