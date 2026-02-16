// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "IViveOpenXRXrTrackerModule.h"
#include "XRMotionControllerBase.h"
#include "OpenXRCommon.h"
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Containers/Ticker.h"
#include "IOpenXRExtensionPlugin.h"
#include "IInputDeviceModule.h"
#include "IInputDevice.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/StrongObjectPtr.h"

#include "OpenXRCore.h"
#include "OpenXRHMD.h"

DECLARE_LOG_CATEGORY_EXTERN(LogViveOpenXRXrTracker, Log, All);

class FViveOpenXRXrTracker :
	public IInputDevice,
	public IOpenXRExtensionPlugin,
	public FXRMotionControllerBase
{
public:
	struct FViveXrTracker
	{
	public:
		FViveXrTracker();

		XrPath XrTrackerUserPath; // XrPath SubactionPath
		XrPath xrTrackerPosePath;
		XrAction xrTrackerPoseAction;
		XrSpaceLocation XrTrackerSpaceLocation;
		TArray<XrPath> XrTrackerPaths;
		XrActionStatePose XrTrackerStatePose;
		int32 DeviceId;

		FString PoseActionNameStr;
		FString MenuActionNameStr;
		FString TriggerActionNameStr;
		FString SqueezeActionNameStr;
		FString TrackpadActionNameStr;

		// Paths
		XrPath          MenuClickPath;
		XrPath          TriggerClickPath;
		XrPath          SqueezeClickPath;
		XrPath          TrackpadClickPath;

		// Actions
		XrAction		MenuClickAction;
		XrAction		TriggerClickAction;
		XrAction		SqueezeClickAction;
		XrAction		TrackpadClickAction;

		// States
		XrActionStateBoolean MenuClickActionState;
		XrActionStateBoolean TriggerClickActionState;
		XrActionStateBoolean SqueezeClickActionState;
		XrActionStateBoolean TrackpadClickActionState;

		// Keys
		FKey MenuClickKey;
		FKey TriggerClickKey;
		FKey SqueezeClickKey;
		FKey TrackpadClickKey;

		TArray<TObjectPtr<const UInputAction>> MenuClickActions;
		TArray<TObjectPtr<const UInputAction>> TriggerClickActions;
		TArray<TObjectPtr<const UInputAction>> SqueezeClickActions;
		TArray<TObjectPtr<const UInputAction>> TrackpadClickActions;

		void AddAction(XrActionSet& InActionSet, const FName& InName, const TArray<XrPath>& InSubactionPaths, XrActionType InActionType, XrAction& InAction);

		int32 AddTrackedDevices(class FOpenXRHMD* HMD);
		void GetSuggestedBindings(TArray<XrActionSuggestedBinding>& OutSuggestedBindings);
		void SyncActionStates(XrSession InSession);

		void CheckAndAddEnhancedInputAction(FEnhancedActionKeyMapping EnhancedActionKeyMapping);
	};
public:
	FViveOpenXRXrTracker(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FViveOpenXRXrTracker();

	//OpenXRFunctions
	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	//virtual bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics) override;
	virtual void PostCreateInstance(XrInstance InInstance) override;
	virtual void AttachActionSets(TSet<XrActionSet>& OutActionSets) override;
	virtual void GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets) override;
	virtual void PostCreateSession(XrSession InSession) override;
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override;
	virtual void OnDestroySession(XrSession InSession) override;
	virtual void PostSyncActions(XrSession InSession) override;
	virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace) override;
	//virtual void OnDestroySession(XrSession InSession) override;

	bool m_EnableXrTracker = false;
	bool m_EnableXrTrackerInputs = false;
	class FOpenXRHMD* OpenXRHMD = nullptr;
	float WorldToMetersScale_ = 100;
	PFN_xrGetSystemProperties xrGetSystemProperties = nullptr;
	PFN_xrEnumeratePathsForInteractionProfileHTC xrEnumeratePathsForInteractionProfileHTC = nullptr;

	TArray<XrPath> XrTrackerUserPaths;
	TArray<XrPath> XrTrackerPaths;

	static inline FViveOpenXRXrTracker* GetInstance() {
		return m_Instance;
	}

	/************************************************************************/
	/* IMotionController                                                    */
	/************************************************************************/
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
	virtual bool GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
	virtual FName GetMotionControllerDeviceTypeName() const override;
	virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;
	virtual bool SetPlayerMappableInputConfig(TObjectPtr<class UPlayerMappableInputConfig> InputConfig) override;

	/************************************************************************/
	/* IInputDevice                                                         */
	/************************************************************************/
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
	virtual void SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override
	{
		MessageHandler = InMessageHandler;
	}
	virtual void SendControllerEvents() override {}
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {}
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override {}
	virtual void Tick(float DeltaTime) override {}
private:
	/** The recipient of motion controller input events */
	TSharedPtr< FGenericApplicationMessageHandler > MessageHandler;
	TArray<TTuple<FName, XrActionStateBoolean*>> KeyActionStates;

	TMap<TStrongObjectPtr<const UInputMappingContext>, uint32> InputMappingContextToPriorityMap;

	void SendInputEvent_Legacy();
	void SendInputEvent_EnhancedInput();

protected:
	static FViveOpenXRXrTracker* m_Instance;

private:
	XrInstance Instance = XR_NULL_HANDLE;
	bool bSessionStarted = false;
	bool bActionsAttached = false;
	bool bPathEnumerated = false;
	int32 DeviceIndex = 0;
	XrPath xrTrackerInteractionProfile = XR_NULL_PATH;
	XrSession m_Session = XR_NULL_HANDLE;
	XrActionSet XrTrackerActionSet = XR_NULL_HANDLE;
	TMap<FName, FViveXrTracker> viveXrTrackerMap;

	////////Tracker User Path//////////
	const TArray<FString> xrTrackerPaths =
	{ FString("/user/xr_tracker_htc/vive_ultimate_tracker_0") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_1") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_2") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_3") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_4") };

	const TArray<FString> xrTrackerPosePaths =
	{ FString("/user/xr_tracker_htc/vive_ultimate_tracker_0/input/entity_htc/pose") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_1/input/entity_htc/pose") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_2/input/entity_htc/pose") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_3/input/entity_htc/pose") ,
		FString("/user/xr_tracker_htc/vive_ultimate_tracker_4/input/entity_htc/pose") };
};

class FViveOpenXRXrTrackerModule : public IViveOpenXRXrTrackerModule
{
public:
	FViveOpenXRXrTrackerModule();
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;

	FViveOpenXRXrTracker* GetXrTracker();

private:
	TSharedPtr<FViveOpenXRXrTracker> XrTracker;

};