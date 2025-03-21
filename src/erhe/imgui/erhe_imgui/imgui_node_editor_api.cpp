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
# include "erhe_imgui/imgui_node_editor_internal.h"
# include <algorithm>


namespace ax::NodeEditor {

//------------------------------------------------------------------------------
template <typename C, typename I, typename F>
static int BuildIdList(C& container, I* list, int listSize, F&& accept)
{
    if (list != nullptr)
    {
        int count = 0;
        for (auto object : container)
        {
            if (listSize <= 0)
                break;

            if (accept(object))
            {
                list[count] = I(object->ID().AsPointer());
                ++count;
                --listSize;}
        }

        return count;
    }
    else
        return static_cast<int>(std::count_if(container.begin(), container.end(), accept));
}


//------------------------------------------------------------------------------
EditorContext::EditorContext(const Config* config)
    : m_impl{std::make_unique<Detail::EditorContext>(config)}
{
}

EditorContext::~EditorContext()
{
}

//const Config& EditorContext::GetConfig()
//{
//    return m_impl->GetConfig();
//}

Style& EditorContext::GetStyle()
{
    return m_impl->GetStyle();
}

const char* EditorContext::GetStyleColorName(StyleColor colorIndex)
{
    return m_impl->GetStyle().GetColorName(colorIndex);
}

void EditorContext::PushStyleColor(StyleColor colorIndex, const ImVec4& color)
{
    m_impl->GetStyle().PushColor(colorIndex, color);
}

void EditorContext::PopStyleColor(int count)
{
    m_impl->GetStyle().PopColor(count);
}

void EditorContext::PushStyleVar(StyleVar varIndex, float value)
{
    m_impl->GetStyle().PushVar(varIndex, value);
}

void EditorContext::PushStyleVar(StyleVar varIndex, const ImVec2& value)
{
    m_impl->GetStyle().PushVar(varIndex, value);
}

void EditorContext::PushStyleVar(StyleVar varIndex, const ImVec4& value)
{
    m_impl->GetStyle().PushVar(varIndex, value);
}

void EditorContext::PopStyleVar(int count)
{
    m_impl->GetStyle().PopVar(count);
}

void EditorContext::Begin(const char* id, const ImVec2& size)
{
    m_impl->Begin(id, size);
}

void EditorContext::End()
{
    m_impl->End();
}

void EditorContext::BeginNode(NodeId id)
{
    m_impl->GetNodeBuilder().Begin(id);
}

void EditorContext::BeginPin(PinId id, PinKind kind)
{
    m_impl->GetNodeBuilder().BeginPin(id, kind);
}

void EditorContext::PinRect(const ImVec2& a, const ImVec2& b)
{
    m_impl->GetNodeBuilder().PinRect(a, b);
}

void EditorContext::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    m_impl->GetNodeBuilder().PinPivotRect(a, b);
}

void EditorContext::PinPivotSize(const ImVec2& size)
{
    m_impl->GetNodeBuilder().PinPivotSize(size);
}

void EditorContext::PinPivotScale(const ImVec2& scale)
{
    m_impl->GetNodeBuilder().PinPivotScale(scale);
}

void EditorContext::PinPivotAlignment(const ImVec2& alignment)
{
    m_impl->GetNodeBuilder().PinPivotAlignment(alignment);
}

void EditorContext::EndPin()
{
    m_impl->GetNodeBuilder().EndPin();
}

void EditorContext::Group(const ImVec2& size)
{
    m_impl->GetNodeBuilder().Group(size);
}

void EditorContext::EndNode()
{
    m_impl->GetNodeBuilder().End();
}

bool EditorContext::BeginGroupHint(NodeId nodeId)
{
    return m_impl->GetHintBuilder().Begin(nodeId);
}

ImVec2 EditorContext::GetGroupMin()
{
    return m_impl->GetHintBuilder().GetGroupMin();
}

ImVec2 EditorContext::GetGroupMax()
{
    return m_impl->GetHintBuilder().GetGroupMax();
}

ImDrawList* EditorContext::GetHintForegroundDrawList()
{
    return m_impl->GetHintBuilder().GetForegroundDrawList();
}

ImDrawList* EditorContext::GetHintBackgroundDrawList()
{
    return m_impl->GetHintBuilder().GetBackgroundDrawList();
}

void EditorContext::EndGroupHint()
{
    m_impl->GetHintBuilder().End();
}

ImDrawList* EditorContext::GetNodeBackgroundDrawList(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        return m_impl->GetNodeBuilder().GetUserBackgroundDrawList(node);
    else
        return nullptr;
}

bool EditorContext::Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, float thickness/* = 1.0f*/)
{
    return m_impl->DoLink(id, startPinId, endPinId, ImColor(color), thickness);
}

void EditorContext::Flow(LinkId linkId, FlowDirection direction)
{
    if (auto link = m_impl->FindLink(linkId))
        m_impl->Flow(link, direction);
}

bool EditorContext::BeginCreate(const ImVec4& color, float thickness)
{
    auto& context = m_impl->GetItemCreator();

    if (context.Begin())
    {
        context.SetStyle(ImColor(color), thickness);
        return true;
    }
    else
        return false;
}

bool EditorContext::QueryNewLink(PinId* startId, PinId* endId)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    return context.QueryLink(startId, endId) == Result::True;
}

bool EditorContext::QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    auto result = context.QueryLink(startId, endId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool EditorContext::QueryNewNode(PinId* pinId)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    return context.QueryNode(pinId) == Result::True;
}

bool EditorContext::QueryNewNode(PinId* pinId, const ImVec4& color, float thickness)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    auto result = context.QueryNode(pinId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool EditorContext::AcceptNewItem()
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    return context.AcceptItem() == Result::True;
}

bool EditorContext::AcceptNewItem(const ImVec4& color, float thickness)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    auto result = context.AcceptItem();
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

void EditorContext::RejectNewItem()
{
    auto& context = m_impl->GetItemCreator();

    context.RejectItem();
}

void EditorContext::RejectNewItem(const ImVec4& color, float thickness)
{
    using Result = Detail::CreateItemAction::Result;

    auto& context = m_impl->GetItemCreator();

    if (context.RejectItem() != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);
}

void EditorContext::EndCreate()
{
    auto& context = m_impl->GetItemCreator();

    context.End();
}

bool EditorContext::BeginDelete()
{
    auto& context = m_impl->GetItemDeleter();

    return context.Begin();
}

bool EditorContext::QueryDeletedLink(LinkId* linkId, PinId* startId, PinId* endId)
{
    auto& context = m_impl->GetItemDeleter();

    return context.QueryLink(linkId, startId, endId);
}

bool EditorContext::QueryDeletedNode(NodeId* nodeId)
{
    auto& context = m_impl->GetItemDeleter();

    return context.QueryNode(nodeId);
}

bool EditorContext::AcceptDeletedItem(bool deleteDependencies)
{
    auto& context = m_impl->GetItemDeleter();

    return context.AcceptItem(deleteDependencies);
}

void EditorContext::RejectDeletedItem()
{
    auto& context = m_impl->GetItemDeleter();

    context.RejectItem();
}

void EditorContext::EndDelete()
{
    auto& context = m_impl->GetItemDeleter();

    context.End();
}

void EditorContext::SetNodePosition(NodeId nodeId, const ImVec2& position)
{
    m_impl->SetNodePosition(nodeId, position);
}

void EditorContext::SetGroupSize(NodeId nodeId, const ImVec2& size)
{
    m_impl->SetGroupSize(nodeId, size);
}

ImVec2 EditorContext::GetNodePosition(NodeId nodeId)
{
    return m_impl->GetNodePosition(nodeId);
}

ImVec2 EditorContext::GetNodeSize(NodeId nodeId)
{
    return m_impl->GetNodeSize(nodeId);
}

void EditorContext::CenterNodeOnScreen(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        node->CenterOnScreenInNextFrame();
}

void EditorContext::SetNodeZPosition(NodeId nodeId, float z)
{
    m_impl->SetNodeZPosition(nodeId, z);
}

float EditorContext::GetNodeZPosition(NodeId nodeId)
{
    return m_impl->GetNodeZPosition(nodeId);
}

void EditorContext::RestoreNodeState(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        m_impl->MarkNodeToRestoreState(node);
}

void EditorContext::Suspend()
{
    m_impl->Suspend();
}

void EditorContext::Resume()
{
    m_impl->Resume();
}

bool EditorContext::IsSuspended()
{
    return m_impl->IsSuspended();
}

bool EditorContext::IsActive()
{
    return m_impl->IsFocused();
}

bool EditorContext::HasSelectionChanged()
{
    return m_impl->HasSelectionChanged();
}

int EditorContext::GetSelectedObjectCount()
{
    return (int)m_impl->GetSelectedObjects().size();
}

int EditorContext::GetSelectedNodes(NodeId* nodes, int size)
{
    return BuildIdList(m_impl->GetSelectedObjects(), nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int EditorContext::GetSelectedLinks(LinkId* links, int size)
{
    return BuildIdList(m_impl->GetSelectedObjects(), links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

bool EditorContext::IsNodeSelected(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        return m_impl->IsSelected(node);
    else
        return false;
}

bool EditorContext::IsLinkSelected(LinkId linkId)
{
    if (auto link = m_impl->FindLink(linkId))
        return m_impl->IsSelected(link);
    else
        return false;
}

void EditorContext::ClearSelection()
{
    m_impl->ClearSelection();
}

void EditorContext::SelectNode(NodeId nodeId, bool append)
{
    if (auto node = m_impl->FindNode(nodeId))
    {
        if (append)
            m_impl->SelectObject(node);
        else
            m_impl->SetSelectedObject(node);
    }
}

void EditorContext::SelectLink(LinkId linkId, bool append)
{
    if (auto link = m_impl->FindLink(linkId))
    {
        if (append)
            m_impl->SelectObject(link);
        else
            m_impl->SetSelectedObject(link);
    }
}

void EditorContext::DeselectNode(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        m_impl->DeselectObject(node);
}

void EditorContext::DeselectLink(LinkId linkId)
{
    if (auto link = m_impl->FindLink(linkId))
        m_impl->DeselectObject(link);
}

bool EditorContext::DeleteNode(NodeId nodeId)
{
    if (auto node = m_impl->FindNode(nodeId))
        return m_impl->GetItemDeleter().Add(node);
    else
        return false;
}

bool EditorContext::DeleteLink(LinkId linkId)
{
    if (auto link = m_impl->FindLink(linkId))
        return m_impl->GetItemDeleter().Add(link);
    else
        return false;
}

bool EditorContext::HasAnyLinks(NodeId nodeId)
{
    return m_impl->HasAnyLinks(nodeId);
}

bool EditorContext::HasAnyLinks(PinId pinId)
{
    return m_impl->HasAnyLinks(pinId);
}

int EditorContext::BreakLinks(NodeId nodeId)
{
    return m_impl->BreakLinks(nodeId);
}

int EditorContext::BreakLinks(PinId pinId)
{
    return m_impl->BreakLinks(pinId);
}

void EditorContext::NavigateToContent(float duration)
{
    m_impl->NavigateTo(m_impl->GetContentBounds(), true, duration);
}

void EditorContext::NavigateToSelection(bool zoomIn, float duration)
{
    m_impl->NavigateTo(m_impl->GetSelectionBounds(), zoomIn, duration);
}

bool EditorContext::ShowNodeContextMenu(NodeId* nodeId)
{
    return m_impl->GetContextMenu().ShowNodeContextMenu(nodeId);
}

bool EditorContext::ShowPinContextMenu(PinId* pinId)
{
    return m_impl->GetContextMenu().ShowPinContextMenu(pinId);
}

bool EditorContext::ShowLinkContextMenu(LinkId* linkId)
{
    return m_impl->GetContextMenu().ShowLinkContextMenu(linkId);
}

bool EditorContext::ShowBackgroundContextMenu()
{
    return m_impl->GetContextMenu().ShowBackgroundContextMenu();
}

void EditorContext::EnableShortcuts(bool enable)
{
    m_impl->EnableShortcuts(enable);
}

bool EditorContext::AreShortcutsEnabled()
{
    return m_impl->AreShortcutsEnabled();
}

bool EditorContext::BeginShortcut()
{
    return m_impl->GetShortcut().Begin();
}

bool EditorContext::AcceptCut()
{
    return m_impl->GetShortcut().AcceptCut();
}

bool EditorContext::AcceptCopy()
{
    return m_impl->GetShortcut().AcceptCopy();
}

bool EditorContext::AcceptPaste()
{
    return m_impl->GetShortcut().AcceptPaste();
}

bool EditorContext::AcceptDuplicate()
{
    return m_impl->GetShortcut().AcceptDuplicate();
}

bool EditorContext::AcceptCreateNode()
{
    return m_impl->GetShortcut().AcceptCreateNode();
}

int EditorContext::GetActionContextSize()
{
    return static_cast<int>(m_impl->GetShortcut().m_Context.size());
}

int EditorContext::GetActionContextNodes(NodeId* nodes, int size)
{
    return BuildIdList(m_impl->GetSelectedObjects(), nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int EditorContext::GetActionContextLinks(LinkId* links, int size)
{
    return BuildIdList(m_impl->GetSelectedObjects(), links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

void EditorContext::EndShortcut()
{
    return m_impl->GetShortcut().End();
}

float EditorContext::GetCurrentZoom()
{
    return m_impl->GetView().InvScale;
}

NodeId EditorContext::GetHoveredNode()
{
    return m_impl->GetHoveredNode();
}

PinId EditorContext::GetHoveredPin()
{
    return m_impl->GetHoveredPin();
}

LinkId EditorContext::GetHoveredLink()
{
    return m_impl->GetHoveredLink();
}

NodeId EditorContext::GetDoubleClickedNode()
{
    return m_impl->GetDoubleClickedNode();
}

PinId EditorContext::GetDoubleClickedPin()
{
    return m_impl->GetDoubleClickedPin();
}

LinkId EditorContext::GetDoubleClickedLink()
{
    return m_impl->GetDoubleClickedLink();
}

bool EditorContext::IsBackgroundClicked()
{
    return m_impl->IsBackgroundClicked();
}

bool EditorContext::IsBackgroundDoubleClicked()
{
    return m_impl->IsBackgroundDoubleClicked();
}

ImGuiMouseButton EditorContext::GetBackgroundClickButtonIndex()
{
    return m_impl->GetBackgroundClickButtonIndex();
}

ImGuiMouseButton EditorContext::GetBackgroundDoubleClickButtonIndex()
{
    return m_impl->GetBackgroundDoubleClickButtonIndex();
}

bool EditorContext::GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId)
{
    auto link = m_impl->FindLink(linkId);
    if (!link)
        return false;

    if (startPinId)
        *startPinId = link->m_StartPin->m_ID;
    if (endPinId)
        *endPinId = link->m_EndPin->m_ID;

    return true;
}

bool EditorContext::PinHadAnyLinks(PinId pinId)
{
    return m_impl->PinHadAnyLinks(pinId);
}

ImVec2 EditorContext::GetScreenSize()
{
    return m_impl->GetRect().GetSize();
}

ImVec2 EditorContext::ScreenToCanvas(const ImVec2& pos)
{
    return m_impl->ToCanvas(pos);
}

ImVec2 EditorContext::CanvasToScreen(const ImVec2& pos)
{
    return m_impl->ToScreen(pos);
}

int EditorContext::GetNodeCount()
{
    return m_impl->CountLiveNodes();
}

int EditorContext::GetOrderedNodeIds(NodeId* nodes, int size)
{
    return m_impl->GetNodeIds(nodes, size);
}

} // namespace ax::NodeEditor
