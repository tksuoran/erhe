#pragma once

/*===============================================
ImViewGuizmo by Marcel Kazemi
Modified by Timo Suoranta

Copyright (c) 2025 Marcel Kazemi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
======================================================*/

#include "imgui/imgui.h"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <vector>


namespace ImViewGuizmo {

struct Style {
	float scale = 1.f;

	// Axis visuals
	float lineLength     =  0.5f;
	float lineWidth      =  4.0f;
	float circleRadius   = 15.0f;
	float fadeFactor     =  0.25f;

	// Highlight
	ImU32 highlightColor = IM_COL32(255, 255, 0, 255);
	float highlightWidth = 2.0f;

	// Axis
	ImU32 axisColors[3] = {
		IM_COL32(230,  51,  51, 255),  // X
		IM_COL32( 51, 230,  51, 255),  // Y
		IM_COL32( 51, 128, 255, 255)   // Z
	};

	// Labels
	float       labelSize               = 1.0f;
	const char* axisLabels[3]           = {"X", "Y", "Z"};
	ImU32       labelColorPos           =  IM_COL32(255, 255, 255, 255);
	ImU32       labelColorNeg           =  IM_COL32(  0,   0,   0, 255);

	// Big Circle
	float       bigCircleRadius         = 80.0f;
	ImU32       bigCircleColor          = IM_COL32(255, 255, 255, 50);

	// Animation
	bool        animateSnap             = true;
	double      snapAnimationDurationNs = 200'000'000.0; // in nano seconds

	// Zoom/Pan Button Visuals
	float       toolButtonRadius        = 25.0f;
	float       toolButtonInnerPadding  =  4.0f;
	ImU32       toolButtonColor         = IM_COL32(144, 144, 144,  50);
	ImU32       toolButtonHoveredColor  = IM_COL32(215, 215, 215,  50);
	ImU32       toolButtonIconColor     = IM_COL32(215, 215, 215, 225);
};

inline Style& GetStyle() {
	static Style style;
	return style;
}

// Gizmo Axis Struct
struct GizmoAxis {
	int       id;         // 0-5 for (+X,-X,+Y,-Y,+Z,-Z), 6=center
	int       axisIndex;  // 0=X, 1=Y, 2=Z
	float     depth;      // Screen-space depth
	glm::vec3 direction;  // 3D vector
};

enum ActiveTool {
	TOOL_NONE,
	TOOL_GIZMO,
	TOOL_ZOOM,
	TOOL_PAN
};

class Context {
public:
	bool IsHoveringGizmo() const;
	void Reset          ();
	bool IsUsing        () const;
	bool IsOver         () const;
	void BeginFrame     ();
	bool Rotate         (int64_t frameTimeNs, glm::vec3& cameraPos, glm::quat& cameraRot, ImVec2 position, float sensitivity, float focusDistance = 5.f);
	bool Zoom           (glm::vec3& cameraPos, const glm::quat& cameraRot, ImVec2 position, float zoomSpeed = 0.005f);
	bool Pan            (glm::vec3& cameraPos, const glm::quat& cameraRot, ImVec2 position, float panSpeed = 0.001f);

private:
	int        m_hoveredAxisID       = -1;
	bool       m_isZoomButtonHovered = false;
	bool       m_isPanButtonHovered  = false;
	ActiveTool m_activeTool          = TOOL_NONE;

	// Animation state
	bool      m_isAnimating          = false;
	int64_t   m_animationStartTimeNs = 0;
	glm::vec3 m_startPos;
	glm::vec3 m_targetPos;
	glm::vec3 m_lookAtPos;
	float     m_focusDistance = 1.0f;
	glm::quat m_startRot;
	glm::quat m_targetRot;
	int       m_lastFrame = -1;
};

} // namespace ImViewGuizmo


