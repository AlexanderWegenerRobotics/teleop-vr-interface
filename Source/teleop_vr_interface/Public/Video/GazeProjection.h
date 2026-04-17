#pragma once

#include "CoreMinimal.h"
#include "UI/GazeComponent.h"   // FGazeData

/**
 * FGazeProjection
 *
 * Projects a world-space gaze ray onto a camera-fixed stereo layer quad
 * and returns the hit as a normalised UV coordinate [0,1]².
 *
 * The quad is assumed to be:
 *   - Attached to the camera, centred on the forward axis
 *   - At a fixed distance PlaneDistance along the camera X axis (UE forward)
 *   - Width  = QuadWidth  UE units  (horizontal)
 *   - Height = QuadHeight UE units  (vertical)
 *
 * This mirrors exactly the geometry in VideoFeedComponent::UpdateLayerSize()
 * and UWidgetBinder::ProjectGazeToUV().
 */
struct FGazeProjection
{
    /**
     * Project gaze onto the video quad.
     *
     * @param GazeData      Current gaze (world-space origin + direction)
     * @param CameraWorld   World transform of the VR camera component
     * @param PlaneDistance Forward distance of the quad from camera (UE units)
     * @param QuadWidth     World-space width of the quad  (UE units)
     * @param QuadHeight    World-space height of the quad (UE units)
     * @param OutUV         Normalised UV [0,1]² if hit, unchanged on miss
     * @return true if the gaze ray intersects the quad
     */
    static bool Project(
        const FGazeData&    GazeData,
        const FTransform&   CameraWorld,
        float               PlaneDistance,
        float               QuadWidth,
        float               QuadHeight,
        FVector2D&          OutUV)
    {
        if (!GazeData.bIsValid || GazeData.Confidence <= 0.0f)
            return false;

        // Transform gaze into camera-local space
        FTransform CamInv = CameraWorld.Inverse();
        FVector LocalOrigin = CamInv.TransformPosition(GazeData.Origin);
        FVector LocalDir    = CamInv.TransformVectorNoScale(GazeData.Direction)
                                    .GetSafeNormal();

        // Camera forward = +X in UE camera space
        if (FMath::IsNearlyZero(LocalDir.X))
            return false;

        float T = (PlaneDistance - LocalOrigin.X) / LocalDir.X;
        if (T < 0.0f)
            return false;

        // Hit point on the quad plane (Y = right, Z = up in camera space)
        float HitY =  LocalOrigin.Y + LocalDir.Y * T; // +Y = right
        float HitZ =  LocalOrigin.Z + LocalDir.Z * T; // +Z = up

        // Map to UV: centre of quad is (0.5, 0.5)
        // U increases left-to-right, V increases top-to-bottom
        float U =  (HitY / QuadWidth)  + 0.5f;
        float V = -(HitZ / QuadHeight) + 0.5f;

        if (U < 0.0f || U > 1.0f || V < 0.0f || V > 1.0f)
            return false;   // gaze is outside the video quad

        OutUV = FVector2D(U, V);
        return true;
    }

    /**
     * Convenience: compute quad dimensions from the same FOV formula used
     * by VideoFeedComponent::UpdateLayerSize().
     *
     * Call once in OperatorPawn::BeginPlay() so both VideoFeedComponent and
     * VideoLogger agree on the same geometry.
     */
    static void ComputeQuadSize(
        float PlaneDistance,
        float FOVCoverage,
        float AspectRatio,   // Width / Height of the video stream
        float& OutQuadWidth,
        float& OutQuadHeight)
    {
        float HalfFOVRad = FMath::DegreesToRadians(110.0f * 0.5f * FOVCoverage);
        float HalfWidth  = PlaneDistance * FMath::Tan(HalfFOVRad);
        OutQuadWidth     = HalfWidth * 2.0f;
        OutQuadHeight    = OutQuadWidth / AspectRatio;
    }
};
