# ifndef IMGUI_DEFINE_MATH_OPERATORS
#     define IMGUI_DEFINE_MATH_OPERATORS
# endif
# include "erhe_imgui/imgui_canvas.h"
# include <type_traits>

// https://stackoverflow.com/a/36079786
# define DECLARE_HAS_MEMBER(__trait_name__, __member_name__)                         \
                                                                                     \
    template <typename __boost_has_member_T__>                                       \
    class __trait_name__                                                             \
    {                                                                                \
        using check_type = ::std::remove_const_t<__boost_has_member_T__>;            \
        struct no_type {char x[2];};                                                 \
        using  yes_type = char;                                                      \
                                                                                     \
        struct  base { void __member_name__() {}};                                   \
        struct mixin : public base, public check_type {};                            \
                                                                                     \
        template <void (base::*)()> struct aux {};                                   \
                                                                                     \
        template <typename U> static no_type  test(aux<&U::__member_name__>*);       \
        template <typename U> static yes_type test(...);                             \
                                                                                     \
        public:                                                                      \
                                                                                     \
        static constexpr bool value = (sizeof(yes_type) == sizeof(test<mixin>(0)));  \
    }

// Special sentinel value. This needs to be unique, so allow it to be overridden in the user's ImGui config
# ifndef ImDrawCallback_ImCanvas
#     define ImDrawCallback_ImCanvas        (ImDrawCallback)(-2)
# endif

namespace ImCanvasDetails {

DECLARE_HAS_MEMBER(HasFringeScale, _FringeScale);

struct FringeScaleRef
{
    // Overload is present when ImDrawList does have _FringeScale member variable.
    template <typename T>
    static float& Get(typename std::enable_if<HasFringeScale<T>::value, T>::type* drawList)
    {
        return drawList->_FringeScale;
    }

    // Overload is present when ImDrawList does not have _FringeScale member variable.
    template <typename T>
    static float& Get(typename std::enable_if<!HasFringeScale<T>::value, T>::type*)
    {
        static float placeholder = 1.0f;
        return placeholder;
    }
};

DECLARE_HAS_MEMBER(HasVtxCurrentOffset, _VtxCurrentOffset);

struct VtxCurrentOffsetRef
{
    // Overload is present when ImDrawList does have _FringeScale member variable.
    template <typename T>
    static unsigned int& Get(typename std::enable_if<HasVtxCurrentOffset<T>::value, T>::type* drawList)
    {
        return drawList->_VtxCurrentOffset;
    }

    // Overload is present when ImDrawList does not have _FringeScale member variable.
    template <typename T>
    static unsigned int& Get(typename std::enable_if<!HasVtxCurrentOffset<T>::value, T>::type* drawList)
    {
        return drawList->_CmdHeader.VtxOffset;
    }
};

} // namespace ImCanvasDetails

// Returns a reference to _FringeScale extension to ImDrawList
//
// If ImDrawList does not have _FringeScale a placeholder is returned.
static inline float& ImFringeScaleRef(ImDrawList* drawList)
{
    using namespace ImCanvasDetails;
    return FringeScaleRef::Get<ImDrawList>(drawList);
}

static inline unsigned int& ImVtxOffsetRef(ImDrawList* drawList)
{
    using namespace ImCanvasDetails;
    return VtxCurrentOffsetRef::Get<ImDrawList>(drawList);
}

static inline ImVec2 ImSelectPositive(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x > 0.0f ? lhs.x : rhs.x, lhs.y > 0.0f ? lhs.y : rhs.y); }

bool ImGuiEx::Canvas::Begin(const char* id, const ImVec2& size)
{
    return Begin(ImGui::GetID(id), size);
}

bool ImGuiEx::Canvas::Begin(ImGuiID id, const ImVec2& size)
{
    IM_ASSERT(m_InBeginEnd == false);

    m_WidgetPosition = ImGui::GetCursorScreenPos();
    m_WidgetSize = ImSelectPositive(size, ImGui::GetContentRegionAvail());
    m_WidgetRect = ImRect(m_WidgetPosition, m_WidgetPosition + m_WidgetSize);
    m_DrawList = ImGui::GetWindowDrawList();

    UpdateViewTransformPosition();

# if IMGUI_VERSION_NUM > 18415
    if (ImGui::IsClippedEx(m_WidgetRect, id))
        return false;
# else
    if (ImGui::IsClippedEx(m_WidgetRect, id, false))
        return false;
# endif

    // Save current channel, so we can assert when user
    // call canvas API with different one.
    m_ExpectedChannel = m_DrawList->_Splitter._Current;

    // #debug: Canvas content.
    //m_DrawList->AddRectFilled(m_StartPos, m_StartPos + m_CurrentSize, IM_COL32(0, 0, 0, 64));
    //m_DrawList->AddRect(m_WidgetRect.Min, m_WidgetRect.Max, IM_COL32(255, 0, 255, 64));

    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));

# if IMGUI_EX_CANVAS_DEFERED()
    m_Ranges.resize(0);
# endif

    SaveInputState();
    SaveViewportState();

    // Record cursor max to prevent scrollbars from appearing.
    m_WindowCursorMaxBackup = ImGui::GetCurrentWindow()->DC.CursorMaxPos;

    EnterLocalSpace();

# if IMGUI_VERSION_NUM >= 18967
    ImGui::SetNextItemAllowOverlap();
# endif

    // Emit dummy widget matching bounds of the canvas. Issue #251: content is
    // now authored in screen space, so reserve the real widget rect (in screen
    // coordinates) rather than the canvas-unit visible region.
    ImGui::SetCursorScreenPos(m_WidgetPosition);
    ImGui::Dummy(m_WidgetSize);

    ImGui::SetCursorScreenPos(m_WidgetPosition);

    m_InBeginEnd = true;

    return true;
}

void ImGuiEx::Canvas::End()
{
    // If you're here your call to Begin() returned false,
    // or Begin() wasn't called at all.
    IM_ASSERT(m_InBeginEnd == true);

    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    //auto& io = ImGui::GetIO();

    // Check: Unmatched calls to Suspend() / Resume(). Please check your code.
    IM_ASSERT(m_SuspendCounter == 0);

    LeaveLocalSpace();

    ImGui::GetCurrentWindow()->DC.CursorMaxPos = m_WindowCursorMaxBackup;

# if IMGUI_VERSION_NUM < 18967
    ImGui::SetItemAllowOverlap();
# endif

    // Emit dummy widget matching bounds of the canvas.
    ImGui::SetCursorScreenPos(m_WidgetPosition);
    ImGui::Dummy(m_WidgetSize);

    // #debug: Rect around canvas. Content should be inside these bounds.
    //m_DrawList->AddRect(m_WidgetPosition - ImVec2(1.0f, 1.0f), m_WidgetPosition + m_WidgetSize + ImVec2(1.0f, 1.0f), IM_COL32(196, 0, 0, 255));

    m_InBeginEnd = false;
}

void ImGuiEx::Canvas::SetView(const ImVec2& origin, float scale)
{
    SetView(CanvasView(origin, scale));
}

void ImGuiEx::Canvas::SetView(const CanvasView& view)
{
    if (m_InBeginEnd)
        LeaveLocalSpace();

    if (m_View.Origin.x != view.Origin.x || m_View.Origin.y != view.Origin.y)
    {
        m_View.Origin = view.Origin;

        UpdateViewTransformPosition();
    }

    if (m_View.Scale != view.Scale)
    {
        m_View.Scale    = view.Scale;
        m_View.InvScale = view.InvScale;
    }

    if (m_InBeginEnd)
        EnterLocalSpace();
}

void ImGuiEx::Canvas::CenterView(const ImVec2& canvasPoint)
{
    auto view = CalcCenterView(canvasPoint);
    SetView(view);
}

ImGuiEx::CanvasView ImGuiEx::Canvas::CalcCenterView(const ImVec2& canvasPoint) const
{
    auto localCenter = ToLocal(m_WidgetPosition + m_WidgetSize * 0.5f);
    auto localOffset = canvasPoint - localCenter;
    auto offset      = FromLocalV(localOffset);

    return CanvasView{ m_View.Origin - offset, m_View.Scale };
}

void ImGuiEx::Canvas::CenterView(const ImRect& canvasRect)
{
    auto view = CalcCenterView(canvasRect);

    SetView(view);
}

ImGuiEx::CanvasView ImGuiEx::Canvas::CalcCenterView(const ImRect& canvasRect) const
{
    auto canvasRectSize = canvasRect.GetSize();

    if (canvasRectSize.x <= 0.0f || canvasRectSize.y <= 0.0f)
        return View();

    auto widgetAspectRatio     = m_WidgetSize.y   > 0.0f ? m_WidgetSize.x   / m_WidgetSize.y   : 0.0f;
    auto canvasRectAspectRatio = canvasRectSize.y > 0.0f ? canvasRectSize.x / canvasRectSize.y : 0.0f;

    if (widgetAspectRatio <= 0.0f || canvasRectAspectRatio <= 0.0f)
        return View();

    auto newOrigin = m_View.Origin;
    auto newScale  = m_View.Scale;
    if (canvasRectAspectRatio > widgetAspectRatio)
    {
        // width span across view
        newScale = m_WidgetSize.x / canvasRectSize.x;
        newOrigin = canvasRect.Min * -newScale;
        newOrigin.y += (m_WidgetSize.y - canvasRectSize.y * newScale) * 0.5f;
    }
    else
    {
        // height span across view
        newScale = m_WidgetSize.y / canvasRectSize.y;
        newOrigin = canvasRect.Min * -newScale;
        newOrigin.x += (m_WidgetSize.x - canvasRectSize.x * newScale) * 0.5f;
    }

    return CanvasView{ newOrigin, newScale };
}

void ImGuiEx::Canvas::Suspend()
{
    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    if (m_SuspendCounter == 0)
        LeaveLocalSpace();

    ++m_SuspendCounter;
}

void ImGuiEx::Canvas::Resume()
{
    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    // Check: Number of calls to Resume() do not match calls to Suspend(). Please check your code.
    IM_ASSERT(m_SuspendCounter > 0);
    if (--m_SuspendCounter == 0)
        EnterLocalSpace();
}

ImVec2 ImGuiEx::Canvas::FromLocal(const ImVec2& point) const
{
    return point * m_View.Scale + m_ViewTransformPosition;
}

ImVec2 ImGuiEx::Canvas::FromLocal(const ImVec2& point, const CanvasView& view) const
{
    return point * view.Scale + view.Origin + m_WidgetPosition;
}

ImVec2 ImGuiEx::Canvas::FromLocalV(const ImVec2& vector) const
{
    return vector * m_View.Scale;
}

ImVec2 ImGuiEx::Canvas::FromLocalV(const ImVec2& vector, const CanvasView& view) const
{
    return vector * view.Scale;
}

ImVec2 ImGuiEx::Canvas::ToLocal(const ImVec2& point) const
{
    return (point - m_ViewTransformPosition) * m_View.InvScale;
}

ImVec2 ImGuiEx::Canvas::ToLocal(const ImVec2& point, const CanvasView& view) const
{
    return (point - view.Origin - m_WidgetPosition) * view.InvScale;
}

ImVec2 ImGuiEx::Canvas::ToLocalV(const ImVec2& vector) const
{
    return vector * m_View.InvScale;
}

ImVec2 ImGuiEx::Canvas::ToLocalV(const ImVec2& vector, const CanvasView& view) const
{
    return vector * view.InvScale;
}

ImRect ImGuiEx::Canvas::CalcViewRect(const CanvasView& view) const
{
    ImRect result;
    result.Min = ImVec2(-view.Origin.x, -view.Origin.y) * view.InvScale;
    result.Max = (m_WidgetSize - view.Origin) * view.InvScale;
    return result;
}

void ImGuiEx::Canvas::UpdateViewTransformPosition()
{
    m_ViewTransformPosition = m_View.Origin + m_WidgetPosition;
}

void ImGuiEx::Canvas::SaveInputState()
{
    auto& io = ImGui::GetIO();
    m_MousePosBackup = io.MousePos;
    m_MousePosPrevBackup = io.MousePosPrev;
    for (auto i = 0; i < IM_ARRAYSIZE(m_MouseClickedPosBackup); ++i)
        m_MouseClickedPosBackup[i] = io.MouseClickedPos[i];
}

void ImGuiEx::Canvas::RestoreInputState()
{
    auto& io = ImGui::GetIO();
    io.MousePos = m_MousePosBackup;
    io.MousePosPrev = m_MousePosPrevBackup;
    for (auto i = 0; i < IM_ARRAYSIZE(m_MouseClickedPosBackup); ++i)
        io.MouseClickedPos[i] = m_MouseClickedPosBackup[i];
}

void ImGuiEx::Canvas::SaveViewportState()
{
# if defined(IMGUI_HAS_VIEWPORT)
    auto window = ImGui::GetCurrentWindow();
    auto viewport = ImGui::GetWindowViewport();

    m_WindowPosBackup = window->Pos;
    m_ViewportPosBackup = viewport->Pos;
    m_ViewportSizeBackup = viewport->Size;
# if IMGUI_VERSION_NUM > 18002
    m_ViewportWorkPosBackup = viewport->WorkPos;
    m_ViewportWorkSizeBackup = viewport->WorkSize;
# else
    m_ViewportWorkOffsetMinBackup = viewport->WorkOffsetMin;
    m_ViewportWorkOffsetMaxBackup = viewport->WorkOffsetMax;
# endif
# endif
}

void ImGuiEx::Canvas::RestoreViewportState()
{
# if defined(IMGUI_HAS_VIEWPORT)
    auto window = ImGui::GetCurrentWindow();
    auto viewport = ImGui::GetWindowViewport();

    window->Pos = m_WindowPosBackup;
    viewport->Pos = m_ViewportPosBackup;
    viewport->Size = m_ViewportSizeBackup;
# if IMGUI_VERSION_NUM > 18002
    viewport->WorkPos = m_ViewportWorkPosBackup;
    viewport->WorkSize = m_ViewportWorkSizeBackup;
# else
    viewport->WorkOffsetMin = m_ViewportWorkOffsetMinBackup;
    viewport->WorkOffsetMax = m_ViewportWorkOffsetMaxBackup;
# endif
# endif
}

void ImGuiEx::Canvas::EnterLocalSpace()
{
    // Issue #251: native-resolution rendering. The canvas no longer fakes a
    // scaled-down local coordinate space; the node editor authors every
    // primitive and every node-content widget directly in screen space at the
    // zoomed size (points mapped through canvas->screen, sizes / fonts scaled by
    // the view scale at submission time). So this used to (a) fake io.MousePos /
    // viewport / window->Pos into local space, (b) scale _FringeScale, and (c)
    // record vertex ranges for LeaveLocalSpace to post-multiply - none of which
    // is needed now. All that remains is:
    //   - clip canvas content to the widget rect (in real screen space), and
    //   - publish m_ViewRect (the visible region of the plane, in canvas units),
    //     which the editor uses for the grid, hit-region and navigation math.
    ImGui::PushClipRect(m_WidgetPosition, m_WidgetPosition + m_WidgetSize, true);

    m_ViewRect = CalcViewRect(m_View);
}

void ImGuiEx::Canvas::LeaveLocalSpace()
{
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    // Issue #251: content is authored directly in screen space, so there is no
    // vertex / clip-rect post-transform, no _FringeScale restore and no
    // ImDrawCallback_ImCanvas sentinel to strip. Just balance the clip push and
    // restore the (unchanged) input / viewport backups.
    ImGui::PopClipRect();

    RestoreInputState();
    RestoreViewportState();
}
