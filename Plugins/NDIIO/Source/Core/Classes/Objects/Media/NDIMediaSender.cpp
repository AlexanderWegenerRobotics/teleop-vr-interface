/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Objects/Media/NDIMediaSender.h>
#include <Misc/CoreDelegates.h>
#include <RenderTargetPool.h>
#include <TextureResource.h>
#include <PipelineStateCache.h>

#include <GlobalShader.h>
#include <ShaderParameterUtils.h>
#include <Services/NDIConnectionService.h>
#include <MediaShaders.h>

#include <Async/Async.h>

#include <Misc/EngineVersionComparison.h>

#include "NDIShaders.h"

#if WITH_EDITOR
#include <Editor.h>
#endif

#include <string>


#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later

static FBufferRHIRef CreateColorVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICmdList.CreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHICmdList.LockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set(-1.0f,  1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f,  1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set(-1.0f,  1.0f,      1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f,  1.0f,      1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHICmdList.UnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

static FBufferRHIRef CreateAlphaEvenVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICmdList.CreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHICmdList.LockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set(-1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 0.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f,      1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 0.0f, -1.0f,      1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHICmdList.UnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

static FBufferRHIRef CreateAlphaOddVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICmdList.CreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHICmdList.LockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set( 0.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set( 0.0f, -1.0f,      1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f,      1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHICmdList.UnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

#elif ENGINE_MAJOR_VERSION == 5	// Before 5.3

static FBufferRHIRef CreateColorVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHILockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set(-1.0f,  1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f,  1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set(-1.0f,  1.0f,      1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f,  1.0f,      1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHIUnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

static FBufferRHIRef CreateAlphaEvenVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHILockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set(-1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 0.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set(-1.0f, -1.0f,      1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 0.0f, -1.0f,      1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHIUnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

static FBufferRHIRef CreateAlphaOddVertexBuffer(FRHICommandListImmediate& RHICmdList, const FIntPoint& FitFrameSize, const FIntPoint& DrawFrameSize, bool OutputAlpha)
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VertexBufferRHI"));
	FBufferRHIRef VertexBufferRHI = RHICreateVertexBuffer(sizeof(FMediaElementVertex) * 4, BUF_Volatile, CreateInfo);

	void* VoidPtr = RHILockBuffer(VertexBufferRHI, 0, sizeof(FMediaElementVertex) * 4, RLM_WriteOnly);

	FMediaElementVertex* Vertices = (FMediaElementVertex*)VoidPtr;
	if (OutputAlpha == false)
	{
		Vertices[0].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set( 0.0f, -1.0f, 1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f, 1.0f, 1.0f); // Bottom Right
	}
	else
	{
		Vertices[0].Position.Set( 0.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Left
		Vertices[1].Position.Set( 1.0f, -1.0f/3.0f, 1.0f, 1.0f); // Top Right
		Vertices[2].Position.Set( 0.0f, -1.0f,      1.0f, 1.0f); // Bottom Left
		Vertices[3].Position.Set( 1.0f, -1.0f,      1.0f, 1.0f); // Bottom Right
	}

	Vertices[0].TextureCoordinate.Set(0.0f, 0.0f);
	Vertices[1].TextureCoordinate.Set(1.0f, 0.0f);
	Vertices[2].TextureCoordinate.Set(0.0f, 1.0f);
	Vertices[3].TextureCoordinate.Set(1.0f, 1.0f);

	RHIUnlockBuffer(VertexBufferRHI);

	return VertexBufferRHI;
}

#else
	#error "Unsupported engine major version"
#endif







UNDIMediaSender::UNDIMediaSender(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}



/**
	Attempts to perform initialization logic for creating a sender through the NDI(R) sdk api
*/
void UNDIMediaSender::Initialize()
{
	if (this->p_send_instance == nullptr)
	{
		// Create valid settings to be seen on the network
		CreateSender();

		// If it's valid then lets do some engine related setup
		if (p_send_instance != nullptr)
		{
			// Update the Render Target Configuration
			ChangeRenderTargetConfiguration(FrameSize, FrameRate);

			// Send audio frames at the end of the 'update' loop
			FNDIConnectionService::AddAudioSender(this, &UNDIMediaSender::TrySendAudioFrame);

			// We don't want to limit the engine rendering speed to the sync rate of the connection hook
			// into the core delegates render thread 'EndFrame'
			FNDIConnectionService::EventOnSendVideoFrame.AddUObject(this, &UNDIMediaSender::TrySendVideoFrame);

			// Initialize the 'LastRender' timecode
			LastRenderTime = FTimecode::FromTimespan(0, FrameRate, FTimecode::IsDropFormatTimecodeSupported(FrameRate),
													 true // use roll-over timecode
			);

#if UE_EDITOR

			// We don't want to provide perceived issues with the plugin not working so
			// when we get a Pre-exit message, forcefully shutdown the receiver
			FCoreDelegates::OnPreExit.AddWeakLambda(this, [&]() {
				this->Shutdown();
				FCoreDelegates::OnPreExit.RemoveAll(this);
			});

			// We handle this in the 'Play In Editor' versions as well.
			FEditorDelegates::PrePIEEnded.AddWeakLambda(this, [&](const bool) {
				this->Shutdown();
				FEditorDelegates::PrePIEEnded.RemoveAll(this);
			});

#endif
		}
	}
}

void UNDIMediaSender::PrepareDefaultTexture()
{
	if (!DefaultVideoTextureRHI.IsValid())
	{
			// Default to 240p
			static int32 DefaultWidth = 352;
			static int32 DefaultHeight = 240;

			// Set the default video texture to reference nothing
			TRefCountPtr<FRHITexture2D> RenderableTexture;

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
			const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaSenderInitializeTexture"))
				.SetExtent(DefaultWidth, DefaultHeight)
				.SetFormat(PF_B8G8R8A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable)
				.SetClearValue(FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f)));

			RenderableTexture = RHICreateTexture(CreateDesc);
#elif ENGINE_MAJOR_VERSION == 5
			TRefCountPtr<FRHITexture2D> ShaderTexture2D;

			FRHIResourceCreateInfo CreateInfo = { TEXT("NDIMediaSenderInitializeTexture"),
												 FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f)) };

			RHICreateTargetableShaderResource2D(DefaultWidth, DefaultHeight, PF_B8G8R8A8, 1, TexCreate_None,
				TexCreate_RenderTargetable, false, CreateInfo, RenderableTexture,
				ShaderTexture2D);
#else
			#error "Unsupported engine major version"
#endif

			DefaultVideoTextureRHI = (FTexture2DRHIRef&)RenderableTexture;
	}
}

bool UNDIMediaSender::CreateSender()
{
	if (p_send_instance != nullptr)
	{
		// free up the old sender instance
		NDIlib_send_destroy(p_send_instance);

		p_send_instance = nullptr;
	}

	// Create valid settings to be seen on the network
	NDIlib_send_create_t settings;
	settings.clock_audio = false;
	settings.clock_video = false;
	// Beware of the limited lifetime of TCHAR_TO_UTF8 values
	std::string SourceNameStr(TCHAR_TO_UTF8(*this->SourceName));
	settings.p_ndi_name = SourceNameStr.c_str();

	// create the instance and store it
	p_send_instance = NDIlib_send_create(&settings);

	if (p_send_instance != nullptr)
	{
		// We are going to mark this as if it was a PTZ camera.
		NDIlib_metadata_frame_t NDI_capabilities;
		if (bEnablePTZ == true)
			NDI_capabilities.p_data = const_cast<char*>("<ndi_capabilities"
			                                            " ntk_ptz=\"true\""
			                                            " ntk_pan_tilt=\"true\""
			                                            " ntk_zoom=\"true\""
			                                            " ntk_iris=\"false\""
			                                            " ntk_white_balance=\"false\""
			                                            " ntk_exposure=\"false\""
			                                            " ntk_record=\"false\""
			                                            "/>");
		else
			NDI_capabilities.p_data = const_cast<char*>("<ndi_capabilities ntk_ptz=\"false\"/>");
		NDIlib_send_add_connection_metadata(p_send_instance, &NDI_capabilities);
	}

	return p_send_instance != nullptr ? true : false;
}


/**
	Changes the name of the sender object as seen on the network for remote connections
*/
void UNDIMediaSender::ChangeSourceName(const FString& InSourceName)
{
	this->SourceName = InSourceName;

	if (p_send_instance != nullptr)
	{
		FScopeLock AudioLock(&AudioSyncContext);
		FScopeLock RenderLock(&RenderSyncContext);

		// Get the command list interface
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// send an empty frame over NDI to be able to cleanup the buffers
		ReadbackTextures.Flush(RHICmdList, p_send_instance);

		CreateSender();
	}
}

/**
	Attempts to change the Broadcast information associated with this media object
*/
void UNDIMediaSender::ChangeBroadcastConfiguration(const FNDIBroadcastConfiguration& InConfiguration)
{
	bIsChangingBroadcastSize = true;

	// Determine if we need to prevent the audio / video threads from updating frames
	if (p_send_instance != nullptr)
	{
		FScopeLock AudioLock(&AudioSyncContext);
		FScopeLock RenderLock(&RenderSyncContext);

		// Get the command list interface
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// send an empty frame over NDI to be able to cleanup the buffers
		ReadbackTextures.Flush(RHICmdList, p_send_instance);
	}

	// Change the render target configuration based on the incoming configuration
	ChangeRenderTargetConfiguration(InConfiguration.FrameSize, InConfiguration.FrameRate);

	bIsChangingBroadcastSize = false;
}

/**
	This will attempt to generate an audio frame, add the frame to the stack and return immediately,
	having scheduled the frame asynchronously.
*/
void UNDIMediaSender::TrySendAudioFrame(int64 time_code, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	if (bEnableAudio && (p_send_instance != nullptr) && (!bIsChangingBroadcastSize))
	{
		FScopeLock Lock(&AudioSyncContext);

		if (NDIlib_send_get_no_connections(p_send_instance, 0) > 0)
		{
			// Convert from the interleaved audio that Unreal Engine produces

			NDIlib_audio_frame_interleaved_32f_t NDI_interleaved_audio_frame;
			NDI_interleaved_audio_frame.timecode = time_code;
			NDI_interleaved_audio_frame.sample_rate = SampleRate;
			NDI_interleaved_audio_frame.no_channels = NumChannels;
			NDI_interleaved_audio_frame.no_samples = NumSamples / NumChannels;
			NDI_interleaved_audio_frame.p_data = AudioData;

			NDIlib_audio_frame_v2_t NDI_audio_frame;
			SendAudioData.Reset(NumSamples);
			NDI_audio_frame.p_data = SendAudioData.GetData();
			NDI_audio_frame.channel_stride_in_bytes = (NumSamples / NumChannels) * sizeof(float);

			NDIlib_util_audio_from_interleaved_32f_v2(&NDI_interleaved_audio_frame, &NDI_audio_frame);


			OnSenderAudioPreSend.Broadcast(this);

			NDIlib_send_send_audio_v2(p_send_instance, &NDI_audio_frame);

			OnSenderAudioSent.Broadcast(this);
		}
	}
}

/**
	This will attempt to generate a video frame, add the frame to the stack and return immediately,
	having scheduled the frame asynchronously.
*/
void UNDIMediaSender::TrySendVideoFrame(int64 time_code)
{
	// This function is called on the Engine's Main Rendering Thread. Be very careful when doing stuff here.
	// Make sure things are done quick and efficient.

	if (p_send_instance != nullptr && !bIsChangingBroadcastSize)
	{
		FScopeLock Lock(&RenderSyncContext);

		while(GetMetadataFrame())
			; // Potential improvement: limit how much metadata is processed, to avoid appearing to lock up due to a metadata flood

		if (GetRenderTargetResource() != nullptr)
		{
			// Alright time to perform the magic :D
			if (NDIlib_send_get_no_connections(p_send_instance, 0) > 0)
			{
				FTimecode RenderTimecode =
					FTimecode::FromTimespan(FTimespan::FromSeconds(time_code / (float)1e+7), FrameRate,
											FTimecode::IsDropFormatTimecodeSupported(FrameRate),
											true // use roll-over timecode
					);

				if (RenderTimecode.Frames != LastRenderTime.Frames)
				{
					// Get the command list interface
					FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

					// alright, lets hope the render target hasn't changed sizes
					NDI_video_frame.timecode = time_code;

					// performing color conversion if necessary and copy pixels into the data buffer for sending
					if (DrawRenderTarget(RHICmdList))
					{
						int32 Width = 0, Height = 0, LineStride = 0;

						// Map the staging surface so we can copy the buffer for the NDI SDK to use
						ReadbackTextures.Map(RHICmdList, Width, Height, LineStride);
						// Width and height are the size of the readback texture, and not the framesize represented
						// Readback texture is used in 4:2:2 format, so actual width in pixels is double
						Width *= 2;
						// Readback texture may be extended in height to accomodate alpha values; remove it
						if (ReadbackTexturesHaveAlpha == true)
							Height = (2*Height) / 3;

						NDI_video_frame.line_stride_in_bytes = LineStride;

						// If we don't have a draw result, ensure we send an empty frame and resize our frame
						if (FrameSize != FIntPoint(Width, Height))
						{
							// send an empty frame over NDI to be able to cleanup the buffers
							ReadbackTextures.Flush(RHICmdList, p_send_instance);

							// Do not hold the lock when going into ChangeRenderTargetConfiguration()
							Lock.Unlock();

							// Change the render target configuration based on what the RHI determines the size to be
							ChangeRenderTargetConfiguration(FIntPoint(Width, Height), this->FrameRate);
						}
						else
						{
							OnSenderVideoPreSend.Broadcast(this);

							// send the frame over NDI
							ReadbackTextures.Send(RHICmdList, p_send_instance, NDI_video_frame);

							// Update the Last Render Time to the current Render Timecode
							LastRenderTime = RenderTimecode;

							OnSenderVideoSent.Broadcast(this);
						}
					}
				}
			}
		}
	}
}

/**
	Perform the color conversion (if any) and bit copy from the gpu
*/
bool UNDIMediaSender::DrawRenderTarget(FRHICommandListImmediate& RHICmdList)
{
	bool DrawResult = false;

	// We should only do conversions and pixel copies, if we have something to work with
	if (!bIsChangingBroadcastSize && (GetRenderTargetResource() != nullptr))
	{
		// Get the underlying texture to use for the color conversion
		FTexture2DRHIRef SourceTexture = (FTexture2DRHIRef&)GetRenderTargetResource()->TextureRHI;

		// Validate the Source Texture
		if (SourceTexture.IsValid())
		{
			// We have something to draw
			DrawResult = true;

			TRefCountPtr<IPooledRenderTarget> RenderTargetTexturePooled;

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDescriptor, RenderTargetTexturePooled, TEXT("NDIIO"));

			FRHITexture* TargetableTexture = RenderTargetTexturePooled->GetRHI();

			PrepareDefaultTexture();

			// Get the target size of the conversion
			FIntPoint TargetSize = SourceTexture->GetSizeXY();

			// Calculate the rectangle in which to draw the source, maintaining aspect ratio
			float FrameRatio = FrameSize.X / (float)FrameSize.Y;
			float TargetRatio = TargetSize.X / (float)TargetSize.Y;

			FIntPoint NewFrameSize = FrameSize;

			if (TargetRatio > FrameRatio)
			{
				// letterbox
				NewFrameSize.Y = FMath::RoundToInt(FrameSize.X / TargetRatio);
			}
			else if (TargetRatio < FrameRatio)
			{
				// pillarbox
				NewFrameSize.X = FMath::RoundToInt(FrameSize.Y * TargetRatio);
			}

			float ULeft   = (NewFrameSize.X - FrameSize.X) / (float)(2*NewFrameSize.X);
			float URight  = (NewFrameSize.X + FrameSize.X) / (float)(2*NewFrameSize.X);
			float VTop    = (NewFrameSize.Y - FrameSize.Y) / (float)(2*NewFrameSize.Y);
			float VBottom = (NewFrameSize.Y + FrameSize.Y) / (float)(2*NewFrameSize.Y);

			FBufferRHIRef ColorVertexBuffer = CreateColorVertexBuffer(RHICmdList, FrameSize, NewFrameSize, this->OutputAlpha);
			FBufferRHIRef AlphaEvenVertexBuffer = CreateAlphaEvenVertexBuffer(RHICmdList, FrameSize, NewFrameSize, this->OutputAlpha);
			FBufferRHIRef AlphaOddVertexBuffer = CreateAlphaOddVertexBuffer(RHICmdList, FrameSize, NewFrameSize, this->OutputAlpha);

			// Initialize the Graphics Pipeline State Object
			FGraphicsPipelineStateInitializer GraphicsPSOInit;

			// Configure shaders
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			// Construct the shaders
			TShaderMapRef<FNDIIOShaderVS> VertexShader(ShaderMap);
			TShaderMapRef<FNDIIOShaderBGRAtoUYVYPS> ConvertShader(ShaderMap);
			TShaderMapRef<FNDIIOShaderBGRAtoAlphaEvenPS> ConvertAlphaEvenShader(ShaderMap);
			TShaderMapRef<FNDIIOShaderBGRAtoAlphaOddPS> ConvertAlphaOddShader(ShaderMap);

			// Scaled drawing pass with conversion to UYVY
			{
				// Initialize the Render pass with the conversion texture
				FRHITexture* ConversionTexture = TargetableTexture;
				FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

				RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Send Scaling Conversion"));

				// Do as it suggests
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				// Set the state objects
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
																		CW_NONE, CW_NONE, CW_NONE>::GetRHI();
				// Perform binding operations for the shaders to be used
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
				// Going to draw triangle strips
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

				// Ensure the pipeline state is set to the one we've configured
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				// Set the stream source
				RHICmdList.SetStreamSource(0, ColorVertexBuffer, 0);

				// Set the texture parameter of the conversion shader
				FNDIIOShaderBGRAtoUYVYPS::Params Params(SourceTexture, DefaultVideoTextureRHI, FrameSize,
				                                        FVector2D(ULeft, VTop), FVector2D(URight-ULeft, VBottom-VTop),
				                                        bPerformLinearTosRGB ? FNDIIOShaderPS::EColorCorrection::LinearTosRGB : FNDIIOShaderPS::EColorCorrection::None,
				                                        FVector2D(this->AlphaMin, this->AlphaMax));
				ConvertShader->SetParameters(RHICmdList, Params);

				// Draw the texture
				RHICmdList.DrawPrimitive(0, 2, 1);

				// Release the reference to SourceTexture from the shader
				// The SourceTexture may be the viewport's backbuffer, and Unreal does not like
				// extra references to the backbuffer when the viewport is resized
				Params.InputTarget = DefaultVideoTextureRHI;
				ConvertShader->SetParameters(RHICmdList, Params);

				RHICmdList.EndRenderPass();
			}

			// Scaled drawing pass with conversion to the alpha part of UYVA
			if (this->OutputAlpha == true)
			{
				// Alpha even-numbered lines
				{
					// Initialize the Render pass with the conversion texture
					FRHITexture* ConversionTexture = TargetableTexture;
					FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

					RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Send Scaling Conversion"));

					// Do as it suggests
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					// Set the state objects
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
					                                                        CW_NONE, CW_NONE, CW_NONE>::GetRHI();
					// Perform binding operations for the shaders to be used
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertAlphaEvenShader.GetPixelShader();
					// Going to draw triangle strips
					GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

					// Ensure the pipeline state is set to the one we've configured
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					// Set the stream source
					RHICmdList.SetStreamSource(0, AlphaEvenVertexBuffer, 0);

					// Set the texture parameter of the conversion shader
					FNDIIOShaderBGRAtoAlphaEvenPS::Params Params(SourceTexture, DefaultVideoTextureRHI, FrameSize,
					                                             FVector2D(ULeft, VTop), FVector2D(URight-ULeft, VBottom-VTop),
					                                             bPerformLinearTosRGB ? FNDIIOShaderPS::EColorCorrection::LinearTosRGB : FNDIIOShaderPS::EColorCorrection::None,
					                                             FVector2D(this->AlphaMin, this->AlphaMax));
					ConvertAlphaEvenShader->SetParameters(RHICmdList, Params);

					// Draw the texture
					RHICmdList.DrawPrimitive(0, 2, 1);

					// Release the reference to SourceTexture from the shader
					// The SourceTexture may be the viewport's backbuffer, and Unreal does not like
					// extra references to the backbuffer when the viewport is resized
					Params.InputTarget = DefaultVideoTextureRHI;
					ConvertAlphaEvenShader->SetParameters(RHICmdList, Params);

					RHICmdList.EndRenderPass();
				}

				// Alpha odd-numbered lines
				{
					// Initialize the Render pass with the conversion texture
					FRHITexture* ConversionTexture = TargetableTexture;
					FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

					RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Send Scaling Conversion"));

					// Do as it suggests
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					// Set the state objects
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
					                                                        CW_NONE, CW_NONE, CW_NONE>::GetRHI();
					// Perform binding operations for the shaders to be used
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertAlphaOddShader.GetPixelShader();
					// Going to draw triangle strips
					GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

					// Ensure the pipeline state is set to the one we've configured
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					// Set the stream source
					RHICmdList.SetStreamSource(0, AlphaOddVertexBuffer, 0);

					// Set the texture parameter of the conversion shader
					FNDIIOShaderBGRAtoAlphaOddPS::Params Params(SourceTexture, DefaultVideoTextureRHI, FrameSize,
					                                            FVector2D(ULeft, VTop), FVector2D(URight-ULeft, VBottom-VTop),
					                                            bPerformLinearTosRGB ? FNDIIOShaderPS::EColorCorrection::LinearTosRGB : FNDIIOShaderPS::EColorCorrection::None,
					                                            FVector2D(this->AlphaMin, this->AlphaMax));
					ConvertAlphaOddShader->SetParameters(RHICmdList, Params);

					// Draw the texture
					RHICmdList.DrawPrimitive(0, 2, 1);

					// Release the reference to SourceTexture from the shader
					// The SourceTexture may be the viewport's backbuffer, and Unreal does not like
					// extra references to the backbuffer when the viewport is resized
					Params.InputTarget = DefaultVideoTextureRHI;
					ConvertAlphaOddShader->SetParameters(RHICmdList, Params);

					RHICmdList.EndRenderPass();
				}
			}

			// Copy to resolve target...
			// This is by far the most expensive in terms of cost, since we are having to pull
			// data from the gpu, while in the render thread.
			ReadbackTextures.Resolve(RHICmdList, TargetableTexture, FResolveRect(0, 0, FrameSize.X/2,FrameSize.Y), FResolveRect(0, 0, FrameSize.X/2,FrameSize.Y));

			// Force all the drawing to be done here and now
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
		}
	}

	return DrawResult;
}

/**
	Change the render target configuration based on the passed in parameters

	@param InFrameSize The frame size to resize the render target to
	@param InFrameRate The frame rate at which we should be sending frames via NDI
*/
void UNDIMediaSender::ChangeRenderTargetConfiguration(FIntPoint InFrameSize, FFrameRate InFrameRate)
{
	FScopeLock RenderLock(&RenderSyncContext);

	// Ensure that the frame size matches what we are told the frame size is
	this->FrameSize = InFrameSize;
	this->FrameRate = InFrameRate;

	// Reiterate the properties that the frame needs to be when sent
	NDI_video_frame.xres = FrameSize.X;
	NDI_video_frame.yres = FrameSize.Y;
	NDI_video_frame.line_stride_in_bytes = 0;
	NDI_video_frame.frame_rate_D = FrameRate.Denominator;
	NDI_video_frame.frame_rate_N = FrameRate.Numerator;
	NDI_video_frame.FourCC = this->OutputAlpha ?  NDIlib_FourCC_type_UYVA : NDIlib_FourCC_type_UYVY;

	// Size of the readback texture in UYVY format, optionally with alpha
	FIntPoint UYVYTextureSize(FrameSize.X/2, FrameSize.Y + (this->OutputAlpha ? FrameSize.Y/2 : 0));

	// Create readback textures, suitably sized for UYVY
	this->ReadbackTextures.Create(UYVYTextureSize);
	this->ReadbackTexturesHaveAlpha = this->OutputAlpha;

	// Create the RenderTarget descriptor, suitably sized for UYVY
	RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(UYVYTextureSize, PF_B8G8R8A8, FClearValueBinding::None,
	                                                               TexCreate_None, TexCreate_RenderTargetable, false);

	// If our RenderTarget is valid change the size
	if (IsValid(this->RenderTarget))
	{
		// Ensure that our render target is the same size as we expect
		this->RenderTarget->ResizeTarget(FrameSize.X, FrameSize.Y);
	}

	// Do not hold a lock when broadcasting, as it calls outside of the sender's context
	RenderLock.Unlock();

	// determine if the notifier is bound
	if (this->OnBroadcastConfigurationChanged.IsBound())
	{
		// broadcast the notification to all interested parties
		OnBroadcastConfigurationChanged.Broadcast(this);
	}
}


/**
	This will send a metadata frame to all receivers
	The data is expected to be valid XML
*/
void UNDIMediaSender::SendMetadataFrame(const FString& Data, bool AttachToVideoFrame)
{
	if (p_send_instance != nullptr)
	{
		if(AttachToVideoFrame == true)
		{
			// Attach the metadata to the next video frame to be sent
			FScopeLock RenderLock(&RenderSyncContext);
			this->ReadbackTextures.AddMetaData(Data);
		}
		else
		{
			OnSenderMetaDataPreSend.Broadcast(this);

			// Send the metadata separate from the video frame
			NDIlib_metadata_frame_t metadata;
			std::string DataStr(TCHAR_TO_UTF8(*Data));
			metadata.p_data = const_cast<char*>(DataStr.c_str());
			metadata.length = DataStr.length();
			metadata.timecode = FDateTime::Now().GetTimeOfDay().GetTicks();

			NDIlib_send_send_metadata(p_send_instance, &metadata);

			OnSenderMetaDataSent.Broadcast(this);
		}
	}
}

/**
	This will send a metadata frame to all receivers
	The data will be formatted as: <Element AttributeData/>
*/
void UNDIMediaSender::SendMetadataFrameAttr(const FString& Element, const FString& ElementData, bool AttachToVideoFrame)
{
	FString Data = "<" + Element + ">" + ElementData + "</" + Element + ">";
	SendMetadataFrame(Data, AttachToVideoFrame);
}

/**
	This will send a metadata frame to all receivers
	The data will be formatted as: <Element Key0="Value0" Key1="Value1" Keyn="Valuen"/>
*/
void UNDIMediaSender::SendMetadataFrameAttrs(const FString& Element, const TMap<FString,FString>& Attributes, bool AttachToVideoFrame)
{
	FString Data = "<" + Element;
	
	for(const auto& Attribute : Attributes)
	{
		Data += " " + Attribute.Key + "=\"" + Attribute.Value + "\"";
	}

	Data += "/>";

	SendMetadataFrame(Data, AttachToVideoFrame);
}


/**
	Attempts to get a metadata frame from the sender.
	If there is one, the data is broadcast through OnSenderMetaDataReceived.
	Returns true if metadata was received, false otherwise.
*/
bool UNDIMediaSender::GetMetadataFrame()
{
	bool bProcessed = false;

	if (p_send_instance != nullptr)
	{
		NDIlib_metadata_frame_t metadata;
		if(NDIlib_send_capture(p_send_instance, &metadata, 0) == NDIlib_frame_type_metadata)
		{
			if ((metadata.p_data != nullptr) && (metadata.length > 0))
			{
				FString Data(UTF8_TO_TCHAR(metadata.p_data));
				OnSenderMetaDataReceived.Broadcast(this, Data);
			}
			NDIlib_send_free_metadata(p_send_instance, &metadata);

			bProcessed = true;
		}
	}

	return bProcessed;
}

/**
	Attempts to change the RenderTarget used in sending video frames over NDI
*/
void UNDIMediaSender::ChangeVideoTexture(UTextureRenderTarget2D* VideoTexture)
{
	// Wait render thread so that we can do something
	FScopeLock RenderLock(&RenderSyncContext);

	// Set our Render Target to the incoming video texture
	this->RenderTarget = VideoTexture;
}

/**
	Change the alpha remapping settings
*/
void UNDIMediaSender::ChangeAlphaRemap(float AlphaMinIn, float AlphaMaxIn)
{
	// Wait render thread so that we can do something
	FScopeLock RenderLock(&RenderSyncContext);

	this->AlphaMin = AlphaMinIn;
	this->AlphaMax = AlphaMaxIn;
}

/**
	Determines the current tally information. If you specify a timeout then it will wait until it has
	changed, otherwise it will simply poll it and return the current tally immediately

	@param IsOnPreview - A state indicating whether this source in on preview of a receiver
	@param IsOnProgram - A state indicating whether this source is on program of a receiver
	@param TimeOut	- Indicates the amount of time to wait (in milliseconds) until a change has occurred
*/
void UNDIMediaSender::GetTallyInformation(bool& IsOnPreview, bool& IsOnProgram, uint32 Timeout)
{
	// reset the parameters with the default values
	IsOnPreview = IsOnProgram = false;

	// validate our sender object
	if (p_send_instance != nullptr)
	{
		// construct a tally structure
		NDIlib_tally_t tally_info;

		// retrieve the tally information from the SDK
		NDIlib_send_get_tally(p_send_instance, &tally_info, 0);

		// perform a copy from the tally info object to our parameters
		IsOnPreview = tally_info.on_preview;
		IsOnProgram = tally_info.on_program;
	}
}

/**
	Gets the current number of receivers connected to this source. This can be used to avoid rendering
	when nothing is connected to the video source. which can significantly improve the efficiency if
	you want to make a lot of sources available on the network
*/
void UNDIMediaSender::GetNumberOfConnections(int32& Result)
{
	// reset the result
	Result = 0;

	// have we created a sender object
	if (p_send_instance != nullptr)
	{
		// call the SDK to get the current number of connection for the sender instance of this object
		Result = NDIlib_send_get_no_connections(p_send_instance, 0);
	}
}

/**
	Attempts to immediately stop sending frames over NDI to any connected receivers
*/
void UNDIMediaSender::Shutdown()
{
	// Perform cleanup on the audio related materials
	{
		FScopeLock Lock(&AudioSyncContext);

		// Remove the handler for the send audio frame
		FNDIConnectionService::RemoveAudioSender(this);
	}

	// Perform cleanup on the renderer related materials
	{
		FScopeLock RenderLock(&RenderSyncContext);

		// destroy the sender
		if (p_send_instance != nullptr)
		{
			// Get the command list interface
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

			// send an empty frame over NDI to be able to cleanup the buffers
			this->ReadbackTextures.Flush(RHICmdList, p_send_instance);

			NDIlib_send_destroy(p_send_instance);
			p_send_instance = nullptr;
		}

		this->DefaultVideoTextureRHI.SafeRelease();

		this->ReadbackTextures.Destroy();

		this->RenderTargetDescriptor.Reset();
	}
}

/**
   Called before destroying the object.  This is called immediately upon deciding to destroy the object,
   to allow the object to begin an asynchronous cleanup process.
 */
void UNDIMediaSender::BeginDestroy()
{
	// Call the shutdown procedure here.
	this->Shutdown();

	// Call the base implementation of 'BeginDestroy'
	Super::BeginDestroy();
}

/**
	Set whether or not a Linear to sRGB conversion is made
*/
void UNDIMediaSender::PerformLinearTosRGBConversion(bool Value)
{
	this->bPerformLinearTosRGB = Value;
}

/**
	Set whether or not to enable PTZ support
*/
void UNDIMediaSender::EnablePTZ(bool Value)
{
	this->bEnablePTZ = Value;
}

/**
	Returns the Render Target used for sending a frame over NDI
*/
UTextureRenderTarget2D* UNDIMediaSender::GetRenderTarget()
{
	return this->RenderTarget;
}


FTextureResource* UNDIMediaSender::GetRenderTargetResource() const
{
	if(IsValid(this->RenderTarget))
		return this->RenderTarget->GetResource();

	return nullptr;
}



/**
	A texture with CPU readback
*/

/**
	Check that the MappedTexture is not mapped, and the readback texture has been destroyed.
*/
UNDIMediaSender::MappedTexture::~MappedTexture()
{
	check(Texture.IsValid() == false);
	check(pData == nullptr);
}

/**
	Create the readback texture. If the texture was already created it will first be destroyed.
	The MappedTexture must currently not be mapped.
*/
void UNDIMediaSender::MappedTexture::Create(FIntPoint InFrameSize)
{
	Destroy();

	FrameSize = InFrameSize;
}

/**
	Destroy the readback texture (if not already destroyed). The MappedTexture must currently not be mapped.
*/
void UNDIMediaSender::MappedTexture::Destroy()
{
	check(pData == nullptr);

	if (Texture.IsValid())
	{
		Texture.SafeRelease();
		Texture = nullptr;
	}
	pData = nullptr;

	check(Texture.IsValid() == false);
	check(pData == nullptr);
}

void UNDIMediaSender::MappedTexture::PrepareTexture()
{
	if (Texture.IsValid() && (Texture->GetSizeXY() == FrameSize))
		return;

	Destroy();

	check(Texture.IsValid() == false);
	check(pData == nullptr);

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
	const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaSenderMappedTexture"))
		.SetExtent(FrameSize.X, FrameSize.Y)
		.SetFormat(PF_B8G8R8A8)
		.SetNumMips(1)
		.SetFlags(ETextureCreateFlags::CPUReadback);
	Texture = RHICreateTexture(CreateDesc);
#elif ENGINE_MAJOR_VERSION == 5
	// Resource creation structure
	FRHIResourceCreateInfo CreateInfo(TEXT("NDIMediaSenderMappedTexture"));

	// Recreate the read back texture
	Texture = RHICreateTexture2D(FrameSize.X, FrameSize.Y, PF_B8G8R8A8, 1, 1, TexCreate_CPUReadback, CreateInfo);
#else
	#error "Unsupported engine major version"
#endif

	pData = nullptr;

	check(Texture.IsValid() == true);
	check(pData == nullptr);
}

FIntPoint UNDIMediaSender::MappedTexture::GetSizeXY() const
{
	return FrameSize;
}

/**
	Resolve the source texture to the readback texture. The readback texture must have been created.
	The MappedTexture must currently not be mapped.
*/
void UNDIMediaSender::MappedTexture::Resolve(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTextureRHI, const FResolveRect& Rect, const FResolveRect& DestRect)
{
	PrepareTexture();

	check(Texture.IsValid() == true);
	check(pData == nullptr);
	check(SourceTextureRHI != nullptr);

	// Copy to resolve target...
	// This is by far the most expensive in terms of cost, since we are having to pull
	// data from the gpu, while in the render thread.
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
	RHICmdList.CopyTexture(SourceTextureRHI, Texture, FRHICopyTextureInfo());
#else
	// NOTE: On UE5 (at least up to and including 5.0.3) using a non-default destination
	//       rectangle will fail in the D3D12 render engine as currently not supported.
	RHICmdList.CopyToResolveTarget(SourceTextureRHI, Texture, FResolveParams());
#endif
}

/**
	Map the readback texture so that its content can be read by the CPU.
	The readback texture must have been created. The MappedTexture must currently not be mapped.
*/
void UNDIMediaSender::MappedTexture::Map(FRHICommandListImmediate& RHICmdList, int32& OutWidth, int32& OutHeight, int32& OutLineStride)
{
	check(Texture.IsValid() == true);
	check(pData == nullptr);

	// Map the staging surface so we can copy the buffer for the NDI SDK to use
	int32 MappedWidth = 0, MappedHeight = 0;
	RHICmdList.MapStagingSurface(Texture, pData, MappedWidth, MappedHeight);
	OutWidth = FrameSize.X;
	OutHeight = FrameSize.Y;
	OutLineStride = MappedWidth * 4;

	check(pData != nullptr);
}

/**
	Return a pointer to the mapped readback texture content.
	The MappedTexture must currently be mapped.
*/
void* UNDIMediaSender::MappedTexture::MappedData() const
{
	check(pData != nullptr);

	return pData;
}

/**
	Unmap the readback texture (if currently mapped).
*/
void UNDIMediaSender::MappedTexture::Unmap(FRHICommandListImmediate& RHICmdList)
{
	if(pData != nullptr)
	{
		check(Texture.IsValid() == true);

		RHICmdList.UnmapStagingSurface(Texture);
		pData = nullptr;
	}

	MetaData.clear();

	check(pData == nullptr);
}


/**
	Adds metadata to the texture
*/
void UNDIMediaSender::MappedTexture::AddMetaData(const FString& Data)
{
	std::string DataStr(TCHAR_TO_UTF8(*Data));
	MetaData += DataStr;
}

/**
	Gets the metadata for the texture
*/
const std::string& UNDIMediaSender::MappedTexture::GetMetaData() const
{
	return MetaData;
}


/**
	Class for managing the sending of mapped texture data to an NDI video stream.
	Sending is done asynchronously, so mapping and unmapping of texture data must
	be managed so that CPU accessible texture content remains valid until the
	sending of the frame is guaranteed to have been completed. This is achieved
	by double-buffering readback textures.
*/

/**
	Create the mapped texture sender. If the mapped texture sender was already created
	it will first be destroyed. No texture must currently be mapped.
*/
void UNDIMediaSender::MappedTextureASyncSender::Create(FIntPoint InFrameSize)
{
	Destroy();

	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.Create(InFrameSize);

	MappedTexture& PreviousMappedTexture = MappedTextures[1-CurrentIndex];
	PreviousMappedTexture.Create(InFrameSize);
}

/**
	Destroy the mapped texture sender (if not already destroyed). No texture must currently be mapped.
*/
void UNDIMediaSender::MappedTextureASyncSender::Destroy()
{
	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.Destroy();

	MappedTexture& PreviousMappedTexture = MappedTextures[1-CurrentIndex];
	PreviousMappedTexture.Destroy();
}

FIntPoint UNDIMediaSender::MappedTextureASyncSender::GetSizeXY() const
{
	const MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	return CurrentMappedTexture.GetSizeXY();
}

/**
	Resolve the source texture to the current texture of the mapped texture sender.
	The mapped texture sender must have been created. The current texture must currently not be mapped.
*/
void UNDIMediaSender::MappedTextureASyncSender::Resolve(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTextureRHI, const FResolveRect& Rect, const FResolveRect& DestRect)
{
	// Copy to resolve target...
	// This is by far the most expensive in terms of cost, since we are having to pull
	// data from the gpu, while in the render thread.
	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.Resolve(RHICmdList, SourceTextureRHI, Rect, DestRect);
}

/**
	Map the current texture of the mapped texture sender so that its content can be read by the CPU.
	The mapped texture sender must have been created. The current texture must currently not be mapped.
*/
void UNDIMediaSender::MappedTextureASyncSender::Map(FRHICommandListImmediate& RHICmdList, int32& OutWidth, int32& OutHeight, int32& OutLineStride)
{
	// Map the staging surface so we can copy the buffer for the NDI SDK to use
	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.Map(RHICmdList, OutWidth, OutHeight, OutLineStride);
}

/**
	Send the current texture of the mapped texture sender to an NDI video stream, then swaps the textures.
	The mapped texture sender must have been created. The current texture must currently be mapped.
*/
void UNDIMediaSender::MappedTextureASyncSender::Send(FRHICommandListImmediate& RHICmdList, NDIlib_send_instance_t p_send_instance_in, NDIlib_video_frame_v2_t& p_video_data)
{
	// Send the currently mapped data to an NDI stream asynchronously

	check(p_send_instance_in != nullptr);

	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];

	p_video_data.p_data = (uint8_t*)CurrentMappedTexture.MappedData();

	auto& MetaData = CurrentMappedTexture.GetMetaData();
	if(MetaData.empty() == false)
	{
		p_video_data.p_metadata = MetaData.c_str();
	}
	else
	{
		p_video_data.p_metadata = nullptr;
	}

	NDIlib_send_send_video_async_v2(p_send_instance_in, &p_video_data);

	// After send_video_async returns, the frame sent before this one is guaranteed to have been processed
	// So the texture for the previous frame can be unmapped
	MappedTexture& PreviousMappedTexture = MappedTextures[1-CurrentIndex];
	PreviousMappedTexture.Unmap(RHICmdList);

	// Switch the current and previous textures
	CurrentIndex = 1 - CurrentIndex;
}

/**
	Flushes the NDI video stream, and unmaps the textures (if mapped)
*/
void UNDIMediaSender::MappedTextureASyncSender::Flush(FRHICommandListImmediate& RHICmdList, NDIlib_send_instance_t p_send_instance_in)
{
	// Flush the asynchronous NDI stream and unmap all the textures

	check(p_send_instance_in != nullptr);

	NDIlib_send_send_video_async_v2(p_send_instance_in, nullptr);

	// After send_video_async returns, the frame sent before this one is guaranteed to have been processed
	// So the texture for the previous frame can be unmapped
	MappedTexture& PreviousMappedTexture = MappedTextures[1-CurrentIndex];
	PreviousMappedTexture.Unmap(RHICmdList);

	// As the send queue was flushed, also unmap the current frame as it is not used
	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.Unmap(RHICmdList);

	// Switch the current and previous textures
	CurrentIndex = 1 - CurrentIndex;
}

/**
	Adds metadata to the current texture
*/
void UNDIMediaSender::MappedTextureASyncSender::AddMetaData(const FString& Data)
{
	MappedTexture& CurrentMappedTexture = MappedTextures[CurrentIndex];
	CurrentMappedTexture.AddMetaData(Data);
}
