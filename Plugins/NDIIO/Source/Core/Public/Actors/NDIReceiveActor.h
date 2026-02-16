/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <GameFramework/Actor.h>
#include <Components/AudioComponent.h>
#include <Components/StaticMeshComponent.h>
#include <Components/NDIReceiverComponent.h>
#include <Enumerations/NDIAudioChannels.h>

#include "NDIReceiveActor.generated.h"

UCLASS(HideCategories = (Activation, Rendering, AssetUserData, Material, Attachment, Actor, Input, Cooking, LOD, Sound,
						 StaticMesh, Materials),
	   Category = "NDI IO", META = (DisplayName = "NDI Receive Actor"))
class NDIIO_API ANDIReceiveActor : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	/** The desired width of the frame in cm, represented in the virtual scene */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Interp, BlueprintSetter = "SetFrameWidth", Category = "NDI IO",
			  META = (DisplayName = "Frame Width", AllowPrivateAccess = true))
	float FrameWidth = 177.778;

	/** The desired height of the frame in cm, represented in the virtual scene */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Interp, BlueprintSetter = "SetFrameHeight", Category = "NDI IO",
			  META = (DisplayName = "Frame Height", AllowPrivateAccess = true))
	float FrameHeight = 100.0f;

	/**
		Indicates that this object should play the audio.

		*Note Audio played by this object will be played as a UI sound, and won't normalize the audio
			  if the same 'MediaSource' object is being used as the audio source on multiple receivers.
	*/
	UPROPERTY(EditInstanceOnly, BlueprintSetter = "UpdateAudioPlayback", Category = "NDI IO",
			  META = (DisplayName = "Enable Audio Playback?", AllowPrivateAccess = true))
	bool bEnableAudioPlayback = false;

	UPROPERTY(EditInstanceOnly, BlueprintSetter = "UpdateAudioPlaybackChannels", Category = "NDI IO",
			  META = (DisplayName = "Audio Playback Channels", AllowPrivateAccess = true))
	ENDIAudioChannels AudioPlaybackChannels = ENDIAudioChannels::Mono;

	/** Enable/disable the use of the color channels (if there are any) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintSetter = "EnableColor", Category = "NDI IO",
			  META = (DisplayName = "Enable Color?", AllowPrivateAccess = true))
	bool bEnableColor = true;

	/** Enable/disable the use of the alpha channel (if there is one) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintSetter = "EnableAlpha", Category = "NDI IO",
			  META = (DisplayName = "Enable Alpha?", AllowPrivateAccess = true))
	bool bEnableAlpha = true;

	/** The Receiver object used to get Audio, Video, and Metadata from on the network */
	UPROPERTY(BlueprintReadWrite, EditInstanceOnly, Category = "NDI IO",
			  META = (DisplayName = "NDI Media Source", AllowPrivateAccess = true))
	UNDIMediaReceiver* NDIMediaSource = nullptr;

	/** The component used to display the video received from the Media Sender object */
	UPROPERTY(Transient, META = (DisplayName = "Video Mesh Component"))
	UStaticMeshComponent* VideoMeshComponent = nullptr;

private:
	/** The component used to play the audio from the NDI Media source */
	UPROPERTY(transient)
	UAudioComponent* AudioComponent = nullptr;

	/** The audio sound wave which receives the audio from the NDI Media source */
	UPROPERTY(transient)
	UNDIMediaSoundWave* AudioSoundWave = nullptr;

private:
	/** The material we are trying to apply to the video mesh */
	class UMaterialInterface* VideoMaterial = nullptr;

	/** The dynamic material to apply to the plane object of this actor */
	UPROPERTY()
	class UMaterialInstanceDynamic* VideoMaterialInstance = nullptr;

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	/**
		Attempts to set the desired frame size in cm, represented in the virtual scene
	*/
	void SetFrameSize(FVector2D InFrameSize);

	/**
		Returns the current frame size of the 'VideoMeshComponent' for this object
	*/
	const FVector2D GetFrameSize() const;

private:
	UFUNCTION(BlueprintSetter)
	void SetFrameHeight(const float& InFrameHeight);

	UFUNCTION(BlueprintSetter)
	void SetFrameWidth(const float& InFrameWidth);

	UFUNCTION(BlueprintSetter)
	void UpdateAudioPlayback(const bool& Enabled);

	UFUNCTION(BlueprintSetter)
	void UpdateAudioPlaybackChannels(const ENDIAudioChannels& Channels);

	UFUNCTION(BlueprintSetter)
	void EnableColor(const bool& Enabled);

	UFUNCTION(BlueprintSetter)
	void EnableAlpha(const bool& Enabled);

#if WITH_EDITORONLY_DATA

	virtual void PreEditChange(FProperty* InProperty) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

#endif

	void ApplyChannelsMode();
	bool bStoppedForChannelsMode = false;
};