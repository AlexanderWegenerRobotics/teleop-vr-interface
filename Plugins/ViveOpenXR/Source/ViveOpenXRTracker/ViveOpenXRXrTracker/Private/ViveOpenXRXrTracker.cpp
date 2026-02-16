// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRXrTracker.h"
#include "IXRTrackingSystem.h"
#include "OpenXRCore.h"
#include "UObject/UObjectIterator.h"
#include "Modules/ModuleManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"

#include "Modules/ModuleManager.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputLibrary.h"
#include "EnhancedInputSubsystemInterface.h"
#include "EnhancedInputModule.h"
#include "PlayerMappableInputConfig.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "EnhancedInputEditorSubsystem.h"
#endif

DEFINE_LOG_CATEGORY(LogViveOpenXRXrTracker);

FViveOpenXRXrTracker* FViveOpenXRXrTracker::m_Instance = nullptr;

FViveOpenXRXrTracker::FViveXrTracker::FViveXrTracker()
	: XrTrackerUserPath(XR_NULL_PATH)
	, xrTrackerPosePath(XR_NULL_PATH)
	, MenuClickPath(XR_NULL_PATH)
	, TriggerClickPath(XR_NULL_PATH)
	, SqueezeClickPath(XR_NULL_PATH)
	, TrackpadClickPath(XR_NULL_PATH)
	, xrTrackerPoseAction(XR_NULL_HANDLE)
	, MenuClickAction(XR_NULL_HANDLE)
	, TriggerClickAction(XR_NULL_HANDLE)
	, SqueezeClickAction(XR_NULL_HANDLE)
	, TrackpadClickAction(XR_NULL_HANDLE)
	, XrTrackerSpaceLocation({ XR_TYPE_SPACE_LOCATION })
	, XrTrackerStatePose({ XR_TYPE_ACTION_STATE_POSE })
	, MenuClickActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, TriggerClickActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, SqueezeClickActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, TrackpadClickActionState({ XR_TYPE_ACTION_STATE_BOOLEAN })
	, DeviceId(0)
{
	XrTrackerPaths.RemoveAll([](const int& num) {
		return true;
		});
}

void FViveOpenXRXrTracker::FViveXrTracker::AddAction(XrActionSet& InActionSet, const FName& InName, const TArray<XrPath>& InSubactionPaths, XrActionType InActionType, XrAction& InAction)
{
	check(InActionSet != XR_NULL_HANDLE);
	if (InAction != XR_NULL_HANDLE) {
		xrDestroyAction(InAction);
		InAction = XR_NULL_HANDLE;
	}

	char ActionName[NAME_SIZE];
	InName.GetPlainANSIString(ActionName);
	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	Info.actionType = InActionType;
	Info.countSubactionPaths = InSubactionPaths.Num();
	Info.subactionPaths = InSubactionPaths.GetData();
	FCStringAnsi::Strcpy(Info.actionName, XR_MAX_ACTION_NAME_SIZE, ActionName);
	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, ActionName);
	XR_ENSURE(xrCreateAction(InActionSet, &Info, &InAction));
}

FViveOpenXRXrTracker::FViveOpenXRXrTracker(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
{
	// Register modular feature manually
	IModularFeatures::Get().RegisterModularFeature(IMotionController::GetModularFeatureName(), static_cast<IMotionController*>(this));
	IModularFeatures::Get().RegisterModularFeature(IOpenXRExtensionPlugin::GetModularFeatureName(), static_cast<IOpenXRExtensionPlugin*>(this));

	m_Instance = this;
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("FViveOpenXRXrTracker() register extension feature Xr Tracker %p."), m_Instance);
}

FViveOpenXRXrTracker::~FViveOpenXRXrTracker()
{
	// Unregister modular feature manually
	IModularFeatures::Get().UnregisterModularFeature(IMotionController::GetModularFeatureName(), static_cast<IMotionController*>(this));
	IModularFeatures::Get().UnregisterModularFeature(IOpenXRExtensionPlugin::GetModularFeatureName(), static_cast<IOpenXRExtensionPlugin*>(this));
}

bool FViveOpenXRXrTracker::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (m_EnableXrTracker)
	{
		UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("GetRequiredExtensions() XR_HTC_path_enumeration."));
		OutExtensions.Add("XR_HTC_path_enumeration");
		UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("GetRequiredExtensions() XR_HTC_vive_xr_tracker_interaction."));
		OutExtensions.Add("XR_HTC_vive_xr_tracker_interaction");
	}
	return true;
}

#define LOCTEXT_NAMESPACE "FViveOpenXRXrTrackerModule"

void FViveOpenXRXrTracker::PostCreateInstance(XrInstance InInstance)
{
	if (!m_EnableXrTracker) return;
	Instance = InInstance;
	XR_ENSURE(xrGetInstanceProcAddr(InInstance, "xrEnumeratePathsForInteractionProfileHTC", (PFN_xrVoidFunction*)&xrEnumeratePathsForInteractionProfileHTC));
	{
		UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("Get xrEnumeratePathsForInteractionProfileHTC function pointer."));

		if (!bPathEnumerated)
		{
			// Enumerate tracker user paths
			uint32_t userCount = 0;
			uint32_t userCountInput = 0;

			// initialization arrays
			XrTrackerUserPaths.Empty();
			XrTrackerPaths.Empty();

			// Create enumerateInfo
			XrPathsForInteractionProfileEnumerateInfoHTC enumerateInfo;
			enumerateInfo.type = XR_TYPE_PATHS_FOR_INTERACTION_PROFILE_ENUMERATE_INFO_HTC;
			// Get the user paths with the input interaction profile.
			xrTrackerInteractionProfile = FOpenXRPath("/interaction_profiles/htc/vive_xr_tracker");
			enumerateInfo.interactionProfile = xrTrackerInteractionProfile;

			// Use XR_NULL_PATH pathenumerate to get supported userspath under interactionprofile
			enumerateInfo.userPath = XR_NULL_PATH;

			TArray<XrPath> XrTrackerEnumerateUserPaths;
			XrTrackerEnumerateUserPaths.Empty();

			// Enumerate user paths 
			XR_ENSURE(xrEnumeratePathsForInteractionProfileHTC(Instance, &enumerateInfo, 0, &userCount, XrTrackerEnumerateUserPaths.GetData()));
			{
				userCountInput = userCount;

				bool enumerateUltimatePath = false;

				if (userCountInput > 0)
				{
					// Get how meny user paths
					for (uint32_t i = 0; i < userCount; i++)
					{
						XrTrackerEnumerateUserPaths.Emplace(XR_NULL_PATH);
					}
					// Get user paths names
					XR_ENSURE(xrEnumeratePathsForInteractionProfileHTC(Instance, &enumerateInfo, userCountInput, &userCount, XrTrackerEnumerateUserPaths.GetData()));

					for (uint32_t i = 0; i < userCountInput; i++)
					{
						FOpenXRPath path = XrTrackerEnumerateUserPaths[i];
						UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("XrPath : %s"), *path.ToString());
						if (path.ToString().Contains("ultimate"))
							XrTrackerUserPaths.Emplace(XrTrackerEnumerateUserPaths[i]);
					}
					if (XrTrackerUserPaths.Num() >= 1)
						enumerateUltimatePath = true;
					else
						UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("xrEnumeratePathsForInteractionProfileHTC no any path."));
				}
				if (!enumerateUltimatePath)
				{
					XrTrackerUserPaths.Empty();
					for (int i = 0; i < 5; i++)
					{
						XrTrackerUserPaths.Emplace(FOpenXRPath(xrTrackerPaths[i]));
					}
				}
				TArray<XrPath> XrTrackerSupportPaths;
				// Get inputs for each user
				for (int i = 0; i < XrTrackerUserPaths.Num(); i++)
				{
					FName XrTrackerName = (FName)("UltimateTracker" + FString::FormatAsNumber(i + 1));
					FViveXrTracker viveXrTracker = FViveXrTracker();

					viveXrTrackerMap.Add(XrTrackerName, viveXrTracker);

					FOpenXRPath XrTrackerUser = XrTrackerUserPaths[i];

					viveXrTrackerMap[XrTrackerName].XrTrackerUserPath = XrTrackerUser;
					uint32_t pathCount = 0;
					uint32_t pathCountInput = 0;
					// Get the input and output source paths with user path. 
					enumerateInfo.userPath = viveXrTrackerMap[XrTrackerName].XrTrackerUserPath;

					// Enumerate user input paths
					XR_ENSURE(xrEnumeratePathsForInteractionProfileHTC(Instance, &enumerateInfo, 0, &pathCount, XrTrackerSupportPaths.GetData()));
					{
						// Get how meny input paths
						pathCountInput = pathCount;

						TArray<XrPath> XrTrackerSupportInputPaths;
						XrTrackerSupportPaths.Empty();

						if (pathCountInput >= 0)
						{

							for (uint32_t j = 0; j < pathCountInput; j++)
							{
								XrTrackerSupportPaths.Emplace(XR_NULL_PATH);
							}

							XR_ENSURE(xrEnumeratePathsForInteractionProfileHTC(Instance, &enumerateInfo, pathCountInput, &pathCount, XrTrackerSupportPaths.GetData()));
							{
								for (uint32_t j = 0; j < pathCountInput; j++)
								{
									uint32 PathCount = 0;
									char PathChars[XR_MAX_PATH_LENGTH];
									XrResult Result = xrPathToString(Instance, XrTrackerSupportPaths[j], XR_MAX_PATH_LENGTH, &PathCount, PathChars);

									if (FString(PathCount, PathChars).Contains("pose"))
									{
										FString pathStr;
										viveXrTrackerMap[XrTrackerName].PoseActionNameStr = "xrtracker_" + FString::FormatAsNumber(i + 1) + "pose";
										PathCount = FString(PathChars).Len();
										// Set up pose xrpath
										pathStr = XrTrackerUser.ToString() + FString(PathCount, PathChars);
										FTCHARToUTF8 Converter(*pathStr);
										const char* utf8PathStr = Converter.Get();
										XrPath posePath = static_cast<FOpenXRPath>(utf8PathStr);
										viveXrTrackerMap[XrTrackerName].xrTrackerPosePath = posePath;
									}
									else if (FString(PathCount, PathChars).Contains("menu"))
									{
										FString pathStr;
										viveXrTrackerMap[XrTrackerName].MenuActionNameStr = "xrtracker_" + FString::FormatAsNumber(i + 1) + "_menu" + "_click";
										PathCount = FString(PathChars).Len();
										// Set up manu xrpath
										pathStr = XrTrackerUser.ToString() + FString(PathCount, PathChars);
										FTCHARToUTF8 Converter(*pathStr);
										const char* utf8PathStr = Converter.Get();
										XrPath menuClickPathPath = static_cast<FOpenXRPath>(utf8PathStr);
										viveXrTrackerMap[XrTrackerName].MenuClickPath = menuClickPathPath;

										// Set up manu name
										FName actionFName(*viveXrTrackerMap[XrTrackerName].MenuActionNameStr);
										viveXrTrackerMap[XrTrackerName].MenuClickKey = FKey(actionFName);
									}
									else if (FString(PathCount, PathChars).Contains("squeeze"))
									{
										FString pathStr;
										viveXrTrackerMap[XrTrackerName].SqueezeActionNameStr = "xrtracker_" + FString::FormatAsNumber(i + 1) + "_squeeze" + "_click";
										PathCount = FString(PathChars).Len();
										// Set up squeeze xrpath
										pathStr = XrTrackerUser.ToString() + FString(PathCount, PathChars);
										FTCHARToUTF8 Converter(*pathStr);
										const char* utf8PathStr = Converter.Get();
										XrPath squeezeClickPath = static_cast<FOpenXRPath>(utf8PathStr);
										viveXrTrackerMap[XrTrackerName].SqueezeClickPath = squeezeClickPath;

										// Set up squeeze name
										FName actionFName(*viveXrTrackerMap[XrTrackerName].SqueezeActionNameStr);
										viveXrTrackerMap[XrTrackerName].SqueezeClickKey = FKey(actionFName);
									}
									else if (FString(PathCount, PathChars).Contains("trigger"))
									{
										FString pathStr;
										viveXrTrackerMap[XrTrackerName].TriggerActionNameStr = "xrtracker_" + FString::FormatAsNumber(i + 1) + "_trigger" + "_click";
										PathCount = FString(PathChars).Len();
										// Set up trigger xrpath
										pathStr = XrTrackerUser.ToString() + FString(PathCount, PathChars);
										FTCHARToUTF8 Converter(*pathStr);
										const char* utf8PathStr = Converter.Get();
										XrPath triggerClickPathPath = static_cast<FOpenXRPath>(utf8PathStr);
										viveXrTrackerMap[XrTrackerName].TriggerClickPath = triggerClickPathPath;

										// Set up trigger name
										FName actionFName(*viveXrTrackerMap[XrTrackerName].TriggerActionNameStr);
										viveXrTrackerMap[XrTrackerName].TriggerClickKey = FKey(actionFName);
									}
									else if (FString(PathCount, PathChars).Contains("trackpad"))
									{
										FString pathStr;
										viveXrTrackerMap[XrTrackerName].TrackpadActionNameStr = "xrtracker_" + FString::FormatAsNumber(i + 1) + "_trackpad" + "_click";
										PathCount = FString(PathChars).Len();
										// Set up trackpad xrpath
										pathStr = XrTrackerUser.ToString() + FString(PathCount, PathChars);
										FTCHARToUTF8 Converter(*pathStr);
										const char* utf8PathStr = Converter.Get();
										XrPath trackpadClickPath = static_cast<FOpenXRPath>(utf8PathStr);
										viveXrTrackerMap[XrTrackerName].TrackpadClickPath = trackpadClickPath;

										// Set up trackpad name
										FName actionFName(*viveXrTrackerMap[XrTrackerName].TrackpadActionNameStr);
										viveXrTrackerMap[XrTrackerName].TrackpadClickKey = FKey(actionFName);
									}
								}
							}
						}
					}
				}
			}
		}
		bPathEnumerated = true;
	}
}

void FViveOpenXRXrTracker::PostCreateSession(XrSession InSession)
{
	m_Session = InSession;
}

void FViveOpenXRXrTracker::AttachActionSets(TSet<XrActionSet>& OutActionSets)
{
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("AttachActionSets() Entry."));
	if (!m_EnableXrTracker || !bPathEnumerated) return;

	check(Instance != XR_NULL_HANDLE);
	OutActionSets.Add(XrTrackerActionSet);
	for (auto& viveXrTracker : viveXrTrackerMap)
	{
		viveXrTracker.Value.AddTrackedDevices(OpenXRHMD); //Wait for subacitonPath
	}

	if (m_EnableXrTrackerInputs)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
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
					for (auto& viveXrTracker : viveXrTrackerMap)
					{
						viveXrTracker.Value.CheckAndAddEnhancedInputAction(Mapping);
					}
				}
			}
		}
		else
		{
			for (auto& viveXrTracker : viveXrTrackerMap)
			{
				KeyActionStates.Emplace(viveXrTracker.Value.MenuClickKey.GetFName(), &viveXrTracker.Value.MenuClickActionState);
				KeyActionStates.Emplace(viveXrTracker.Value.TriggerClickKey.GetFName(), &viveXrTracker.Value.TriggerClickActionState);
				KeyActionStates.Emplace(viveXrTracker.Value.SqueezeClickKey.GetFName(), &viveXrTracker.Value.SqueezeClickActionState);
				KeyActionStates.Emplace(viveXrTracker.Value.TrackpadClickKey.GetFName(), &viveXrTracker.Value.TrackpadClickActionState);
			}
		}
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
	}
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("AttachActionSets() finished."));
	bActionsAttached = true;
}

void FViveOpenXRXrTracker::GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets)
{
	if (!m_EnableXrTracker || !bPathEnumerated) return;

	check(XrTrackerActionSet != XR_NULL_HANDLE);
	OutActiveSets.Add(XrActiveActionSet{ XrTrackerActionSet, XR_NULL_PATH });
}

const void* FViveOpenXRXrTracker::OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext)
{
	static FName SystemName(TEXT("OpenXR"));

	Instance = InInstance;

	if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
	{
		OpenXRHMD = (FOpenXRHMD*)GEngine->XRSystem.Get();
	}

	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("OnCreateSession() entry."));
	if (!m_EnableXrTracker || !bPathEnumerated) return InNext;

	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("Entry XrTracker OnCreateSession."));

	//	Create ActionSet
	if (XrTrackerActionSet != XR_NULL_HANDLE)
	{
		xrDestroyActionSet(XrTrackerActionSet);
		XrTrackerActionSet = XR_NULL_HANDLE;
	}
	{
		XrActionSetCreateInfo xrTrackerActionInfo;
		xrTrackerActionInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
		xrTrackerActionInfo.next = nullptr;
		FCStringAnsi::Strcpy(xrTrackerActionInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "xrtracker");
		FCStringAnsi::Strcpy(xrTrackerActionInfo.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "VIVE OpenXR Xr Tracker Action Set");
		xrTrackerActionInfo.priority = 0;
		XR_ENSURE(xrCreateActionSet(Instance, &xrTrackerActionInfo, &XrTrackerActionSet));
	}

	TArray<XrActionSuggestedBinding> Bindings;
	for (auto& viveXrTracker : viveXrTrackerMap)
	{
		FName poseActionFName(*viveXrTracker.Value.PoseActionNameStr);
		viveXrTracker.Value.AddAction(XrTrackerActionSet, poseActionFName, XrTrackerUserPaths, XR_ACTION_TYPE_POSE_INPUT, viveXrTracker.Value.xrTrackerPoseAction);

		if (m_EnableXrTrackerInputs)
		{
			FName manuActionFName(*viveXrTracker.Value.MenuActionNameStr);
			viveXrTracker.Value.AddAction(XrTrackerActionSet, manuActionFName, XrTrackerUserPaths, XR_ACTION_TYPE_BOOLEAN_INPUT, viveXrTracker.Value.MenuClickAction);

			FName triggerActionFName(*viveXrTracker.Value.TriggerActionNameStr);
			viveXrTracker.Value.AddAction(XrTrackerActionSet, triggerActionFName, XrTrackerUserPaths, XR_ACTION_TYPE_BOOLEAN_INPUT, viveXrTracker.Value.TriggerClickAction);

			FName squeezeActionFName(*viveXrTracker.Value.SqueezeActionNameStr);
			viveXrTracker.Value.AddAction(XrTrackerActionSet, squeezeActionFName, XrTrackerUserPaths, XR_ACTION_TYPE_BOOLEAN_INPUT, viveXrTracker.Value.SqueezeClickAction);

			FName trackpadActionFName(*viveXrTracker.Value.TrackpadActionNameStr);
			viveXrTracker.Value.AddAction(XrTrackerActionSet, trackpadActionFName, XrTrackerUserPaths, XR_ACTION_TYPE_BOOLEAN_INPUT, viveXrTracker.Value.TrackpadClickAction);
		}
		viveXrTracker.Value.GetSuggestedBindings(Bindings);
		viveXrTracker.Value.XrTrackerPaths = XrTrackerPaths;
	}

	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("xrSuggestInteractionProfileBindings()"));

	XrInteractionProfileSuggestedBinding SuggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	SuggestedBindings.interactionProfile = xrTrackerInteractionProfile;
	SuggestedBindings.suggestedBindings = Bindings.GetData();
	SuggestedBindings.countSuggestedBindings = (uint32)Bindings.Num();
	XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &SuggestedBindings));
	return InNext;
}

const void* FViveOpenXRXrTracker::OnBeginSession(XrSession InSession, const void* InNext)
{
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("OnBeginSession() entry."));
	if (!m_EnableXrTracker || !bPathEnumerated) return InNext;

	bSessionStarted = true;

	return InNext;
}

void FViveOpenXRXrTracker::OnDestroySession(XrSession InSession)
{
	if (bActionsAttached)
	{
		bActionsAttached = false;
		bSessionStarted = false;
		KeyActionStates.Reset();
	}

}

void FViveOpenXRXrTracker::UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace)
{
}

/************************************************************************/
/* IMotionController                                                    */
/************************************************************************/
bool FViveOpenXRXrTracker::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	if (!bActionsAttached || OpenXRHMD == nullptr)
	{
		return false;
	}

	if (!MotionSource.ToString().Contains("UltimateTracker"))
		return false;

	if (viveXrTrackerMap.Num() == 0)
		return false;

	if (!viveXrTrackerMap.Contains(MotionSource)) {
		return false;
	}

	FString motionSource = MotionSource.ToString();
	if (ControllerIndex == DeviceIndex && motionSource.Contains("XrTracker"))
	{
		int32 DeviceId = viveXrTrackerMap[MotionSource].DeviceId;
		FQuat Orientation;
		bool Success = OpenXRHMD->GetCurrentPose(DeviceId, Orientation, OutPosition);
		OutOrientation = FRotator(Orientation);
		return Success;
	}
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("GetControllerOrientationAndPosition return false."));

	return false;
}

bool FViveOpenXRXrTracker::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
	// FTimespan initializes to 0 and GetControllerOrientationAndPositionForTime with time 0 will return the latest data.
	FTimespan Time;
	bool OutTimeWasUsed = false;
	return GetControllerOrientationAndPositionForTime(ControllerIndex, MotionSource, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityAsAxisAndLength, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
}


bool FViveOpenXRXrTracker::GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
	if (!bActionsAttached || OpenXRHMD == nullptr)
	{
		return false;
	}

	FString motionSource = MotionSource.ToString();
	if (ControllerIndex == DeviceIndex && motionSource.Contains("UltimateTracker"))
	{
		int32 DeviceId = viveXrTrackerMap[MotionSource].DeviceId;
		FQuat Orientation;
		bool Success = OpenXRHMD->GetPoseForTime(DeviceId, Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
		OutOrientation = FRotator(Orientation);
		return Success;
	}
	return false;
}

ETrackingStatus FViveOpenXRXrTracker::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
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

	if (!MotionSource.ToString().Contains("UltimateTracker"))
		return ETrackingStatus::NotTracked;

	if (viveXrTrackerMap.Num() == 0)
		return ETrackingStatus::NotTracked;

	if (!viveXrTrackerMap.Contains(MotionSource)) {
		return ETrackingStatus::NotTracked;
	}

	FViveXrTracker xrTracker = viveXrTrackerMap[MotionSource];
	if (xrTracker.xrTrackerPoseAction == nullptr)
		return ETrackingStatus::NotTracked;

	PoseActionStateInfo.action = xrTracker.xrTrackerPoseAction;
	PoseActionStateInfo.subactionPath = xrTracker.xrTrackerPosePath;

	//UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("State.isActive"));

	return ETrackingStatus::Tracked;
}

FName FViveOpenXRXrTracker::GetMotionControllerDeviceTypeName() const
{
	const static FName DefaultName(TEXT("OpenXRXrTracker"));
	return DefaultName;
}

void FViveOpenXRXrTracker::PostSyncActions(XrSession InSession)
{
	if (!m_EnableXrTracker || !bPathEnumerated) return;

	WorldToMetersScale_ = OpenXRHMD->GetWorldToMetersScale();

	for (TPair<FName, FViveXrTracker>& viveXrTracker : viveXrTrackerMap)
	{
		viveXrTracker.Value.SyncActionStates(InSession);
	}

	if (m_EnableXrTrackerInputs)
	{
		SendInputEvent_Legacy();
		SendInputEvent_EnhancedInput();
	}
}

void FViveOpenXRXrTracker::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
	check(IsInGameThread());

	// PC suppots 20 trackers, Android supports 5 trackers
#if PLATFORM_ANDROID
	for (int i = 1; i <= 5; i++)
	{
		FString name = "UltimateTracker" + FString::FormatAsNumber(i);
		SourcesOut.Add(FMotionControllerSource(FName(*name)));
	}
#endif
#if PLATFORM_WINDOWS
	for (int i = 1; i <= 20; i++)
	{
		FString name = "UltimateTracker" + FString::FormatAsNumber(i);
		SourcesOut.Add(FMotionControllerSource(FName(*name)));
	}
#endif

}

int32 FViveOpenXRXrTracker::FViveXrTracker::AddTrackedDevices(FOpenXRHMD* HMD)
{
	if (HMD)
	{
		DeviceId = HMD->AddTrackedDevice(xrTrackerPoseAction, xrTrackerPosePath, XrTrackerUserPath);
	}
	return DeviceId;
}

void FViveOpenXRXrTracker::FViveXrTracker::GetSuggestedBindings(TArray<XrActionSuggestedBinding>& OutSuggestedBindings)
{
	OutSuggestedBindings.Add(XrActionSuggestedBinding{ xrTrackerPoseAction, xrTrackerPosePath });
	if (m_Instance->m_EnableXrTrackerInputs)
	{
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ MenuClickAction, MenuClickPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ TriggerClickAction, TriggerClickPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ SqueezeClickAction, SqueezeClickPath });
		OutSuggestedBindings.Add(XrActionSuggestedBinding{ TrackpadClickAction, TrackpadClickPath });
	}
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
bool FViveOpenXRXrTracker::SetPlayerMappableInputConfig(TObjectPtr<class UPlayerMappableInputConfig> InputConfig)
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
PRAGMA_DISABLE_DEPRECATION_WARNINGS

void FViveOpenXRXrTracker::FViveXrTracker::SyncActionStates(XrSession InSession)
{
	XrActionStateGetInfo GetXrTrackerStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
	GetXrTrackerStateInfo.action = xrTrackerPoseAction;
	GetXrTrackerStateInfo.subactionPath = XrTrackerUserPath;
	XR_ENSURE(xrGetActionStatePose(InSession, &GetXrTrackerStateInfo, &XrTrackerStatePose));

	if (m_Instance->m_EnableXrTrackerInputs)
	{
		XrActionStateGetInfo MenuClickActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		MenuClickActionStateInfo.action = MenuClickAction;
		MenuClickActionStateInfo.subactionPath = XrTrackerUserPath;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &MenuClickActionStateInfo, &MenuClickActionState));

		XrActionStateGetInfo TriggerClickActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		TriggerClickActionStateInfo.action = TriggerClickAction;
		TriggerClickActionStateInfo.subactionPath = XrTrackerUserPath;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &TriggerClickActionStateInfo, &TriggerClickActionState));

		XrActionStateGetInfo SqueezeClickActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		SqueezeClickActionStateInfo.action = SqueezeClickAction;
		SqueezeClickActionStateInfo.subactionPath = XrTrackerUserPath;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &SqueezeClickActionStateInfo, &SqueezeClickActionState));

		XrActionStateGetInfo TrackpadClickActionStateInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		TrackpadClickActionStateInfo.action = TrackpadClickAction;
		TrackpadClickActionStateInfo.subactionPath = XrTrackerUserPath;
		XR_ENSURE(xrGetActionStateBoolean(InSession, &TrackpadClickActionStateInfo, &TrackpadClickActionState));
	}
}

void FViveOpenXRXrTracker::FViveXrTracker::CheckAndAddEnhancedInputAction(FEnhancedActionKeyMapping EnhancedActionKeyMapping)
{
	if (EnhancedActionKeyMapping.Key == MenuClickKey)
	{
		MenuClickActions.Emplace(EnhancedActionKeyMapping.Action);
	}

	if (EnhancedActionKeyMapping.Key == TriggerClickKey)
	{
		TriggerClickActions.Emplace(EnhancedActionKeyMapping.Action);
	}

	if (EnhancedActionKeyMapping.Key == SqueezeClickKey)
	{
		SqueezeClickActions.Emplace(EnhancedActionKeyMapping.Action);
	}

	if (EnhancedActionKeyMapping.Key == TrackpadClickKey)
	{
		TrackpadClickActions.Emplace(EnhancedActionKeyMapping.Action);
	}
}

void FViveOpenXRXrTracker::SendInputEvent_Legacy()
{
	if (!m_EnableXrTracker || !bPathEnumerated) return;

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();

	for (auto& KeyActionState : KeyActionStates)
	{
		XrActionStateBoolean state = *KeyActionState.Value;
		if (state.changedSinceLastSync)
		{
			FName keyName = KeyActionState.Key;
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

void FViveOpenXRXrTracker::SendInputEvent_EnhancedInput()
{
	if (!m_EnableXrTracker || !bPathEnumerated) return;

	auto InjectEnhancedInput = [](XrActionStateBoolean& State, TArray<TObjectPtr<const UInputAction>>& Actions)
		{
			FInputActionValue InputValue;
			TArray<TObjectPtr<UInputTrigger>> Triggers = {};
			TArray<TObjectPtr<UInputModifier>> Modifiers = {};

			InputValue = FInputActionValue(State.isActive ? (bool)State.currentState : false);
			//UE_LOG(LogViveOpenXRWristTracker, Log, TEXT("SendInputEvent_EnhancedInput()."));

			for (auto InputAction : Actions)
			{
				//if(State.isActive) UE_LOG(LogViveOpenXRWristTracker, Log, TEXT("SendInputEvent_EnhancedInput() Action: %s, Value: %d"), *InputAction.GetFName().ToString(), (uint8_t)State.currentState);
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

	for (TPair<FName, FViveXrTracker>& viveXrTracker : viveXrTrackerMap)
	{
		InjectEnhancedInput(viveXrTracker.Value.MenuClickActionState, viveXrTracker.Value.MenuClickActions);
		InjectEnhancedInput(viveXrTracker.Value.TriggerClickActionState, viveXrTracker.Value.TriggerClickActions);
		InjectEnhancedInput(viveXrTracker.Value.SqueezeClickActionState, viveXrTracker.Value.SqueezeClickActions);
		InjectEnhancedInput(viveXrTracker.Value.TrackpadClickActionState, viveXrTracker.Value.TrackpadClickActions);
	}
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "FViveOpenXRXrTrackerModule"

FName IViveOpenXRXrTrackerModule::ViveOpenXRXrTrackerModularKeyName = FName(TEXT("ViveOpenXRXrTracker"));

FViveOpenXRXrTrackerModule::FViveOpenXRXrTrackerModule()
{
	
}

void FViveOpenXRXrTrackerModule::StartupModule()
{
	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("StartupModule() Entry."));

	IViveOpenXRXrTrackerModule::StartupModule();

	TSharedPtr<FGenericApplicationMessageHandler> DummyMessageHandler(new FGenericApplicationMessageHandler());
	CreateInputDevice(DummyMessageHandler.ToSharedRef());
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 1", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 1"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 2", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 2"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 3", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 3"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 4", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 4"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 5", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 5"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 6", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 6"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 7", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 7"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 8", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 8"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 9", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 9"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 10", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 10"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 11", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 11"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 12", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 12"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 13", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 13"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 14", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 14"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 15", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 15"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 16", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 16"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 17", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 17"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 18", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 18"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 19", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 19"), TEXT("GraphEditor.PadEvent_16x"));
	EKeys::AddMenuCategoryDisplayInfo("Xr Tracker 20", LOCTEXT("XrTrackerSubCategory", "HTC Xr Tracker 20"), TEXT("GraphEditor.PadEvent_16x"));
	// Check if the modeule is enabled
	check(GConfig && GConfig->IsReadyForUse());
	FString modeName;
	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnableXrTracker"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{

			UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("StartupModule() Entry."));
			GetXrTracker()->m_EnableXrTracker = false;
		}
		else if (modeName.Equals("True"))
		{
			GetXrTracker()->m_EnableXrTracker = true;
		}
	}

	if (GConfig->GetString(TEXT("/Script/ViveOpenXRRuntimeSettings.ViveOpenXRRuntimeSettings"), TEXT("bEnableXrTrackerInputs"), modeName, GEngineIni))
	{
		if (modeName.Equals("False"))
		{
			GetXrTracker()->m_EnableXrTrackerInputs = false;
		}
		else if (modeName.Equals("True"))
		{
			GetXrTracker()->m_EnableXrTrackerInputs = true;
		}
	}

	if (GetXrTracker()->m_EnableXrTrackerInputs)
	{
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 1", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 1"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 2", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 2"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 3", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 3"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 4", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 4"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 5", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 5"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 6", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 6"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 7", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 7"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 8", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 8"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 9", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 9"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 10", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 10"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 11", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 11"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 12", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 12"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 13", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 13"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 14", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 14"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 15", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 15"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 16", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 16"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 17", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 17"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 18", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 18"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 19", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 19"), TEXT("GraphEditor.PadEvent_16x"));
		EKeys::AddMenuCategoryDisplayInfo("UltimateTracker 20", LOCTEXT("XrTrackerSubCategory", "HTC UltimateTracker 20"), TEXT("GraphEditor.PadEvent_16x"));
#if PLATFORM_ANDROID
		for (int i = 1; i <= 5; i++)
		{
			// UltimateTracker name
			FName TrackerName("UltimateTracker " + FString::FormatAsNumber(i));

			// Menu action input
			FString MenuActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_menu" + "_click";
			FName MenuActionName(*MenuActionString);
			FText MenuText = FText::Format(LOCTEXT("XrTracker_{0}_Menu_Click", "UltimateTracker ({0}) Menu Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(MenuActionName), MenuText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Squeeze action input
			FString SqueezeActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_squeeze" + "_click";
			FName SqueezeActionName(*SqueezeActionString);
			FText SqueezeText = FText::Format(LOCTEXT("XrTracker_{0}_Squeeze_Click", "UltimateTracker ({0}) Squeeze Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(SqueezeActionName), SqueezeText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Trigger action input
			FString TriggerActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_trigger" + "_click";
			FName TriggerActionName(*TriggerActionString);
			FText TriggerText = FText::Format(LOCTEXT("XrTracker_{0}_Trigger_Click", "UltimateTracker ({0}) Trigger Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(TriggerActionName), TriggerText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Trackpad action input
			FString TrackpadActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_trackpad" + "_click";
			FName TrackpadActionName(*TrackpadActionString);
			FText TrackpadText = FText::Format(LOCTEXT("XrTracker_{0}_Trackpad_Click", "UltimateTracker ({0}) Trackpad Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(TrackpadActionName), TrackpadText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

	}
#endif
#if PLATFORM_WINDOWS
		for (int i = 1; i <= 20; i++)
		{
			// UltimateTracker name
			FName TrackerName("UltimateTracker " + FString::FormatAsNumber(i));

			// Menu action input
			FString MenuActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_menu" + "_click";
			FName MenuActionName(*MenuActionString);
			FText MenuText = FText::Format(LOCTEXT("XrTracker_{0}_Menu_Click", "UltimateTracker ({0}) Menu Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(MenuActionName), MenuText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Squeeze action input
			FString SqueezeActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_squeeze" + "_click";
			FName SqueezeActionName(*SqueezeActionString);
			FText SqueezeText = FText::Format(LOCTEXT("XrTracker_{0}_Squeeze_Click", "UltimateTracker ({0}) Squeeze Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(SqueezeActionName), SqueezeText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Trigger action input
			FString TriggerActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_trigger" + "_click";
			FName TriggerActionName(*TriggerActionString);
			FText TriggerText = FText::Format(LOCTEXT("XrTracker_{0}_Trigger_Click", "UltimateTracker ({0}) Trigger Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(TriggerActionName), TriggerText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

			// Trackpad action input
			FString TrackpadActionString = "xrtracker_" + FString::FormatAsNumber(i) + "_trackpad" + "_click";
			FName TrackpadActionName(*TrackpadActionString);
			FText TrackpadText = FText::Format(LOCTEXT("XrTracker_{0}_Trackpad_Click", "UltimateTracker ({0}) Trackpad Click"), FText::AsNumber(i));
			EKeys::AddKey(FKeyDetails(FKey(TrackpadActionName), TrackpadText, FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, TrackerName));

		}
#endif
	}

	if (GetXrTracker()->m_EnableXrTracker)
	{
		UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("Enable Xr Tracker."));
	}
	else
	{
		UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("Disable Xr Tracker."));
	}

	UE_LOG(LogViveOpenXRXrTracker, Log, TEXT("StartupModule() Finished."));

}

void FViveOpenXRXrTrackerModule::ShutdownModule()
{
}

TSharedPtr<class IInputDevice> FViveOpenXRXrTrackerModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	if (!XrTracker.IsValid())
	{
		auto InputDevice = new FViveOpenXRXrTracker(InMessageHandler);
		XrTracker = TSharedPtr<FViveOpenXRXrTracker>(InputDevice);

		return XrTracker;
	}
	else
	{
		XrTracker.Get()->SetMessageHandler(InMessageHandler);
		return XrTracker;
	}
	return nullptr;
}

FViveOpenXRXrTracker* FViveOpenXRXrTrackerModule::GetXrTracker()
{
	return FViveOpenXRXrTracker::GetInstance();
}

IMPLEMENT_MODULE(FViveOpenXRXrTrackerModule, ViveOpenXRXrTracker)

