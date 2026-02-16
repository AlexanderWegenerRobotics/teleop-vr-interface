/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Actors/NDIBroadcastActor.h>



ANDIBroadcastActor::ANDIBroadcastActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{

	this->ViewportCaptureComponent = ObjectInitializer.CreateDefaultSubobject<UNDIViewportCaptureComponent>(this, TEXT("ViewportCaptureComponent"));
	this->ViewportCaptureComponent->AttachToComponent(this->RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

	this->PTZController = ObjectInitializer.CreateDefaultSubobject<UPTZController>(this, TEXT("PTZController"));
}

void ANDIBroadcastActor::BeginPlay()
{
	Super::BeginPlay();

	// validate the viewport capture component
	if (IsValid(this->ViewportCaptureComponent))
	{
		// Initialize the Capture Component with the media source
		ViewportCaptureComponent->Initialize(this->NDIMediaSource);
	}

	if (IsValid(this->PTZController))
	{
		// Initialize the PTZ Controller with the media source
		PTZController->Initialize(this->NDIMediaSource);
	}

	if (IsValid(this->NDIMediaSource))
	{
		this->NDIMediaSource->Initialize();
	}
}

FPTZState ANDIBroadcastActor::GetPTZStateFromUE() const
{
	FPTZState PTZState;

	PTZState.CameraTransform = GetActorTransform();
	FTransform Transform = FTransform::Identity;
	if (IsValid(this->ViewportCaptureComponent))
		Transform = this->ViewportCaptureComponent->GetRelativeTransform();
	FQuat Rotation = Transform.GetRotation();
	FVector Euler = Rotation.Euler();
	PTZState.Pan = FMath::DegreesToRadians(Euler[2]);
	PTZState.Tilt = FMath::DegreesToRadians(Euler[1]);

	if (IsValid(this->ViewportCaptureComponent))
	{
		PTZState.FieldOfView = this->ViewportCaptureComponent->FOVAngle;
		PTZState.FocusDistance = 1.f - 1.f / (this->ViewportCaptureComponent->PostProcessSettings.DepthOfFieldFocalDistance / 100.f + 1.f);
		PTZState.bAutoFocus = (this->ViewportCaptureComponent->PostProcessSettings.bOverride_DepthOfFieldFocalDistance == true) ? false : true;
	}

	return PTZState;
}

void ANDIBroadcastActor::SetPTZStateToUE(const FPTZState& PTZState)
{
	SetActorTransform(PTZState.CameraTransform);
	FVector Euler(0, FMath::RadiansToDegrees(PTZState.Tilt), FMath::RadiansToDegrees(PTZState.Pan));
	FQuat NewRotation = FQuat::MakeFromEuler(Euler);

	if (IsValid(this->ViewportCaptureComponent))
	{
		this->ViewportCaptureComponent->SetRelativeLocationAndRotation(this->ViewportCaptureComponent->GetRelativeLocation(), NewRotation);
		this->ViewportCaptureComponent->FOVAngle = PTZState.FieldOfView;
		this->ViewportCaptureComponent->PostProcessSettings.DepthOfFieldFocalDistance = (1.f / FMath::Max(1 - PTZState.FocusDistance, 0.01f) - 1.f) * 100.f;
		this->ViewportCaptureComponent->PostProcessSettings.DepthOfFieldFocalDistance = FMath::Max(this->ViewportCaptureComponent->PostProcessSettings.DepthOfFieldFocalDistance, SMALL_NUMBER);
		this->ViewportCaptureComponent->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = (PTZState.bAutoFocus == true) ? false : true;
	}
}
