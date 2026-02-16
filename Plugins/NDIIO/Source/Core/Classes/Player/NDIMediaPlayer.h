/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <NDIIOPluginAPI.h>
#include <Objects/Media/NDIMediaReceiver.h>
#include <MediaIOCorePlayerBase.h>


class FNDIMediaPlayer : public FMediaIOCorePlayerBase
{
	using Super = FMediaIOCorePlayerBase;

public:
	FNDIMediaPlayer(IMediaEventSink& InEventSink);

	virtual ~FNDIMediaPlayer();


	//~ IMediaPlayer interface
	virtual FGuid GetPlayerPluginGUID() const override;
	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;
	virtual void Close() override;

	virtual void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;
	virtual void TickFetch(FTimespan DeltaTime, FTimespan Timecode) override;

protected:
	virtual bool IsHardwareReady() const override;
	virtual void SetupSampleChannels() override;
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later
	virtual TSharedPtr<FMediaIOCoreTextureSampleBase> AcquireTextureSample_AnyThread() const override;
	virtual TSharedPtr<FMediaIOCoreTextureSampleConverter> CreateTextureSampleConverter() const override;
#endif

	void DisplayFrame(const NDIlib_video_frame_v2_t& video_frame);
	void PlayAudio(const NDIlib_audio_frame_v2_t& audio_frame);

	void ProcessFrame();
	void VerifyFrameDropCount();

public:
	//~ ITimedDataInput interface
#if WITH_EDITOR
	virtual const FSlateBrush* GetDisplayIcon() const override;
#endif

private:
	/** Max sample count our different buffer can hold. Taken from MediaSource */
	int32 MaxNumAudioFrameBuffer = 0;
	int32 MaxNumMetadataFrameBuffer = 0;
	int32 MaxNumVideoFrameBuffer = 0;

	/** Current state of the media player. */
	EMediaState NDIPlayerState = EMediaState::Closed;

	/** The media event handler. */
	IMediaEventSink& EventSink;

	UNDIMediaReceiver* Receiver = nullptr;
	bool bInternalReceiver = true;

	FDelegateHandle VideoCaptureEventHandle;
	FDelegateHandle AudioCaptureEventHandle;
	FDelegateHandle ConnectedEventHandle;
	FDelegateHandle DisconnectedEventHandle;

	class NDIMediaTextureSamplePool* TextureSamplePool;
	class NDIMediaAudioSamplePool* AudioSamplePool;
};
