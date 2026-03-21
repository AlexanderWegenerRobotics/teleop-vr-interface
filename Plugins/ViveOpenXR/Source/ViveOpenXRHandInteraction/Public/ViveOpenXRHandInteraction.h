// Copyright HTC Corporation. All Rights Reserved.

#pragma once
#include "IViveOpenXRHandInteractionModule.h"
#include "XRMotionControllerBase.h"
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Containers/Ticker.h"
#include "IOpenXRExtensionPlugin.h"
#include "IInputDevice.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/StrongObjectPtr.h"

#include "OpenXRCore.h"
#include "OpenXRHMD.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "InputCoreTypes.h"
#include "IInputDeviceModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogViveOpenXRHandInteraction, Log, All);

namespace Side
{
	const int RIGHT = 0;
	const int LEFT = 1;
	const int COUNT = 2;
}

namespace Type
{
	const int SQUEEZE = 0;
	const int SELECT = 1;
	const int AIM = 2;
	const int GRIP = 3;
	const int COUNT = 4;
}

namespace HandInteractionKeys
{
	/// ---- Left hand only ----

	// "/input/pinch_ext/value"
	const FKey  HandInteraction_Left_Pinch_Value("HandInteraction_Left_Pinch_Value");
	// "/input/aim_activate_ext/value"
	const FKey  HandInteraction_Left_Aim_Value("HandInteraction_Left_Aim_Value");
	// "/input/grasp_ext/value"
	const FKey  HandInteraction_Left_Grasp_Value("HandInteraction_Left_Grasp_Value");
	// "/input/pinch_ext/ready_ext"
	const FKey  HandInteraction_Left_Pinch_Ready("HandInteraction_Left_Pinch_Ready");
	// "/input/aim_activate_ext/ready_ext"
	const FKey  HandInteraction_Left_Aim_Ready("HandInteraction_Left_Aim_Ready");
	// "/input/grasp_ext/ready_ext"
	const FKey  HandInteraction_Left_Grasp_Ready("HandInteraction_Left_Grasp_Ready");
	
	/// ---- Right hand only ----

	// "/input/pinch_ext/value"
	const FKey  HandInteraction_Right_Pinch_Value("HandInteraction_Right_Pinch_Value");
	// "/input/a/click"
	const FKey  HandInteraction_Right_Aim_Value("HandInteraction_Right_Aim_Value");
	// "/input/a/click"
	const FKey  HandInteraction_Right_Grasp_Value("HandInteraction_Right_Grasp_Value");
	// "/input/pinch_ext/ready_ext"
	const FKey  HandInteraction_Right_Pinch_Ready("HandInteraction_Right_Pinch_Ready");
	// "/input/aim_activate_ext/ready_ext"
	const FKey  HandInteraction_Right_Aim_Ready("HandInteraction_Right_Aim_Ready");
	// "/input/grasp_ext/ready_ext"
	const FKey  HandInteraction_Right_Grasp_Ready("HandInteraction_Right_Grasp_Ready");
}

namespace HandInteractionMotionSource
{
	const FName LeftPinch = TEXT("LeftPinchInteraction");
	const FName RightPinch = TEXT("RightPinchInteraction");
	const FName LeftAim = TEXT("LeftAimInteraction");
	const FName RightAim = TEXT("RightAimInteraction");
	const FName LeftGrip= TEXT("LeftGripInteraction");
	const FName RightGrip = TEXT("RightGripInteraction");
	const FName LeftPoke = TEXT("LeftPokeInteraction");
	const FName RightPoke = TEXT("RightPokeInteraction");
}

namespace HandInteractionRolePath
{
	const FString Left = FString("/user/hand/left");
	const FString Right = FString("/user/hand/right");
}

namespace HandInteractionActionPath
{
	const FString LeftPinchPose = FString("/user/hand/left/input/pinch_ext/pose");
	const FString LeftPinchValue = FString("/user/hand/left/input/pinch_ext/value");
	const FString LeftPinchReady = FString("/user/hand/left/input/pinch_ext/ready_ext");
	
	const FString LeftAimPose = FString("/user/hand/left/input/aim/pose");
	const FString LeftAimValue = FString("/user/hand/left/input/aim_activate_ext/value");
	const FString LeftAimReady = FString("/user/hand/left/input/aim_activate_ext/ready_ext");

	const FString LeftGripPose = FString("/user/hand/left/input/grip/pose");
	const FString LeftGraspValue = FString("/user/hand/left/input/grasp_ext/value");
	const FString LeftGraspReady = FString("/user/hand/left/input/grasp_ext/ready_ext");

	const FString LeftPokePose = FString("/user/hand/left/input/poke_ext/pose");
	
	const FString RightPinchPose = FString("/user/hand/right/input/pinch_ext/pose");
	const FString RightPinchValue = FString("/user/hand/right/input/pinch_ext/value");
	const FString RightPinchReady = FString("/user/hand/right/input/pinch_ext/ready_ext");

	const FString RightAimPose = FString("/user/hand/right/input/aim/pose");
	const FString RightAimValue = FString("/user/hand/right/input/aim_activate_ext/value");
	const FString RightAimReady = FString("/user/hand/right/input/aim_activate_ext/ready_ext");

	const FString RightGripPose = FString("/user/hand/right/input/grip/pose");
	const FString RightGraspValue = FString("/user/hand/right/input/grasp_ext/value");
	const FString RightGraspReady = FString("/user/hand/right/input/grasp_ext/ready_ext");

	const FString RightPokePose = FString("/user/hand/right/input/poke_ext/pose");
}

namespace HTCHandInteractionRolePath
{
	const FString Left = FString("/user/hand_htc/left");
	const FString Right = FString("/user/hand_htc/right");
}

namespace HTCHandInteractionActionPath
{
	const FString LeftAimPose = FString("/user/hand_htc/left/input/aim/pose");
	const FString LeftAimValue = FString("/user/hand_htc/left/input/select/value");

	const FString LeftGripPose = FString("/user/hand_htc/left/input/grip/pose");
	const FString LeftGraspValue = FString("/user/hand_htc/left/input/squeeze/value");

	const FString RightAimPose = FString("/user/hand_htc/right/input/aim/pose");
	const FString RightAimValue = FString("/user/hand_htc/right/input/select/value");

	const FString RightGripPose = FString("/user/hand_htc/right/input/grip/pose");
	const FString RightGraspValue = FString("/user/hand_htc/right/input/squeeze/value");
}

class FViveOpenXRHandInteraction : 
	public IInputDevice,
	public IOpenXRExtensionPlugin, 
	public FXRMotionControllerBase
{
public:
	struct FViveInteractionController
	{
		// Valid for top level user path:
		XrPath          RolePath;
		// Supported component paths:
		// Pose
		XrAction        PinchPoseAction;
		XrPath          PinchPoseActionPath;
		XrAction        AimPoseAction;
		XrPath          AimPoseActionPath;
		XrAction        GripPoseAction;
		XrPath          GripPoseActionPath;
		XrAction        PokePoseAction;
		XrPath          PokePoseActionPath;
		// Value
		XrAction        PinchAction;
		XrPath          PinchActionPath;
		XrAction        AimAction;
		XrPath          AimActionPath;
		XrAction        GraspAction;
		XrPath          GraspActionPath;
		// Boolean
		XrAction        PinchReadyAction;
		XrPath          PinchReadyActionPath;
		XrAction        AimReadyAction;
		XrPath          AimReadyActionPath;
		XrAction        GraspReadyAction;
		XrPath          GraspReadyActionPath;

		int32           PinchDeviceId;
		int32           AimDeviceId;
		int32           PokeDeviceId;
		int32           GripDeviceId;
		TArray<XrPath>  SubactionPaths;

		XrActionStateBoolean PinchReadyActionState;
		XrActionStateBoolean AimReadyActionState;
		XrActionStateBoolean GraspReadyActionState;

		XrActionStateFloat PinchValueActionState;
		XrActionStateFloat AimValueActionState;
		XrActionStateFloat GraspValueActionState;

		FKey PinchKey;
		FKey AimKey;
		FKey GraspKey;

		FKey PinchReadyKey;
		FKey AimReadyKey;
		FKey GraspReadyKey;

		TArray<TObjectPtr<const UInputAction>> PinchInputActions;
		TArray<TObjectPtr<const UInputAction>> AimInputActions;
		TArray<TObjectPtr<const UInputAction>> GraspInputActions;

		TArray<TObjectPtr<const UInputAction>> PinchReadyInputActions;
		TArray<TObjectPtr<const UInputAction>> AimReadyInputActions;
		TArray<TObjectPtr<const UInputAction>> GraspReadyInputActions;

		FViveInteractionController();

		void SetupPath(FString InRolePath, FString InPinchPoseActionPath, FString InAimPoseActionPath, FString InGripPoseActionPath, FString InPokePoseActionPath, FString InPinchActionPath, FString InAimActionPath, FString InGraspActionPath, FString InPinchReadyActionPath, FString InAimReadyActionPath, FString InGraspReadyActionPath);
		void HTCSetupPath(FString InRolePath, FString InAimPoseActionPath, FString InGripPoseActionPath, FString AimActionPath, FString GraspActionPath);

		
		int32 AddTrackedDevices(class FOpenXRHMD* HMD);
		void GetSuggestedBindings(TArray<XrActionSuggestedBinding>& OutSuggestedBindings);
		void AddAction(XrActionSet& InActionSet, XrAction& OutAction, FOpenXRPath InBindingPath, XrActionType InActionType);
		void AddActions(XrActionSet& InActionSet);
		FName GetActionName(FOpenXRPath ActionPath);
		void SyncActionStates(XrSession InSession);
		void CheckAndAddEnhancedInputAction(FEnhancedActionKeyMapping EnhancedActionKeyMapping);
	};

public:
	FViveOpenXRHandInteraction(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FViveOpenXRHandInteraction();

	virtual FString GetDisplayName() override
	{
		return FString(TEXT("ViveOpenXRHandInteraction"));
	}

	static inline FViveOpenXRHandInteraction* GetInstance() {
		return m_Instance;
	}

	virtual void AttachActionSets(TSet<XrActionSet>& OutActionSets) override;
	virtual void GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets) override;
	virtual void PostCreateInstance(XrInstance InInstance) override;
	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override;
	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override;
	virtual void OnDestroySession(XrSession InSession) override;
	virtual void PostSyncActions(XrSession InSession) override;
	//virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace) override;

	virtual bool GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions) override;
	//virtual bool GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics) override;	

	inline float GetWorldToMeterScale() { return WorldToMetersScale_; }

	bool m_EnableHandInteraction = false;
	bool m_UseHTCHandInteraction = false;

	FQuat GetAimRotation(bool isLeft)
	{
		FRotator rotation;
		FVector location;
		FName motionSource = HandInteractionMotionSource::LeftAim;
		if (!isLeft) motionSource = HandInteractionMotionSource::RightAim;
		GetControllerOrientationAndPosition(DeviceIndex, motionSource, rotation, location, WorldToMetersScale_);
		return rotation.Quaternion();
	}
	FVector GetAimPosition(bool isLeft)
	{
		FRotator rotation;
		FVector location;
		FName motionSource = HandInteractionMotionSource::LeftAim;
		if (!isLeft) motionSource = HandInteractionMotionSource::RightAim;
		GetControllerOrientationAndPosition(DeviceIndex, motionSource, rotation, location, WorldToMetersScale_);
		return location;
	}
	FQuat GetGriphRotation(bool isLeft)
	{
		FRotator rotation;
		FVector location;
		FName motionSource = HandInteractionMotionSource::LeftGrip;
		if (!isLeft) motionSource = HandInteractionMotionSource::RightGrip;
		GetControllerOrientationAndPosition(DeviceIndex, motionSource, rotation, location, WorldToMetersScale_);
		return rotation.Quaternion();
	}
	FVector GetGripPosition(bool isLeft)
	{
		FRotator rotation;
		FVector location;
		FName motionSource = HandInteractionMotionSource::LeftGrip;
		if (!isLeft) motionSource = HandInteractionMotionSource::RightGrip;
		GetControllerOrientationAndPosition(DeviceIndex, motionSource, rotation, location, WorldToMetersScale_);
		return location;
	}
	bool CheckAimDataValid(int HandSide)
	{
		bool isLeft = (HandSide == Side::LEFT);
		return GetAimActive(isLeft) || GetAimValid(isLeft) || GetAimTracked(isLeft);
	}
	bool CheckGripDataValid(int HandSide)
	{
		bool isLeft = (HandSide == Side::LEFT);
		return GetGripActive(isLeft) || GetGripValid(isLeft) || GetGripTracked(isLeft);
	}
	bool GetAimActive(bool isLeft)
	{
		return isLeft ? LeftAimStatePose.isActive > 0 : RightAimStatePose.isActive > 0;
	}
	bool GetGripActive(bool isLeft)
	{
		return isLeft ? LeftGripStatePose.isActive > 0 : RightGripStatePose.isActive > 0;
	}
	bool GetAimValid(bool isLeft)
	{
		if (!GetAimActive(isLeft)) { return false; }

		auto locationFlags = isLeft ? LeftAimSpaceLocation.locationFlags : RightAimSpaceLocation.locationFlags;
		return locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT &&
			locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
	}
	bool GetGripValid(bool isLeft)
	{
		if (!GetGripActive(isLeft)) { return false; }

		auto locationFlags = isLeft ? LeftGripSpaceLocation.locationFlags : RightGripSpaceLocation.locationFlags;
		return locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT &&
			locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
	}
	bool GetAimTracked(bool isLeft)
	{
		if (!GetAimActive(isLeft)) { return false; }

		auto locationFlags = isLeft ? LeftAimSpaceLocation.locationFlags : RightAimSpaceLocation.locationFlags;
		return locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT &&
			locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}
	bool GetGripTracked(bool isLeft)
	{
		if (!GetGripActive(isLeft)) { return false; }

		auto locationFlags = isLeft ? LeftGripSpaceLocation.locationFlags : RightGripSpaceLocation.locationFlags;
		return locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT &&
			locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	}

private:
	class IXRTrackingSystem* XRTrackingSystem = nullptr;
	XrActionsSyncInfo SyncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
	XrSpaceLocation RightAimSpaceLocation{ XR_TYPE_SPACE_LOCATION };
	XrSpaceLocation LeftAimSpaceLocation{ XR_TYPE_SPACE_LOCATION };
	XrSpaceLocation RightGripSpaceLocation{ XR_TYPE_SPACE_LOCATION };
	XrSpaceLocation LeftGripSpaceLocation{ XR_TYPE_SPACE_LOCATION };
	XrActionStatePose RightAimStatePose{ XR_TYPE_ACTION_STATE_POSE };
	XrActionStatePose LeftAimStatePose{ XR_TYPE_ACTION_STATE_POSE };
	XrActionStatePose RightGripStatePose{ XR_TYPE_ACTION_STATE_POSE };
	XrActionStatePose LeftGripStatePose{ XR_TYPE_ACTION_STATE_POSE };
	TArray<XrPath> SubactionPaths;
	TArray<XrAction>R_HandInteraction;
	TArray<XrAction>L_HandInteraction;
	TArray<XrSpace>AimSpace;
	TArray<XrSpace>GripSpace;

protected:
		static FViveOpenXRHandInteraction* m_Instance;
public:
	/************************************************************************/
	/* IMotionController                                                    */
	/************************************************************************/
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
	virtual bool GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
	virtual FName GetMotionControllerDeviceTypeName() const override;
	virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;
	virtual bool SetPlayerMappableInputConfig(TObjectPtr<class UPlayerMappableInputConfig> InputConfig);
	virtual bool AttachInputMappingContexts(const TSet<TObjectPtr<UInputMappingContext>>& MappingContexts) override;

	/************************************************************************/
	/* IInputDevice                                                         */
	/************************************************************************/
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) { return false; }
	virtual void SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
	{
		MessageHandler = InMessageHandler;
	}
	virtual void SendControllerEvents() {}
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) {}
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) {}
	virtual void Tick(float DeltaTime) {}

	FViveInteractionController LeftInteractionController;
	FViveInteractionController RightInteractionController;

private:
	/** The recipient of motion controller input events */
	TSharedPtr< FGenericApplicationMessageHandler > MessageHandler;
	TArray<TTuple<FName, XrActionStateFloat*>> KeyActionStates;

	TMap<TStrongObjectPtr<const UInputMappingContext>, uint32> InputMappingContextToPriorityMap;

	TArray<TTuple<FName, XrActionStateBoolean*>> KeyReadyActionStates;
	TStrongObjectPtr<UPlayerMappableInputConfig> MappableInputConfig;


	void SendInputEvent_Legacy();
	void SendInputEvent_EnhancedInput();

	bool bSessionStarted = false;
	bool bActionsAttached = false;

	XrInstance Instance = XR_NULL_HANDLE;
	XrActionSet HandInteractionActionSet = XR_NULL_HANDLE;

	class FOpenXRHMD* OpenXRHMD = nullptr;
	int32 DeviceIndex = 0;
	float WorldToMetersScale_ = 100;

};

class FViveOpenXRHandInteractionModule : public IViveOpenXRHandInteractionModule 
{
public:
	FViveOpenXRHandInteractionModule();
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);

	FViveOpenXRHandInteraction* GetHandInteraction();
private:
	TSharedPtr<FViveOpenXRHandInteraction> HandInteractionController;

};

