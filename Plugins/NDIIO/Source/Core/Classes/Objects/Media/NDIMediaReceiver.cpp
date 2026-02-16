/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Objects/Media/NDIMediaReceiver.h>
#include <Misc/CoreDelegates.h>
#include <TextureResource.h>
#include <RenderTargetPool.h>
#include <GlobalShader.h>
#include <ShaderParameterUtils.h>
#include <MediaShaders.h>
#include <MediaIOCorePlayerBase.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Async/Async.h>
#include <GenericPlatform/GenericPlatformProcess.h>
#include <Misc/EngineVersionComparison.h>
#include <UObject/UObjectGlobals.h>
#include <UObject/Package.h>

#include "NDIShaders.h"

#if WITH_EDITOR
#include <Editor.h>
#endif

#include <string>

UNDIMediaReceiver::UNDIMediaReceiver()
{
	this->InternalVideoTexture = NewObject<UNDIMediaTexture2D>(GetTransientPackage(), UNDIMediaTexture2D::StaticClass(), NAME_None, RF_Transient | RF_MarkAsNative);
}

/**
	Attempts to perform initialization logic for creating a receiver through the NDI sdk api
*/
bool UNDIMediaReceiver::Initialize(const FNDIConnectionInformation& InConnectionInformation, UNDIMediaReceiver::EUsage InUsage)
{
	if (this->p_receive_instance == nullptr)
	{
		if (IsValid(this->InternalVideoTexture))
			this->InternalVideoTexture->UpdateResource();

		// create a non-connected receiver instance
		NDIlib_recv_create_v3_t settings;
		settings.allow_video_fields = false;
		settings.bandwidth = NDIlib_recv_bandwidth_highest;
		settings.color_format = NDIlib_recv_color_format_fastest;

		p_receive_instance = NDIlib_recv_create_v3(&settings);

		// check if it was successful
		if (p_receive_instance != nullptr)
		{
			// If the incoming connection information is valid
			if (InConnectionInformation.IsValid())
			{
				//// Alright we created a non-connected receiver. Lets actually connect
				ChangeConnection(InConnectionInformation);
			}

			if (InUsage == UNDIMediaReceiver::EUsage::Standalone)
			{
				this->OnNDIReceiverVideoCaptureEvent.Remove(VideoCaptureEventHandle);
				VideoCaptureEventHandle = this->OnNDIReceiverVideoCaptureEvent.AddLambda([this](UNDIMediaReceiver* receiver, const NDIlib_video_frame_v2_t& video_frame)
				{
					FTextureRHIRef ConversionTexture = this->DisplayFrame(video_frame);
					if (ConversionTexture != nullptr)
					{
						if ((GetVideoTextureResource() != nullptr) && (GetVideoTextureResource()->TextureRHI != ConversionTexture))
						{
							GetVideoTextureResource()->TextureRHI = ConversionTexture;
							RHIUpdateTextureReference(this->VideoTexture->TextureReference.TextureReferenceRHI, ConversionTexture);
						}
						if ((GetInternalVideoTextureResource() != nullptr) && (GetInternalVideoTextureResource()->TextureRHI != ConversionTexture))
						{
							GetInternalVideoTextureResource()->TextureRHI = ConversionTexture;
							RHIUpdateTextureReference(this->InternalVideoTexture->TextureReference.TextureReferenceRHI, ConversionTexture);
						}
					}
				});

				// We don't want to limit the engine rendering speed to the sync rate of the connection hook
				// into the core delegates render thread 'EndFrame'
				FCoreDelegates::OnEndFrameRT.Remove(FrameEndRTHandle);
				FrameEndRTHandle.Reset();
				FrameEndRTHandle = FCoreDelegates::OnEndFrameRT.AddLambda([this]()
				{
					while(this->CaptureConnectedMetadata())
						; // Potential improvement: limit how much metadata is processed, to avoid appearing to lock up due to a metadata flood
					this->CaptureConnectedVideo();
				});

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

			return true;
		}
	}

	return false;
}

bool UNDIMediaReceiver::Initialize(UNDIMediaReceiver::EUsage InUsage)
{
	return Initialize(ConnectionSetting, InUsage);
}


void UNDIMediaReceiver::StartConnection()
{
	FScopeLock RenderLock(&RenderSyncContext);
	FScopeLock AudioLock(&AudioSyncContext);
	FScopeLock MetadataLock(&MetadataSyncContext);

	if (this->ConnectionInformation.IsValid())
	{
		// Create a non-connected receiver instance
		NDIlib_recv_create_v3_t settings;
		settings.allow_video_fields = true;
		settings.bandwidth = this->ConnectionInformation;
		settings.color_format = NDIlib_recv_color_format_fastest;

		// Do the conversion on the connection information
		// Beware of the limited lifetime of TCHAR_TO_UTF8 values
		NDIlib_source_t connection;
		std::string SourceNameStr(TCHAR_TO_UTF8(*this->ConnectionInformation.GetNDIName()));
		connection.p_ndi_name = SourceNameStr.c_str();
		std::string UrlStr(TCHAR_TO_UTF8(*this->ConnectionInformation.Url));
		connection.p_url_address = UrlStr.c_str();

		// Create a receiver and connect to the source
		auto* receive_instance = NDIlib_recv_create_v3(&settings);
		NDIlib_recv_connect(receive_instance, &connection);

		// Get rid of existing connection
		StopConnection();

		// set the receiver to the new connection
		p_receive_instance = receive_instance;

		// create a new frame sync instance
		p_framesync_instance = NDIlib_framesync_create(p_receive_instance);
	}
}

void UNDIMediaReceiver::StopConnection()
{
	FScopeLock RenderLock(&RenderSyncContext);
	FScopeLock AudioLock(&AudioSyncContext);
	FScopeLock MetadataLock(&MetadataSyncContext);

	// destroy the framesync instance
	if (p_framesync_instance != nullptr)
		NDIlib_framesync_destroy(p_framesync_instance);
	p_framesync_instance = nullptr;

	// Free the receiver
	if (p_receive_instance != nullptr)
		NDIlib_recv_destroy(p_receive_instance);
	p_receive_instance = nullptr;
}

/**
	Attempts to change the connection to another NDI sender source
*/
void UNDIMediaReceiver::ChangeConnection(const FNDIConnectionInformation& InConnectionInformation)
{
	// Ensure some thread-safety because our 'Capture Connected Video' function is called on the render thread
	FScopeLock RenderLock(&RenderSyncContext);
	FScopeLock AudioLock(&AudioSyncContext);
	FScopeLock MetadataLock(&MetadataSyncContext);

	// We should only worry about connections that are already created
	if (p_receive_instance != nullptr)
	{
		// Set the connection information for the requested new connection
		if (this->ConnectionInformation != InConnectionInformation)
		{
			bool bSourceChanged = false;
			if(this->ConnectionInformation.SourceName != InConnectionInformation.SourceName)
				bSourceChanged = true;
			if(this->ConnectionInformation.Url != InConnectionInformation.Url)
				bSourceChanged = true;
			if(this->ConnectionInformation.MachineName != InConnectionInformation.MachineName)
				bSourceChanged = true;
			if(this->ConnectionInformation.StreamName != InConnectionInformation.StreamName)
				bSourceChanged = true;

			bool bBandwidthChanged = false;
			if(this->ConnectionInformation.Bandwidth != InConnectionInformation.Bandwidth)
				bBandwidthChanged = true;

			bool bMutingChanged = false;
			if(this->ConnectionInformation.bMuteAudio != InConnectionInformation.bMuteAudio)
				bMutingChanged = true;
			if(this->ConnectionInformation.bMuteVideo != InConnectionInformation.bMuteVideo)
				bMutingChanged = true;

			this->ConnectionInformation = InConnectionInformation;

			if (this->ConnectionInformation.IsValid())
			{
				if (bSourceChanged || bBandwidthChanged || (p_receive_instance == nullptr) || (p_framesync_instance == nullptr))
				{
					// Connection information is valid, and something has changed that requires the connection to be remade

					StartConnection();
				}
			}
			else
			{
				// Requested connection is invalid, indicating we should close the current connection

				StopConnection();
			}
		}
	}
}

/**
	Attempts to change the Video Texture object used as the video frame capture object
*/
void UNDIMediaReceiver::ChangeVideoTexture(UNDIMediaTexture2D* InVideoTexture)
{
	FScopeLock Lock(&RenderSyncContext);

	if (IsValid(this->VideoTexture))
	{
		// make sure that the old texture is not referencing the rendering of this texture
		this->VideoTexture->UpdateTextureReference(FRHICommandListExecutor::GetImmediateCommandList(), nullptr);
	}
	if (IsValid(this->InternalVideoTexture))
	{
		// make sure that the old texture is not referencing the rendering of this texture
		this->InternalVideoTexture->UpdateTextureReference(FRHICommandListExecutor::GetImmediateCommandList(), nullptr);
	}

	// Just copy the new texture here.
	this->VideoTexture = InVideoTexture;
}

/**
	Attempts to generate the pcm data required by the 'AudioWave' object
	We will generate mono audio, down-mixing if the source has multiple channels
*/
int32 UNDIMediaReceiver::GeneratePCMData(UNDIMediaSoundWave* AudioWave, uint8* PCMData, const int32 SamplesNeeded)
{
	FScopeLock Lock(&AudioSyncContext);

	int32 samples_generated = 0;
	int32 requested_frame_rate = IsValid(AudioWave) ? AudioWave->GetSampleRateForCurrentPlatform() : 48000;
	int32 requested_no_channels = IsValid(AudioWave) ? AudioWave->NumChannels : 1;
	int32 requested_no_frames = SamplesNeeded / requested_no_channels;

	if ((p_framesync_instance != nullptr) && (ConnectionInformation.bMuteAudio == false))
	{
		int available_no_frames = NDIlib_framesync_audio_queue_depth(p_framesync_instance);	// Samples per channel

		if (available_no_frames > 0)
		{
			NDIlib_audio_frame_v2_t audio_frame;
			NDIlib_framesync_capture_audio(p_framesync_instance, &audio_frame, requested_frame_rate, 0, FMath::Min(available_no_frames, requested_no_frames));

			if (requested_no_channels == audio_frame.no_channels)
			{
				// Convert to PCM
				for (int32 channel_index = 0; channel_index < requested_no_channels; ++channel_index)
				{
					const float* channel_data = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(audio_frame.p_data) + channel_index * audio_frame.channel_stride_in_bytes);
					uint8* pcm_data = PCMData + channel_index * sizeof(int16);

					for (int32 sample_index = 0; sample_index < audio_frame.no_samples; ++sample_index)
					{
						// convert float to int16
						int32 sample_int32 = FMath::RoundToInt(*channel_data * 32767.0f);
						// perform clamp between different integer types
						int16 sample = sample_int32 < INT16_MIN ? INT16_MIN : sample_int32 > INT16_MAX ? INT16_MAX : sample_int32;

						pcm_data[0] = sample & 0xff;
						pcm_data[1] = (sample >> 8) & 0xff;

						++channel_data;
						pcm_data += requested_no_channels * sizeof(int16);
					}
				}
			}

			else if (requested_no_channels < audio_frame.no_channels)
			{
				// Add extra channels to all common channels

				const int32 no_extra_channels = audio_frame.no_channels - requested_no_channels;

				for (int32 src_channel_index = requested_no_channels; src_channel_index < audio_frame.no_channels; ++src_channel_index)
				{
					const float* src_channel_data = reinterpret_cast<const float*>(reinterpret_cast<const uint8*>(audio_frame.p_data) + src_channel_index * audio_frame.channel_stride_in_bytes);
					for (int32 dst_channel_index = 0; dst_channel_index < requested_no_channels; ++dst_channel_index)
					{
						float* dst_channel_data = reinterpret_cast<float*>(reinterpret_cast<uint8*>(audio_frame.p_data) + dst_channel_index * audio_frame.channel_stride_in_bytes);
						for (int32 sample_index = 0; sample_index < audio_frame.no_samples; ++sample_index)
						{
							dst_channel_data[sample_index] += src_channel_data[sample_index];
						}
					}
				}

				// Convert to PCM, taking care of any normalization
				for (int32 channel_index = 0; channel_index < requested_no_channels; ++channel_index)
				{
					const float* channel_data = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(audio_frame.p_data) + channel_index * audio_frame.channel_stride_in_bytes);
					uint8* pcm_data = PCMData + channel_index * sizeof(int16);

					for (int32 sample_index = 0; sample_index < audio_frame.no_samples; ++sample_index)
					{
						// normalize and convert float to int16
						int32 sample_int32 = FMath::RoundToInt(*channel_data / (no_extra_channels+1) * 32767.0f);
						// perform clamp between different integer types
						int16 sample = sample_int32 < INT16_MIN ? INT16_MIN : sample_int32 > INT16_MAX ? INT16_MAX : sample_int32;

						pcm_data[0] = sample & 0xff;
						pcm_data[1] = (sample >> 8) & 0xff;

						++channel_data;
						pcm_data += requested_no_channels * sizeof(int16);
					}
				}
			}

			else if (requested_no_channels > audio_frame.no_channels)
			{
				// Copy common channels

				// Convert to PCM, taking care of any normalization
				for (int32 channel_index = 0; channel_index < audio_frame.no_channels; ++channel_index)
				{
					const float* channel_data = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(audio_frame.p_data) + channel_index * audio_frame.channel_stride_in_bytes);
					uint8* pcm_data = PCMData + channel_index * sizeof(int16);

					for (int32 sample_index = 0; sample_index < audio_frame.no_samples; ++sample_index)
					{
						// normalize and convert float to int16
						int32 sample_int32 = FMath::RoundToInt(*channel_data * 32767.0f);
						// perform clamp between different integer types
						int16 sample = sample_int32 < INT16_MIN ? INT16_MIN : sample_int32 > INT16_MAX ? INT16_MAX : sample_int32;

						pcm_data[0] = sample & 0xff;
						pcm_data[1] = (sample >> 8) & 0xff;

						++channel_data;
						pcm_data += requested_no_channels * sizeof(int16);
					}
				}

				// Average source channels to duplicate to extra channels

				for (int32 sample_index = 0; sample_index < audio_frame.no_samples; ++sample_index)
				{
					float sample_value = 0.f;
					for (int32 src_channel_index = 0; src_channel_index < audio_frame.no_channels; ++src_channel_index)
					{
						const float* src_channel_data = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(audio_frame.p_data) + src_channel_index * audio_frame.channel_stride_in_bytes);
						sample_value += src_channel_data[sample_index];
					}

					// normalize and convert float to int16
					int32 sample_int32 = FMath::RoundToInt(sample_value / audio_frame.no_channels * 32767.0f);
					// perform clamp between different integer types
					int16 sample = sample_int32 < INT16_MIN ? INT16_MIN : sample_int32 > INT16_MAX ? INT16_MAX : sample_int32;

					for (int32 dst_channel_index = audio_frame.no_channels; dst_channel_index < requested_no_channels; ++dst_channel_index)
					{
						uint8* pcm_data = PCMData + dst_channel_index * sizeof(int16) + sample_index * requested_no_channels * sizeof(int16);
						pcm_data[0] = sample & 0xff;
						pcm_data[1] = (sample >> 8) & 0xff;
					}
				}
			}

			samples_generated = audio_frame.no_samples * requested_no_channels;

			// clean up our audio frame
			NDIlib_framesync_free_audio(p_framesync_instance, &audio_frame);
		}
		else
		{
			const int32 available_samples = FMath::Min(128 * requested_no_channels, SamplesNeeded);

			FMemory::Memset(PCMData, 0, available_samples * sizeof(int16));

			samples_generated = available_samples;
		}
	}

	return samples_generated;
}

int32 UNDIMediaReceiver::GetAudioChannels()
{
	FScopeLock Lock(&AudioSyncContext);

	int32 no_channels = 0;

	if ((p_framesync_instance != nullptr) && (ConnectionInformation.bMuteAudio == false))
	{
		int available_no_frames = NDIlib_framesync_audio_queue_depth(p_framesync_instance);	// Samples per channel

		if (available_no_frames > 0)
		{
			NDIlib_audio_frame_v2_t audio_frame;
			NDIlib_framesync_capture_audio(p_framesync_instance, &audio_frame, 48000, 0, 0);
			no_channels = audio_frame.no_channels;
		}
	}

	return no_channels;
}

/**
	Attempts to register a sound wave object with this object
*/
void UNDIMediaReceiver::RegisterAudioWave(UNDIMediaSoundWave* InAudioWave)
{
	FScopeLock Lock(&AudioSyncContext);

	// Determine if the audio wave being passed into this object is valid
	if (IsValid(InAudioWave))
	{
		// Only add sources which are not already a part of this receiver
		if (!AudioSourceCollection.ContainsByPredicate(
				[&](UNDIMediaSoundWave* Source) { return Source == InAudioWave; }))
		{

			AudioSourceCollection.Add(InAudioWave);
			InAudioWave->SetConnectionSource(this);
		}
	}
}

/**
	This will send a metadata frame to the sender
	The data is expected to be valid XML
*/
void UNDIMediaReceiver::SendMetadataFrame(const FString& Data)
{
	FScopeLock Lock(&MetadataSyncContext);

	if (p_receive_instance != nullptr)
	{
		NDIlib_metadata_frame_t metadata;
		std::string DataStr(TCHAR_TO_UTF8(*Data));
		metadata.p_data = const_cast<char*>(DataStr.c_str());
		metadata.length = DataStr.length();
		metadata.timecode = FDateTime::Now().GetTimeOfDay().GetTicks();

		NDIlib_recv_send_metadata(p_receive_instance, &metadata);
	}
}

/**
	This will send a metadata frame to the sender
	The data will be formatted as: <Element AttributeData/>
*/
void UNDIMediaReceiver::SendMetadataFrameAttr(const FString& Element, const FString& ElementData)
{
	FString Data = "<" + Element + ">" + ElementData + "</" + Element + ">";
	SendMetadataFrame(Data);
}

/**
	This will send a metadata frame to the sender
	The data will be formatted as: <Element Key0="Value0" Key1="Value1" Keyn="Valuen"/>
*/
void UNDIMediaReceiver::SendMetadataFrameAttrs(const FString& Element, const TMap<FString,FString>& Attributes)
{
	FString Data = "<" + Element;
	
	for(const auto& Attribute : Attributes)
	{
		Data += " " + Attribute.Key + "=\"" + Attribute.Value + "\"";
	}

	Data += "/>";

	SendMetadataFrame(Data);
}


/**
	This will set the up-stream tally notifications. If no streams are connected, it will automatically
	send the tally state upon connection
*/
void UNDIMediaReceiver::SendTallyInformation(const bool& IsOnPreview, const bool& IsOnProgram)
{
	// Currently unsupported
}

/**
	Attempts to immediately stop receiving frames from the connected NDI sender
*/
void UNDIMediaReceiver::Shutdown()
{
	ENQUEUE_RENDER_COMMAND(NDIMediaReceiver_ShutdownRT)([this](FRHICommandListImmediate& RHICmdList)
	{
		this->RenderTarget.SafeRelease();
		this->RenderTargetDescriptor = FPooledRenderTargetDesc();
	});

	this->OnNDIReceiverVideoCaptureEvent.Remove(VideoCaptureEventHandle);
	VideoCaptureEventHandle.Reset();

	// Unregister render thread frame end delegate lambda.
	FCoreDelegates::OnEndFrameRT.Remove(FrameEndRTHandle);
	FrameEndRTHandle.Reset();

	// Move audio source collection to temporary, so that cleanup can be done without
	// holding the lock (which could otherwise cause a deadlock if UNDIMediaSoundWave
	// is still generating PCM data)
	TArray<UNDIMediaSoundWave*> OldAudioSourceCollection;
	{
		FScopeLock AudioLock(&AudioSyncContext);

		OldAudioSourceCollection = MoveTemp(AudioSourceCollection);
	}

	// get the number of available audio sources within the collection
	int32 source_count = OldAudioSourceCollection.Num();

	// iterate the collection of available audio sources
	for (int32 iter = source_count - 1; iter >= 0; --iter)
	{
		// Define and Determine the validity of an item within the collection
		if (auto* AudioWave = OldAudioSourceCollection[iter])
		{
			// ensure that we remove the audio source reference
			OldAudioSourceCollection.RemoveAt(iter);

			// Remove ourselves from the Audio wave object which is trying to render audio frames
			// as fast as possible
			AudioWave->SetConnectionSource(nullptr);
		}
	}

	{
		FScopeLock RenderLock(&RenderSyncContext);
		FScopeLock AudioLock(&AudioSyncContext);
		FScopeLock MetadataLock(&MetadataSyncContext);

		if (p_receive_instance != nullptr)
		{
			if (p_framesync_instance != nullptr)
			{
				NDIlib_framesync_destroy(p_framesync_instance);
				p_framesync_instance = nullptr;
			}

			NDIlib_recv_destroy(p_receive_instance);
			p_receive_instance = nullptr;
		}
	}

	// Reset the connection status of this object
	SetIsCurrentlyConnected(false);

	this->ConnectionInformation.Reset();
	this->PerformanceData.Reset();
	this->FrameRate = FFrameRate(60, 1);
	this->Resolution = FIntPoint(0, 0);
	this->Timecode = FTimecode(0, FrameRate, true, true);
}

/**
	Remove the AudioWave object from this object (if it was previously registered)

	@param InAudioWave An NDIMediaSoundWave object registered with this object
*/
void UNDIMediaReceiver::UnregisterAudioWave(UNDIMediaSoundWave* InAudioWave)
{
	FScopeLock Lock(&AudioSyncContext);

	// Determine if the audio wave being passed into this object is valid
	if (IsValid(InAudioWave))
	{
		// We don't care about the order of the collection,
		// we only care to remove the object as fast as possible
		this->AudioSourceCollection.RemoveSwap(InAudioWave);
	}
}

/**
	Updates the DynamicMaterial with the VideoTexture of this object
*/
void UNDIMediaReceiver::UpdateMaterialTexture(UMaterialInstanceDynamic* MaterialInstance, FString ParameterName)
{
	// Ensure that both the material instance and the video texture are valid
	if (IsValid(MaterialInstance))
	{
		 if (IsValid(this->VideoTexture))
		 {
			// Call the function to set the texture parameter with the proper texture
			MaterialInstance->SetTextureParameterValue(FName(*ParameterName), this->VideoTexture);
		 }
		 else if (IsValid(this->InternalVideoTexture))
		 {
			// Call the function to set the texture parameter with the proper texture
			MaterialInstance->SetTextureParameterValue(FName(*ParameterName), this->InternalVideoTexture);
		 }
	}
}

/**
   Called before destroying the object.  This is called immediately upon deciding to destroy the object,
   to allow the object to begin an asynchronous cleanup process.
 */
void UNDIMediaReceiver::BeginDestroy()
{
	// Call the shutdown procedure here.
	this->Shutdown();

	// Call the base implementation of 'BeginDestroy'
	Super::BeginDestroy();
}

/**
	Attempts to capture a video frame from the connected source.  If a new frame is captured, broadcast it to
	interested receivers through the capture event.
*/
bool UNDIMediaReceiver::CaptureConnectedVideo()
{
	// This function is called on the Engine's Main Rendering Thread. Be very careful when doing stuff here.
	// Make sure things are done quick and efficient.

	// Ensure thread safety
	FScopeLock Lock(&RenderSyncContext);

	bool bHaveCaptured = false;

	// check for our frame sync object and that we are actually connected to the end point
	if ((p_framesync_instance != nullptr) && (ConnectionInformation.bMuteVideo == false))
	{
		// Using a frame-sync we can always get data which is the magic and it will adapt
		// to the frame-rate that it is being called with.
		NDIlib_video_frame_v2_t video_frame;
		NDIlib_framesync_capture_video(p_framesync_instance, &video_frame, NDIlib_frame_format_type_progressive);

		// Update our Performance Metrics
		GatherPerformanceMetrics();

		if (video_frame.p_data)
		{
			// Ensure that we inform all those interested when the stream starts up
			SetIsCurrentlyConnected(true);

			// Update the Framerate, if it has changed
			this->FrameRate.Numerator = video_frame.frame_rate_N;
			this->FrameRate.Denominator = video_frame.frame_rate_D;

			// Update the Resolution
			this->Resolution.X = video_frame.xres;
			this->Resolution.Y = video_frame.yres;

			if (bSyncTimecodeToSource)
			{
				int64_t SourceTime = video_frame.timecode % 864000000000; // Modulo the number of 100ns intervals in 24 hours
				// Update the timecode from the current 'SourceTime' value
				this->Timecode = FTimecode::FromTimespan(FTimespan::FromSeconds(SourceTime / (float)1e+7), FrameRate,
															FTimecode::IsDropFormatTimecodeSupported(FrameRate),
															true // use roll-over timecode
				);
			}
			else
			{
				int64_t SystemTime = FDateTime::Now().GetTimeOfDay().GetTicks();
				// Update the timecode from the current 'SystemTime' value
				this->Timecode = FTimecode::FromTimespan(FTimespan::FromSeconds(SystemTime / (float)1e+7), FrameRate,
															FTimecode::IsDropFormatTimecodeSupported(FrameRate),
															true // use roll-over timecode
				);
			}

			// Redraw if:
			// - timestamp is undefined, or
			// - timestamp has changed, or
			// - frame format type has changed (e.g. different field)
			if ((video_frame.timestamp == NDIlib_recv_timestamp_undefined) ||
				(video_frame.timestamp != LastFrameTimestamp) ||
				(video_frame.frame_format_type != LastFrameFormatType))
			{
				bHaveCaptured = true;

				LastFrameTimestamp = video_frame.timestamp;
				LastFrameFormatType = video_frame.frame_format_type;

				OnNDIReceiverVideoCaptureEvent.Broadcast(this, video_frame);

				OnReceiverVideoReceived.Broadcast(this);

				if (video_frame.p_metadata)
				{
					FString Data(UTF8_TO_TCHAR(video_frame.p_metadata));
					OnReceiverMetaDataReceived.Broadcast(this, Data, true);
				}
			}
		}

		// Release the video. You could keep the frame if you want and release it later.
		NDIlib_framesync_free_video(p_framesync_instance, &video_frame);
	}

	return bHaveCaptured;
}


/**
	Attempts to capture an audio frame from the connected source.  If a new frame is captured, broadcast it to
	interested receivers through the capture event.
*/
bool UNDIMediaReceiver::CaptureConnectedAudio()
{
	FScopeLock Lock(&AudioSyncContext);

	bool bHaveCaptured = false;

	if ((p_framesync_instance != nullptr) && (ConnectionInformation.bMuteAudio == false))
	{
		int no_samples = NDIlib_framesync_audio_queue_depth(p_framesync_instance);

		// Using a frame-sync we can always get data which is the magic and it will adapt
		// to the frame-rate that it is being called with.
		NDIlib_audio_frame_v2_t audio_frame;
		NDIlib_framesync_capture_audio(p_framesync_instance, &audio_frame, 0, 0, no_samples);

		if (audio_frame.p_data)
		{
			// Ensure that we inform all those interested when the stream starts up
			SetIsCurrentlyConnected(true);

			const int32 available_samples = audio_frame.no_samples * audio_frame.no_channels;

			if (available_samples > 0)
			{
				bHaveCaptured = true;

				OnNDIReceiverAudioCaptureEvent.Broadcast(this, audio_frame);

				OnReceiverAudioReceived.Broadcast(this);
			}
		}

		// Release the audio frame
		NDIlib_framesync_free_audio(p_framesync_instance, &audio_frame);
	}

	return bHaveCaptured;
}


bool UNDIMediaReceiver::CaptureConnectedMetadata()
{
	FScopeLock Lock(&MetadataSyncContext);

	bool bHaveCaptured = false;

	if (p_receive_instance != nullptr)
	{
		NDIlib_metadata_frame_t metadata;
		NDIlib_frame_type_e frame_type = NDIlib_recv_capture_v3(p_receive_instance, nullptr, nullptr, &metadata, 0);
		if (frame_type == NDIlib_frame_type_metadata)
		{
			if (metadata.p_data)
			{
				// Ensure that we inform all those interested when the stream starts up
				SetIsCurrentlyConnected(true);

				if (metadata.length > 0)
				{
					bHaveCaptured = true;

					OnNDIReceiverMetadataCaptureEvent.Broadcast(this, metadata);

					FString Data(UTF8_TO_TCHAR(metadata.p_data));
					OnReceiverMetaDataReceived.Broadcast(this, Data, false);
				}
			}

			NDIlib_recv_free_metadata(p_receive_instance, &metadata);
		}
	}

	return bHaveCaptured;
}


void UNDIMediaReceiver::SetIsCurrentlyConnected(bool bConnected)
{
	if (bConnected != bIsCurrentlyConnected)
	{
		FScopeLock Lock(&ConnectionSyncContext);

		if (bConnected != bIsCurrentlyConnected)
		{
			bIsCurrentlyConnected = bConnected;

			if (bConnected == true)
			{
				if (OnNDIReceiverConnectedEvent.IsBound())
				{
					AsyncTask(ENamedThreads::GameThread, [&]() {
						// Broadcast the event
						OnNDIReceiverConnectedEvent.Broadcast(this);
					});
				}
			}
			else
			{
				if (OnNDIReceiverDisconnectedEvent.IsBound())
				{
					AsyncTask(ENamedThreads::GameThread, [&]() {
						// Broadcast the event
						OnNDIReceiverDisconnectedEvent.Broadcast(this);
					});
				}
			}
		}
	}
}


/**
	Attempts to immediately update the 'VideoTexture' object with the last capture video frame
	from the connected source
*/
FTextureRHIRef UNDIMediaReceiver::DisplayFrame(const NDIlib_video_frame_v2_t& video_frame)
{
	// we need a command list to work with
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	// Actually draw the video frame from cpu to gpu
	switch(video_frame.frame_format_type)
	{
		case NDIlib_frame_format_type_progressive:
			if(video_frame.FourCC == NDIlib_FourCC_video_type_UYVY)
				return DrawProgressiveVideoFrame(RHICmdList, video_frame);
			else if(video_frame.FourCC == NDIlib_FourCC_video_type_UYVA)
				return DrawProgressiveVideoFrameAlpha(RHICmdList, video_frame);
			break;
		case NDIlib_frame_format_type_field_0:
		case NDIlib_frame_format_type_field_1:
			if(video_frame.FourCC == NDIlib_FourCC_video_type_UYVY)
				return DrawInterlacedVideoFrame(RHICmdList, video_frame);
			else if(video_frame.FourCC == NDIlib_FourCC_video_type_UYVA)
				return DrawInterlacedVideoFrameAlpha(RHICmdList, video_frame);
			break;
	}

	return nullptr;
}

/**
	Perform the color conversion (if any) and bit copy from the gpu
*/
FTextureRHIRef UNDIMediaReceiver::DrawProgressiveVideoFrame(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result)
{
	// Ensure thread safety
	FScopeLock Lock(&RenderSyncContext);

	FTextureRHIRef TargetableTexture;

	// check for our frame sync object and that we are actually connected to the end point
	if (p_framesync_instance != nullptr)
	{
		// Initialize the frame size parameter
		FIntPoint FrameSize = FIntPoint(Result.xres, Result.yres);

		if (!RenderTarget.IsValid() || !RenderTargetDescriptor.IsValid() ||
			RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0) ||
			DrawMode != EDrawMode::Progressive)
		{
			// Create the RenderTarget descriptor
			RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(
				FrameSize, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_SRGB, false);

			// Update the shader resource for the 'SourceTexture'
			// The source texture will be given UYVY data, so make it half-width
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
			const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverProgressiveSourceTexture"))
				.SetExtent(FrameSize.X / 2, FrameSize.Y)
				.SetFormat(PF_B8G8R8A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceTexture = RHICreateTexture(CreateDesc);
#elif (ENGINE_MAJOR_VERSION == 5)
			FRHIResourceCreateInfo CreateInfo(TEXT("NDIMediaReceiverProgressiveSourceTexture"));
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
			RHICreateTargetableShaderResource2D(FrameSize.X / 2, FrameSize.Y, PF_B8G8R8A8, 1, TexCreate_Dynamic,
			                                    TexCreate_RenderTargetable, false, CreateInfo, SourceTexture,
			                                    DummyTexture2DRHI);
#else
			#error "Unsupported engine major version"
#endif

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDescriptor, RenderTarget, TEXT("NDIIO"));

			DrawMode = EDrawMode::Progressive;
		}

		TargetableTexture = RenderTarget->GetRHI();

		// Initialize the Graphics Pipeline State Object
		FGraphicsPipelineStateInitializer GraphicsPSOInit;

		// Initialize the Render pass with the conversion texture
		FRHITexture* ConversionTexture = TargetableTexture.GetReference();
		FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

		// configure media shaders
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// construct the shaders
		TShaderMapRef<FNDIIOShaderVS> VertexShader(ShaderMap);
		TShaderMapRef<FNDIIOShaderUYVYtoBGRAPS> ConvertShader(ShaderMap);

		FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();

		// Needs to be called *before* ApplyCachedRenderTargets, since BeginRenderPass is caching the render targets.
		RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Recv Color Conversion"));

		// do as it suggests
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// set the state objects
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
																CW_NONE, CW_NONE>::GetRHI();
		// perform binding operations for the shaders to be used
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
		// Going to draw triangle strips
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

		// Ensure the pipeline state is set to the one we've configured
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		// set the stream source
		RHICmdList.SetStreamSource(0, VertexBuffer, 0);

		// set the texture parameter of the conversion shader
		FNDIIOShaderUYVYtoBGRAPS::Params Params(SourceTexture, SourceTexture, FrameSize,
		                                        FVector2D(0, 0), FVector2D(1, 1),
		                                        bPerformsRGBtoLinear ? FNDIIOShaderPS::EColorCorrection::sRGBToLinear : FNDIIOShaderPS::EColorCorrection::None,
		                                        FVector2D(0.f, 1.f));
		ConvertShader->SetParameters(RHICmdList, Params);

		// Create the update region structure
		FUpdateTextureRegion2D Region(0, 0, 0, 0, FrameSize.X/2, FrameSize.Y);

		// Set the Pixel data of the NDI Frame to the SourceTexture
		RHIUpdateTexture2D(SourceTexture, 0, Region, Result.line_stride_in_bytes, (uint8*&)Result.p_data);

		// begin our drawing
		{
			RHICmdList.SetViewport(0, 0, 0.0f, FrameSize.X, FrameSize.Y, 1.0f);
			RHICmdList.DrawPrimitive(0, 2, 1);
		}

		RHICmdList.EndRenderPass();
	}

	return TargetableTexture;
}

FTextureRHIRef UNDIMediaReceiver::DrawProgressiveVideoFrameAlpha(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result)
{
	// Ensure thread safety
	FScopeLock Lock(&RenderSyncContext);

	FTextureRHIRef TargetableTexture;

	// check for our frame sync object and that we are actually connected to the end point
	if (p_framesync_instance != nullptr)
	{
		// Initialize the frame size parameter
		FIntPoint FrameSize = FIntPoint(Result.xres, Result.yres);

		if (!RenderTarget.IsValid() || !RenderTargetDescriptor.IsValid() ||
			RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0) ||
			DrawMode != EDrawMode::ProgressiveAlpha)
		{
			// Create the RenderTarget descriptor
			RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(
				FrameSize, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_SRGB, false);

			// Update the shader resource for the 'SourceTexture'
			// The source texture will be given UYVY data, so make it half-width
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
			const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverProgressiveAlphaSourceTexture"))
				.SetExtent(FrameSize.X / 2, FrameSize.Y)
				.SetFormat(PF_B8G8R8A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceTexture = RHICreateTexture(CreateDesc);

			const FRHITextureCreateDesc CreateAlphaDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverProgressiveAlphaSourceAlphaTexture"))
				.SetExtent(FrameSize.X, FrameSize.Y)
				.SetFormat(PF_A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceAlphaTexture = RHICreateTexture(CreateAlphaDesc);
#elif (ENGINE_MAJOR_VERSION == 5)
			FRHIResourceCreateInfo CreateInfo(TEXT("NDIMediaReceiverProgressiveAlphaSourceTexture"));
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
			RHICreateTargetableShaderResource2D(FrameSize.X/2, FrameSize.Y, PF_B8G8R8A8, 1, TexCreate_Dynamic,
												TexCreate_RenderTargetable, false, CreateInfo, SourceTexture,
												DummyTexture2DRHI);
			FRHIResourceCreateInfo CreateAlphaInfo(TEXT("NDIMediaReceiverProgressiveAlphaSourceAlphaTexture"));
			TRefCountPtr<FRHITexture2D> DummyAlphaTexture2DRHI;
			RHICreateTargetableShaderResource2D(FrameSize.X, FrameSize.Y, PF_A8, 1, TexCreate_Dynamic,
												TexCreate_RenderTargetable, false, CreateAlphaInfo, SourceAlphaTexture,
												DummyAlphaTexture2DRHI);
#else
			#error "Unsupported engine major version"
#endif

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDescriptor, RenderTarget, TEXT("NDIIO"));

			DrawMode = EDrawMode::ProgressiveAlpha;
		}

		TargetableTexture = RenderTarget->GetRHI();

		// Initialize the Graphics Pipeline State Object
		FGraphicsPipelineStateInitializer GraphicsPSOInit;

		// Initialize the Render pass with the conversion texture
		FRHITexture* ConversionTexture = TargetableTexture.GetReference();
		FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

		// configure media shaders
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// construct the shaders
		TShaderMapRef<FNDIIOShaderVS> VertexShader(ShaderMap);
		TShaderMapRef<FNDIIOShaderUYVAtoBGRAPS> ConvertShader(ShaderMap);

		FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();

		// Needs to be called *before* ApplyCachedRenderTargets, since BeginRenderPass is caching the render targets.
		RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Recv Color Conversion"));

		// do as it suggests
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// set the state objects
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
																CW_NONE, CW_NONE>::GetRHI();
		// perform binding operations for the shaders to be used
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
		// Going to draw triangle strips
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

		// Ensure the pipeline state is set to the one we've configured
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		// set the stream source
		RHICmdList.SetStreamSource(0, VertexBuffer, 0);

		// set the texture parameter of the conversion shader
		//bool bHasAlpha = (Result.FourCC == NDIlib_FourCC_video_type_UYVA) ? true : false;
		FNDIIOShaderUYVAtoBGRAPS::Params Params(SourceTexture, SourceAlphaTexture, FrameSize,
		                                        FVector2D(0, 0), FVector2D(1, 1),
		                                        bPerformsRGBtoLinear ? FNDIIOShaderPS::EColorCorrection::sRGBToLinear : FNDIIOShaderPS::EColorCorrection::None,
		                                        FVector2D(0.f, 1.f));
		ConvertShader->SetParameters(RHICmdList, Params);

		// Create the update region structure
		FUpdateTextureRegion2D Region(0, 0, 0, 0, FrameSize.X/2, FrameSize.Y);
		FUpdateTextureRegion2D AlphaRegion(0, 0, 0, 0, FrameSize.X, FrameSize.Y);

		// Set the Pixel data of the NDI Frame to the SourceTexture
		RHIUpdateTexture2D(SourceTexture, 0, Region, Result.line_stride_in_bytes, (uint8*&)Result.p_data);
		RHIUpdateTexture2D(SourceAlphaTexture, 0, AlphaRegion, FrameSize.X, ((uint8*&)Result.p_data)+FrameSize.Y*Result.line_stride_in_bytes);

		// begin our drawing
		{
			RHICmdList.SetViewport(0, 0, 0.0f, FrameSize.X, FrameSize.Y, 1.0f);
			RHICmdList.DrawPrimitive(0, 2, 1);
		}

		RHICmdList.EndRenderPass();
	}

	return TargetableTexture;
}


FTextureRHIRef UNDIMediaReceiver::DrawInterlacedVideoFrame(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result)
{
	// Ensure thread safety
	FScopeLock Lock(&RenderSyncContext);

	FTextureRHIRef TargetableTexture;

	// check for our frame sync object and that we are actually connected to the end point
	if (p_framesync_instance != nullptr)
	{
		// Initialize the frame size parameter
		FIntPoint FieldSize = FIntPoint(Result.xres, Result.yres);
		FIntPoint FrameSize = FIntPoint(Result.xres, Result.yres*2);

		if (!RenderTarget.IsValid() || !RenderTargetDescriptor.IsValid() ||
			RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0) ||
			DrawMode != EDrawMode::Interlaced)
		{
			// Create the RenderTarget descriptor
			RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(
				FrameSize, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_SRGB, false);

			// Update the shader resource for the 'SourceTexture'
			// The source texture will be given UYVY data, so make it half-width
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
			const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverInterlacedSourceTexture"))
				.SetExtent(FieldSize.X / 2, FieldSize.Y)
				.SetFormat(PF_B8G8R8A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceTexture = RHICreateTexture(CreateDesc);
#elif (ENGINE_MAJOR_VERSION == 5)
			FRHIResourceCreateInfo CreateInfo(TEXT("NDIMediaReceiverInterlacedSourceTexture"));
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
			RHICreateTargetableShaderResource2D(FieldSize.X/2, FieldSize.Y, PF_B8G8R8A8, 1, TexCreate_Dynamic,
												TexCreate_RenderTargetable, false, CreateInfo, SourceTexture,
												DummyTexture2DRHI);
#else
			#error "Unsupported engine major version"
#endif

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDescriptor, RenderTarget, TEXT("NDIIO"));

			DrawMode = EDrawMode::Interlaced;
		}

		TargetableTexture = RenderTarget->GetRHI();

		// Initialize the Graphics Pipeline State Object
		FGraphicsPipelineStateInitializer GraphicsPSOInit;

		// Initialize the Render pass with the conversion texture
		FRHITexture* ConversionTexture = TargetableTexture.GetReference();
		FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

		// configure media shaders
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// construct the shaders
		TShaderMapRef<FNDIIOShaderVS> VertexShader(ShaderMap);
		TShaderMapRef<FNDIIOShaderUYVYtoBGRAPS> ConvertShader(ShaderMap);

		float FieldUVOffset = (Result.frame_format_type == NDIlib_frame_format_type_field_1) ? 0.5f/Result.yres : 0.f;
		FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(0.f, 1.f, 0.f-FieldUVOffset, 1.f-FieldUVOffset);

		// Needs to be called *before* ApplyCachedRenderTargets, since BeginRenderPass is caching the render targets.
		RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Recv Color Conversion"));

		// do as it suggests
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// set the state objects
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
																CW_NONE, CW_NONE>::GetRHI();
		// perform binding operations for the shaders to be used
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
		// Going to draw triangle strips
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

		// Ensure the pipeline state is set to the one we've configured
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		// set the stream source
		RHICmdList.SetStreamSource(0, VertexBuffer, 0);

		// set the texture parameter of the conversion shader
		FNDIIOShaderUYVYtoBGRAPS::Params Params(SourceTexture, SourceTexture, FrameSize,
		                                        FVector2D(0, 0), FVector2D(1, 1),
		                                        bPerformsRGBtoLinear ? FNDIIOShaderPS::EColorCorrection::sRGBToLinear : FNDIIOShaderPS::EColorCorrection::None,
		                                        FVector2D(0.f, 1.f));
		ConvertShader->SetParameters(RHICmdList, Params);

		// Create the update region structure
		FUpdateTextureRegion2D Region(0, 0, 0, 0, FieldSize.X/2, FieldSize.Y);

		// Set the Pixel data of the NDI Frame to the SourceTexture
		RHIUpdateTexture2D(SourceTexture, 0, Region, Result.line_stride_in_bytes, (uint8*&)Result.p_data);

		// begin our drawing
		{
			RHICmdList.SetViewport(0, 0, 0.0f, FrameSize.X, FrameSize.Y, 1.0f);
			RHICmdList.DrawPrimitive(0, 2, 1);
		}

		RHICmdList.EndRenderPass();
	}

	return TargetableTexture;
}

FTextureRHIRef UNDIMediaReceiver::DrawInterlacedVideoFrameAlpha(FRHICommandListImmediate& RHICmdList, const NDIlib_video_frame_v2_t& Result)
{
	// Ensure thread safety
	FScopeLock Lock(&RenderSyncContext);

	FTextureRHIRef TargetableTexture;

	// check for our frame sync object and that we are actually connected to the end point
	if (p_framesync_instance != nullptr)
	{
		// Initialize the frame size parameter
		FIntPoint FieldSize = FIntPoint(Result.xres, Result.yres);
		FIntPoint FrameSize = FIntPoint(Result.xres, Result.yres*2);

		if (!RenderTarget.IsValid() || !RenderTargetDescriptor.IsValid() ||
			RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0) ||
			DrawMode != EDrawMode::InterlacedAlpha)
		{
			// Create the RenderTarget descriptor
			RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(
				FrameSize, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_SRGB, false);

			// Update the shader resource for the 'SourceTexture'
			// The source texture will be given UYVY data, so make it half-width
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 or later
			const FRHITextureCreateDesc CreateDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverInterlacedAlphaSourceTexture"))
				.SetExtent(FieldSize.X / 2, FieldSize.Y)
				.SetFormat(PF_B8G8R8A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceTexture = RHICreateTexture(CreateDesc);

			const FRHITextureCreateDesc CreateAlphaDesc = FRHITextureCreateDesc::Create2D(TEXT("NDIMediaReceiverInterlacedAlphaSourceAlphaTexture"))
				.SetExtent(FieldSize.X, FieldSize.Y)
				.SetFormat(PF_A8)
				.SetNumMips(1)
				.SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::Dynamic);

			SourceAlphaTexture = RHICreateTexture(CreateAlphaDesc);
#elif (ENGINE_MAJOR_VERSION == 5)
			FRHIResourceCreateInfo CreateInfo(TEXT("NDIMediaReceiverInterlacedAlphaSourceTexture"));
			TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
			RHICreateTargetableShaderResource2D(FieldSize.X/2, FieldSize.Y, PF_B8G8R8A8, 1, TexCreate_Dynamic,
												TexCreate_RenderTargetable, false, CreateInfo, SourceTexture,
												DummyTexture2DRHI);
			FRHIResourceCreateInfo CreateAlphaInfo(TEXT("NDIMediaReceiverInterlacedAlphaSourceAlphaTexture"));
			TRefCountPtr<FRHITexture2D> DummyAlphaTexture2DRHI;
			RHICreateTargetableShaderResource2D(FieldSize.X, FieldSize.Y, PF_A8, 1, TexCreate_Dynamic,
												TexCreate_RenderTargetable, false, CreateAlphaInfo, SourceAlphaTexture,
												DummyAlphaTexture2DRHI);
#else
			#error "Unsupported engine major version"
#endif

			// Find a free target-able texture from the render pool
			GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDescriptor, RenderTarget, TEXT("NDIIO"));

			DrawMode = EDrawMode::InterlacedAlpha;
		}

		TargetableTexture = RenderTarget->GetRHI();

		// Initialize the Graphics Pipeline State Object
		FGraphicsPipelineStateInitializer GraphicsPSOInit;

		// Initialize the Render pass with the conversion texture
		FRHITexture* ConversionTexture = TargetableTexture.GetReference();
		FRHIRenderPassInfo RPInfo(ConversionTexture, ERenderTargetActions::DontLoad_Store);

		// configure media shaders
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		// construct the shaders
		TShaderMapRef<FNDIIOShaderVS> VertexShader(ShaderMap);
		TShaderMapRef<FNDIIOShaderUYVAtoBGRAPS> ConvertShader(ShaderMap);

		float FieldUVOffset = (Result.frame_format_type == NDIlib_frame_format_type_field_1) ? 0.5f/Result.yres : 0.f;
		FBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(0.f, 1.f, 0.f-FieldUVOffset, 1.f-FieldUVOffset);

		// Needs to be called *before* ApplyCachedRenderTargets, since BeginRenderPass is caching the render targets.
		RHICmdList.BeginRenderPass(RPInfo, TEXT("NDI Recv Color Conversion"));

		// do as it suggests
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// set the state objects
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE,
																CW_NONE, CW_NONE>::GetRHI();
		// perform binding operations for the shaders to be used
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
		// Going to draw triangle strips
		GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

		// Ensure the pipeline state is set to the one we've configured
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		// set the stream source
		RHICmdList.SetStreamSource(0, VertexBuffer, 0);

		// set the texture parameter of the conversion shader
		FNDIIOShaderUYVAtoBGRAPS::Params Params(SourceTexture, SourceAlphaTexture, FrameSize,
		                                        FVector2D(0, 0), FVector2D(1, 1),
		                                        bPerformsRGBtoLinear ? FNDIIOShaderPS::EColorCorrection::sRGBToLinear : FNDIIOShaderPS::EColorCorrection::None,
		                                        FVector2D(0.f, 1.f));
		ConvertShader->SetParameters(RHICmdList, Params);

		// Create the update region structure
		FUpdateTextureRegion2D Region(0, 0, 0, 0, FieldSize.X/2, FieldSize.Y);
		FUpdateTextureRegion2D AlphaRegion(0, 0, 0, 0, FieldSize.X, FieldSize.Y);

		// Set the Pixel data of the NDI Frame to the SourceTexture
		RHIUpdateTexture2D(SourceTexture, 0, Region, Result.line_stride_in_bytes, (uint8*&)Result.p_data);
		RHIUpdateTexture2D(SourceAlphaTexture, 0, AlphaRegion, FieldSize.X, ((uint8*&)Result.p_data)+FieldSize.Y*Result.line_stride_in_bytes);

		// begin our drawing
		{
			RHICmdList.SetViewport(0, 0, 0.0f, FrameSize.X, FrameSize.Y, 1.0f);
			RHICmdList.DrawPrimitive(0, 2, 1);
		}

		RHICmdList.EndRenderPass();
	}

	return TargetableTexture;
}

/**
	Attempts to gather the performance metrics of the connection to the remote source
*/
void UNDIMediaReceiver::GatherPerformanceMetrics()
{
	// provide references to store the values
	NDIlib_recv_performance_t stable_performance;
	NDIlib_recv_performance_t dropped_performance;

	// get the performance values from the SDK
	NDIlib_recv_get_performance(p_receive_instance, &stable_performance, &dropped_performance);

	// update our structure with the updated values
	this->PerformanceData.AudioFrames = stable_performance.audio_frames;
	this->PerformanceData.DroppedAudioFrames = dropped_performance.audio_frames;
	this->PerformanceData.DroppedMetadataFrames = dropped_performance.metadata_frames;
	this->PerformanceData.DroppedVideoFrames = dropped_performance.video_frames;
	this->PerformanceData.MetadataFrames = stable_performance.metadata_frames;
	this->PerformanceData.VideoFrames = stable_performance.video_frames;
}

/**
	Returns the current performance data of the receiver while connected to the source
*/
const FNDIReceiverPerformanceData& UNDIMediaReceiver::GetPerformanceData() const
{
	return this->PerformanceData;
}

/**
	Returns a value indicating whether this object is currently connected to the sender source
*/
const bool UNDIMediaReceiver::GetIsCurrentlyConnected() const
{
	if (p_receive_instance != nullptr)
		return NDIlib_recv_get_no_connections(p_receive_instance) > 0 ? true : false;
	else
		return false;
}

/**
	Returns the current connection information of the connected source
*/
const FNDIConnectionInformation& UNDIMediaReceiver::GetCurrentConnectionInformation() const
{
	return this->ConnectionInformation;
}

/**
	Returns the current timecode of the connected source
*/
const FTimecode& UNDIMediaReceiver::GetCurrentTimecode() const
{
	return this->Timecode;
}

/**
	Set whether or not a sRGB to Linear conversion is made
*/
void UNDIMediaReceiver::PerformsRGBToLinearConversion(bool Value)
{
	this->bPerformsRGBtoLinear = Value;
}

/**
	Returns the current framerate of the connected source
*/
const FFrameRate& UNDIMediaReceiver::GetCurrentFrameRate() const
{
	return this->FrameRate;
}

const FIntPoint& UNDIMediaReceiver::GetCurrentResolution() const
{
	return this->Resolution;
}


FString UNDIMediaReceiver::GetUrl() const
{
	if(!ConnectionInformation.SourceName.IsEmpty())
		return "ndiio://" + ConnectionInformation.SourceName;
	else if(!ConnectionSetting.SourceName.IsEmpty())
		return "ndiio://" + ConnectionSetting.SourceName;
	else if(!ConnectionInformation.Url.IsEmpty())
		return "ndiio://" + ConnectionInformation.Url;
	else if(!ConnectionSetting.Url.IsEmpty())
		return "ndiio://" + ConnectionSetting.Url;
	else
		return "ndiio://";
}

bool UNDIMediaReceiver::GetMediaOption(const FName& Key, bool DefaultValue) const
{
	if (Key == NDIMediaOption::IsNDIMediaReceiver) { return true; }

	return Super::GetMediaOption(Key, DefaultValue);
}

int64 UNDIMediaReceiver::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == FMediaIOCoreMediaOption::FrameRateNumerator) { return FrameRate.Numerator; }
	if (Key == FMediaIOCoreMediaOption::FrameRateDenominator) { return FrameRate.Denominator; }
	if (Key == FMediaIOCoreMediaOption::ResolutionWidth) { return Resolution.X; }
	if (Key == FMediaIOCoreMediaOption::ResolutionHeight) { return Resolution.Y; }

	return Super::GetMediaOption(Key, DefaultValue);
}

FString UNDIMediaReceiver::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	return Super::GetMediaOption(Key, DefaultValue);
}

bool UNDIMediaReceiver::HasMediaOption(const FName& Key) const
{
	if (   Key == NDIMediaOption::IsNDIMediaReceiver)
	{
		return true;
	}

	if (   Key == FMediaIOCoreMediaOption::FrameRateNumerator
		|| Key == FMediaIOCoreMediaOption::FrameRateDenominator
		|| Key == FMediaIOCoreMediaOption::ResolutionWidth
		|| Key == FMediaIOCoreMediaOption::ResolutionHeight)
	{
		return true;
	}

	return Super::HasMediaOption(Key);
}


FTextureResource* UNDIMediaReceiver::GetVideoTextureResource() const
{
	if(IsValid(this->VideoTexture))
		return this->VideoTexture->GetResource();

	return nullptr;
}

FTextureResource* UNDIMediaReceiver::GetInternalVideoTextureResource() const
{
	if(IsValid(this->InternalVideoTexture))
		return this->InternalVideoTexture->GetResource();

	return nullptr;
}


#if WITH_EDITORONLY_DATA

void UNDIMediaReceiver::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// get the name of the property which changed
	FName MemberPropertyName =
		(PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	FName PropertyName =
		(PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UNDIMediaReceiver, ConnectionSetting))
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FNDIConnectionInformation, SourceName))
		{
			ConnectionSetting.SourceName.Split(TEXT(" "), &ConnectionSetting.MachineName, &ConnectionSetting.StreamName);
			ConnectionSetting.StreamName.RemoveFromStart("(");
			ConnectionSetting.StreamName.RemoveFromEnd(")");
		}

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FNDIConnectionInformation, MachineName))
		{
			if ((!ConnectionSetting.MachineName.IsEmpty()) && (!ConnectionSetting.StreamName.IsEmpty()))
				ConnectionSetting.SourceName = ConnectionSetting.MachineName + " (" + ConnectionSetting.StreamName + ")";
			else
				ConnectionSetting.SourceName = FString("");
		}

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FNDIConnectionInformation, StreamName))
		{
			if ((!ConnectionSetting.MachineName.IsEmpty()) && (!ConnectionSetting.StreamName.IsEmpty()))
				ConnectionSetting.SourceName = ConnectionSetting.MachineName + " (" + ConnectionSetting.StreamName + ")";
			else
				ConnectionSetting.SourceName = FString("");
		}
	}

	// call the base class 'PostEditChangeProperty'
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif
