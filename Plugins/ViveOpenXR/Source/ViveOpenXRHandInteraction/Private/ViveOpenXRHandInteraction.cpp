// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRHandInteraction.h"
#include "IXRTrackingSystem.h"
#include "OpenXRCore.h"
#include "UObject/UObjectIterator.h"
#include "IOpenXRExtensionPlugin.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"

#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputLibrary.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableInputConfig.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "EnhancedInputEditorSubsystem.h"
#endif

#define LOCTEXT_NAMESPACE "FViveOpenXRHandInteraction"

DEFINE_LOG_CATEGORY(LogViveOpenXRHandInteraction);

FViveOpenXRHandInteraction* FViveOpenXRHandInteraction::m_Instance = nullptr;

FViveOpenXRHandInteraction::FViveInteractionController::FViveInteractionController()
	: RolePath(XR_NULL_PATH)
	, PinchPoseAction(XR_NULL_HANDLE)
	, PinchPoseActionPath(XR_NULL_PATH)
	, AimPoseAction(XR_NULL_HANDLE)
    , AimPoseActionPath(XR_NULL_PATH)
    , GripPoseAction(XR_NULL_HANDLE)
    , GripPoseActionPath(XR_NULL_PATH)
    , PokePoseAction(XR_NULL_HANDLE)
    , PokePoseActionPath(XR_NULL_PATH)
	, PinchAction(XR_NULL_HANDLE)
	, PinchActionPath(XR_NULL_PATH)
	, AimAction(XR_NULL_HANDLE)
	, AimActionPath(XR_NULL_PATH)
	, GraspAction(XR_NULL_HANDLE)
	, GraspActionPath(XR_NULL_PATH)
	, PinchReadyAction(XR_NULL_HANDLE)
	, PinchReadyActionPath(XR_NULL_PATH)
	, AimReadyAction(XR_NULL_HANDLE)
	, AimReadyActionPath(XR_NULL_PATH)
	, GraspReadyAction(XR_NULL_HANDLE)
	, GraspReadyActionPath(XR_NULL_PATH)
	, PinchDeviceId(-1)
	, AimDeviceId(-1)
	, GripDeviceId(-1)
	, PinchReadyActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, AimReadyActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, GraspReadyActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, PinchValueActionState({ XR_TYPE_ACTION_STATE_FLOAT })
	, AimValueActionState({ XR_TYPE_ACTION_STATE_FLOAT })
	, GraspValueActionState({ XR_TYPE_ACTION_STATE_FLOAT })
	, PinchKey(TEXT(""))
	, AimKey(TEXT(""))
	, GraspKey(TEXT(""))
{
	SubactionPaths.RemoveAll([](const int& num) {
		return true;
		});
	PinchInputActions.Empty();
	AimInputActions.Empty();
	GraspInputActions.Empty();
	PinchReadyInputActions.Empty();
	AimReadyInputActions.Empty();
	GraspReadyInputActions.Empty();
}

void FViveOpenXRHandInteraction::FViveInteractionController::SetupPath(FString InRolePath, FString InPinchPoseActionPath, FString InAimPoseActionPath, FString InGripPoseActionPath, FString InPokePoseActionPath, FString InPinchActionPath, FString InAimActionPath, FString InGraspActionPath, FString InPinchReadyActionPath, FString InAimReadyActionPath, FString InGraspReadyActionPath)
{
	RolePath = FOpenXRPath(InRolePath);
	PinchPoseActionPath = FOpenXRPath(InPinchPoseActionPath);
	AimPoseActionPath = FOpenXRPath(InAimPoseActionPath);
	GripPoseActionPath = FOpenXRPath(InGripPoseActionPath);
	PokePoseActionPath = FOpenXRPath(InPokePoseActionPath);
	PinchActionPath = FOpenXRPath(InPinchActionPath);
	AimActionPath = FOpenXRPath(InAimActionPath);
	GraspActionPath = FOpenXRPath(InGraspActionPath);
	PinchReadyActionPath = FOpenXRPath(InPinchReadyActionPath);
	AimReadyActionPath = FOpenXRPath(InAimReadyActionPath);
	GraspReadyActionPath = FOpenXRPath(InGraspReadyActionPath);
	SubactionPaths.Reset();
	SubactionPaths.Emplace(RolePath);
}

void FViveOpenXRHandInteraction::FViveInteractionController::HTCSetupPath(FString InRolePath, FString InAimPoseActionPath, FString InGripPoseActionPath, FString InAimActionPath, FString InGraspActionPath)
{
	RolePath = FOpenXRPath(InRolePath);
	AimPoseActionPath = FOpenXRPath(InAimPoseActionPath);
	GripPoseActionPath = FOpenXRPath(InGripPoseActionPath);
	PinchActionPath = FOpenXRPath(InAimActionPath);
	GraspActionPath = FOpenXRPath(InGraspActionPath);
	SubactionPaths.Reset();
	SubactionPaths.Emplace(RolePath);
}

int32 FViveOpenXRHandInteraction::FViveInteractionController::AddTrackedDevices(FOpenXRHMD* HMD)
{
	if (HMD)
	{
		AimDeviceId = HMD->AddTrackedDevice(AimPoseAction, AimPoseActionPath);
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("AddTrackedDevices ID: %d"), AimDeviceId);
		GripDeviceId = HMD->AddTrackedDevice(GripPoseAction, GripPoseActionPath);
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("AddTrackedDevices ID: %d"), GripDeviceId);
		if(!m_Instance->m_UseHTCHandInteraction)
		{
			PinchDeviceId = HMD->AddTrackedDevice(PinchPoseAction, PinchPoseActionPath);
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("AddTrackedDevices ID: %d"), PinchDeviceId);
			PokeDeviceId = HMD->AddTrackedDevice(PokePoseAction, PokePoseActionPath);
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("AddTrackedDevices ID: %d"), PokeDeviceId);
		}
	}
	return AimDeviceId;
}

void FViveOpenXRHandInteraction::FViveInteractionController::GetSuggestedBindings(TArray<XrActionSuggestedBinding>& OutSuggestedBindings)
{
	OutSuggestedBindings.Add(XrActionSuggestedBinding{ AimPoseAction, AimPoseActionPath });
	OutSuggestedBindings.Add(XrActionSuggestedBinding{ GripPoseAction, GripPoseActionPath });
	OutSuggestedBindings.Add(XrActionSuggestedBinding{ PinchAction, PinchActionPath });
	OutSuggestedBindings.Add(XrActionSuggestedBinding{ GraspAction, GraspActionPath });
	if (!m_Instance->m_UseHTCHandInteraction)
	{
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ AimAction, AimActionPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ PinchPoseAction, PinchPoseActionPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ PokePoseAction, PokePoseActionPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ PinchReadyAction, PinchReadyActionPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ AimReadyAction, AimReadyActionPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ GraspReadyAction, GraspReadyActionPath });
	}
}

void FViveOpenXRHandInteraction::FViveInteractionController::AddAction(XrActionSet& InActionSet, XrAction& OutAction, FOpenXRPath InBindingPath, XrActionType InActionType)//, const TArray<XrPath>& InSubactionPaths)
{
	check(InActionSet != XR_NULL_HANDLE);
	if (OutAction != XR_NULL_HANDLE) {
		xrDestroyAction(OutAction);
		OutAction = XR_NULL_HANDLE;
	}

	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("xrCreateAction %s"), *InBindingPath.ToString());

	char ActionName[NAME_SIZE];
	GetActionName(InBindingPath).GetPlainANSIString(ActionName);

	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	Info.actionType = InActionType;
	Info.countSubactionPaths = SubactionPaths.Num();
	Info.subactionPaths = SubactionPaths.GetData();
	FCStringAnsi::Strcpy(Info.actionName, XR_MAX_ACTION_NAME_SIZE, ActionName);
	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, ActionName);
	XR_ENSURE(xrCreateAction(InActionSet, &Info, &OutAction));

}

void FViveOpenXRHandInteraction::FViveInteractionController::AddActions(XrActionSet& InActionSet)
{
	
	AddAction(InActionSet, AimPoseAction, AimPoseActionPath, XR_ACTION_TYPE_POSE_INPUT);
	AddAction(InActionSet, GripPoseAction, GripPoseActionPath, XR_ACTION_TYPE_POSE_INPUT);
	AddAction(InActionSet, PinchAction, PinchActionPath, XR_ACTION_TYPE_FLOAT_INPUT);
	AddAction(InActionSet, GraspAction, GraspActionPath, XR_ACTION_TYPE_FLOAT_INPUT);
	if (!m_Instance->m_UseHTCHandInteraction)
	{
		AddAction(InActionSet, PinchPoseAction, PinchPoseActionPath, XR_ACTION_TYPE_POSE_INPUT);
		AddAction(InActionSet, PokePoseAction, PokePoseActionPath, XR_ACTION_TYPE_POSE_INPUT);
		AddAction(InActionSet, AimAction, AimActionPath, XR_ACTION_TYPE_FLOAT_INPUT);
		AddAction(InActionSet, PinchReadyAction, PinchReadyActionPath, XR_ACTION_TYPE_BOOLEAN_INPUT);
		AddAction(InActionSet, AimReadyAction, AimReadyActionPath, XR_ACTION_TYPE_BOOLEAN_INPUT);
		AddAction(InActionSet, GraspReadyAction, GraspReadyActionPath, XR_ACTION_TYPE_BOOLEAN_INPUT);
	}
}

FName FViveOpenXRHandInteraction::FViveInteractionController::GetActionName(FOpenXRPath ActionPath)
{
	TArray<FString> Tokens;
	ActionPath.ToString().ParseIntoArray(Tokens, TEXT("/"));
	Tokens[4].RemoveFromEnd("_htc");
	FString ActionNameString = Tokens[2] + "_" + Tokens[4] + "_" + Tokens[5];
	return FName(ActionNameString);
}

void FViveOpenXRHandInteraction::FViveInteractionController::SyncActionStates(XrSession InSession)
{
	if (!m_Instance->m_UseHTCHandInteraction)
	{
		XrActionStateGetInfo AimActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		AimActionStateInfo.action = AimAction;
		XR_ENSURE(xrGetActionStateFloat(InSession, &AimActionStateInfo, &AimValueActionState));
		
		XrActionStateGetInfo PinchReadyActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		PinchReadyActionStateInfo.action = PinchReadyAction;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &PinchReadyActionStateInfo, &PinchReadyActionState));
		
		XrActionStateGetInfo AimReadyActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		AimReadyActionStateInfo.action = AimReadyAction;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &AimReadyActionStateInfo, &AimReadyActionState));
		
		XrActionStateGetInfo GraspReadyActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		GraspReadyActionStateInfo.action = GraspReadyAction;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &GraspReadyActionStateInfo, &GraspReadyActionState));
	}

	XrActionStateGetInfo PinchActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	PinchActionStateInfo.action = PinchAction;
	XR_ENSURE(xrGetActionStateFloat(InSession, &PinchActionStateInfo, &PinchValueActionState));

	XrActionStateGetInfo GraspActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	GraspActionStateInfo.action = GraspAction;
	XR_ENSURE(xrGetActionStateFloat(InSession, &GraspActionStateInfo, &GraspValueActionState));
}

void FViveOpenXRHandInteraction::FViveInteractionController::CheckAndAddEnhancedInputAction(FEnhancedActionKeyMapping EnhancedActionKeyMapping)
{
	if (!m_Instance->m_UseHTCHandInteraction)
	{
		if (EnhancedActionKeyMapping.Key == AimKey)
		{
			AimInputActions.Emplace(EnhancedActionKeyMapping.Action);
		}
		if (EnhancedActionKeyMapping.Key == PinchReadyKey)
		{
			PinchReadyInputActions.Emplace(EnhancedActionKeyMapping.Action);
		}
		if (EnhancedActionKeyMapping.Key == AimReadyKey)
		{
			AimReadyInputActions.Emplace(EnhancedActionKeyMapping.Action);
		}
		if (EnhancedActionKeyMapping.Key == GraspReadyKey)
		{
			GraspReadyInputActions.Emplace(EnhancedActionKeyMapping.Action);
		}
	}

	if (EnhancedActionKeyMapping.Key == PinchKey)
	{
		PinchInputActions.Emplace(EnhancedActionKeyMapping.Action);
	}
	if (EnhancedActionKeyMapping.Key == GraspKey)
	{
		GraspInputActions.Emplace(EnhancedActionKeyMapping.Action);
	}
}

FViveOpenXRHandInteraction::FViveOpenXRHandInteraction(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
{
	IModularFeatures::Get().RegisterModularFeature(IMotionController::GetModularFeatureName(), static_cast<IMotionController*>(this));
	RegisterOpenXRExtensionModularFeature();

	m_Instance = this;
	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("FViveOpenXRHandInteraction() register extension feature HandInteraction %p."), m_Instance);
}

FViveOpenXRHandInteraction::~FViveOpenXRHandInteraction()
{
	IModularFeatures::Get().UnregisterModularFeature(IMotionController::GetModularFeatureName(), static_cast<IMotionController*>(this));
	UnregisterOpenXRExtensionModularFeature();
	bActionsAttached = false;
}

bool FViveOpenXRHandInteraction::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (m_EnableHandInteraction)
	{
		if (m_UseHTCHandInteraction)
		{
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("GetRequiredExtensions() XR_HTC_hand_interaction."));
			OutExtensions.Add("XR_HTC_hand_interaction");
		}
		else
		{
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("GetRequiredExtensions() XR_EXT_hand_interaction."));
			OutExtensions.Add("XR_EXT_hand_interaction");
		}
	}

	return true;
}

void FViveOpenXRHandInteraction::AttachActionSets(TSet<XrActionSet>& OutActionSets)
{
	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("AttachActionSets() Entry."));
	if (!m_EnableHandInteraction) return;

	check(Instance != XR_NULL_HANDLE);
	LeftInteractionController.AddTrackedDevices(OpenXRHMD);
	RightInteractionController.AddTrackedDevices(OpenXRHMD);
	OutActionSets.Add(HandInteractionActionSet);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// For enhanced input
	const UEnhancedInputDeveloperSettings* InputSettings = GetDefault<UEnhancedInputDeveloperSettings>();
	if (InputSettings)
	{
		for (const auto& Context : InputSettings->DefaultMappingContexts)
		{
			if (Context.InputMappingContext)
			{
				TStrongObjectPtr<const UInputMappingContext> Obj(Context.InputMappingContext.LoadSynchronous());
				InputMappingContextToPriorityMap.Add(Obj, Context.Priority);
			}
			else
			{
				UE_LOG(LogHMD, Warning, TEXT("Default Mapping Contexts contains an Input Mapping Context set to \"None\", ignoring while building OpenXR actions."));
			}
		}
	}

	if (!InputMappingContextToPriorityMap.IsEmpty())
	{
		for (const auto& MappingContext : InputMappingContextToPriorityMap)
		{
			for (const FEnhancedActionKeyMapping& Mapping : MappingContext.Key->GetMappings())
			{
				LeftInteractionController.CheckAndAddEnhancedInputAction(Mapping);
				RightInteractionController.CheckAndAddEnhancedInputAction(Mapping);
			}
		}
	}
	else
	{
		// Setup Key to ActionState map
		if (!m_UseHTCHandInteraction)
		{
			KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Aim_Value.GetFName(), &LeftInteractionController.AimValueActionState);
			KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Aim_Value.GetFName(), &RightInteractionController.AimValueActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Pinch_Ready.GetFName(), &LeftInteractionController.PinchReadyActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Pinch_Ready.GetFName(), &RightInteractionController.PinchReadyActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Aim_Ready.GetFName(), &LeftInteractionController.AimReadyActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Aim_Ready.GetFName(), &RightInteractionController.AimReadyActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Grasp_Ready.GetFName(), &LeftInteractionController.GraspReadyActionState);
			KeyReadyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Grasp_Ready.GetFName(), &RightInteractionController.GraspReadyActionState);

		}
		KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Pinch_Value.GetFName(), &LeftInteractionController.PinchValueActionState);
		KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Pinch_Value.GetFName(), &RightInteractionController.PinchValueActionState);
		KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Left_Grasp_Value.GetFName(), &LeftInteractionController.GraspValueActionState);
		KeyActionStates.Emplace(HandInteractionKeys::HandInteraction_Right_Grasp_Value.GetFName(), &RightInteractionController.GraspValueActionState);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bActionsAttached = true;
}

void FViveOpenXRHandInteraction::GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets)
{
	if (!m_EnableHandInteraction) return;

	check(HandInteractionActionSet != XR_NULL_HANDLE);
	OutActiveSets.Add(XrActiveActionSet{ HandInteractionActionSet, XR_NULL_PATH });
}

void FViveOpenXRHandInteraction::PostCreateInstance(XrInstance InInstance)
{
	Instance = InInstance;
}

const void* FViveOpenXRHandInteraction::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	if (m_EnableHandInteraction)
	{
		Instance = InInstance;

		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			OpenXRHMD = (FOpenXRHMD*)GEngine->XRSystem.Get();
		}

		bSessionStarted = true;

		// Setup FKey
		if (!m_UseHTCHandInteraction)
		{
			LeftInteractionController.AimKey = HandInteractionKeys::HandInteraction_Left_Aim_Value;
			RightInteractionController.AimKey = HandInteractionKeys::HandInteraction_Right_Aim_Value;
			LeftInteractionController.PinchReadyKey = HandInteractionKeys::HandInteraction_Left_Pinch_Ready;
			RightInteractionController.PinchReadyKey = HandInteractionKeys::HandInteraction_Right_Pinch_Ready;
			LeftInteractionController.AimReadyKey = HandInteractionKeys::HandInteraction_Left_Aim_Ready;
			RightInteractionController.AimReadyKey = HandInteractionKeys::HandInteraction_Right_Aim_Ready;
			LeftInteractionController.GraspReadyKey = HandInteractionKeys::HandInteraction_Left_Grasp_Ready;
			RightInteractionController.GraspReadyKey = HandInteractionKeys::HandInteraction_Right_Grasp_Ready;
		}
		LeftInteractionController.PinchKey = HandInteractionKeys::HandInteraction_Left_Pinch_Value;
		RightInteractionController.PinchKey = HandInteractionKeys::HandInteraction_Right_Pinch_Value;
		LeftInteractionController.GraspKey = HandInteractionKeys::HandInteraction_Left_Grasp_Value;
		RightInteractionController.GraspKey = HandInteractionKeys::HandInteraction_Right_Grasp_Value;

		// Setup XrPath (include SubActionPaths)
		if (m_UseHTCHandInteraction)
		{
			LeftInteractionController.HTCSetupPath(HTCHandInteractionRolePath::Left, HTCHandInteractionActionPath::LeftAimPose, HTCHandInteractionActionPath::LeftGripPose, HTCHandInteractionActionPath::LeftAimValue, HTCHandInteractionActionPath::LeftGraspValue);
			RightInteractionController.HTCSetupPath(HTCHandInteractionRolePath::Right, HTCHandInteractionActionPath::RightAimPose, HTCHandInteractionActionPath::RightGripPose, HTCHandInteractionActionPath::RightAimValue, HTCHandInteractionActionPath::RightGraspValue);
		}
		else
		{
			LeftInteractionController.SetupPath(HandInteractionRolePath::Left, HandInteractionActionPath::LeftPinchPose, HandInteractionActionPath::LeftAimPose, HandInteractionActionPath::LeftGripPose, HandInteractionActionPath::LeftPokePose, HandInteractionActionPath::LeftPinchValue, HandInteractionActionPath::LeftAimValue, HandInteractionActionPath::LeftGraspValue, HandInteractionActionPath::LeftPinchReady, HandInteractionActionPath::LeftAimReady, HandInteractionActionPath::LeftGraspReady);
			RightInteractionController.SetupPath(HandInteractionRolePath::Right, HandInteractionActionPath::RightPinchPose, HandInteractionActionPath::RightAimPose, HandInteractionActionPath::RightGripPose, HandInteractionActionPath::RightPokePose, HandInteractionActionPath::RightPinchValue, HandInteractionActionPath::RightAimValue, HandInteractionActionPath::RightGraspValue, HandInteractionActionPath::RightPinchReady, HandInteractionActionPath::RightAimReady, HandInteractionActionPath::RightGraspReady);
		}

		//	Create ActionSet
		if (HandInteractionActionSet != XR_NULL_HANDLE)
		{
			xrDestroyActionSet(HandInteractionActionSet);
			HandInteractionActionSet = XR_NULL_HANDLE;
		}
		{
			XrActionSetCreateInfo Info;
			Info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
			Info.next = nullptr;
			FCStringAnsi::Strcpy(Info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "viveopenxrhandinteractionactionset");
			FCStringAnsi::Strcpy(Info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "VIVE OpenXR Hand Interaction Action Set");
			Info.priority = 0;
			XR_ENSURE(xrCreateActionSet(Instance, &Info, &HandInteractionActionSet));
		}

		// Create Action
		LeftInteractionController.AddActions(HandInteractionActionSet);
		RightInteractionController.AddActions(HandInteractionActionSet);

		// Create suggested bindings
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("xrSuggestInteractionProfileBindings()"));
		TArray<XrActionSuggestedBinding> Bindings;
		LeftInteractionController.GetSuggestedBindings(Bindings);
		RightInteractionController.GetSuggestedBindings(Bindings);
		XrInteractionProfileSuggestedBinding InteractionProfileSuggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		if (m_UseHTCHandInteraction)
			InteractionProfileSuggestedBindings.interactionProfile = FOpenXRPath("/interaction_profiles/htc/hand_interaction");
		else
			InteractionProfileSuggestedBindings.interactionProfile = FOpenXRPath("/interaction_profiles/ext/hand_interaction_ext");
		
		InteractionProfileSuggestedBindings.suggestedBindings = Bindings.GetData();
		InteractionProfileSuggestedBindings.countSuggestedBindings = Bindings.Num();
		XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &InteractionProfileSuggestedBindings));
	}

	return InNext;
}

void FViveOpenXRHandInteraction::OnDestroySession(XrSession InSession)
{
	if (bActionsAttached)
	{
		// If the session shut down, clean up.
		bActionsAttached = false;
		bSessionStarted = false;
		KeyActionStates.Reset();
		KeyReadyActionStates.Reset();
	}
}

const void* FViveOpenXRHandInteraction::OnBeginSession(XrSession InSession, const void* InNext)
{
	if (m_EnableHandInteraction)
	{
		bSessionStarted = true;
	}

	return InNext;
}

void FViveOpenXRHandInteraction::PostSyncActions(XrSession InSession)
{
	if (!m_EnableHandInteraction) return;

	WorldToMetersScale_ = OpenXRHMD->GetWorldToMetersScale();

	LeftInteractionController.SyncActionStates(InSession);
	RightInteractionController.SyncActionStates(InSession);

	SendInputEvent_Legacy();
	SendInputEvent_EnhancedInput();
}

#pragma region
bool FViveOpenXRHandInteraction::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	if (!bActionsAttached || OpenXRHMD == nullptr)
	{
		return false;
	}

	if (ControllerIndex == DeviceIndex)
	{	
		if (!m_UseHTCHandInteraction)
		{
			if (MotionSource == HandInteractionMotionSource::LeftPinch || MotionSource == HandInteractionMotionSource::RightPinch)
			{
				int32 DeviceId = LeftInteractionController.PinchDeviceId;
				if (MotionSource == HandInteractionMotionSource::RightPinch) DeviceId = RightInteractionController.PinchDeviceId;

				FQuat Orientation;
				bool Success = OpenXRHMD->GetCurrentPose(DeviceId, Orientation, OutPosition);
				OutOrientation = FRotator(Orientation);
				return Success;
			}

			if (MotionSource == HandInteractionMotionSource::LeftPoke || MotionSource == HandInteractionMotionSource::RightPoke)
			{
				int32 DeviceId = LeftInteractionController.PokeDeviceId;
				if (MotionSource == HandInteractionMotionSource::RightPoke) DeviceId = RightInteractionController.PokeDeviceId;

				FQuat Orientation;
				bool Success = OpenXRHMD->GetCurrentPose(DeviceId, Orientation, OutPosition);
				OutOrientation = FRotator(Orientation);
				return Success;
			}
		}
		if (MotionSource == HandInteractionMotionSource::LeftAim || MotionSource == HandInteractionMotionSource::RightAim)
		{
			int32 AimDeviceId = LeftInteractionController.AimDeviceId;
			if (MotionSource == HandInteractionMotionSource::RightAim) 
				AimDeviceId = RightInteractionController.AimDeviceId;

			FQuat Orientation;
			bool Success = OpenXRHMD->GetCurrentPose(AimDeviceId, Orientation, OutPosition);

			OutOrientation = FRotator(Orientation);
			return Success;
		}
		if (MotionSource == HandInteractionMotionSource::LeftGrip || MotionSource == HandInteractionMotionSource::RightGrip)
		{
			int32 GripDeviceId = LeftInteractionController.GripDeviceId;
			if (MotionSource == HandInteractionMotionSource::RightGrip) 
				GripDeviceId = RightInteractionController.GripDeviceId;

			FQuat Orientation;
			bool Success = OpenXRHMD->GetCurrentPose(GripDeviceId, Orientation, OutPosition);
			
			OutOrientation = FRotator(Orientation);
			return Success;
		}
	}
	return false;
}

bool FViveOpenXRHandInteraction::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
	// FTimespan initializes to 0 and GetControllerOrientationAndPositionForTime with time 0 will return the latest data.
	FTimespan Time;
	bool OutTimeWasUsed = false;
	return GetControllerOrientationAndPositionForTime(ControllerIndex, MotionSource, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityAsAxisAndLength, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);

}

bool FViveOpenXRHandInteraction::GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
	if (!bActionsAttached || OpenXRHMD == nullptr)
	{
		return false;
	}

	if (ControllerIndex == DeviceIndex)
	{
		if (!m_UseHTCHandInteraction)
		{
			if (MotionSource == HandInteractionMotionSource::LeftPinch || MotionSource == HandInteractionMotionSource::RightPinch)
			{
				int32 DeviceId = LeftInteractionController.PinchDeviceId;
				if (MotionSource == HandInteractionMotionSource::RightPinch) DeviceId = RightInteractionController.PinchDeviceId;

				FQuat Orientation;
				bool Success = OpenXRHMD->GetPoseForTime(DeviceId, Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
				OutOrientation = FRotator(Orientation);
				return Success;
			}
			if (MotionSource == HandInteractionMotionSource::LeftPoke || MotionSource == HandInteractionMotionSource::RightPoke)
			{
				int32 DeviceId = LeftInteractionController.PokeDeviceId;
				if (MotionSource == HandInteractionMotionSource::RightPoke) DeviceId = RightInteractionController.PokeDeviceId;

				FQuat Orientation;
				bool Success = OpenXRHMD->GetPoseForTime(DeviceId, Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
				OutOrientation = FRotator(Orientation);
				return Success;
			}
		}
		if (MotionSource == HandInteractionMotionSource::LeftAim || MotionSource == HandInteractionMotionSource::RightAim)
		{
			int32 DeviceId = LeftInteractionController.AimDeviceId;
			if (MotionSource == HandInteractionMotionSource::RightAim)
				DeviceId = RightInteractionController.AimDeviceId;

			FQuat Orientation;
			bool Success = OpenXRHMD->GetPoseForTime(DeviceId, Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
			OutOrientation = FRotator(Orientation);
			return Success;
		}
		if (MotionSource == HandInteractionMotionSource::LeftGrip || MotionSource == HandInteractionMotionSource::RightGrip)
		{
			int32 DeviceId = LeftInteractionController.GripDeviceId;
			if (MotionSource == HandInteractionMotionSource::RightGrip) 
				DeviceId = RightInteractionController.GripDeviceId;

			FQuat Orientation;
			bool Success = OpenXRHMD->GetPoseForTime(DeviceId, Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
			OutOrientation = FRotator(Orientation);
			return Success;
		}
	}
	return false;
}

ETrackingStatus FViveOpenXRHandInteraction::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
{
	if (!bActionsAttached || OpenXRHMD == nullptr)
	{
		return ETrackingStatus::NotTracked;
	}

	XrSession Session = OpenXRHMD->GetSession();
	if (Session == XR_NULL_HANDLE)
	{
		return ETrackingStatus::NotTracked;
	}

	XrActionStateGetInfo PoseActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };

	if (!m_UseHTCHandInteraction)
	{
		if (MotionSource == HandInteractionMotionSource::LeftPinch)
		{
			// left entity pose
			PoseActionStateInfo.action = LeftInteractionController.PinchPoseAction;
		}
		else if (MotionSource == HandInteractionMotionSource::RightPinch)
		{
			// right entity pose
			PoseActionStateInfo.action = RightInteractionController.PinchPoseAction;
		}
		else if (MotionSource == HandInteractionMotionSource::LeftPoke)
		{
			// right entity pose
			PoseActionStateInfo.action = LeftInteractionController.PokePoseAction;
		}
		else if (MotionSource == HandInteractionMotionSource::RightPoke)
		{
			// right entity pose
			PoseActionStateInfo.action = RightInteractionController.PokePoseAction;
		}
	}

	if (MotionSource == HandInteractionMotionSource::LeftAim)
	{
		PoseActionStateInfo.action = LeftInteractionController.AimPoseAction;
	}
	else if (MotionSource == HandInteractionMotionSource::RightAim)
	{
		PoseActionStateInfo.action = RightInteractionController.AimPoseAction;
	}
	else if (MotionSource == HandInteractionMotionSource::LeftGrip)
	{
		PoseActionStateInfo.action = LeftInteractionController.GripPoseAction;
	}
	else if (MotionSource == HandInteractionMotionSource::RightGrip)
	{
		PoseActionStateInfo.action = RightInteractionController.GripPoseAction;
	}
	else
	{
		return ETrackingStatus::NotTracked;
	}
	PoseActionStateInfo.subactionPath = XR_NULL_PATH;

	XrActionStatePose State = { XR_TYPE_ACTION_STATE_POSE };
	if (!XR_ENSURE(xrGetActionStatePose(Session, &PoseActionStateInfo, &State)))
	{
		return ETrackingStatus::NotTracked;
	}

	return State.isActive ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
}

FName FViveOpenXRHandInteraction::GetMotionControllerDeviceTypeName() const
{
	const static FName DefaultName(TEXT("OpenXRViveHandInteraction"));
	return DefaultName;
}

void FViveOpenXRHandInteraction::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
	check(IsInGameThread());


	SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::LeftAim));
	SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::RightAim));
	SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::LeftGrip));
	SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::RightGrip));
	if (!m_UseHTCHandInteraction)
	{
		SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::LeftPinch));
		SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::RightPinch));
		SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::LeftPoke));
		SourcesOut.Add(FMotionControllerSource(HandInteractionMotionSource::RightPoke));
	}
}
	

PRAGMA_DISABLE_DEPRECATION_WARNINGS
bool FViveOpenXRHandInteraction::SetPlayerMappableInputConfig(TObjectPtr<class UPlayerMappableInputConfig> InputConfig)
{
	if (bActionsAttached)
	{
		UE_LOG(LogHMD, Error, TEXT("Attempted to attach an input config while one is already attached for the current session."));

		return false;
	}

	TSet<TObjectPtr<UInputMappingContext>> MappingContexts;
	InputConfig->GetMappingContexts().GetKeys(MappingContexts);
	return AttachInputMappingContexts(MappingContexts);
}

bool FViveOpenXRHandInteraction::AttachInputMappingContexts(const TSet<TObjectPtr<UInputMappingContext>>& MappingContexts)
{
	if (bActionsAttached)
	{
		UE_LOG(LogHMD, Error, TEXT("Attempted to attach input mapping contexts when action sets are already attached for the current session."));

		return false;
	}

	for (const auto& Context : MappingContexts)
	{
		InputMappingContextToPriorityMap.Add(TStrongObjectPtr<UInputMappingContext>(Context), 0);
	}
	return true;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FViveOpenXRHandInteraction::SendInputEvent_Legacy()
{
	if (!m_EnableHandInteraction) return;

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	for (auto& KeyActionState : KeyActionStates)
	{
		XrActionStateFloat state = *KeyActionState.Value;
		if (state.changedSinceLastSync)
		{
			FName keyName = KeyActionState.Key;
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("SendControllerEvents() %s = %d."), *keyName.ToString(), (uint8_t)state.currentState);
			if (state.isActive && state.currentState)
			{
				MessageHandler->OnControllerButtonPressed(keyName, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
			}
			else
			{
				MessageHandler->OnControllerButtonReleased(keyName, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
			}
		}
	}

	for (auto& KeyReadyActionState : KeyReadyActionStates)
	{
		XrActionStateBoolean state = *KeyReadyActionState.Value;
		if (state.changedSinceLastSync)
		{
			FName keyName = KeyReadyActionState.Key;
			UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("SendControllerEvents() %s = %d."), *keyName.ToString(), (uint8_t)state.currentState);
			if (state.isActive && state.currentState)
			{
				MessageHandler->OnControllerButtonPressed(keyName, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
			}
			else
			{
				MessageHandler->OnControllerButtonReleased(keyName, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
			}
		}
	}
}

void FViveOpenXRHandInteraction::SendInputEvent_EnhancedInput()
{
	if (!m_EnableHandInteraction) return;

	auto InjectEnhancedFloatInput = [](XrActionStateFloat& State, TArray<TObjectPtr<const UInputAction>>& Actions)
	{
		FInputActionValue InputValue;
		TArray<TObjectPtr<UInputTrigger>> Triggers = {};
		TArray<TObjectPtr<UInputModifier>> Modifiers = {};

		InputValue = FInputActionValue(State.isActive ? State.currentState : false);

		for (auto InputAction : Actions)
		{
			auto InjectSubsystemInput = [InputAction, InputValue, Triggers, Modifiers](IEnhancedInputSubsystemInterface* Subsystem)
			{
				if (Subsystem)
				{
					Subsystem->InjectInputForAction(InputAction, InputValue, Modifiers, Triggers);
				}
			};
			IEnhancedInputModule::Get().GetLibrary()->ForEachSubsystem(InjectSubsystemInput);

#if WITH_EDITOR
			if (GEditor)
			{
				// UEnhancedInputLibrary::ForEachSubsystem only enumerates runtime subsystems.
				InjectSubsystemInput(GEditor->GetEditorSubsystem<UEnhancedInputEditorSubsystem>());
			}
#endif
		}

	};
	auto InjectEnhancedBooleanInput = [](XrActionStateBoolean& State, TArray<TObjectPtr<const UInputAction>>& Actions)
	{
		FInputActionValue InputValue;
		TArray<TObjectPtr<UInputTrigger>> Triggers = {};
		TArray<TObjectPtr<UInputModifier>> Modifiers = {};

		InputValue = FInputActionValue(State.isActive ? (bool)State.currentState : false);

		for (auto InputAction : Actions)
		{
			auto InjectSubsystemInput = [InputAction, InputValue, Triggers, Modifiers](IEnhancedInputSubsystemInterface* Subsystem)
				{
					if (Subsystem)
					{
						Subsystem->InjectInputForAction(InputAction, InputValue, Modifiers, Triggers);
					}
				};
			IEnhancedInputModule::Get().GetLibrary()->ForEachSubsystem(InjectSubsystemInput);

#if WITH_EDITOR
			if (GEditor)
			{
				// UEnhancedInputLibrary::ForEachSubsystem only enumerates runtime subsystems.
				InjectSubsystemInput(GEditor->GetEditorSubsystem<UEnhancedInputEditorSubsystem>());
			}
#endif
		}

	};

	if (m_UseHTCHandInteraction)
	{
		InjectEnhancedFloatInput(LeftInteractionController.PinchValueActionState, LeftInteractionController.PinchInputActions);
		InjectEnhancedFloatInput(RightInteractionController.PinchValueActionState, RightInteractionController.PinchInputActions);
		InjectEnhancedFloatInput(LeftInteractionController.GraspValueActionState, LeftInteractionController.GraspInputActions);
		InjectEnhancedFloatInput(RightInteractionController.GraspValueActionState, RightInteractionController.GraspInputActions);
	}
	else
	{
		InjectEnhancedFloatInput(LeftInteractionController.AimValueActionState, LeftInteractionController.AimInputActions);
		InjectEnhancedFloatInput(RightInteractionController.AimValueActionState, RightInteractionController.AimInputActions);
		InjectEnhancedBooleanInput(LeftInteractionController.PinchReadyActionState, LeftInteractionController.PinchReadyInputActions);
		InjectEnhancedBooleanInput(RightInteractionController.PinchReadyActionState, RightInteractionController.PinchReadyInputActions);
		InjectEnhancedBooleanInput(LeftInteractionController.AimReadyActionState, LeftInteractionController.AimReadyInputActions);
		InjectEnhancedBooleanInput(RightInteractionController.AimReadyActionState, RightInteractionController.AimReadyInputActions);
		InjectEnhancedBooleanInput(LeftInteractionController.GraspReadyActionState, LeftInteractionController.GraspReadyInputActions);
		InjectEnhancedBooleanInput(RightInteractionController.GraspReadyActionState, RightInteractionController.GraspReadyInputActions);
		InjectEnhancedFloatInput(LeftInteractionController.PinchValueActionState, LeftInteractionController.PinchInputActions);
		InjectEnhancedFloatInput(RightInteractionController.PinchValueActionState, RightInteractionController.PinchInputActions);
		InjectEnhancedFloatInput(LeftInteractionController.GraspValueActionState, LeftInteractionController.GraspInputActions);
		InjectEnhancedFloatInput(RightInteractionController.GraspValueActionState, RightInteractionController.GraspInputActions);
	}
}
#pragma endregion IInputDevice overrides

FViveOpenXRHandInteractionModule::FViveOpenXRHandInteractionModule()
{
}

FName IViveOpenXRHandInteractionModule::ViveOpenXRHandInteractionModularKeyName = FName(TEXT("ViveOpenXRHandInteraction"));

void FViveOpenXRHandInteractionModule::StartupModule()
{
	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("StartupModule() Entry."));
	//IViveOpenXRHandInteractionModule::StartupModule();

	TSharedPtr<FGenericApplicationMessageHandler> DummyMessageHandler(new FGenericApplicationMessageHandler());
	CreateInputDevice(DummyMessageHandler.ToSharedRef());

	EKeys::AddMenuCategoryDisplayInfo("Hand Interaction", LOCTEXT("HandInteractionSubCategory", "HTC Hand Interaction"), TEXT("GraphEditor.PadEvent_16x"));

	check(GConfig && GConfig->IsReadyForUse());
	FString modeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnableHandInteraction"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{
			GetHandInteraction()->m_EnableHandInteraction = false;
		}
		else if (modeName.Equals("True"))
		{
			GetHandInteraction()->m_EnableHandInteraction = true;
			if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bUseHTCHandInteraction"), modeName, GEngineIni))
			{
				if (modeName.Equals("False"))
				{
					GetHandInteraction()->m_UseHTCHandInteraction = false;
				}
				else if (modeName.Equals("True"))
				{
					GetHandInteraction()->m_UseHTCHandInteraction = true;
				}
			}
		}
	}

	/// ---- Left hand only ----
	if (!GetHandInteraction()->m_UseHTCHandInteraction)
	{
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Aim_Value, LOCTEXT("HandInteraction_Left_Aim_Value", "Hand Interaction (L) Aim Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Pinch_Ready, LOCTEXT("HandInteraction_Left_Pinch_Ready", "Hand Interaction (L) Pinch Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Aim_Ready, LOCTEXT("HandInteraction_Left_Pinch_Ready", "Hand Interaction (L) Aim Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Grasp_Ready, LOCTEXT("HandInteraction_Left_Pinch_Ready", "Hand Interaction (L) Grasp Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
	}
	EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Pinch_Value, LOCTEXT("HandInteraction_Left_Pinch_Value", "Hand Interaction (L) Pinch Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
	EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Left_Grasp_Value, LOCTEXT("HandInteraction_Left_Grasp_Value", "Hand Interaction (L) Grasp Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));

	/// ---- Right hand only ----
	if (!GetHandInteraction()->m_UseHTCHandInteraction)
	{
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Aim_Value, LOCTEXT("HandInteraction_Right_Aim_Value", "Hand Interaction (R) Aim Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Pinch_Ready, LOCTEXT("HandInteraction_Right_Pinch_Ready", "Hand Interaction (R) Pinch Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Aim_Ready, LOCTEXT("HandInteraction_Right_Pinch_Ready", "Hand Interaction (R) Aim Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
		EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Grasp_Ready, LOCTEXT("HandInteraction_Right_Pinch_Ready", "Hand Interaction (R) Grasp Ready"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
	}
	EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Pinch_Value, LOCTEXT("HandInteraction_Right_Pinch_Value", "Hand Interaction (R) Pinch Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));
	EKeys::AddKey(FKeyDetails(HandInteractionKeys::HandInteraction_Right_Grasp_Value, LOCTEXT("HandInteraction_Right_Grasp_Value", "Hand Interaction (R) Grasp Value"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "Hand Interaction"));

	if (GetHandInteraction()->m_EnableHandInteraction)
	{
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("Enable Hand Interaction."));
	}
	else
	{
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("Disable Hand Interaction."));
	}
	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("StartupModule() Finished."));
}

void FViveOpenXRHandInteractionModule::ShutdownModule()
{
	UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("ShutdownModule()"));
}

TSharedPtr<class IInputDevice> FViveOpenXRHandInteractionModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	if (!HandInteractionController.IsValid())
	{
		auto InputDevice = new FViveOpenXRHandInteraction(InMessageHandler);
		HandInteractionController = TSharedPtr<FViveOpenXRHandInteraction>(InputDevice);
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("CreateInputDevice() new HandInteractionController %p"), HandInteractionController.Get());

		return HandInteractionController;
	}
	else
	{
		HandInteractionController.Get()->SetMessageHandler(InMessageHandler);
		UE_LOG(LogViveOpenXRHandInteraction, Log, TEXT("CreateInputDevice() update HandInteractionController %p"), HandInteractionController.Get());

		return HandInteractionController;
	}
	return nullptr;
}

FViveOpenXRHandInteraction* FViveOpenXRHandInteractionModule::GetHandInteraction()
{
	return FViveOpenXRHandInteraction::GetInstance();
}

IMPLEMENT_MODULE(FViveOpenXRHandInteractionModule, ViveOpenXRHandInteraction);

#undef LOCTEXT_NAMESPACE