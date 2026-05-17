#pragma once

namespace RenderConfig
{
    // Camera.
    constexpr float CamEyeX = 0.0f, CamEyeY = 2.2f, CamEyeZ = 3.5f;
    constexpr float CamLookX = 0.0f, CamLookY = 1.0f, CamLookZ = -2.0f;
    constexpr float CamUpX = 0.0f, CamUpY = 1.0f, CamUpZ = 0.0f;
    constexpr float CamFovDeg = 55.0f;

    // Room bounds.
    constexpr float RoomMinX = -5.0f, RoomMaxX = 5.0f;
    constexpr float RoomMinY = 0.0f, RoomMaxY = 5.0f;
    constexpr float RoomMinZ = -6.0f, RoomMaxZ = 6.0f;

    // Light.
    constexpr float LightBaseY = 2.5f;
    constexpr float LightOrbitR = 2.0f;
    constexpr float LightOrbitSpeed = 1.0f;
    constexpr float LightBobAmp = 0.4f;
    constexpr float LightBobFreq = 0.7f;
    constexpr float LightIntensity = 8.0f;
    constexpr float LightRadius = 0.15f;
    constexpr float LightAttenClamp = 0.35f;

    // Shading.
    constexpr float AmbientSkyScale = 0.15f;
    constexpr float AmbientFillR = 0.04f, AmbientFillG = 0.04f, AmbientFillB = 0.05f;
    constexpr float FloorBounceR = 1.0f, FloorBounceG = 0.75f, FloorBounceB = 0.55f;
    constexpr float FloorBounceStr = 0.2f;
    constexpr int TraceDepthPrimary = 2;
    constexpr int TraceDepthSecondary = 2;

    // Ray precision.
    constexpr float RayEpsilon = 1e-3f;
    constexpr float RayTMax = 1e30f;

    // CPU-specific.
    constexpr int CPUTileSize = 64;
}
