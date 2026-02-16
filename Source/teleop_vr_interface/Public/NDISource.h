#pragma once

#include "IVideoSource.h"
#include "Objects/Media/NDIMediaReceiver.h"
#include "Objects/Media/NDIMediaTexture2D.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "TextureResource.h"
#include "RenderingThread.h"

/**
 * NDISource
 *
 * Wraps NDI media receiver behind the IVideoSource interface.
 * Receives video via NDI protocol and copies frames into a standard UTexture2D.
 */
class FNDISource : public IVideoSource
{
public:

	struct FConfig
	{
		FString SourceName = TEXT("");
		bool bMuteAudio = true;
	};

	FNDISource(const FConfig& InConfig)
		: Config(InConfig)
	{
	}

	virtual ~FNDISource() override
	{
		Stop();
	}

	virtual bool Initialize() override
	{
		if (bInitialized) return true;

		// NDI objects are UObjects — need to be created in a valid context
		Receiver = NewObject<UNDIMediaReceiver>();
		if (!Receiver)
		{
			UE_LOG(LogTemp, Error, TEXT("NDISource: Failed to create NDI receiver"));
			return false;
		}

		NDITexture = NewObject<UNDIMediaTexture2D>();
		if (!NDITexture)
		{
			UE_LOG(LogTemp, Error, TEXT("NDISource: Failed to create NDI texture"));
			return false;
		}

		bInitialized = true;
		return true;
	}

	virtual bool Start() override
	{
		if (!bInitialized || !Receiver || !NDITexture) return false;

		Receiver->SetMediaOptionInt64(NDIMediaOption::MaxVideoFrameBuffer, 1);

		FNDIConnectionInformation ConnectionInfo;
		ConnectionInfo.SourceName = Config.SourceName;
		ConnectionInfo.Bandwidth = ENDISourceBandwidth::Highest;
		ConnectionInfo.bMuteVideo = false;
		ConnectionInfo.bMuteAudio = Config.bMuteAudio;

		if (!Receiver->Initialize(ConnectionInfo, UNDIMediaReceiver::EUsage::Standalone))
		{
			UE_LOG(LogTemp, Warning, TEXT("NDISource: Failed to initialize for source '%s'"), *Config.SourceName);
			return false;
		}

		Receiver->ChangeVideoTexture(NDITexture);
		NDITexture->UpdateResource();
		Receiver->bUseTimeSynchronization = true;
		Receiver->StartConnection();

		bRunning = true;
		UE_LOG(LogTemp, Log, TEXT("NDISource: Started receiving from '%s'"), *Config.SourceName);
		return true;
	}

	virtual void Stop() override
	{
		if (Receiver && bRunning)
		{
			Receiver->StopConnection();
			UE_LOG(LogTemp, Log, TEXT("NDISource: Stopped"));
		}
		bRunning = false;
	}

	virtual bool UpdateTexture(UTexture2D* Texture) override
	{
		if (!bRunning || !NDITexture || !IsValid(NDITexture) || !Texture) return false;

		FTextureResource* NDIRes = NDITexture->GetResource();
		if (!NDIRes || !NDIRes->TextureRHI) return false;

		FRHITexture2D* SourceRHI = NDIRes->TextureRHI->GetTexture2D();
		if (!SourceRHI) return false;

		// Cache dimensions
		CachedWidth = SourceRHI->GetSizeX();
		CachedHeight = SourceRHI->GetSizeY();

		if (CachedWidth == 0 || CachedHeight == 0) return false;

		FTextureResource* TargetRes = Texture->GetResource();
		if (!TargetRes || !TargetRes->TextureRHI) return false;

		FRHITexture2D* TargetRHI = TargetRes->TextureRHI->GetTexture2D();
		if (!TargetRHI) return false;

		// Copy full NDI frame to target texture via render command
		const uint32 W = CachedWidth;
		const uint32 H = CachedHeight;

		ENQUEUE_RENDER_COMMAND(CopyNDIFrame)(
			[SourceRHI, TargetRHI, W, H](FRHICommandListImmediate& RHICmdList)
			{
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.SourcePosition.X = 0;
				CopyInfo.Size.X = W;
				CopyInfo.Size.Y = H;
				RHICmdList.CopyTexture(SourceRHI, TargetRHI, CopyInfo);
			});

		FrameCount++;
		double Now = FPlatformTime::Seconds();
		if (Now - LastFPSTime >= 1.0)
		{
			CurrentFPS = FrameCount;
			FrameCount = 0;
			LastFPSTime = Now;
		}

		return true;
	}

	virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const override
	{
		OutWidth = CachedWidth;
		OutHeight = CachedHeight;
		return (CachedWidth > 0 && CachedHeight > 0);
	}

	virtual FVideoSourceStats GetStats() const override
	{
		FVideoSourceStats Stats;
		Stats.CurrentFPS = CurrentFPS;
		Stats.bIsReceiving = (CurrentFPS > 0 && bRunning);
		// NDI doesn't expose latency/jitter directly through the plugin
		return Stats;
	}

	virtual FString GetSourceName() const override
	{
		return FString::Printf(TEXT("NDI (%s)"), *Config.SourceName);
	}

private:
	FConfig Config;

	// NDI UObjects — must stay alive while in use
	UPROPERTY()
	UNDIMediaReceiver* Receiver = nullptr;

	UPROPERTY()
	UNDIMediaTexture2D* NDITexture = nullptr;

	// State
	bool bInitialized = false;
	bool bRunning = false;

	// Cached dimensions (updated each frame from RHI texture)
	int32 CachedWidth = 0;
	int32 CachedHeight = 0;

	// FPS tracking
	int32 CurrentFPS = 0;
	int32 FrameCount = 0;
	double LastFPSTime = 0.0;
};