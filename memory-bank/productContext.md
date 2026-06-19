§MBEL:5.0
©erhe>tksuoran::CppGraphicsLibrary+Editor

[VISION]
erhe::CppGraphicsLib+Editor{Vulkan!default|OpenGL|Metal}
@purpose::RealtimeRendering+SceneAuthoring+GeometryExperiments+XR

[PROBLEMS_SOLVED]
RenderGraph::DAG{typed-io,dependency-order/frame}
SceneGraph::glTF-like{Node+Mesh+Camera+Light+Scene}
Physics::Jolt{abstracted}
Geometry::Geogram-backend{CatmullClark+Conway+CSG-experimental}
GPU::Vulkan-style-abstraction-over-backends
Editor::ImGui-based{tools+windows+operations}
XR::OpenXR{Quest-target}

[USER_GOALS]
BuildEditor✓+RunEditor✓
IterateGeometry+Rendering+XR
CrossPlatform::Win|macOS|Linux|Quest

[SUCCESS]
editor.exe::builds+runs{MainLoop✓}
LowAllocSteadyFrame{#0allocs}
RootCauseFixes¬BandAid
