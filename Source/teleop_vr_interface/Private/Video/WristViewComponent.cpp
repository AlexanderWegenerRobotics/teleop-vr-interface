#include "Video/WristViewComponent.h"
#include "Components/StereoLayerComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Texture2D.h"
#include "Video/VideoLookAround.h"

UWristViewComponent::UWristViewComponent() {
	PrimaryComponentTick.bCanEverTick = false;
}

void UWristViewComponent::Initialize(UCameraComponent* Camera, int32 Priority, float SideSign, const FWristViewSettings& Settings) {
	if (!Camera || !GetOwner()) return;
	Camera_   = Camera;
	Priority_ = Priority;
	SideSign_ = SideSign >= 0.f ? 1.f : -1.f;
	Settings_ = Settings;

	const FName LayerName = MakeUniqueObjectName(GetOwner(), UStereoLayerComponent::StaticClass(),
		FName(*FString::Printf(TEXT("WristViewLayer_%s"), *GetName())));
	Layer_ = NewObject<UStereoLayerComponent>(GetOwner(), LayerName);
	Layer_->bLiveTexture   = true;
	Layer_->bSupportsDepth = false;
	Layer_->SetPriority(Priority_);
	Layer_->AttachToComponent(Camera, FAttachmentTransformRules::KeepRelativeTransform);
	Layer_->SetRelativeLocation(FVector(700.f, 0.f, 0.f));
	Layer_->SetQuadSize(FVector2D(Settings_.WidthCm, Settings_.WidthCm * 0.75f));
	Layer_->RegisterComponent();
	Layer_->SetVisibility(false);
	bWorldLocked_ = false;
}

void UWristViewComponent::ApplyMode(bool bWorldLocked) {
	if (!Layer_) return;
	if (bWorldLocked) {
		Layer_->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		FVideoLookAround::SetLayerType(Layer_, SLT_WorldLocked, Priority_);
	} else {
		if (Camera_) Layer_->AttachToComponent(Camera_, FAttachmentTransformRules::KeepRelativeTransform);
		FVideoLookAround::SetLayerType(Layer_, SLT_FaceLocked, Priority_);
	}
	bWorldLocked_ = bWorldLocked;
}

bool UWristViewComponent::GazeHits(const FGazeData& Gaze, const FTransform& LayerWorld) const {
	if (!Gaze.bIsValid || Gaze.Confidence <= 0.f) return false;
	const FVector O = LayerWorld.InverseTransformPositionNoScale(Gaze.Origin);
	const FVector D = LayerWorld.InverseTransformVectorNoScale(Gaze.Direction.GetSafeNormal());
	if (FMath::IsNearlyZero(D.X)) return false;
	const double T = -O.X / D.X;
	if (T <= 0.0) return false;
	const double Y = O.Y + D.Y * T;
	const double Z = O.Z + D.Z * T;
	return FMath::Abs(Y) <= SizeCm_.X * 0.5 + Settings_.GazeMarginCm
		&& FMath::Abs(Z) <= SizeCm_.Y * 0.5 + Settings_.GazeMarginCm;
}

void UWristViewComponent::UpdateView(UTexture2D* Texture, bool bWantVisible, bool bHaveAnchor, const FVector2D& AnchorCm,
	const FTransform& QuadWorld, float PlaneDistance, bool bWorldLocked, const FGazeData& Gaze, float DeltaTime) {
	if (!Layer_) return;

	if (!(bWantVisible && Texture && bHaveAnchor)) {
		if (bShown_) {
			Layer_->SetVisibility(false);
			bShown_ = false;
		}
		bHaveCenter_ = false;
		bExpanded_   = false;
		Scale_       = 1.f;
		AwayTimer_   = 0.f;
		return;
	}

	if (Texture != Texture_) {
		Texture_ = Texture;
		Layer_->SetTexture(Texture);
	}

	if (bShown_ && GazeHits(Gaze, LayerWorld_)) {
		bExpanded_ = true;
		AwayTimer_ = 0.f;
	} else if (bExpanded_) {
		AwayTimer_ += DeltaTime;
		if (AwayTimer_ >= Settings_.ShrinkDelayS) bExpanded_ = false;
	}
	Scale_ = FMath::FInterpTo(Scale_, bExpanded_ ? Settings_.ExpandScale : 1.f, DeltaTime, 10.f);

	const float Alpha = Settings_.FollowTauS > 0.f ? 1.f - FMath::Exp(-DeltaTime / Settings_.FollowTauS) : 1.f;
	AnchorSmooth_ = bHaveCenter_ ? FMath::Lerp(AnchorSmooth_, AnchorCm, static_cast<double>(Alpha)) : AnchorCm;
	bHaveCenter_ = true;

	const int32 TexW = Texture->GetSizeX();
	const int32 TexH = Texture->GetSizeY();
	const double Aspect = (TexW > 0 && TexH > 0) ? static_cast<double>(TexH) / TexW : 0.75;
	SizeCm_   = FVector2D(Settings_.WidthCm, Settings_.WidthCm * Aspect) * static_cast<double>(Scale_);
	CenterCm_ = FVector2D(AnchorSmooth_.X + SideSign_ * (Settings_.GapCm + SizeCm_.X * 0.5), AnchorSmooth_.Y + Settings_.UpCm);

	if (!bShown_ || bWorldLocked != bWorldLocked_) ApplyMode(bWorldLocked);

	if (bWorldLocked) {
		const FVector Rel(-Settings_.InsetCm, CenterCm_.X, CenterCm_.Y);
		LayerWorld_ = FTransform(QuadWorld.GetRotation(), QuadWorld.TransformPositionNoScale(Rel));
		Layer_->SetWorldLocationAndRotation(LayerWorld_.GetLocation(), LayerWorld_.GetRotation());
	} else {
		Layer_->SetRelativeLocationAndRotation(FVector(PlaneDistance - Settings_.InsetCm, CenterCm_.X, CenterCm_.Y), FRotator::ZeroRotator);
		LayerWorld_ = Layer_->GetComponentTransform();
	}
	Layer_->SetQuadSize(SizeCm_);

	if (!bShown_) {
		Layer_->SetVisibility(true);
		bShown_ = true;
	}
}
