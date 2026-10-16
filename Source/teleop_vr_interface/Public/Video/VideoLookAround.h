#pragma once

#include "CoreMinimal.h"
#include "Components/StereoLayerComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/UnrealType.h"

struct FVideoLookAround
{
    float MaxYawDeg   = 25.f;
    float MaxPitchDeg = 15.f;
    float KneeDeg     = 5.f;

    bool  bAnchored = false;
    FQuat HomeRot   = FQuat::Identity;

    void Anchor(const FTransform& HeadWorld)
    {
        const FRotator R = HeadWorld.GetRotation().Rotator();
        HomeRot   = FQuat(FRotator(R.Pitch, R.Yaw, 0.f));
        bAnchored = true;
    }

    static float SoftDeadzone(float X, float Bound, float Knee)
    {
        const float B = FMath::Max(Bound, 0.f);
        const float K = FMath::Clamp(Knee, KINDA_SMALL_NUMBER, FMath::Max(B, KINDA_SMALL_NUMBER));
        const float A = FMath::Abs(X);
        float Out = 0.f;
        if (A >= B + K)      Out = A - B;
        else if (A > B - K)  Out = FMath::Square(A - B + K) / (4.f * K);
        return X < 0.f ? -Out : Out;
    }

    FTransform ComputeQuadWorld(const FTransform& HeadWorld, float PlaneDistance, float& OutViewYawDeg, float& OutViewPitchDeg) const
    {
        const FQuat HeadRot = HeadWorld.GetRotation();
        const FRotator Delta = (HomeRot.Inverse() * HeadRot).Rotator();
        const float FollowYaw   = SoftDeadzone(static_cast<float>(Delta.Yaw),   MaxYawDeg,   KneeDeg);
        const float FollowPitch = SoftDeadzone(static_cast<float>(Delta.Pitch), MaxPitchDeg, KneeDeg);
        const FQuat QuadRot = HomeRot * FQuat(FRotator(FollowPitch, FollowYaw, 0.f));

        const FRotator View = (QuadRot.Inverse() * HeadRot).Rotator();
        OutViewYawDeg   = static_cast<float>(View.Yaw);
        OutViewPitchDeg = static_cast<float>(View.Pitch);

        return FTransform(QuadRot, HeadWorld.GetLocation() + QuadRot.RotateVector(FVector(PlaneDistance, 0.f, 0.f)));
    }

    static FTransform FaceLockedQuadWorld(const FTransform& HeadWorld, float PlaneDistance)
    {
        const FTransform Head(HeadWorld.GetRotation(), HeadWorld.GetLocation());
        return FTransform(FQuat::Identity, FVector(PlaneDistance, 0.f, 0.f)) * Head;
    }

    static void SetLayerType(UStereoLayerComponent* Layer, EStereoLayerType Type, int32 Priority)
    {
        if (FByteProperty* Prop = FindFProperty<FByteProperty>(Layer->GetClass(), TEXT("StereoLayerType")))
        {
            Prop->SetPropertyValue_InContainer(Layer, static_cast<uint8>(Type));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("VideoLookAround: StereoLayerType property not found on %s"), *Layer->GetName());
        }
        Layer->SetPriority(Priority + 1);
        Layer->SetPriority(Priority);
    }

    static void ApplyToLayer(UStereoLayerComponent* Layer, bool bWorldLocked, bool bModeChanged,
        const FTransform& QuadWorld, USceneComponent* FaceParent, float PlaneDistance, int32 Priority)
    {
        if (!Layer) return;

        if (bModeChanged)
        {
            if (bWorldLocked)
            {
                Layer->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
                SetLayerType(Layer, SLT_WorldLocked, Priority);
            }
            else
            {
                if (FaceParent)
                    Layer->AttachToComponent(FaceParent, FAttachmentTransformRules::KeepRelativeTransform);
                Layer->SetRelativeLocationAndRotation(FVector(PlaneDistance, 0.f, 0.f), FRotator::ZeroRotator);
                SetLayerType(Layer, SLT_FaceLocked, Priority);
            }
        }

        if (bWorldLocked)
            Layer->SetWorldLocationAndRotation(QuadWorld.GetLocation(), QuadWorld.GetRotation());
    }
};
