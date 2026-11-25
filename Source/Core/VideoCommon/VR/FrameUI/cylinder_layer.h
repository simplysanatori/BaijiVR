// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame curve: the screen's quad layer bent as a section of a cylinder (frame_xr/app.cpp
// MakeCylinder), with the quad's place, swapchain, eye and flags.

#pragma once

#include <openxr/openxr.h>

#include "VideoCommon/VR/FrameUI/geometry.h"

namespace FrameXr {

inline XrCompositionLayerCylinderKHR MakeCylinderLayer(const XrCompositionLayerQuad& quad,
                                                       float radius) {
    const auto& [orientation, position] = quad.pose;
    const Input::Cylinder cylinder =
        Input::CylinderFor({{orientation.x, orientation.y, orientation.z, orientation.w},
                            {position.x, position.y, position.z}},
                           quad.size.width, quad.size.height, radius);
    const Input::Vec3f& axis = cylinder.pose.position;

    XrCompositionLayerCylinderKHR layer{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    layer.layerFlags = quad.layerFlags;
    layer.space = quad.space;
    layer.eyeVisibility = quad.eyeVisibility;
    layer.subImage = quad.subImage;
    layer.pose = {orientation, {axis.x, axis.y, axis.z}};
    layer.radius = cylinder.radius;
    layer.centralAngle = cylinder.central_angle;
    layer.aspectRatio = cylinder.aspect_ratio;
    return layer;
}

} // namespace FrameXr
