/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <NDIIOPluginAPI.h>

#include <RendererInterface.h>
#include <UObject/Object.h>
#include <Misc/FrameRate.h>
#include <Engine/TextureRenderTarget2D.h>
#include <Structures/NDIBroadcastConfiguration.h>
#include <Objects/Media/NDIMediaTexture2D.h>
#include <BaseMediaSource.h>

#include <string>

#include "NDIMediaSender.generated.h"

/**
	A delegate used for notifications on property changes on the NDIMediaSender object
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderPropertyChanged, UNDIMediaSender*, Sender);

/**
	A delegate used for notifications on the NDIMediaSender object receiving metadata
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNDIMediaSenderMetaDataReceived, UNDIMediaSender*, Sender, FString, Data);

/**
	Delegates to notify just before and after the NDIMediaSender sends a video, audio, or metadata frame
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderVideoPreSend, UNDIMediaSender*, Sender);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderVideoSent, UNDIMediaSender*, Sender);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderAudioPreSend, UNDIMediaSender*, Sender);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderAudioSent, UNDIMediaSender*, Sender);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderMetaDataPreSend, UNDIMediaSender*, Sender);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIMediaSenderMetaDataSent, UNDIMediaSender*, Sender);

/**
	Defines a media object representing an NDI(R) Sender object. This object is used with the
	NDI Broadcast Component to send Audio / Video / Metadata to a 'receiving' NDI object.
*/
UCLASS(BlueprintType, Blueprintable, HideCategories = ("Platforms"), Category = "NDI IO",
		HideCategories = ("Information"), AutoCollapseCategories = ("Content"),
		META = (DisplayName = "NDI Sender Object"))
class NDIIO_API UNDIMediaSender : public UBaseMediaSource
{
	GENERATED_UCLASS_BODY()

private:
	/** Describes a user-friendly name of the output stream to differentiate from other output streams on the current
	 * machine */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Broadcast Settings",
			  META = (DisplayName = "Source Name", AllowPrivateAccess = true))
	FString SourceName = TEXT("Unreal Engine Output");

	/** Describes the output frame size while sending video frame over NDI */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Broadcast Settings",
			  META = (DisplayName = "Frame Size", AllowPrivateAccess = true))
	FIntPoint FrameSize = FIntPoint(1920, 1080);

	/** Represents the desired number of frames (per second) for video to be sent over NDI */
	UPROPERTY(BlueprintReadwrite, EditDefaultsOnly, Category = "Broadcast Settings",
			  META = (DisplayName = "Frame Rate", AllowPrivateAccess = true))
	FFrameRate FrameRate = FFrameRate(60, 1);

	/** Sets whether or not to output an alpha channel */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Broadcast Settings", 
			  META = (DisplayName="Output Alpha", AllowPrivateAccess = true))
	bool OutputAlpha = false;

	UPROPERTY(BlueprintReadonly, VisibleAnywhere, Category = "Broadcast Settings",
			  META = (DisplayName = "Alpha Remap Min", AllowPrivateAccess = true))
	float AlphaMin = 0.f;

	UPROPERTY(BlueprintReadonly, VisibleAnywhere, Category = "Broadcast Settings",
			  META = (DisplayName = "Alpha Remap Max", AllowPrivateAccess = true))
	float AlphaMax = 1.f;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Broadcast Settings", 
			  META = (DisplayName="Enable Audio", AllowPrivateAccess = true))
	bool bEnableAudio = true;

	/** Sets whether or not to present PTZ capabilities */
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "Broadcast Settings", 
			  META = (DisplayName="Enable PTZ", AllowPrivateAccess = true))
	bool bEnablePTZ = true;

	/** Indicates the texture to send over NDI (optional) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Content",
			  AdvancedDisplay, META = (DisplayName = "Render Target (optional)", AllowPrivateAccess = true))
	UTextureRenderTarget2D* RenderTarget = nullptr;

	/**
		Should perform the Linear to sRGB color space conversion
	*/
	UPROPERTY(BlueprintReadonly, VisibleAnywhere, Category = "Information",
			  META = (DisplayName = "Perform Linear to sRGB?", AllowPrivateAccess = true))
	bool bPerformLinearTosRGB = true;

public:
	UPROPERTY()
	FNDIMediaSenderPropertyChanged OnBroadcastConfigurationChanged;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On MetaData Received by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderMetaDataReceived OnSenderMetaDataReceived;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Before Video Being Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderVideoPreSend OnSenderVideoPreSend;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Video Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderVideoSent OnSenderVideoSent;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Before Audio Being Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderAudioPreSend OnSenderAudioPreSend;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Audio Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderAudioSent OnSenderAudioSent;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On Before MetaData Being Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderMetaDataPreSend OnSenderMetaDataPreSend;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On MetaData Sent by Sender", AllowPrivateAccess = true))
	FNDIMediaSenderMetaDataSent OnSenderMetaDataSent;

public:
	/**
		Attempts to perform initialization logic for creating a sender through the NDI(R) sdk api
	*/
	void Initialize();

	/**
		Changes the name of the sender object as seen on the network for remote connections
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Change Source Name"))
	void ChangeSourceName(const FString& InSourceName);

	/**
		Attempts to change the Broadcast information associated with this media object
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Change Broadcast Configuration"))
	void ChangeBroadcastConfiguration(const FNDIBroadcastConfiguration& InConfiguration);

	/**
		This will send a metadata frame to all receivers
		The data is expected to be valid XML
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Receivers"))
	void SendMetadataFrame(const FString& Data, bool AttachToVideoFrame = true);
	/**
		This will send a metadata frame to all receivers
		The data will be formatted as: <Element>ElementData</Element>
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Receivers (Element + Data)"))
	void SendMetadataFrameAttr(const FString& Element, const FString& ElementData, bool AttachToVideoFrame = true);
	/**
		This will send a metadata frame to all receivers
		The data will be formatted as: <Element Key0="Value0" Key1="Value1" Keyn="Valuen"/>
	*/
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Send Metadata To Receivers (Element + Attributes)"))
	void SendMetadataFrameAttrs(const FString& Element, const TMap<FString,FString>& Attributes, bool AttachToVideoFrame = true);

	/**
		Attempts to change the RenderTarget used in sending video frames over NDI
	*/
	void ChangeVideoTexture(UTextureRenderTarget2D* VideoTexture = nullptr);

	/**
		Change the alpha remapping settings
	*/
	void ChangeAlphaRemap(float AlphaMinIn, float AlphaMaxIn);

	/**
		Determines the current tally information. If you specify a timeout then it will wait until it has
		changed, otherwise it will simply poll it and return the current tally immediately

		@param IsOnPreview - A state indicating whether this source in on preview of a receiver
		@param IsOnProgram - A state indicating whether this source is on program of a receiver
		@param TimeOut	- Indicates the amount of time to wait (in milliseconds) until a change has occurred
	*/
	void GetTallyInformation(bool& IsOnPreview, bool& IsOnProgram, uint32 Timeout = 0);

	/**
		Gets the current number of receivers connected to this source. This can be used to avoid rendering
		when nothing is connected to the video source. which can significantly improve the efficiency if
		you want to make a lot of sources available on the network
	*/
	void GetNumberOfConnections(int32& Result);

	/**
		Attempts to immediately stop sending frames over NDI to any connected receivers
	*/
	void Shutdown();

	/**
	   Called before destroying the object.  This is called immediately upon deciding to destroy the object,
	   to allow the object to begin an asynchronous cleanup process.
	 */
	virtual void BeginDestroy() override;

	/**
		Set whether or not a RGB to Linear conversion is made
	*/
	void PerformLinearTosRGBConversion(bool Value);

	/**
		Set whether or not to enable PTZ support
	*/
	void EnablePTZ(bool Value);

	/**
		Returns the Render Target used for sending a frame over NDI
	*/
	UTextureRenderTarget2D* GetRenderTarget();

	const FIntPoint& GetFrameSize()
	{
		return this->FrameSize;
	}

	const FFrameRate& GetFrameRate()
	{
		return this->FrameRate;
	}

private:

	bool CreateSender();

	/**
		Attempts to get a metadata frame from the sender.
		If there is one, the data is broadcast through OnSenderMetaDataReceived.
		Returns true if metadata was received, false otherwise.
	*/
	bool GetMetadataFrame();

	/**
		This will attempt to generate an audio frame, add the frame to the stack and return immediately,
		having scheduled the frame asynchronously.
	*/
	void TrySendAudioFrame(int64 time_code, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock);

	/**
		This will attempt to generate a video frame, add the frame to the stack and return immediately,
		having scheduled the frame asynchronously.
	*/
	void TrySendVideoFrame(int64 time_code = 0);

	/**
		Perform the color conversion (if any) and bit copy from the gpu
	*/
	bool DrawRenderTarget(FRHICommandListImmediate& RHICmdList);

	/**
		Change the render target configuration based on the passed in parameters

		@param InFrameSize The frame size to resize the render target to
		@param InFrameRate The frame rate at which we should be sending frames via NDI
	*/
	void ChangeRenderTargetConfiguration(FIntPoint InFrameSize, FFrameRate InFrameRate);

	virtual bool Validate() const override
	{
		return true;
	}
	virtual FString GetUrl() const override
	{
		return FString();
	}

	FTextureResource* GetRenderTargetResource() const;

	void PrepareDefaultTexture();

private:
	std::atomic<bool> bIsChangingBroadcastSize { false };

	FTimecode LastRenderTime;

	FTexture2DRHIRef DefaultVideoTextureRHI;

	TArray<float> SendAudioData;

	NDIlib_video_frame_v2_t NDI_video_frame;
	NDIlib_send_instance_t p_send_instance = nullptr;

	FCriticalSection AudioSyncContext;
	FCriticalSection RenderSyncContext;

	/**
		A texture with CPU readback
	*/
	class MappedTexture
	{
	private:
		FTexture2DRHIRef Texture = nullptr;
		void* pData = nullptr;
		std::string MetaData;
		FIntPoint FrameSize;

	public:
		~MappedTexture();

		void Create(FIntPoint FrameSize);
		void Destroy();

		FIntPoint GetSizeXY() const;

		void Resolve(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTextureRHI, const FResolveRect& Rect = FResolveRect(), const FResolveRect& DestRect = FResolveRect());

		void Map(FRHICommandListImmediate& RHICmdList, int32& OutWidth, int32& OutHeight, int32& OutLineStride);
		void* MappedData() const;
		void Unmap(FRHICommandListImmediate& RHICmdList);

		void AddMetaData(const FString& Data);
		const std::string& GetMetaData() const;

	private:
		void PrepareTexture();
	};

	/**
		Class for managing the sending of mapped texture data to an NDI video stream.
		Sending is done asynchronously, so mapping and unmapping of texture data must
		be managed so that CPU accessible texture content remains valid until the
		sending of the frame is guaranteed to have been completed. This is achieved
		by double-buffering readback textures.
	*/
	class MappedTextureASyncSender
	{
	private:
		MappedTexture MappedTextures[2];
		int32 CurrentIndex = 0;

	public:
		void Create(FIntPoint FrameSize);
		void Destroy();

		FIntPoint GetSizeXY() const;

		void Resolve(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTextureRHI, const FResolveRect& Rect = FResolveRect(), const FResolveRect& DestRect = FResolveRect());

		void Map(FRHICommandListImmediate& RHICmdList, int32& OutWidth, int32& OutHeight, int32& OutLineStride);
		void Send(FRHICommandListImmediate& RHICmdList, NDIlib_send_instance_t p_send_instance, NDIlib_video_frame_v2_t& p_video_data);
		void Flush(FRHICommandListImmediate& RHICmdList, NDIlib_send_instance_t p_send_instance);

		void AddMetaData(const FString& Data);
	};

	MappedTextureASyncSender ReadbackTextures;
	bool ReadbackTexturesHaveAlpha = false;
	FPooledRenderTargetDesc RenderTargetDescriptor;
};
