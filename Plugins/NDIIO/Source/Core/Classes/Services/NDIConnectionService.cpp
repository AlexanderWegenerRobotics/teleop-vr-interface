/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Services/NDIConnectionService.h>

#include <UObject/UObjectGlobals.h>
#include <UObject/Package.h>
#include <Misc/CoreDelegates.h>
#include <NDIIOPluginSettings.h>
#include <Objects/Media/NDIMediaSender.h>
#include <Framework/Application/SlateApplication.h>
#include <Misc/EngineVersionComparison.h>
#include <Engine/Engine.h>
#include <TextureResource.h>

#if WITH_EDITOR

#include <Editor.h>

#endif

/** Define Global Accessors */

FNDIConnectionServiceSendVideoEvent FNDIConnectionService::EventOnSendVideoFrame;
FNDIConnectionServiceSendAudioEvent FNDIConnectionService::EventOnSendAudioFrame;


FCriticalSection FNDIConnectionService::AudioSyncContext;
FCriticalSection FNDIConnectionService::RenderSyncContext;

/** ************************ **/

/**
	Constructs a new instance of this object
*/
FNDIConnectionService::FNDIConnectionService() {}

// Begin the service
bool FNDIConnectionService::Start()
{
	if (!bIsInitialized)
	{
		bIsInitialized = true;

		// Define some basic properties
		FNDIBroadcastConfiguration Configuration;
		FString BroadcastName = TEXT("Unreal Engine");
		EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transient | RF_MarkAsNative;

		bool bBeginBroadcastOnPlay = false;

		// Load the plugin settings for broadcasting the active viewport
		if (auto* CoreSettings = NewObject<UNDIIOPluginSettings>())
		{
			// Define the configuration properties
			Configuration.FrameRate = CoreSettings->BroadcastRate;
			Configuration.FrameSize = FIntPoint(FMath::Clamp(CoreSettings->PreferredFrameSize.X, 240, 3840),
												FMath::Clamp(CoreSettings->PreferredFrameSize.Y, 240, 3840));

			// Set the broadcast name
			BroadcastName = CoreSettings->ApplicationStreamName;

			bBeginBroadcastOnPlay = CoreSettings->bBeginBroadcastOnPlay;

			// clean-up the settings object
			CoreSettings->ConditionalBeginDestroy();
			CoreSettings = nullptr;
		}

		/** Construct the Active Viewport video texture */
		this->VideoTexture = NewObject<UTextureRenderTarget2D>(
			GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), TEXT("NDIViewportVideoTexture"), Flags);

		/** Construct the active viewport sender */
		this->ActiveViewportSender = NewObject<UNDIMediaSender>(GetTransientPackage(), UNDIMediaSender::StaticClass(),
																TEXT("NDIViewportSender"), Flags);

		VideoTexture->UpdateResource();

		// Update the active viewport sender, with the properties defined in the settings configuration
		this->ActiveViewportSender->ChangeSourceName(BroadcastName);
		this->ActiveViewportSender->ChangeVideoTexture(VideoTexture);
		this->ActiveViewportSender->ChangeBroadcastConfiguration(Configuration);

		// Hook into the core for the end of frame handlers
		FCoreDelegates::OnEndFrameRT.AddRaw(this, &FNDIConnectionService::OnEndRenderFrame);

		if (!GIsEditor)
		{
			FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNDIConnectionService::OnPostEngineInit);
			FCoreDelegates::OnEnginePreExit.AddRaw(this, &FNDIConnectionService::OnEnginePreExit);
			if (bBeginBroadcastOnPlay)
				BeginBroadcastingActiveViewport();
		}
#if WITH_EDITOR
		else
		{
			FEditorDelegates::PostPIEStarted.AddLambda([this](const bool Success) {
				if (auto* CoreSettings = NewObject<UNDIIOPluginSettings>())
				{
					if (CoreSettings->bBeginBroadcastOnPlay == true)
						BeginBroadcastingActiveViewport();
					else
						BeginAudioCapture();

					// clean-up the settings object
					CoreSettings->ConditionalBeginDestroy();
					CoreSettings = nullptr;
				}
				bIsInPIEMode = true;
			});
			FEditorDelegates::PrePIEEnded.AddLambda([this](const bool Success) { StopBroadcastingActiveViewport(); });
		}
#endif
	}

	return true;
}

// Stop the service
void FNDIConnectionService::Shutdown()
{
	// Wait for the sync context locks
	FScopeLock AudioLock(&AudioSyncContext);
	FScopeLock RenderLock(&RenderSyncContext);

	// reset the initialization properties
	bIsInitialized = false;

	StopAudioCapture();

	// unbind our handlers for our frame events
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	FCoreDelegates::OnEndFrameRT.RemoveAll(this);

	// Cleanup the broadcasting of the active viewport
	StopBroadcastingActiveViewport();
}


// Handler for when the render thread frame has ended
void FNDIConnectionService::OnEndRenderFrame()
{
	FScopeLock Lock(&RenderSyncContext);

	if (bIsInitialized)
	{
		int64 ticks = FDateTime::Now().GetTimeOfDay().GetTicks();

		if (FNDIConnectionService::EventOnSendVideoFrame.IsBound())
		{
			FNDIConnectionService::EventOnSendVideoFrame.Broadcast(ticks);
		}
	}
}

void FNDIConnectionService::BeginAudioCapture()
{
	if (bIsInitialized)
	{
		if (!bIsAudioInitialized)
		{
			if (GEngine)
			{
				FAudioDeviceHandle AudioDevice = GEngine->GetActiveAudioDevice();
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 4))	// 5.4 or later
				AudioDevice->RegisterSubmixBufferListener(AsShared(), AudioDevice->GetMainSubmixObject());
#else
				AudioDevice->RegisterSubmixBufferListener(this);
#endif
				bIsAudioInitialized = true;
			}
		}
	}
}

void FNDIConnectionService::StopAudioCapture()
{
	if (bIsAudioInitialized)
	{
		if (GEngine)
		{
			FAudioDeviceHandle AudioDevice = GEngine->GetActiveAudioDevice();
			if (AudioDevice)
			{
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 4))	// 5.4 or later
				AudioDevice->UnregisterSubmixBufferListener(AsShared(), AudioDevice->GetMainSubmixObject());
#else
				AudioDevice->UnregisterSubmixBufferListener(this);
#endif
			}
		}
		bIsAudioInitialized = false;
	}
}

void FNDIConnectionService::OnPostEngineInit()
{
	BeginAudioCapture();
}

void FNDIConnectionService::OnEnginePreExit()
{
	StopAudioCapture();
}

bool FNDIConnectionService::BeginBroadcastingActiveViewport()
{
	if (!bIsBroadcastingActiveViewport && IsValid(ActiveViewportSender))
	{
		// Load the plugin settings for broadcasting the active viewport
		if (auto* CoreSettings = NewObject<UNDIIOPluginSettings>())
		{
			// Define some basic properties
			FNDIBroadcastConfiguration Configuration;
			FString BroadcastName = TEXT("Unreal Engine");

			// Define the configuration properties
			Configuration.FrameRate = CoreSettings->BroadcastRate;
			Configuration.FrameSize = FIntPoint(FMath::Clamp(CoreSettings->PreferredFrameSize.X, 240, 3840),
												FMath::Clamp(CoreSettings->PreferredFrameSize.Y, 240, 3840));

			// Set the broadcast name
			BroadcastName = CoreSettings->ApplicationStreamName;

			// clean-up the settings object
			CoreSettings->ConditionalBeginDestroy();
			CoreSettings = nullptr;

			// Update the active viewport sender, with the properties defined in the settings configuration
			this->ActiveViewportSender->ChangeSourceName(BroadcastName);
			this->ActiveViewportSender->ChangeBroadcastConfiguration(Configuration);
		}

		// we don't want to perform the linear conversion for the active viewport,
		// since it's already had the conversion completed by the engine before passing to the sender
		ActiveViewportSender->PerformLinearTosRGBConversion(false);

		// Do not enable PTZ capabilities for active viewport sender
		ActiveViewportSender->EnablePTZ(false);

		// Initialize the sender, this will automatically start rendering output via NDI
		ActiveViewportSender->Initialize();

		// We've initialized the active viewport
		bIsBroadcastingActiveViewport = true;

		// However we need to update the 'Video Texture' to the active viewport back buffer...
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().AddRaw(
			this, &FNDIConnectionService::OnActiveViewportBackbufferPreResize);
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(
			this, &FNDIConnectionService::OnActiveViewportBackbufferReadyToPresent);

		BeginAudioCapture();
	}

	// always return true
	return true;
}

// Handler for when the active viewport back buffer has been resized
void FNDIConnectionService::OnActiveViewportBackbufferPreResize(void* Backbuffer)
{
	check(IsInGameThread());

	// Ensure we have a valid video texture
	FTextureResource* TextureResource = GetVideoTextureResource();
	if (TextureResource != nullptr)
	{
		FRenderCommandFence Fence;

		TextureResource->TextureRHI.SafeRelease();
		this->ActiveViewportSender->ChangeVideoTexture(VideoTexture);

		ENQUEUE_RENDER_COMMAND(FlushRHIThreadToUpdateTextureRenderTargetReference)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				RHIUpdateTextureReference(VideoTexture->TextureReference.TextureReferenceRHI, nullptr);
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});

		// Wait for render thread to finish, so that renderthread texture references are updated
		Fence.BeginFence();
		Fence.Wait();
	}
}

// Handler for when the back buffer is read to present to the end user
void FNDIConnectionService::OnActiveViewportBackbufferReadyToPresent(SWindow& Window,
																	 const FTexture2DRHIRef& Backbuffer)
{
	if (Window.GetType() == EWindowType::GameWindow || (Window.IsRegularWindow() && IsRunningInPIE()))
	{
		FTextureResource* TextureResource = GetVideoTextureResource();
		if (TextureResource != nullptr)
		{
			// Lets improve the performance a bit
			if (TextureResource->TextureRHI != Backbuffer)
			{
				TextureResource->TextureRHI = (FTexture2DRHIRef&)Backbuffer;
				this->ActiveViewportSender->ChangeVideoTexture(VideoTexture);
				RHIUpdateTextureReference(VideoTexture->TextureReference.TextureReferenceRHI, Backbuffer);
			}
		}
	}
}

void FNDIConnectionService::StopBroadcastingActiveViewport()
{
	// Wait for the sync context locks
	FScopeLock RenderLock(&RenderSyncContext);

	// reset the initialization properties
	bIsInPIEMode = false;

	StopAudioCapture();

	// Ensure that if the active viewport sender is active, that we shut it down
	if (IsValid(this->ActiveViewportSender))
	{
		FSlateApplication::Get().GetRenderer()->OnPreResizeWindowBackBuffer().RemoveAll(this);
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);

		// shutdown the active viewport sender (just in case it was activated)
		this->ActiveViewportSender->Shutdown();

		// reset the broadcasting flag, so that we can restart the broadcast later
		this->bIsBroadcastingActiveViewport = false;

		FTextureResource* TextureResource = GetVideoTextureResource();
		if (TextureResource != nullptr)
		{
			TextureResource->TextureRHI.SafeRelease();
			this->ActiveViewportSender->ChangeVideoTexture(VideoTexture);
		}
	}
}


FTextureResource* FNDIConnectionService::GetVideoTextureResource() const
{
	if(IsValid(this->VideoTexture))
		return this->VideoTexture->GetResource();

	return nullptr;
}


void FNDIConnectionService::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	if (NumSamples > 0)
	{
		FScopeLock Lock(&AudioSyncContext);

		if (bIsAudioInitialized)
		{
			int64 ticks = FDateTime::Now().GetTimeOfDay().GetTicks();

			if (FNDIConnectionService::EventOnSendAudioFrame.IsBound())
			{
				FNDIConnectionService::EventOnSendAudioFrame.Broadcast(ticks, AudioData, NumSamples, NumChannels, SampleRate, AudioClock);
			}
		}
	}
}

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 4))	// 5.4 or later
const FString& FNDIConnectionService::GetListenerName() const
{
	static const FString ListenerName(TEXT("NDIConnectionServiceListener"));
	return ListenerName;
}
#endif
