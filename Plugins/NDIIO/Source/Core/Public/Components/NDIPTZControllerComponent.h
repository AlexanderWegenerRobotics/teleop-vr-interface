/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <UObject/Interface.h>
#include <Components/ActorComponent.h>
#include <Objects/Media/NDIMediaSender.h>

#include "NDIPTZControllerComponent.generated.h"


USTRUCT(BlueprintType, Blueprintable, Category = "NDI IO", META = (DisplayName = "NDI PTZ State"))
struct NDIIO_API FPTZState
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float Pan;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float Tilt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float FieldOfView;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	float FocusDistance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	bool bAutoFocus;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PTZ")
	FTransform CameraTransform;

	FPTZState()
		: Pan(0.f)
		, Tilt(0.f)
		, FieldOfView(90.f)
		, FocusDistance(0.5f)
		, bAutoFocus(false)
	{}
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNDIEventDelegate_OnPTZPanTiltSpeed, float, PanSpeed, float, TiltSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIEventDelegate_OnPTZZoomSpeed, float, ZoomSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNDIEventDelegate_OnPTZFocus, bool, AutoMode, float, Distance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIEventDelegate_OnPTZStore, int, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNDIEventDelegate_OnPTZRecall, int, Index);



UINTERFACE(BlueprintType, Blueprintable, Category = "NDI IO",
	   META = (DisplayName = "NDI PTZ Controllable", BlueprintSpawnableComponent))
class NDIIO_API UPTZControllableInterface : public UInterface
{
	GENERATED_BODY()
};

class IPTZControllableInterface
{
	GENERATED_BODY()

public:
	virtual FPTZState GetPTZStateFromUE() const = 0;
	virtual void SetPTZStateToUE(const FPTZState& PTZState) = 0;
};


UCLASS(BlueprintType, Blueprintable, Category = "NDI IO",
	   META = (DisplayName = "NDI PTZ Controller", BlueprintSpawnableComponent))
class UPTZController : public UActorComponent
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Enable PTZ", AllowPrivateAccess = true), Category="PTZ")
	bool EnablePTZ = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Pan Limit", AllowPrivateAccess = true), Category="PTZ")
	bool PTZWithPanLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Pan Min Limit", UIMin="-180", UIMax="180", AllowPrivateAccess = true), Category="PTZ")
	float PTZPanMinLimit = -180.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Pan Max Limit", UIMin="-180", UIMax="180", AllowPrivateAccess = true), Category="PTZ")
	float PTZPanMaxLimit = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Invert Pan", AllowPrivateAccess = true), Category="PTZ")
	bool bPTZPanInvert = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Tilt Limit", AllowPrivateAccess = true), Category="PTZ")
	bool PTZWithTiltLimit = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Tilt Min Limit", UIMin="-180", UIMax="180", AllowPrivateAccess = true), Category="PTZ")
	float PTZTiltMinLimit = -90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Tilt Max Limit", UIMin="-180", UIMax="180", AllowPrivateAccess = true), Category="PTZ")
	float PTZTiltMaxLimit = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Invert Tilt", AllowPrivateAccess = true), Category="PTZ")
	bool bPTZTiltInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Field of View Limit", AllowPrivateAccess = true), Category="PTZ")
	bool PTZWithFoVLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Field of View Min Limit", UIMin="5", UIMax="170", AllowPrivateAccess = true), Category="PTZ")
	float PTZFoVMinLimit = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Field of View Max Limit", UIMin="5", UIMax="170", AllowPrivateAccess = true), Category="PTZ")
	float PTZFoVMaxLimit = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="Preset Recall Easing", UIMin="0", UIMax="60", AllowPrivateAccess = true), Category="PTZ")
	float PTZRecallEasing = 2.f;

	UPROPERTY(BlueprintReadWrite, meta=(AllowPrivateAccess = true), Category="PTZ")
	float PTZPanSpeed = 0.f;
	UPROPERTY(BlueprintReadWrite, meta=(AllowPrivateAccess = true), Category="PTZ")
	float PTZTiltSpeed = 0.f;
	UPROPERTY(BlueprintReadWrite, meta=(AllowPrivateAccess = true), Category="PTZ")
	float PTZZoomSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayName="PTZ Presets", AllowPrivateAccess = true), Category="PTZ")
	TArray<FPTZState> PTZStoredStates;

	UPROPERTY(BlueprintReadWrite, EditInstanceOnly, Category = "NDI IO", META = (DisplayName = "NDI Media Source", AllowPrivateAccess = true))
	UNDIMediaSender* NDIMediaSource = nullptr;

	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On PTZ Pan Tilt Speed", AllowPrivateAccess = true))
	FNDIEventDelegate_OnPTZPanTiltSpeed OnPTZPanTiltSpeed;
	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On PTZ Zoom Speed", AllowPrivateAccess = true))
	FNDIEventDelegate_OnPTZZoomSpeed OnPTZZoomSpeed;
	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On PTZ Focus", AllowPrivateAccess = true))
	FNDIEventDelegate_OnPTZFocus OnPTZFocus;
	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On PTZ Store", AllowPrivateAccess = true))
	FNDIEventDelegate_OnPTZStore OnPTZStore;
	UPROPERTY(BlueprintAssignable, Category="NDI Events", META = (DisplayName = "On PTZ Recall", AllowPrivateAccess = true))
	FNDIEventDelegate_OnPTZRecall OnPTZRecall;

public:
	/** Call with the PTZ metadata received from an NDI media sender */
	UFUNCTION(BlueprintCallable, Category = "NDI IO", META = (DisplayName = "Receive Metadata From Sender"))
	void ReceiveMetaDataFromSender(UNDIMediaSender* Sender, FString Data);

public:
	UPTZController();
	virtual ~UPTZController();

	/**
		Initialize this component with the required media source to receive metadata from.
		Returns false, if the MediaSource is already been set. This is usually the case when this component is
		initialized in Blueprints.
	*/
	bool Initialize(UNDIMediaSender* InMediaSource = nullptr);

	void SetPTZPanTiltSpeed(float PanSpeed, float TiltSpeed);
	void SetPTZZoomSpeed(float ZoomSpeed);
	void SetPTZFocus(bool AutoMode, float Distance);
	void StorePTZState(int Index);
	void RecallPTZState(int Index);

	FPTZState GetPTZStateFromUE() const;
	void SetPTZStateToUE(const FPTZState& PTZState);

protected:
	virtual void InitializeComponent() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	TSharedPtr<class NDIXmlParser> NDIMetadataParser;

	struct FPTZStateInterp
	{
		FPTZState PTZTargetState;
		float EasingDuration { 0 };
		float EasingRemaining { 0 };
	};
	FPTZStateInterp PTZStateInterp;
};
