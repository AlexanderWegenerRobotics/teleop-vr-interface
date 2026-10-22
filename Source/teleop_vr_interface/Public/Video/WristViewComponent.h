#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UI/GazeComponent.h"
#include "WristViewComponent.generated.h"

class UStereoLayerComponent;
class UCameraComponent;
class UTexture2D;

struct FWristViewSettings
{
	float WidthCm      = 200.f;
	float GapCm        = 40.f;
	float UpCm         = 0.f;
	float FollowTauS   = 0.15f;
	float ExpandScale  = 1.6f;
	float ShrinkDelayS = 0.6f;
	float InsetCm      = 2.f;
	float GazeMarginCm = 10.f;
};

UCLASS(ClassGroup = (Custom))
class TELEOP_VR_INTERFACE_API UWristViewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWristViewComponent();

	void Initialize(UCameraComponent* Camera, int32 Priority, float SideSign, const FWristViewSettings& Settings);

	void UpdateView(UTexture2D* Texture, bool bWantVisible, bool bHaveAnchor, const FVector2D& AnchorCm,
		const FTransform& QuadWorld, float PlaneDistance, bool bWorldLocked, const FGazeData& Gaze, float DeltaTime);

	bool IsShown() const { return bShown_; }
	FVector2D GetCenterCm() const { return CenterCm_; }
	FVector2D GetSizeCm() const { return SizeCm_; }
	UTexture2D* GetTexture() const { return Texture_; }

private:
	bool GazeHits(const FGazeData& Gaze, const FTransform& LayerWorld) const;
	void ApplyMode(bool bWorldLocked);

	UPROPERTY() TObjectPtr<UStereoLayerComponent> Layer_;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera_;
	UPROPERTY() TObjectPtr<UTexture2D> Texture_;

	FWristViewSettings Settings_;
	int32 Priority_   = 3;
	float SideSign_   = 1.f;
	bool  bShown_       = false;
	bool  bWorldLocked_ = false;
	bool  bHaveCenter_  = false;
	bool  bExpanded_    = false;
	float Scale_      = 1.f;
	float AwayTimer_  = 0.f;
	FVector2D AnchorSmooth_ = FVector2D::ZeroVector;
	FVector2D CenterCm_     = FVector2D::ZeroVector;
	FVector2D SizeCm_       = FVector2D::ZeroVector;
	FTransform LayerWorld_  = FTransform::Identity;
};
