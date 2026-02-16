/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Components/NDIViewportCaptureComponent.h>
#include <Rendering/RenderingCommon.h>
#include <SceneView.h>
#include <SceneViewExtension.h>
#include <CanvasTypes.h>
#include <EngineModule.h>
#include <LegacyScreenPercentageDriver.h>
#include <RenderResource.h>
#include <UnrealClient.h>
#include <Engine/Engine.h>
#include <EngineUtils.h>
#include <Runtime/Renderer/Private/ScenePrivate.h>
#include <Misc/CoreDelegates.h>


UNDIViewportCaptureComponent::UNDIViewportCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->bWantsInitializeComponent = true;
	this->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	this->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
	this->PostProcessSettings.DepthOfFieldFocalDistance = 10000.f;
}

UNDIViewportCaptureComponent::~UNDIViewportCaptureComponent()
{}

void UNDIViewportCaptureComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// validate the Media Source object
	if (IsValid(NDIMediaSource))
	{
		// define default capture values
		const auto& capture_size = !bOverrideBroadcastSettings ? NDIMediaSource->GetFrameSize() : CaptureSize;
		const auto& capture_rate = !bOverrideBroadcastSettings ? NDIMediaSource->GetFrameRate() : CaptureRate;

		// change the capture sizes as necessary
		ChangeCaptureSettings(capture_size, capture_rate);

		// ensure we are subscribed to the broadcast configuration changed event
		this->NDIMediaSource->OnBroadcastConfigurationChanged.RemoveAll(this);
		this->NDIMediaSource->OnBroadcastConfigurationChanged.AddDynamic(
			this, &UNDIViewportCaptureComponent::OnBroadcastConfigurationChanged);
	}
}

void UNDIViewportCaptureComponent::UninitializeComponent()
{
	if (IsValid(NDIMediaSource))
	{
		if (IsValid(TextureTarget))
		{
			NDIMediaSource->ChangeVideoTexture(nullptr);
		}
	}

	Super::UninitializeComponent();
}

bool UNDIViewportCaptureComponent::Initialize(UNDIMediaSender* InMediaSource)
{
	// is the media source already set?
	if (this->NDIMediaSource == nullptr && InMediaSource != nullptr)
	{
		// we passed validation, so set the media source
		this->NDIMediaSource = InMediaSource;

		// validate the Media Source object
		if (IsValid(NDIMediaSource))
		{
			// define default capture values
			const auto& capture_size = !bOverrideBroadcastSettings ? NDIMediaSource->GetFrameSize() : CaptureSize;
			const auto& capture_rate = !bOverrideBroadcastSettings ? NDIMediaSource->GetFrameRate() : CaptureRate;

			// change the capture sizes as necessary
			ChangeCaptureSettings(capture_size, capture_rate);

			// ensure we are subscribed to the broadcast configuration changed event
			this->NDIMediaSource->OnBroadcastConfigurationChanged.RemoveAll(this);
			this->NDIMediaSource->OnBroadcastConfigurationChanged.AddDynamic(
				this, &UNDIViewportCaptureComponent::OnBroadcastConfigurationChanged);
		}
	}

	// did we pass validation
	return InMediaSource != nullptr && InMediaSource == NDIMediaSource;
}

/**
	Changes the name of the sender object as seen on the network for remote connections

	@param InSourceName The new name of the source to be identified as on the network
*/
void UNDIViewportCaptureComponent::ChangeSourceName(const FString& InSourceName)
{
	// validate the Media Source object
	if (IsValid(NDIMediaSource))
	{
		// call the media source implementation of the function
		NDIMediaSource->ChangeSourceName(InSourceName);
	}
}

/**
	Attempts to change the Broadcast information associated with this media object

	@param InConfiguration The new configuration to broadcast
*/
void UNDIViewportCaptureComponent::ChangeBroadcastConfiguration(const FNDIBroadcastConfiguration& InConfiguration)
{
	// validate the Media Source object
	if (IsValid(NDIMediaSource))
	{
		// call the media source implementation of the function
		NDIMediaSource->ChangeBroadcastConfiguration(InConfiguration);
	}
}

/**
	Attempts to change the RenderTarget used in sending video frames over NDI

	@param BroadcastTexture The texture to use as video, while broadcasting over NDI
*/
void UNDIViewportCaptureComponent::ChangeBroadcastTexture(UTextureRenderTarget2D* BroadcastTexture)
{
	// ensure we have some thread-safety
	FScopeLock Lock(&UpdateRenderContext);

	this->TextureTarget = BroadcastTexture;
}

/**
	Change the capture settings of the viewport capture

	@param InCaptureSize The Capture size of the frame to capture of the viewport
	@param InCaptureRate A framerate at which to capture frames of the viewport
*/
void UNDIViewportCaptureComponent::ChangeCaptureSettings(FIntPoint InCaptureSize, FFrameRate InCaptureRate)
{
	// clamp our viewport capture size
	int32 capture_width = FMath::Max(InCaptureSize.X, 64);
	int32 capture_height = FMath::Max(InCaptureSize.Y, 64);

	// set the capture size
	this->CaptureSize = FIntPoint(capture_width, capture_height);

	// set the capture rate
	this->CaptureRate = InCaptureRate;

	// clamp the maximum capture rate to something reasonable
	float capture_rate_max = 1 / 1000.0f;
	float capture_rate = CaptureRate.Denominator / (float)CaptureRate.Numerator;

	// set the primary tick interval to the sensible capture rate
	this->PrimaryComponentTick.TickInterval = capture_rate >= capture_rate_max ? capture_rate : -1.0f;

	// ensure we have some thread-safety
	FScopeLock Lock(&UpdateRenderContext);

	if (!IsValid(this->TextureTarget))
	{
		this->TextureTarget = NewObject<UTextureRenderTarget2D>(
			GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), NAME_None, RF_Transient | RF_MarkAsNative);
		this->TextureTarget->UpdateResource();
	}
	this->TextureTarget->ResizeTarget(this->CaptureSize.X, this->CaptureSize.Y);
}

/**
	Determines the current tally information. If you specify a timeout then it will wait until it has
	changed, otherwise it will simply poll it and return the current tally immediately

	@param IsOnPreview - A state indicating whether this source in on preview of a receiver
	@param IsOnProgram - A state indicating whether this source is on program of a receiver
*/
void UNDIViewportCaptureComponent::GetTallyInformation(bool& IsOnPreview, bool& IsOnProgram)
{
	// Initialize the properties
	IsOnPreview = false;
	IsOnProgram = false;

	// validate the Media Source object
	if (IsValid(NDIMediaSource))
	{
		// call the media source implementation of the function
		NDIMediaSource->GetTallyInformation(IsOnPreview, IsOnProgram, 0);
	}
}

/**
	Gets the current number of receivers connected to this source. This can be used to avoid rendering
	when nothing is connected to the video source. which can significantly improve the efficiency if
	you want to make a lot of sources available on the network

	@param Result The total number of connected receivers attached to the broadcast of this object
*/
void UNDIViewportCaptureComponent::GetNumberOfConnections(int32& Result)
{
	// Initialize the property
	Result = 0;

	// validate the Media Source object
	if (IsValid(NDIMediaSource))
	{
		// call the media source implementation of the function
		NDIMediaSource->GetNumberOfConnections(Result);
	}
}


void UNDIViewportCaptureComponent::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
	// ensure we have some thread-safety
	FScopeLock Lock(&UpdateRenderContext);

	if (TextureTarget == nullptr)
		return;

	if (IsValid(NDIMediaSource))
	{
		NDIMediaSource->ChangeVideoTexture(TextureTarget);

		// Some capture sources treat alpha as opacity, some sources use transparency.
		// Alpha in NDI is opacity. Reverse the alpha mapping to always get opacity.
		bool flip_alpha = (CaptureSource == SCS_SceneColorHDR) || (CaptureSource == SCS_SceneColorHDRNoAlpha) ||
						  (CaptureSource == SCS_SceneDepth) || (CaptureSource == SCS_Normal) ||
						  (CaptureSource == SCS_BaseColor);
		if (flip_alpha == false)
			NDIMediaSource->ChangeAlphaRemap(AlphaMin, AlphaMax);
		else
			NDIMediaSource->ChangeAlphaRemap(AlphaMax, AlphaMin);

		// Do the actual capturing
		Super::UpdateSceneCaptureContents(Scene);
	}
}

void UNDIViewportCaptureComponent::OnBroadcastConfigurationChanged(UNDIMediaSender* Sender)
{
	// If we are not overriding the broadcast settings and the sender is valid
	if (!bOverrideBroadcastSettings && IsValid(Sender))
	{
		// change the capture sizes as necessary
		ChangeCaptureSettings(Sender->GetFrameSize(), Sender->GetFrameRate());
	}
}
