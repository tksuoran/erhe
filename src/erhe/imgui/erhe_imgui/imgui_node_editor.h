//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_NODE_EDITOR_H__
# define __IMGUI_NODE_EDITOR_H__
# pragma once


//------------------------------------------------------------------------------
# include <imgui/imgui.h>
# include <cstdint> // std::uintXX_t
# include <utility> // std::move


//------------------------------------------------------------------------------
# define IMGUI_NODE_EDITOR_VERSION      "0.9.4"
# define IMGUI_NODE_EDITOR_VERSION_NUM  000904


//------------------------------------------------------------------------------
#ifndef IMGUI_NODE_EDITOR_API
#define IMGUI_NODE_EDITOR_API
#endif


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {


//------------------------------------------------------------------------------
struct NodeId;
struct LinkId;
struct PinId;


//------------------------------------------------------------------------------
enum class PinKind
{
    Input,
    Output
};

enum class FlowDirection
{
    Forward,
    Backward
};

enum class CanvasSizeMode
{
    FitVerticalView,        // Previous view will be scaled to fit new view on Y axis
    FitHorizontalView,      // Previous view will be scaled to fit new view on X axis
    CenterOnly,             // Previous view will be centered on new view
};


//------------------------------------------------------------------------------
enum class SaveReasonFlags: uint32_t
{
    None       = 0x00000000,
    Navigation = 0x00000001,
    Position   = 0x00000002,
    Size       = 0x00000004,
    Selection  = 0x00000008,
    AddNode    = 0x00000010,
    RemoveNode = 0x00000020,
    User       = 0x00000040
};

inline SaveReasonFlags operator |(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)); }
inline SaveReasonFlags operator &(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)); }

using ConfigSaveSettings     = bool   (*)(const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
using ConfigLoadSettings     = size_t (*)(char* data, void* userPointer);

using ConfigSaveNodeSettings = bool   (*)(NodeId nodeId, const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
using ConfigLoadNodeSettings = size_t (*)(NodeId nodeId, char* data, void* userPointer);

using ConfigSession          = void   (*)(void* userPointer);

struct Config
{
    using CanvasSizeModeAlias = ax::NodeEditor::CanvasSizeMode;

    const char*             SettingsFile;
    ConfigSession           BeginSaveSession;
    ConfigSession           EndSaveSession;
    ConfigSaveSettings      SaveSettings;
    ConfigLoadSettings      LoadSettings;
    ConfigSaveNodeSettings  SaveNodeSettings;
    ConfigLoadNodeSettings  LoadNodeSettings;
    void*                   UserPointer;
    ImVector<float>         CustomZoomLevels;
    CanvasSizeModeAlias     CanvasSizeMode;
    int                     DragButtonIndex;        // Mouse button index drag action will react to (0-left, 1-right, 2-middle)
    int                     SelectButtonIndex;      // Mouse button index select action will react to (0-left, 1-right, 2-middle)
    int                     NavigateButtonIndex;    // Mouse button index navigate action will react to (0-left, 1-right, 2-middle)
    int                     ContextMenuButtonIndex; // Mouse button index context menu action will react to (0-left, 1-right, 2-middle)
    ImGuiKey                CutLinksKey;            // Key held to turn a select-button drag into a Houdini-style wire cut (erhe)
    bool                    EnableSmoothZoom;
    float                   SmoothZoomPower;

    Config()
        : SettingsFile(nullptr) // "NodeEditor.json")
        , BeginSaveSession(nullptr)
        , EndSaveSession(nullptr)
        , SaveSettings(nullptr)
        , LoadSettings(nullptr)
        , SaveNodeSettings(nullptr)
        , LoadNodeSettings(nullptr)
        , UserPointer(nullptr)
        , CustomZoomLevels()
        , CanvasSizeMode(CanvasSizeModeAlias::FitVerticalView)
        , DragButtonIndex(0)
        , SelectButtonIndex(0)
        , NavigateButtonIndex(1)
        , ContextMenuButtonIndex(1)
        , CutLinksKey(ImGuiKey_Y)
        , EnableSmoothZoom(false)
# ifdef __APPLE__
        , SmoothZoomPower(1.1f)
# else
        , SmoothZoomPower(1.3f)
# endif
    {
    }
};


//------------------------------------------------------------------------------
enum StyleColor
{
    StyleColor_Bg,
    StyleColor_Grid,
    StyleColor_NodeBg,
    StyleColor_NodeBorder,
    StyleColor_HovNodeBorder,
    StyleColor_SelNodeBorder,
    StyleColor_NodeSelRect,
    StyleColor_NodeSelRectBorder,
    StyleColor_HovLinkBorder,
    StyleColor_SelLinkBorder,
    StyleColor_HighlightLinkBorder,
    StyleColor_LinkSelRect,
    StyleColor_LinkSelRectBorder,
    StyleColor_PinRect,
    StyleColor_PinRectBorder,
    StyleColor_Flow,
    StyleColor_FlowMarker,
    StyleColor_GroupBg,
    StyleColor_GroupBorder,

    StyleColor_Count
};

enum StyleVar
{
    StyleVar_NodePadding,
    StyleVar_NodeRounding,
    StyleVar_NodeBorderWidth,
    StyleVar_HoveredNodeBorderWidth,
    StyleVar_SelectedNodeBorderWidth,
    StyleVar_PinRounding,
    StyleVar_PinBorderWidth,
    StyleVar_LinkStrength,
    StyleVar_SourceDirection,
    StyleVar_TargetDirection,
    StyleVar_ScrollDuration,
    StyleVar_FlowMarkerDistance,
    StyleVar_FlowSpeed,
    StyleVar_FlowDuration,
    StyleVar_PivotAlignment,
    StyleVar_PivotSize,
    StyleVar_PivotScale,
    StyleVar_PinCorners,
    StyleVar_PinRadius,
    StyleVar_PinArrowSize,
    StyleVar_PinArrowWidth,
    StyleVar_GroupRounding,
    StyleVar_GroupBorderWidth,
    StyleVar_HighlightConnectedLinks,
    StyleVar_SnapLinkToPinDir,
    StyleVar_HoveredNodeBorderOffset,
    StyleVar_SelectedNodeBorderOffset,

    StyleVar_Count
};

struct Style
{
    ImVec4  NodePadding;
    float   NodeRounding;
    float   NodeBorderWidth;
    float   HoveredNodeBorderWidth;
    float   HoverNodeBorderOffset;
    float   SelectedNodeBorderWidth;
    float   SelectedNodeBorderOffset;
    float   PinRounding;
    float   PinBorderWidth;
    float   LinkStrength;
    ImVec2  SourceDirection;
    ImVec2  TargetDirection;
    float   ScrollDuration;
    float   FlowMarkerDistance;
    float   FlowSpeed;
    float   FlowDuration;
    ImVec2  PivotAlignment;
    ImVec2  PivotSize;
    ImVec2  PivotScale;
    float   PinCorners;
    float   PinRadius;
    float   PinArrowSize;
    float   PinArrowWidth;
    float   GroupRounding;
    float   GroupBorderWidth;
    float   HighlightConnectedLinks;
    float   SnapLinkToPinDir; // when true link will start on the line defined by pin direction
    ImVec4  Colors[StyleColor_Count];

    Style()
    {
        NodePadding              = ImVec4(8, 8, 8, 8);
        NodeRounding             = 12.0f;
        NodeBorderWidth          = 1.5f;
        HoveredNodeBorderWidth   = 3.5f;
        HoverNodeBorderOffset    = 0.0f;
        SelectedNodeBorderWidth  = 3.5f;
        SelectedNodeBorderOffset = 0.0f;
        PinRounding              = 4.0f;
        PinBorderWidth           = 0.0f;
        LinkStrength             = 100.0f;
        SourceDirection          = ImVec2(1.0f, 0.0f);
        TargetDirection          = ImVec2(-1.0f, 0.0f);
        ScrollDuration           = 0.35f;
        FlowMarkerDistance       = 30.0f;
        FlowSpeed                = 150.0f;
        FlowDuration             = 2.0f;
        PivotAlignment           = ImVec2(0.5f, 0.5f);
        PivotSize                = ImVec2(0.0f, 0.0f);
        PivotScale               = ImVec2(1, 1);
#if IMGUI_VERSION_NUM > 18101
        PinCorners               = ImDrawFlags_RoundCornersAll;
#else
        PinCorners               = ImDrawCornerFlags_All;
#endif
        PinRadius                = 0.0f;
        PinArrowSize             = 0.0f;
        PinArrowWidth            = 0.0f;
        GroupRounding            = 6.0f;
        GroupBorderWidth         = 1.0f;
        HighlightConnectedLinks  = 0.0f;
        SnapLinkToPinDir         = 0.0f;

        Colors[StyleColor_Bg]                 = ImColor( 60,  60,  70, 200);
        Colors[StyleColor_Grid]               = ImColor(120, 120, 120,  40);
        Colors[StyleColor_NodeBg]             = ImColor( 32,  32,  32, 200);
        Colors[StyleColor_NodeBorder]         = ImColor(255, 255, 255,  96);
        Colors[StyleColor_HovNodeBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelNodeBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_NodeSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_NodeSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_HovLinkBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelLinkBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_HighlightLinkBorder]= ImColor(204, 105,   0, 255);
        Colors[StyleColor_LinkSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_LinkSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_PinRect]            = ImColor( 60, 180, 255, 100);
        Colors[StyleColor_PinRectBorder]      = ImColor( 60, 180, 255, 128);
        Colors[StyleColor_Flow]               = ImColor(255, 128,  64, 255);
        Colors[StyleColor_FlowMarker]         = ImColor(255, 128,  64, 255);
        Colors[StyleColor_GroupBg]            = ImColor(  0,   0,   0, 160);
        Colors[StyleColor_GroupBorder]        = ImColor(255, 255, 255,  32);
    }
};


//------------------------------------------------------------------------------

namespace Detail {
    class EditorContext;
};

class EditorContext
{
public:
    EditorContext(const Config* config);
    ~EditorContext() noexcept;

    Style& GetStyle();
    const char* GetStyleColorName(StyleColor colorIndex);

    void PushStyleColor(StyleColor colorIndex, const ImVec4& color);
    void PopStyleColor(int count = 1);

    void PushStyleVar(StyleVar varIndex, float value);
    void PushStyleVar(StyleVar varIndex, const ImVec2& value);
    void PushStyleVar(StyleVar varIndex, const ImVec4& value);
    void PopStyleVar(int count = 1);

    void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
    void End();

    void BeginNode(NodeId id);
    void BeginPin(PinId id, PinKind kind);
    void PinRect(const ImVec2& a, const ImVec2& b);
    void PinPivotRect(const ImVec2& a, const ImVec2& b);
    void PinPivotSize(const ImVec2& size);
    void PinPivotScale(const ImVec2& scale);
    void PinPivotAlignment(const ImVec2& alignment);
    void EndPin();
    void Group(const ImVec2& size);
    void EndNode();

    bool BeginGroupHint(NodeId nodeId);
    ImVec2 GetGroupMin();
    ImVec2 GetGroupMax();
    ImDrawList* GetHintForegroundDrawList();
    ImDrawList* GetHintBackgroundDrawList();
    void EndGroupHint();

    // TODO: Add a way to manage node background channels
    ImDrawList* GetNodeBackgroundDrawList(NodeId nodeId);

    bool Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

    void Flow(LinkId linkId, FlowDirection direction = FlowDirection::Forward);

    bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
    bool QueryNewLink(PinId* startId, PinId* endId);
    bool QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness = 1.0f);
    bool QueryNewNode(PinId* pinId);
    bool QueryNewNode(PinId* pinId, const ImVec4& color, float thickness = 1.0f);
    bool AcceptNewItem();
    bool AcceptNewItem(const ImVec4& color, float thickness = 1.0f);
    void RejectNewItem();
    void RejectNewItem(const ImVec4& color, float thickness = 1.0f);
    void EndCreate();

    bool BeginDelete();
    bool QueryDeletedLink(LinkId* linkId, PinId* startId = nullptr, PinId* endId = nullptr);
    bool QueryDeletedNode(NodeId* nodeId);
    bool AcceptDeletedItem(bool deleteDependencies = true);
    void RejectDeletedItem();
    void EndDelete();

    void SetNodePosition(NodeId nodeId, const ImVec2& editorPosition);
    void SetGroupSize(NodeId nodeId, const ImVec2& size);
    ImVec2 GetNodePosition(NodeId nodeId);
    ImVec2 GetNodeSize(NodeId nodeId);

    // Interactive resizing of plain (non-group) nodes. When enabled, hovering
    // a node's edges / corners shows the matching sizing cursor and dragging
    // with the left mouse button resizes the node (left / top edges also move
    // it). The editor does not own a plain node's size - the application must
    // adopt the gesture: while a resize drag is active, GetNodeResize()
    // returns true with the dragged node and its requested position + size
    // (canvas units); apply those to the node's own layout each frame.
    void EnableNodeResize(bool enable);
    bool IsNodeResizeEnabled() const;
    bool GetNodeResize(NodeId& nodeId, ImVec2& position, ImVec2& size);
    void CenterNodeOnScreen(NodeId nodeId);
    void SetNodeZPosition(NodeId nodeId, float z); // Sets node z position, nodes with higher value are drawn over nodes with lower value
    float GetNodeZPosition(NodeId nodeId); // Returns node z position, defaults is 0.0f

    void RestoreNodeState(NodeId nodeId);

    void Suspend();
    void Resume();
    bool IsSuspended();

    bool IsActive();

    bool HasSelectionChanged();
    int  GetSelectedObjectCount();
    int  GetSelectedNodes(NodeId* nodes, int size);
    int  GetSelectedLinks(LinkId* links, int size);
    bool IsNodeSelected(NodeId nodeId);
    bool IsLinkSelected(LinkId linkId);
    void ClearSelection();
    void SelectNode(NodeId nodeId, bool append = false);
    void SelectLink(LinkId linkId, bool append = false);
    void DeselectNode(NodeId nodeId);
    void DeselectLink(LinkId linkId);

    bool DeleteNode(NodeId nodeId);
    bool DeleteLink(LinkId linkId);

    bool HasAnyLinks(NodeId nodeId); // Returns true if node has any link connected
    bool HasAnyLinks(PinId pinId); // Return true if pin has any link connected
    int BreakLinks(NodeId nodeId); // Break all links connected to this node
    int BreakLinks(PinId pinId); // Break all links connected to this pin

    void NavigateToContent(float duration = -1);
    void NavigateToSelection(bool zoomIn = false, float duration = -1);

    bool ShowNodeContextMenu(NodeId* nodeId);
    bool ShowPinContextMenu(PinId* pinId);
    bool ShowLinkContextMenu(LinkId* linkId);
    bool ShowBackgroundContextMenu();

    void EnableShortcuts(bool enable);
    bool AreShortcutsEnabled();

    bool BeginShortcut();
    bool AcceptCut();
    bool AcceptCopy();
    bool AcceptPaste();
    bool AcceptDuplicate();
    bool AcceptCreateNode();
    int  GetActionContextSize();
    int  GetActionContextNodes(NodeId* nodes, int size);
    int  GetActionContextLinks(LinkId* links, int size);
    void EndShortcut();

    float GetCurrentZoom();

    // Set the view scale (zoom) immediately, centered on graph content.
    // zoom > 1 zooms in, zoom < 1 zooms out. Deterministic (no animation, no
    // mouse input) - used to drive headless zoom-quality verification (#251).
    void SetZoom(float zoom);

    NodeId GetHoveredNode();
    PinId GetHoveredPin();
    LinkId GetHoveredLink();
    NodeId GetDoubleClickedNode();
    PinId GetDoubleClickedPin();
    LinkId GetDoubleClickedLink();
    bool IsBackgroundClicked();
    bool IsBackgroundDoubleClicked();
    ImGuiMouseButton GetBackgroundClickButtonIndex(); // -1 if none
    ImGuiMouseButton GetBackgroundDoubleClickButtonIndex(); // -1 if none

    bool GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId); // pass nullptr if particular pin do not interest you

    // erhe: link routing mid points (canvas space, ordered from start pin to
    // end pin). Interactively edited on the canvas: double-click on the link
    // adds one at the click position, double-click on a handle removes it,
    // dragging a handle moves it. These accessors expose them for scripting
    // and persistence (clipboard).
    int    GetLinkMidPointCount(LinkId linkId);
    ImVec2 GetLinkMidPoint(LinkId linkId, int index);
    void   SetLinkMidPoints(LinkId linkId, const ImVec2* points, int count); // resets every point to Auto tangents

    // erhe: pen-tool tangent handles per mid point. Mode: 0 = Auto (tangents
    // computed; the getter returns the effective computed offsets), 1 =
    // Mirrored, 2 = Aligned, 3 = Free. Tangents are canvas-space offsets from
    // the mid point: tanOut along the outgoing segment, tanIn along the
    // incoming one. Setting mode 0 clears the stored tangents; the setter is
    // a no-op for an out-of-range index (size the list with SetLinkMidPoints
    // first). Interactively: dots show on a selected link; dragging an Auto
    // point's dot captures the computed tangents and switches it to Mirrored,
    // Alt-drag breaks it to Free, double-click on a dot resets to Auto.
    int  GetLinkMidPointMode(LinkId linkId, int index);
    void GetLinkMidPointTangents(LinkId linkId, int index, ImVec2* tanIn, ImVec2* tanOut);
    void SetLinkMidPointTangents(LinkId linkId, int index, int mode, const ImVec2& tanIn, const ImVec2& tanOut);

    // erhe: per-link curve shape parameters (Kochanek-Bartels tension /
    // continuity / bias, each clamped to [-1, 1]; all default 0, which is
    // the standard routing). Tension scales the tangent lengths (+1 makes
    // the link a polyline), continuity and bias reshape the tangents at the
    // routing mid points. Pass nullptr for outputs you do not need.
    void GetLinkCurveParams(LinkId linkId, float* tension, float* continuity, float* bias);
    void SetLinkCurveParams(LinkId linkId, float tension, float continuity, float bias);

    bool PinHadAnyLinks(PinId pinId);

    ImVec2 GetScreenSize();
    ImVec2 ScreenToCanvas(const ImVec2& pos);
    ImVec2 CanvasToScreen(const ImVec2& pos);

    // The canvas widget rectangle in SCREEN space. Node content that draws
    // outside its own ImGui item - and therefore has to escape that item's clip
    // rect - must clip to this instead, or it draws over whatever surrounds the
    // canvas (erhe: the pin sockets straddle the node border).
    void GetCanvasScreenRect(ImVec2& min, ImVec2& max);

    int GetNodeCount();                                // Returns number of submitted nodes since Begin() call
    int GetOrderedNodeIds(NodeId* nodes, int size);    // Fills an array with node id's in order they're drawn; up to 'size` elements are set. Returns actual size of filled id's.

private:
    std::unique_ptr<ax::NodeEditor::Detail::EditorContext> m_impl;
};








//------------------------------------------------------------------------------
namespace Details {

template <typename T, typename Tag>
struct SafeType
{
    SafeType(T t)
        : m_Value(std::move(t))
    {
    }

    SafeType(const SafeType&) = default;

    template <typename T2, typename Tag2>
    SafeType(
        const SafeType
        <
            typename std::enable_if<!std::is_same<T, T2>::value, T2>::type,
            typename std::enable_if<!std::is_same<Tag, Tag2>::value, Tag2>::type
        >&) = delete;

    SafeType& operator=(const SafeType&) = default;

    explicit operator T() const { return Get(); }

    T Get() const { return m_Value; }

private:
    T m_Value;
};


template <typename Tag>
struct SafePointerType
    : SafeType<uintptr_t, Tag>
{
    static const Tag Invalid;

    using SafeType<uintptr_t, Tag>::SafeType;

    SafePointerType()
        : SafePointerType(Invalid)
    {
    }

    template <typename T = void> explicit SafePointerType(T* ptr): SafePointerType(reinterpret_cast<uintptr_t>(ptr)) {}
    template <typename T = void> T* AsPointer() const { return reinterpret_cast<T*>(this->Get()); }

    explicit operator bool() const { return *this != Invalid; }
};

template <typename Tag>
const Tag SafePointerType<Tag>::Invalid = { 0 };

template <typename Tag>
inline bool operator==(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() == rhs.Get();
}

template <typename Tag>
inline bool operator!=(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() != rhs.Get();
}

} // namespace Details

struct NodeId final: Details::SafePointerType<NodeId>
{
    using SafePointerType::SafePointerType;
};

struct LinkId final: Details::SafePointerType<LinkId>
{
    using SafePointerType::SafePointerType;
};

struct PinId final: Details::SafePointerType<PinId>
{
    using SafePointerType::SafePointerType;
};


//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax


//------------------------------------------------------------------------------
# endif // __IMGUI_NODE_EDITOR_H__
