/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <NDIIOPluginAPI.h>

#include <UObject/Object.h>
#include <Misc/Timecode.h>
#include <Misc/FrameRate.h>
#include <TimeSynchronizableMediaSource.h>
#include <RendererInterface.h>

#include <Objects/Media/NDIMediaSoundWave.h>
#include <Objects/Media/NDIMediaTexture2D.h>
#include <Structures/NDIConnectionInformation.h>
#include <Structures/NDIReceiverPerformanceData.h>

#include "NDIMediaReceiver.generated.h"


namespace NDIMediaOption
{
	static const FName IsNDIMediaReceiver("IsNDIMediaReceiver");
	static const FName MaxVideoFrameBuffer("MaxVideoFrameBuffer");
	static const FName MaxAudioFrameBuffer("MaxAudioFrameBuffer");
	static const FName MaxAncillaryFrameBuffer("MaxAncillaryFrameBuffer");
}


/**
	Delegates to notify that the NDIMediaReceiver has received a video, audio, or metadata frame
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaReceiverVideoReceived, UNDIMediaReceiver*, Receiver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaReceiverAudioReceived, UNDIMediaReceiver*, Receiver);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNDIMediaReceiverMetaDataReceived, UNDIMediaReceiver*, Receiver, FString, Data, bool, bAttachedToVideoFrame);


/**
	A Media object representing the NDI Receiver for being able to receive Audio, Video, and Metadata over NDI
*/
UCLASS(BlueprintType, Blueprintable, HideCategories = ("Platforms"), Category = "NDI IO",
		HideCategories = ("Information"), AutoCollapseCategories = ("Content"),
		META = (DisplayName = "NDI Media Receiver"))
class NDIIO_API UNDIMediaReceiver : public UTimeSynchronizableMediaSource
{
	GENERATED_BODY()

public:
	/**
		Information describing detailed information about the sender this receiver is to connect to
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings",
			  META = (DisplayName = "Connection", AllowPrivateAccess = true))
	FNDIConnectionInformation ConnectionSetting;

private:
	/**
		The current frame count, seconds, minutes, and hours in time-code notation
	*/
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Timecode", AllowPrivateAccess = true))
	FTimecode Timecode;

	/**
		The desired number of frames (per second) for video to be displayed
	*/
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Frame Rate", AllowPrivateAccess = true))
	FFrameRate FrameRate;

	/**
		The width and height of the last received video frame
	*/
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Resolution", AllowPrivateAccess = true))
	FIntPoint Resolution;

	/**
		Indicates whether the timecode should be synced to the Source Timecode value
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings",
			  META = (DisplayName = "Sync Timecode to Source", AllowPrivateAccess = true))
	bool bSyncTimecodeToSource = true;

	/**
		Should perform the sRGB to Linear color space conversion
	*/
	UPROPERTY(BlueprintReadonly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Perform sRGB to Linear?", AllowPrivateAccess = true))
	bool bPerformsRGBtoLinear = true;

	/**
		Information describing detailed information about the sender this receiver is currently connected to
	*/
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Connection Information", AllowPrivateAccess = true))
	FNDIConnectionInformation ConnectionInformation;

	/**
		Information describing detailed information about the receiver performance when connected to an NDI sender
	*/
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Performance Data", AllowPrivateAccess = true))
	FNDIReceiverPerformanceData PerformanceData;

	/**
		Provides an NDI Video Texture object to render videos frames from the source onto (optional)
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintSetter = "ChangeVideoTexture", Category = "Content",
			  AdvancedDisplay, META = (DisplayName = "Video Texture (optional)", AllowPrivateAccess = true))
	UNDIMediaTexture2D* VideoTexture = nullptr;

public:
	DECLARE_EVENT_OneParam(FNDIMediaReceiverConnectionEvent, FOnReceiverConnectionEvent,
						   UNDIMediaReceiver*) FOnReceiverConnectionEvent OnNDIReceiverConnectedEvent;
	DECLARE_EVENT_OneParam(FNDIMediaReceiverDisconnectionEvent, FOnReceiverDisconnectionEvent,
						   UNDIMediaReceiver*) FOnReceiverDisconnectionEvent OnNDIReceiverDisconnectedEvent;

	DECLARE_EVENT_TwoParams(FNDIMediaReceiverVideoCaptureEvent, FOnReceiverVideoCaptureEvent,
	                        UNDIMediaReceiver*, const NDIlib_video_frame_v2_t&) FOnReceiverVideoCaptureEvent OnNDIReceiverVideoCaptureEvent;
	DECLARE_EVENT_TwoParams(FNDIMediaReceiverAudioCaptureEvent, FOnReceiverAudioCaptureEvent,
	                        UNDIMediaReceiver*, const NDIlib_audio_frame_v2_t&) FOnReceiverAudioCaptureEvent OnNDIReceiverAudioCaptureEvent;
	DECLARE_EVENT_TwoParams(FNDIMediaReceiverMetadataCaptureEvent, FOnReceiverMetadataCaptureEvent,
	                        UNDIMediaReceiver*, const NDIlib_metadata_frame_t&) FOnReceiverMetadataCaptureEvent OnNDIReceiverMetadataCaptureEvent;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Video Received by Receiver", AllowPrivateAccess = true))
	FNDIMediaReceiverVideoReceived OnReceiverVideoReceived;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Audio Received by Receiver", AllowPrivateAccess = true))
	FNDIMediaReceiverAudioReceived OnReceiverAudioReceived;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On MetaData Received by Receiver", AllowPrivateAccess = true))
	FNDIMediaReceiverMetaDataReceived OnReceiverMetaDataReceived;

public:

	UNDIMediaReceiver();

	/**
	   Called before destroying the object.  This is called immediately upon deciding to destroy the object,
	   to allow the object to begin an asynchronous cleanup process.
	 */
	void BeginDestroy() override;

	/**
		Attempts to perform initialization logic for creating a receiver through the NDI sdk api
	*/
	enum class EUsage
	{
		Standalone,	// The receiver automatically captures its own video frame every engine render frame
		Controlled	// The user of the receiver manually triggers capturing a frame through CaptureConnectedVideo/Audio()
	};
	bool Initialize(const FNDIConnectionInformation& InConnectionInformation, EUsage InUsage);
	bool Initialize(EUsage Inusage);

	/**
		Attempt to (re-)start the connection
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Start Connection"))
	void StartConnection();

	/**
		Stop the connection
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Stop Connection"))
	void StopConnection();

	/**
		Attempts to change the connection to another NDI sender source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Change Connection"))
	void ChangeConnection(const FNDIConnectionInformation& InConnectionInformation);

	/**
		Attempts to change the Video Texture object used as the video frame capture object
	*/
	UFUNCTION(BlueprintSetter)
	void ChangeVideoTexture(UNDIMediaTexture2D* InVideoTexture = nullptr);

	/**
		Attempts to generate the pcm data required by the 'AudioWave' object
	*/
	int32 GeneratePCMData(UNDIMediaSoundWave* AudioWave, uint8* PCMData, const int32 SamplesNeeded);
	int32 GetAudioChannels();

	/**
		Attempts to register a sound wave object with this object
	*/
	void RegisterAudioWave(UNDIMediaSoundWave* InAudioWave = nullptr);

	/**
		This will send a metadata frame to the sender
		The data is expected to be valid XML
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Sender"))
	void SendMetadataFrame(const FString& Data);
	/**
		This will send a metadata frame to the sender
		The data will be formatted as: <Element>ElementData</Element>
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Sender (Element + Data)"))
	void SendMetadataFrameAttr(const FString& Element, const FString& ElementData);
	/**
		This will send a metadata frame to the sender
		The data will be formatted as: <Element Key0="Value0" Key1="Value1" Keyn="Valuen"/>
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Sender (Element + Attributes)"))
	void SendMetadataFrameAttrs(const FString& Element, const TMap<FString,FString>& Attributes);

	/**
		This will set the up-stream tally notifications. If no streams are connected, it will automatically
		send the tally state upon connection
	*/
	void SendTallyInformation(const bool& IsOnPreview, const bool& IsOnProgram);

	/**
		Attempts to immediately stop receiving frames from the connected NDI sender
	*/
	void Shutdown();

	/**
		Remove the AudioWave object from this object (if it was previously registered)

		@param InAudioWave An NDIMediaSoundWave object registered with this object
	*/
	void UnregisterAudioWave(UNDIMediaSoundWave* InAudioWave = nullptr);

	/**
		Updates the DynamicMaterial with the VideoTexture of this object
	*/
	void UpdateMaterialTexture(class UMaterialInstanceDynamic* MaterialInstance, FString ParameterName);

	/**
		Attempts to capture a frame from the connected source.  If a new frame is captured, broadcast it to
		interested receivers through the capture event.  Returns true if new data was captured.
	*/
	bool CaptureConnectedVideo();
	bool CaptureConnectedAudio();
	bool CaptureConnectedMetadata();

	/**
		Attempts to immediately update the 'VideoTexture' object with the captured video frame
	*/
	FTextureRHIRef DisplayFrame(const NDIlib_video_frame_v2_t& video_frame);

private:
	void SetIsCurrentlyConnected(bool bConnected);

	/**
		Attempts to gather the performance metrics of the connection to the remote source
	*/
	void GatherPerformanceMetrics();

public:
	/**
		Set whether or not a RGB to Linear conversion is made
	*/
	void PerformsRGBToLinearConversion(bool Value);

	/**
		Returns the current framerate of the connected source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Get Current Frame Rate"))
	const FFrameRate& GetCurrentFrameRate() const;

	/**
		Returns the current resolution of the connected source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Get Current Resolution"))
	const FIntPoint& GetCurrentResolution() const;

	/**
		Returns the current timecode of the connected source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Get Current Timecode"))
	const FTimecode& GetCurrentTimecode() const;

	/**
		Returns the current connection information of the connected source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Get Current Connection Information"))
	const FNDIConnectionInformation& GetCurrentConnectionInformation() const;

	/**
		Returns the current performance data of the receiver while connected to the source
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Get Performance Data"))
	const FNDIReceiverPerformanceData& GetPerformanceData() const;

	/** Returns a value indicating whether this object is currently connected to the sender source */
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Is Currently Connected"))
	const bool GetIsCurrentlyConnected() const;

private:
	/**
		Perform the color conversion (if any) and bit copy from the gpu
	*/
	FTextureRHIRef DrawProgressiveVideoFrame(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result);
	FTextureRHIRef DrawProgressiveVideoFrameAlpha(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result);
	FTextureRHIRef DrawInterlacedVideoFrame(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result);
	FTextureRHIRef DrawInterlacedVideoFrameAlpha(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result);

	virtual bool Validate() const override
	{
		return true;
	}
	virtual FString GetUrl() const override;

	FTextureResource* GetVideoTextureResource() const;
	FTextureResource* GetInternalVideoTextureResource() const;

#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	virtual bool GetMediaOption(const FName& Key, bool DefaultValue) const override;
	virtual int64 GetMediaOption(const FName& Key, int64 DefaultValue) const override;
	virtual FString GetMediaOption(const FName& Key, const FString& DefaultValue) const override;
	virtual bool HasMediaOption(const FName& Key) const override;

private:
	int64_t LastFrameTimestamp = 0;
	NDIlib_frame_format_type_e LastFrameFormatType = NDIlib_frame_format_type_max;

	bool bIsCurrentlyConnected = false;

	NDIlib_recv_instance_t p_receive_instance = nullptr;
	NDIlib_framesync_instance_t p_framesync_instance = nullptr;

	FCriticalSection RenderSyncContext;
	FCriticalSection AudioSyncContext;
	FCriticalSection MetadataSyncContext;
	FCriticalSection ConnectionSyncContext;

	TArray<UNDIMediaSoundWave*> AudioSourceCollection;

	UNDIMediaTexture2D* InternalVideoTexture = nullptr;

	FTexture2DRHIRef SourceTexture;
	FTexture2DRHIRef SourceAlphaTexture;
	FPooledRenderTargetDesc RenderTargetDescriptor;
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
	enum class EDrawMode
	{
		Invalid,
		Progressive,
		ProgressiveAlpha,
		Interlaced,
		InterlacedAlpha
	};
	EDrawMode DrawMode = EDrawMode::Invalid;

	FDelegateHandle FrameEndRTHandle;
	FDelegateHandle VideoCaptureEventHandle;
};