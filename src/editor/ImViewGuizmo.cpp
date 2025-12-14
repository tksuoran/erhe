#include "ImViewGuizmo.h"

//#define GLM_ENABLE_EXPERIMENTAL

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include "imgui/imgui_internal.h"

#include <array>

namespace ImViewGuizmo {

static constexpr float baseSize = 256.f;
static constexpr glm::vec3 origin      { 0.0f, 0.0f, 0.0f};
static constexpr glm::vec3 worldRight  { 1.0f, 0.0f, 0.0f}; // +X
static constexpr glm::vec3 worldUp     { 0.0f, 1.0f, 0.0f}; // +Y
static constexpr glm::vec3 worldForward{ 0.0f, 0.0f,-1.0f}; // -Z
static constexpr glm::vec3 dirXPos     { 1.0f, 0.0f, 0.0f};
static constexpr glm::vec3 dirXNeg     {-1.0f, 0.0f, 0.0f};
static constexpr glm::vec3 dirYPos     { 0.0f, 1.0f, 0.0f};
static constexpr glm::vec3 dirYNeg     { 0.0f,-1.0f, 0.0f};
static constexpr glm::vec3 dirZPos     { 0.0f, 0.0f, 1.0f};
static constexpr glm::vec3 dirZNeg     { 0.0f, 0.0f,-1.0f};
static constexpr glm::vec3 axisVectors[3] = {dirXPos, dirYPos, dirZPos};
static const std::array<glm::quat, 6> orientations = {
    glm::quatLookAt(dirXPos, dirYPos),
    glm::quatLookAt(dirXNeg, dirYPos),
    glm::quatLookAt(dirYPos, dirZNeg),
    glm::quatLookAt(dirYNeg, dirZNeg),
    glm::quatLookAt(dirZPos, dirYPos),
    glm::quatLookAt(dirZNeg, dirYPos)
};

auto Context::IsHoveringGizmo() const -> bool
{
    return m_hoveredAxisID != -1;
}

void Context::Reset()
{ 
    m_hoveredAxisID       = -1; 
    m_isZoomButtonHovered = false;
    m_isPanButtonHovered  = false;
    m_activeTool          = TOOL_NONE;
}

auto Context::IsUsing() const -> bool
{
    return m_activeTool != TOOL_NONE;
}

auto Context::IsOver() const -> bool
{
    return (m_hoveredAxisID != -1) || m_isZoomButtonHovered || m_isPanButtonHovered;
}

// Internal function to reset hover states once per frame
void Context::BeginFrame()
{
    int currentFrame = ImGui::GetFrameCount();
    if (m_lastFrame != currentFrame) {
        m_lastFrame           = currentFrame;
        // Reset hover states, but keep active tool state
        m_hoveredAxisID       = -1;
        m_isZoomButtonHovered = false;
        m_isPanButtonHovered  = false;
    }
}

bool Context::Rotate(int64_t timeNs, glm::vec3& cameraPos, glm::quat& cameraRot, ImVec2 position, float snapDistance, float rotationSpeed)
{
    BeginFrame();

    ImGuiIO&    io          = ImGui::GetIO();
    ImDrawList* drawList    = ImGui::GetWindowDrawList();
    Style&      style       = GetStyle();
    bool        wasModified = false;

    // Global release check
    if (!ImGui::IsMouseDown(0) && m_activeTool != TOOL_NONE) {
        m_activeTool = TOOL_NONE;
    }

    // Animation logic
    if (m_isAnimating) {
        double elapsedTimeNs = static_cast<double>(timeNs - m_animationStartTimeNs);
        double t = std::min(1.0, elapsedTimeNs / style.snapAnimationDurationNs);
        t = 1.0 - (1.0 - t) * (1.0 - t);

        cameraRot = glm::slerp(m_startRot, m_targetRot, static_cast<float>(t));
        glm::vec3 currentDir = cameraRot * worldForward;
        cameraPos = m_lookAtPos - m_snapDistance * currentDir;
        wasModified = true;

        if (t >= 1.0) {
            glm::vec3 finalPos = m_lookAtPos + m_snapDistance * currentDir;
            cameraPos = m_targetPos;
            cameraRot = m_targetRot;
            m_isAnimating = false;
        }
    }
        
    const float gizmoDiameter         = baseSize * style.scale;
    const float scaledCircleRadius    = style.circleRadius * style.scale;
    const float scaledBigCircleRadius = style.bigCircleRadius * style.scale;
    const float scaledLineWidth       = style.lineWidth * style.scale;
    const float scaledHighlightWidth  = style.highlightWidth * style.scale;
    const float scaledHighlightRadius = (style.circleRadius + 2.0f) * style.scale;
    const float scaledFontSize        = ImGui::GetFontSize() * style.scale * style.labelSize;

    glm::mat4 worldMatrix           = glm::translate(glm::mat4(1.0f), cameraPos) * glm::mat4_cast(cameraRot);
    glm::mat4 viewMatrix            = glm::inverse(worldMatrix);
    glm::mat4 gizmoViewMatrix       = glm::mat4(glm::mat3(viewMatrix));
    glm::mat4 gizmoProjectionMatrix = glm::ortho(-1.f, 1.f, -1.f, 1.f, -100.0f, 100.0f);
    glm::mat4 gizmoMvp              = gizmoProjectionMatrix * gizmoViewMatrix;

    std::vector<GizmoAxis> axes;
    for (int i = 0; i < 3; ++i) {
        axes.push_back({i * 2, i, (gizmoViewMatrix * glm::vec4(axisVectors[i], 0)).z, axisVectors[i]});
        axes.push_back({i * 2 + 1, i, (gizmoViewMatrix * glm::vec4(-axisVectors[i], 0)).z, -axisVectors[i]});
    }

    std::sort(
        axes.begin(), axes.end(),
        [](const GizmoAxis& a, const GizmoAxis& b) {
            return a.depth < b.depth;
        }
    );

    auto worldToScreen = [&](const glm::vec3& worldPos) -> ImVec2 {
        const glm::vec4 clipPos = gizmoMvp * glm::vec4(worldPos, 1.0f);
        if (clipPos.w == 0.0f) {
            return {-FLT_MAX, -FLT_MAX};
        }
        const glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        return {
            position.x + ndc.x * (gizmoDiameter / 2.f),
            position.y - ndc.y * (gizmoDiameter / 2.f)
        };
    };

    if (m_activeTool == TOOL_NONE && !m_isAnimating) {
        const float halfGizmoSize  = gizmoDiameter / 2.f;
        ImVec2      mousePos       = io.MousePos;
        float       distToCenterSq = ImLengthSqr(ImVec2(mousePos.x - position.x, mousePos.y - position.y));

        if (distToCenterSq < (halfGizmoSize + scaledCircleRadius) * (halfGizmoSize + scaledCircleRadius)) {
            const float minDistanceSq = scaledCircleRadius * scaledCircleRadius;
            for (const auto& axis : axes) {
                if (axis.depth < -0.1f) {
                    continue;
                }
                    
                ImVec2 handlePos = worldToScreen(axis.direction * style.lineLength);
                if (ImLengthSqr(ImVec2(handlePos.x - mousePos.x, handlePos.y - mousePos.y)) < minDistanceSq) {
                    m_hoveredAxisID = axis.id;
                }
            }
            if (m_hoveredAxisID == -1) {
                ImVec2 centerPos = worldToScreen(origin);
                if (ImLengthSqr(ImVec2(centerPos.x - mousePos.x, centerPos.y - mousePos.y)) < scaledBigCircleRadius * scaledBigCircleRadius) {
                    m_hoveredAxisID = 6;
                }
            }
        }
    }

    if (m_hoveredAxisID == 6 || m_activeTool == TOOL_GIZMO) {
        drawList->AddCircleFilled(worldToScreen(origin), scaledBigCircleRadius, style.bigCircleColor);
    }

    for (const auto& [id, axis_index, depth, direction] : axes) {
        // Color
        float  factor      = glm::mix(style.fadeFactor, 1.0f, (depth + 1.0f) * 0.5f);
        ImVec4 baseColor   = ImGui::ColorConvertU32ToFloat4(style.axisColors[axis_index]);
        ImVec4 fadedColor  = ImVec4(baseColor.x, baseColor.y, baseColor.z, baseColor.w * factor);
        ImU32  final_color = ImGui::ColorConvertFloat4ToU32(fadedColor);

        // Screen Positions
        ImVec2 originPos = worldToScreen(origin);
        ImVec2 handlePos = worldToScreen(direction * style.lineLength);

        // Line stop at circle edge
        ImVec2 lineDir    = ImVec2(handlePos.x - originPos.x, handlePos.y - originPos.y);
        float  lineLength = sqrtf(lineDir.x * lineDir.x + lineDir.y * lineDir.y) + 1e-6f; // Avoid division by zero
        lineDir.x /= lineLength;
        lineDir.y /= lineLength;
        ImVec2 lineEndPos = ImVec2(handlePos.x - lineDir.x * scaledCircleRadius, handlePos.y - lineDir.y * scaledCircleRadius);
            
        // Drawing
        drawList->AddLine(originPos, lineEndPos, final_color, scaledLineWidth); // Use the new endpoint
        drawList->AddCircleFilled(handlePos, scaledCircleRadius, final_color); // Circle remains at the original position

        // Highlight
        if (m_hoveredAxisID == id) {
            drawList->AddCircle(handlePos, scaledHighlightRadius, style.highlightColor, 0, scaledHighlightWidth);
        }
    }

    ImFont* font = ImGui::GetFont();
    for (const auto& axis : axes) {
        if (axis.depth < -0.1f) {
            continue;
        }
        ImVec2      textPos  = worldToScreen(axis.direction * style.lineLength);
        const char* label    = style.axisLabels[axis.axisIndex];
        const bool  isPos    = (axis.id & 1) == 0;
        ImVec2      textSize = font->CalcTextSizeA(scaledFontSize, FLT_MAX, 0.f, label);
        drawList->AddText(font, scaledFontSize,{textPos.x - textSize.x * 0.5f, textPos.y - textSize.y * 0.5f}, isPos ? style.labelColorPos : style.labelColorNeg, label);
    }

    // Drag logic
    if (ImGui::IsMouseDown(0)) {
        if (m_activeTool == TOOL_NONE && m_hoveredAxisID == 6) {
            m_activeTool = TOOL_GIZMO;
            m_isAnimating = false;
        }
    }

    if (m_activeTool == TOOL_GIZMO) {
        float     yawAngle      = -io.MouseDelta.x * rotationSpeed;
        float     pitchAngle    = -io.MouseDelta.y * rotationSpeed;
        glm::quat yawRotation   = glm::angleAxis(yawAngle, worldUp);
        glm::vec3 rightAxis     = cameraRot * worldRight;
        glm::quat pitchRotation = angleAxis(pitchAngle, rightAxis);
        glm::quat totalRotation = yawRotation * pitchRotation;
        cameraPos = totalRotation * cameraPos;
        cameraRot = totalRotation * cameraRot;
        wasModified = true;
    }

    // Snap
    if (ImGui::IsMouseReleased(0) && (m_hoveredAxisID >= 0) && (m_hoveredAxisID <= 5) && !ImGui::IsMouseDragging(0)) {
        glm::quat targetRotation = orientations[m_hoveredAxisID];
        glm::vec3 targetDir      = targetRotation * glm::vec3{0.0f, 0.0f, 1.0f};
        glm::vec3 cameraForward  = cameraRot * worldForward;
        glm::vec3 lookAtPosition = cameraPos + cameraForward * snapDistance;
        glm::vec3 targetPosition = lookAtPosition + snapDistance * targetDir;

        if (style.animateSnap && (style.snapAnimationDurationNs > 0.0)) {
            bool pos_is_different = glm::length2(cameraPos - targetPosition) > 0.0001f;
            bool rot_is_different = (1.0f - glm::abs(glm::dot(cameraRot, targetRotation))) > 0.0001f;

            if (pos_is_different || rot_is_different) {
                m_isAnimating          = true;
                m_animationStartTimeNs = timeNs;
                m_startPos             = cameraPos;
                m_targetPos            = targetPosition;
                m_lookAtPos            = lookAtPosition;
                m_snapDistance         = snapDistance;
                m_startRot             = glm::normalize(cameraRot);
                m_targetRot            = glm::normalize(targetRotation);
            }
        } else {
            cameraRot = targetRotation;
            cameraPos = targetPosition;
            wasModified = true;
        }
    }

    return wasModified;
}

bool Context::Zoom(glm::vec3& cameraPos, const glm::quat& cameraRot, ImVec2 position, float zoomSpeed)
{
    BeginFrame();

    ImGuiIO&    io          = ImGui::GetIO();
    ImDrawList* drawList    = ImGui::GetWindowDrawList();
    Style&      style       = GetStyle();
    bool        wasModified = false;

    const float  radius = style.toolButtonRadius * style.scale;
    const ImVec2 center = { position.x + radius, position.y + radius };
        
    bool isHovered = false;
    if (m_activeTool == TOOL_NONE || m_activeTool == TOOL_ZOOM) {
        if (ImLengthSqr(ImVec2(io.MousePos.x - center.x, io.MousePos.y - center.y)) < radius * radius) {
            isHovered = true;
        }
    }
        
    m_isZoomButtonHovered = isHovered;

    // Start Drag
    if (isHovered && ImGui::IsMouseDown(0) && m_activeTool == TOOL_NONE) {
        m_activeTool = TOOL_ZOOM;
        m_isAnimating = false;
    }

    // Perform Zoom
    if (m_activeTool == TOOL_ZOOM) {
        if (io.MouseDelta.y != 0.0f) {
            // Use the camera's local forward vector for zooming
            glm::vec3 cameraForward = cameraRot * worldForward;
            cameraPos += cameraForward * -io.MouseDelta.y * zoomSpeed;
            wasModified = true;
        }
    }
        
    // Draw
    ImU32 bgColor = style.toolButtonColor;
    if (m_activeTool == TOOL_ZOOM || isHovered) {
        bgColor = style.toolButtonHoveredColor;
    }
    drawList->AddCircleFilled(center, radius, bgColor);

    const float p         = style.toolButtonInnerPadding * style.scale;
    const float th        = 2.0f * style.scale;
    const ImU32 iconColor = style.toolButtonIconColor;

    constexpr float iconScale = 0.5f; 
    // Magnifying glass circle
    ImVec2 glassCenter = { center.x - (p / 2.0f) * iconScale, center.y - (p / 2.0f) * iconScale };
    float  glassRadius = (radius - p - 1.f) * iconScale;
    drawList->AddCircle(glassCenter, glassRadius, iconColor, 0, th);

    // Handle
    ImVec2 handleStart = { center.x + (radius / 2.0f) * iconScale, center.y + (radius / 2.0f) * iconScale };
    ImVec2 handleEnd   = { center.x + (radius - p) * iconScale, center.y + (radius - p) * iconScale };
    drawList->AddLine(handleStart, handleEnd, iconColor, th);

    // Plus sign (vertical)
    ImVec2 plusVertStart = { center.x - (p / 2.0f) * iconScale, center.y - (radius / 2.0f) * iconScale };
    ImVec2 plusVertEnd   = { center.x - (p / 2.0f) * iconScale, center.y + (radius / 2.0f - p) * iconScale };
    drawList->AddLine(plusVertStart, plusVertEnd, iconColor, th);

    // Plus sign (horizontal)
    ImVec2 plusHorizStart = { center.x + (-radius / 2.0f + p / 2.0f) * iconScale, center.y - (p / 2.0f) * iconScale };
    ImVec2 plusHorizEnd   = { center.x + (radius / 2.0f - p * 1.5f) * iconScale, center.y - (p / 2.0f) * iconScale };
    drawList->AddLine(plusHorizStart, plusHorizEnd, iconColor, th);
        
    return wasModified;
}
    
bool Context::Pan(glm::vec3& cameraPos, const glm::quat& cameraRot, ImVec2 position, float panSpeed)
{
    BeginFrame();

    ImGuiIO&    io          = ImGui::GetIO();
    ImDrawList* drawList    = ImGui::GetWindowDrawList();
    Style&      style       = GetStyle();
    bool        wasModified = false;

    const float  radius = style.toolButtonRadius * style.scale;
    const ImVec2 center = { position.x + radius, position.y + radius };

    // Interaction Logic 
    bool isHovered = false;
    if (m_activeTool == TOOL_NONE || m_activeTool == TOOL_PAN) {
        if (ImLengthSqr(ImVec2(io.MousePos.x - center.x, io.MousePos.y - center.y)) < radius * radius) {
            isHovered = true;
        }
    }
    m_isPanButtonHovered = isHovered;
        
    // Start Drag
    if (isHovered && ImGui::IsMouseDown(0) && m_activeTool == TOOL_NONE) {
        m_activeTool = TOOL_PAN;
        m_isAnimating = false;
    }

    // Perform Pan
    if (m_activeTool == TOOL_PAN) {
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            // Pan along the camera's local axes with natural "drag" controls
            cameraPos += cameraRot * worldRight * -io.MouseDelta.x * panSpeed; // Inverted horizontal
            cameraPos += cameraRot * worldUp * io.MouseDelta.y * panSpeed;    // Natural vertical
            wasModified = true;
        }
    }

    // Drawing 

    // Draw the background circle
    ImU32 bgColor = style.toolButtonColor;
    if (m_activeTool == TOOL_PAN ||isHovered) {
        bgColor = style.toolButtonHoveredColor;
    }
    drawList->AddCircleFilled(center, radius, bgColor);

    // Draw the icon on top of the background
    const ImU32 iconColor = style.toolButtonIconColor;
    const float th        = 2.0f * style.scale; // Use scaled thickness for consistency

    // Four-way arrow symbol
    const float size = radius * 0.5f;
    const float arm  = size * 0.25f; // 1/2 gap
    // Top Arrow (^) 
    const ImVec2 topTip = { center.x, center.y - size };
    drawList->AddLine({ topTip.x - arm, topTip.y + arm }, topTip, iconColor, th);
    drawList->AddLine({ topTip.x + arm, topTip.y + arm }, topTip, iconColor, th);
    // Bottom Arrow (v) 
    const ImVec2 botTip = { center.x, center.y + size };
    drawList->AddLine({ botTip.x - arm, botTip.y - arm }, botTip, iconColor, th);
    drawList->AddLine({ botTip.x + arm, botTip.y - arm }, botTip, iconColor, th);
    // Left Arrow (<) 
    const ImVec2 leftTip = { center.x - size, center.y };
    drawList->AddLine({ leftTip.x + arm, leftTip.y - arm }, leftTip, iconColor, th);
    drawList->AddLine({ leftTip.x + arm, leftTip.y + arm }, leftTip, iconColor, th);
    // Right Arrow (>) 
    const ImVec2 rightTip = { center.x + size, center.y };
    drawList->AddLine({ rightTip.x - arm, rightTip.y - arm }, rightTip, iconColor, th);
    drawList->AddLine({ rightTip.x - arm, rightTip.y + arm }, rightTip, iconColor, th);
        
    return wasModified;
}

} // namespace ImViewGuizmo
