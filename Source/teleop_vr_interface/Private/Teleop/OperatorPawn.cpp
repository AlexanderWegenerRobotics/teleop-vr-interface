#include "Teleop/OperatorPawn.h"

extern ENGINE_API uint32 GGPUFrameTime;
// The same cycle counters stat unit reads. Logged rather than drawn: the stat
// overlay does not composite under stereo rendering, so it is invisible in VR.
extern RENDERCORE_API uint32 GGameThreadTime;
extern RENDERCORE_API uint32 GRenderThreadTime;
#include "Video/GStreamerSource.h"
#include "Video/LocalPreviewSource.h"
#include "Teleop/TeleOpConfig.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Shared/annotation_msg.hpp"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "IOpenXRHMD.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
// Windows A/W macros leak through the unity build; see WindowsMacroCleanup.h.
#include "Shared/WindowsMacroCleanup.h"
#endif

namespace {
// VideoFeedComponent key for the avatar main view. The twin source is registered
// under Config->Stream.TwinStream.Name.
const FString kAvatarMainSourceName = TEXT("AvatarStream");
// User-facing label for the avatar view. The twin label comes from stream.json.
const FString kAvatarMainLabel = TEXT("AVATAR");

// Linear-space sRGB #FF800D; SetColorAndOpacity takes linear 0..1.
const FLinearColor kWarnAmber(1.f, 0.216f, 0.004f);

// Authority colours in linear space: POLICY #35C6E4, HUMAN #3FD47A, HOLD #E8B21F.
const FLinearColor kAuthorityPolicy(0.0356f, 0.5647f, 0.7758f);
const FLinearColor kAuthorityHuman (0.0497f, 0.6584f, 0.1946f);
const FLinearColor kAuthorityHold  (0.8070f, 0.4452f, 0.0137f);
// Arms disagree or authority is unclaimed. Deliberately none of the three state
// colours.
const FLinearColor kAuthorityMixed (0.55f, 0.55f, 0.55f);
// AGREE pips: lit in the POLICY accent, unlit near-black.
const FLinearColor kPipOff(0.15f, 0.15f, 0.15f);

// Foreground for glyph and text drawn on an accent fill, chosen from the fill's
// luminance (Rec.709 on linear values) so it stays readable for any palette.
const FLinearColor kOnAccentDark (0.03f, 0.03f, 0.04f);
const FLinearColor kOnAccentLight(0.96f, 0.96f, 0.96f);

FLinearColor ForegroundOn(const FLinearColor& Fill) {
    const float Luma = 0.2126f * Fill.R + 0.7152f * Fill.G + 0.0722f * Fill.B;
    return Luma > 0.3f ? kOnAccentDark : kOnAccentLight;
}
constexpr int32  kAgreePips            = 5;
// policy_status arrives at ~5 Hz; older than this and INF/AGREE show nothing.
constexpr double kPolicyStatusStaleSec = 1.0;

FLinearColor AuthorityColor(EControlAuthority A) {
    switch (A) {
    case EControlAuthority::Policy: return kAuthorityPolicy;
    case EControlAuthority::Human:  return kAuthorityHuman;
    case EControlAuthority::Hold:   return kAuthorityHold;
    default:                        return kAuthorityMixed;
    }
}

struct FGazeSampleMsg {
    uint64_t frame_id     = 0;
    float    gaze_px_x    = 0.f;
    float    gaze_px_y    = 0.f;
    uint64_t timestamp_ns = 0;
    uint32_t image_w      = 0;
    uint32_t image_h      = 0;
    MSGPACK_DEFINE_MAP(frame_id, gaze_px_x, gaze_px_y, timestamp_ns, image_w, image_h)
};

// Whole-machine CPU utilisation in percent across all processes (game-thread sampled).
float SampleSystemCpuPercent() {
#if PLATFORM_WINDOWS
    static ULARGE_INTEGER prevIdle{}, prevKernel{}, prevUser{};
    FILETIME ftIdle, ftKernel, ftUser;
    if (!GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) return 0.f;
    ULARGE_INTEGER idle, kernel, user;
    idle.LowPart   = ftIdle.dwLowDateTime;   idle.HighPart   = ftIdle.dwHighDateTime;
    kernel.LowPart = ftKernel.dwLowDateTime; kernel.HighPart = ftKernel.dwHighDateTime;
    user.LowPart   = ftUser.dwLowDateTime;   user.HighPart   = ftUser.dwHighDateTime;
    const uint64 idleDelta   = idle.QuadPart   - prevIdle.QuadPart;
    const uint64 kernelDelta = kernel.QuadPart - prevKernel.QuadPart;
    const uint64 userDelta   = user.QuadPart   - prevUser.QuadPart;
    prevIdle = idle; prevKernel = kernel; prevUser = user;
    const uint64 total = kernelDelta + userDelta;   // kernel already includes idle
    if (total == 0) return 0.f;
    return 100.f * (1.f - static_cast<float>(idleDelta) / static_cast<float>(total));
#else
    return FPlatformTime::GetCPUTime().CPUTimePct;
#endif
}

// GPU utilisation [0,100] and core temperature [°C] via NVML (nvml.dll ships with the NVIDIA driver).
bool SampleGpuNvml(float& utilPct, float& tempC) {
    struct FNvml {
        void* lib = nullptr;
        void* dev = nullptr;
        int (*pInit)()                          = nullptr;
        int (*pShutdown)()                      = nullptr;
        int (*pHandle)(unsigned int, void**)    = nullptr;
        int (*pUtil)(void*, void*)              = nullptr;
        int (*pTemp)(void*, int, unsigned int*) = nullptr;
        bool ok = false;
        FNvml() {
            lib = FPlatformProcess::GetDllHandle(TEXT("nvml.dll"));
            if (!lib) return;
            pInit     = reinterpret_cast<int(*)()>(FPlatformProcess::GetDllExport(lib, TEXT("nvmlInit_v2")));
            pShutdown = reinterpret_cast<int(*)()>(FPlatformProcess::GetDllExport(lib, TEXT("nvmlShutdown")));
            pHandle   = reinterpret_cast<int(*)(unsigned int, void**)>(FPlatformProcess::GetDllExport(lib, TEXT("nvmlDeviceGetHandleByIndex_v2")));
            pUtil     = reinterpret_cast<int(*)(void*, void*)>(FPlatformProcess::GetDllExport(lib, TEXT("nvmlDeviceGetUtilizationRates")));
            pTemp     = reinterpret_cast<int(*)(void*, int, unsigned int*)>(FPlatformProcess::GetDllExport(lib, TEXT("nvmlDeviceGetTemperature")));
            if (!pInit || !pHandle || !pUtil || !pTemp) return;
            if (pInit() != 0) return;
            if (pHandle(0, &dev) != 0) return;
            ok = true;
        }
        ~FNvml() {
            if (ok && pShutdown) pShutdown();
            if (lib) FPlatformProcess::FreeDllHandle(lib);
        }
    };
    static FNvml Nvml;
    if (!Nvml.ok) return false;

    struct { unsigned int gpu; unsigned int memory; } util{};
    unsigned int temp = 0;
    if (Nvml.pUtil(Nvml.dev, &util) != 0) return false;
    if (Nvml.pTemp(Nvml.dev, 0, &temp) != 0) return false;   // 0 = NVML_TEMPERATURE_GPU
    utilPct = static_cast<float>(util.gpu);
    tempC   = static_cast<float>(temp);
    return true;
}
}

AOperatorPawn::AOperatorPawn() {
	PrimaryActorTick.bCanEverTick = true;

	PendingArmFault_[0].Store(-1);
	PendingArmFault_[1].Store(-1);

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;
	VRCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	VRCamera->PostProcessSettings.MotionBlurAmount = 0.0f;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingSource(EControllerHand::Left);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingSource(EControllerHand::Right);

	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	VideoFeed = CreateDefaultSubobject<UVideoFeedComponent>(TEXT("VideoFeed"));
	ComLink = CreateDefaultSubobject<UComLink>(TEXT("ComLink"));
	Gaze = CreateDefaultSubobject<UGazeComponent>(TEXT("Gaze"));
	SoundFeedback = CreateDefaultSubobject<USoundFeedback>(TEXT("SoundFeedback"));
	UIBinder = CreateDefaultSubobject<UWidgetBinder>(TEXT("UIBinder"));
	AlertBinder = CreateDefaultSubobject<UWidgetBinder>(TEXT("AlertBinder"));
	LeftWristView  = CreateDefaultSubobject<UWristViewComponent>(TEXT("LeftWristView"));
	RightWristView = CreateDefaultSubobject<UWristViewComponent>(TEXT("RightWristView"));
	LeftTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("LeftTracked"));
	RightTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("RightTracked"));

	VoiceAnnotator = CreateDefaultSubobject<UVoiceAnnotatorComponent>(TEXT("VoiceAnnotator"));
	NudgeReceiver = CreateDefaultSubobject<UNudgeReceiver>(TEXT("NudgeReceiver"));
	IntentHighlight = CreateDefaultSubobject<UIntentHighlightComponent>(TEXT("IntentHighlight"));
	GhostOverlay = CreateDefaultSubobject<UGhostOverlayComponent>(TEXT("GhostOverlay"));
	GhostOverlay->SetCamera(VRCamera);
	GhostOverlay->SetComLink(ComLink);
	GhostOverlay->SetRightHand(RightController);
	GhostOverlay->SetRightTracked(RightTracked);
	GhostOverlay->SetLeftHand(LeftController);
	GhostOverlay->SetLeftTracked(LeftTracked);

	LeftGraspIndicator  = CreateDefaultSubobject<UGraspIndicatorComponent>(TEXT("LeftGraspIndicator"));
	RightGraspIndicator = CreateDefaultSubobject<UGraspIndicatorComponent>(TEXT("RightGraspIndicator"));

	if (UMaterialInterface* LoadedGlyphMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GraspGlyph.M_GraspGlyph"))) {
		LeftGraspIndicator->GlyphMaterial  = LoadedGlyphMat;
		RightGraspIndicator->GlyphMaterial = LoadedGlyphMat;
	}

	LeftWorkspaceBoundary  = CreateDefaultSubobject<UWorkspaceBoundaryComponent>(TEXT("LeftWorkspaceBoundary"));
	RightWorkspaceBoundary = CreateDefaultSubobject<UWorkspaceBoundaryComponent>(TEXT("RightWorkspaceBoundary"));

	if (UMaterialInterface* LoadedPatchMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_BoundaryPatch.MI_BoundaryPatch"))) {
		LeftWorkspaceBoundary->PatchMaterial  = LoadedPatchMat;
		RightWorkspaceBoundary->PatchMaterial = LoadedPatchMat;
		IntentHighlight->DiscMaterial = LoadedPatchMat;
	}
	LeftAlignmentPanel  = CreateDefaultSubobject<UAlignmentPanelComponent>(TEXT("LeftAlignmentPanel"));
	RightAlignmentPanel = CreateDefaultSubobject<UAlignmentPanelComponent>(TEXT("RightAlignmentPanel"));
	LeftGraspRuler  = CreateDefaultSubobject<UGraspRulerComponent>(TEXT("LeftGraspRuler"));
	RightGraspRuler = CreateDefaultSubobject<UGraspRulerComponent>(TEXT("RightGraspRuler"));
	if (UMaterialInterface* LoadedPanelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AlignmentPanel.M_AlignmentPanel"))) {
		LeftAlignmentPanel->PanelMaterial  = LoadedPanelMat;
		RightAlignmentPanel->PanelMaterial = LoadedPanelMat;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HlMat(TEXT("/Game/Materials/MI_IntentHighlight.MI_IntentHighlight"));
	if (HlMat.Succeeded()) IntentHighlight->DiscMaterial = HlMat.Object;
	else UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: MI_IntentHighlight not found, highlight uses the boundary patch material"));

	static ConstructorHelpers::FObjectFinder<UInputMappingContext> IMC(TEXT("/Game/Input/IMC_PoseMapper.IMC_PoseMapper"));
	if (IMC.Succeeded()) InputMappingContext = IMC.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction> LeftTrig(TEXT("/Game/Input/IA_LeftTrigger.IA_LeftTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftGrip(TEXT("/Game/Input/IA_LeftGrip.IA_LeftGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftStop(TEXT("/Game/Input/IA_LeftStop.IA_LeftStop"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftPadUp(TEXT("/Game/Input/IA_LeftPadUp.IA_LeftPadUp"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftPadDown(TEXT("/Game/Input/IA_LeftPadDown.IA_LeftPadDown"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightTrig(TEXT("/Game/Input/IA_RightTrigger.IA_RightTrigger"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightGrip(TEXT("/Game/Input/IA_RightGrip.IA_RightGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightStop(TEXT("/Game/Input/IA_RightStop.IA_RightStop"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightPadUp(TEXT("/Game/Input/IA_RightPadUp.IA_RightPadUp"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightPadDown(TEXT("/Game/Input/IA_RightPadDown.IA_RightPadDown"));
	static ConstructorHelpers::FObjectFinder<UInputAction> LeftHandGrip(TEXT("/Game/Input/IA_LeftHandGrip.IA_LeftHandGrip"));
	static ConstructorHelpers::FObjectFinder<UInputAction> RightHandGrip(TEXT("/Game/Input/IA_RightHandGrip.IA_RightHandGrip"));

	LeftTracked->MotionController = LeftController;
	RightTracked->MotionController = RightController;

	LeftTracked->IA_Trigger = LeftTrig.Object;
	LeftTracked->IA_Grip = LeftGrip.Object;
	LeftTracked->IA_Stop = LeftStop.Object;
	LeftTracked->IA_PadUp = LeftPadUp.Object;
	LeftTracked->IA_PadDown = LeftPadDown.Object;
	LeftTracked->IA_HandGrip = LeftHandGrip.Object;
	RightTracked->IA_Trigger = RightTrig.Object;
	RightTracked->IA_Grip = RightGrip.Object;
	RightTracked->IA_Stop = RightStop.Object;
	RightTracked->IA_PadUp = RightPadUp.Object;
	RightTracked->IA_PadDown = RightPadDown.Object;
	RightTracked->IA_HandGrip = RightHandGrip.Object;

	static ConstructorHelpers::FClassFinder<UUserWidget> FrameClass(TEXT("/Game/UI/WBP_HudFrame.WBP_HudFrame_C"));
	static ConstructorHelpers::FClassFinder<UUserWidget> AlertsClass(TEXT("/Game/UI/WBP_HudAlerts.WBP_HudAlerts_C"));
	if (FrameClass.Succeeded() && AlertsClass.Succeeded()) {
		UIWidgetClass    = FrameClass.Class;
		AlertWidgetClass = AlertsClass.Class;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: WBP_HudFrame / WBP_HudAlerts not found, falling back to WBP_DebugPanel"));
		static ConstructorHelpers::FClassFinder<UUserWidget> UIClass(TEXT("/Game/UI/WBP_DebugPanel.WBP_DebugPanel_C"));
		if (UIClass.Succeeded()) UIWidgetClass = UIClass.Class;
		else UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: WBP_DebugPanel not found"));
	}
}

void AOperatorPawn::BeginPlay() {

	UTeleOpConfig* Config = NewObject<UTeleOpConfig>(this);
	if (!Config->Load(UTeleOpConfig::DefaultConfigPath())) {
		UE_LOG(LogTemp, Fatal, TEXT("OperatorPawn: Config load failed! check Config/TeleOp/ in project directory."));
		return;
	}

	bSwapControllers_ = Config->Robot.bSwapControllers;

	if (!Config->bGazeEnabled && Gaze) {
		Gaze->PrimaryComponentTick.bStartWithTickEnabled = false;
		Gaze->SetComponentTickEnabled(false);
	}
	if (bSwapControllers_) {
		Swap(LeftController, RightController);
		LeftTracked->MotionController  = LeftController;
		RightTracked->MotionController = RightController;
		Swap(LeftTracked->IA_Trigger,  RightTracked->IA_Trigger);
		Swap(LeftTracked->IA_Grip,     RightTracked->IA_Grip);
		Swap(LeftTracked->IA_Stop,     RightTracked->IA_Stop);
		Swap(LeftTracked->IA_PadUp,    RightTracked->IA_PadUp);
		Swap(LeftTracked->IA_PadDown,  RightTracked->IA_PadDown);
		Swap(LeftTracked->IA_HandGrip, RightTracked->IA_HandGrip);
		GhostOverlay->SetLeftHand(LeftController);
		GhostOverlay->SetRightHand(RightController);
		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: controllers swapped -- physical right controller drives arm 0 (arm_left)"));
	}

	RightTracked->ControlPointOffset = bSwapControllers_ ? Config->Robot.WristPivotLeft  : Config->Robot.WristPivotRight;
	LeftTracked->ControlPointOffset  = bSwapControllers_ ? Config->Robot.WristPivotRight : Config->Robot.WristPivotLeft;

	GhostOverlay->GhostNearThresholdM_      = Config->Overlay.NearThresholdM;
	GhostOverlay->GhostFarThresholdM_       = Config->Overlay.FarThresholdM;
	GhostOverlay->GhostMinOpacity_          = Config->Overlay.MinOpacity;
	GhostOverlay->GhostMaxOpacity_          = Config->Overlay.MaxOpacity;
	GhostOverlay->WorkspaceLowerBoundZ_     = Config->Robot.WorkspaceLowerBoundZ;
	GhostOverlay->WorkspaceBoundaryMarginM_ = Config->Robot.WorkspaceBoundaryMargin;
	GhostOverlay->ControllerToEEQuat[0]     = Config->Robot.ControllerToEEQuatLeft;
	GhostOverlay->ControllerToEEQuat[1]     = Config->Robot.ControllerToEEQuatRight;
	GhostOverlay->WorkspaceMinX_            = Config->Robot.WorkspaceMinX;
	GhostOverlay->WorkspaceMaxY_            = Config->Robot.WorkspaceMaxY;
	GhostOverlay->WorkspaceMinY_            = Config->Robot.WorkspaceMinY;
	GhostOverlay->WorkspaceMaxX_            = Config->Robot.WorkspaceMaxX;
	GhostOverlay->WorkspaceMaxZ_            = Config->Robot.WorkspaceMaxZ;

	for (UWorkspaceBoundaryComponent* Boundary : { LeftWorkspaceBoundary, RightWorkspaceBoundary }) {
		Boundary->TtcHorizonS        = Config->Robot.BoundaryTtcHorizonS;
		Boundary->DistFloorM         = Config->Robot.BoundaryDistFloorM;
		Boundary->TtcEngageDistanceM = Config->Robot.BoundaryTtcEngageDistanceM;
		Boundary->OnThreshold        = Config->Robot.BoundaryOnThreshold;
		Boundary->OffThreshold       = Config->Robot.BoundaryOffThreshold;
		Boundary->AttackS            = Config->Robot.BoundaryAttackS;
		Boundary->ReleaseS           = Config->Robot.BoundaryReleaseS;
		Boundary->MinOnS             = Config->Robot.BoundaryMinOnS;
		Boundary->RadiusMinCm        = Config->Robot.BoundaryRadiusMinCm;
		Boundary->RadiusMaxCm        = Config->Robot.BoundaryRadiusMaxCm;
		Boundary->GridSpacingCm      = Config->Robot.BoundaryGridSpacingCm;
	}

	GhostOverlay->LatencyOkMs              = Config->Overlay.LatencyOkMs;
	GhostOverlay->LatencyWarnMs            = Config->Overlay.LatencyWarnMs;
	GhostOverlay->LatencyBadMs             = Config->Overlay.LatencyBadMs;
	GhostOverlay->LatencyBadExitMs         = Config->Overlay.LatencyBadExitMs;
	GhostOverlay->ViewpointMode            = Config->Overlay.ViewpointMode;
	GhostOverlay->HeadBasePosition         = Config->Overlay.HeadBasePosition;
	GhostOverlay->CamOffsetInHead          = Config->Overlay.CamOffsetInHead;
	GhostOverlay->StaticCamPos             = Config->Overlay.StaticCamPos;
	GhostOverlay->StaticCamLookAt          = Config->Overlay.StaticCamLookAt;
	GhostOverlay->StaticCamUp              = Config->Overlay.StaticCamUp;
	GhostOverlay->CaptureFOV               = Config->Overlay.CaptureFOV;
	GhostOverlay->CaptureFPS               = Config->Overlay.CaptureFPS;
	GhostOverlay->StereoCaptureFOV         = Config->Overlay.StereoCaptureFOV;
	GhostOverlay->StereoEyeOffsetCm        = Config->Overlay.StereoEyeOffsetCm;
	GhostOverlay->PlaneDistance            = Config->Overlay.PlaneDistance;
	GhostOverlay->FOVCoverage              = Config->Overlay.FOVCoverage;
	GhostOverlay->HmdHFovDeg               = Config->Overlay.HmdHFovDeg;
	GhostOverlay->RenderTargetSize         = FIntPoint(Config->Overlay.RenderTargetWidth, Config->Overlay.RenderTargetHeight);

	// Video quad and ghost overlay quad must be sized identically, or the ghost and
	// the gaze projection drift off the image. Both read the same overlay config.
	VideoFeed->PlaneDistance               = Config->Overlay.PlaneDistance;
	VideoFeed->FOVCoverage                 = Config->Overlay.FOVCoverage;
	VideoFeed->HmdHFovDeg                  = Config->Overlay.HmdHFovDeg;

	LookAround_.MaxYawDeg   = Config->Overlay.LookAroundMaxYawDeg;
	LookAround_.MaxPitchDeg = Config->Overlay.LookAroundMaxPitchDeg;
	LookAround_.KneeDeg     = Config->Overlay.LookAroundKneeDeg;
	LookReturnSec_          = Config->Overlay.LookAroundReturnSec;
	bLookAroundEnabled_ = Config->Overlay.bLookAroundEnabled
		&& Config->Overlay.ViewpointMode == EOverlayViewpointMode::Static;
	if (Config->Overlay.bLookAroundEnabled && !bLookAroundEnabled_) {
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: look_around requested but disabled -- needs viewpoint.mode \"static\""));
	}
	if (bLookAroundEnabled_ && Config->Stream.bStereo)
		VideoFeed->SetAngularMapping(true, Config->Overlay.StereoCaptureFOV);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: video look-around %s (%s)"),
		bLookAroundEnabled_ ? TEXT("ENABLED") : TEXT("off"), Config->Stream.bStereo ? TEXT("stereo") : TEXT("mono"));

	RecordingSocket_ = MakeUnique<UdpSocket>();
	UdpSocket::Config RecordingCfg;
	RecordingCfg.RemoteIP = TEXT("127.0.0.1");
	RecordingCfg.SendPort = Config->Network.RecordingPort;
	RecordingSocket_->Open(RecordingCfg);

	ComLink->RemoteIP = Config->Network.RemoteIP;
	ComLink->AvatarSendPort = Config->Network.Avatar.Send;
	ComLink->AvatarReceivePort = Config->Network.Avatar.Receive;
	ComLink->ArmLeftSendPort = Config->Network.ArmLeft.Send;
	ComLink->ArmLeftReceivePort = Config->Network.ArmLeft.Receive;
	ComLink->ArmRightSendPort = Config->Network.ArmRight.Send;
	ComLink->ArmRightReceivePort = Config->Network.ArmRight.Receive;
	ComLink->HeadSendPort = Config->Network.Head.Send;
	ComLink->HeadReceivePort = Config->Network.Head.Receive;

	// Twin link -- independent config (network_twin.json), see ComLink.h.
	ComLink->TwinIP = Config->NetworkTwin.RemoteIP;
	ComLink->TwinSendPort = Config->NetworkTwin.Avatar.Send;
	ComLink->TwinReceivePort = Config->NetworkTwin.Avatar.Receive;
	ComLink->TwinArmLeftSendPort = Config->NetworkTwin.ArmLeft.Send;
	ComLink->TwinArmLeftReceivePort = Config->NetworkTwin.ArmLeft.Receive;
	ComLink->TwinArmRightSendPort = Config->NetworkTwin.ArmRight.Send;
	ComLink->TwinArmRightReceivePort = Config->NetworkTwin.ArmRight.Receive;
	ComLink->TwinHeadSendPort = Config->NetworkTwin.Head.Send;
	ComLink->TwinHeadReceivePort = Config->NetworkTwin.Head.Receive;
	ComLink->LocalIP   = Config->Network.LocalOrchestratorIP;
	ComLink->LocalPort = Config->Network.LocalOrchestratorPort;

	FReceiverConfig GstConfig;
	GstConfig.Port = Config->Stream.Port;
	GstConfig.FeedbackPort = Config->Stream.FeedbackPort;
	GstConfig.SenderIP         = Config->Stream.RemoteIP;
	GstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
	GstConfig.StatusPort       = Config->Stream.StatusPort;
	GstConfig.JitterBufferMs   = Config->Stream.JitterBufferMs;
	VideoFeed->RegisterSource(kAvatarMainSourceName, MakeUnique<FGStreamerSource>(GstConfig));
	VideoFeed->SetStereoMode(Config->Stream.bStereo);

	// Optional twin head-cam as a second main-view source. The avatar is registered
	// first so it stays the default.
	if (Config->Stream.bHasTwinStream) {
		FReceiverConfig TwinGstConfig;
		TwinGstConfig.Port             = Config->Stream.TwinStream.Port;
		TwinGstConfig.FeedbackPort     = Config->Stream.TwinStream.FeedbackPort;
		// The twin may run on a different host; an empty TwinRemoteIP falls back to
		// the avatar's RemoteIP.
		TwinGstConfig.SenderIP = Config->Stream.TwinRemoteIP.IsEmpty()
			? Config->Stream.RemoteIP
			: Config->Stream.TwinRemoteIP;
		TwinGstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
		TwinGstConfig.StatusPort       = Config->Stream.TwinStream.StatusPort;
		TwinGstConfig.JitterBufferMs   = Config->Stream.JitterBufferMs;
		VideoFeed->RegisterSource(Config->Stream.TwinStream.Name, MakeUnique<FGStreamerSource>(TwinGstConfig));

		// Cached for the viewmodeButton handler in UpdateStateMachine, which
		// runs long after this BeginPlay-local Config pointer is gone.
		bHasTwinMainStream_  = true;
		TwinMainStreamKey_   = Config->Stream.TwinStream.Name;
		TwinMainStreamLabel_ = Config->Stream.TwinStream.Name;
	}
	GhostOverlay->SetStereoMode(Config->Stream.bStereo);

	if (Config->Stream.Audio.bEnabled) {
		FAudioReceiverConfig AudioCfg;
		AudioCfg.Port         = Config->Stream.Audio.Port;
		AudioCfg.OutputDevice = Config->Stream.Audio.OutputDevice;
		AudioCfg.bSyncToVideo = Config->Stream.Audio.bSyncToVideo;
		AudioCfg.ExtraDelayMs = Config->Stream.Audio.ExtraDelayMs;
		AudioCfg.GainDb       = static_cast<float>(Config->Stream.Audio.GainDb);
		AudioCfg.SafetyMarginMs = Config->Stream.Audio.SafetyMarginMs;
		AudioCfg.OutputBufferMs = Config->Stream.Audio.OutputBufferMs;
		AudioRx_ = MakeUnique<FGStreamerAudioReceiver>();
		if (!AudioRx_->Start(AudioCfg)) AudioRx_.Reset();
		bAudioLog_ = AudioRx_.IsValid() && Config->Stream.Audio.bLog;
	}
	bOverlayEnabled_ = Config->Overlay.bEnabled;
	GhostOverlay->SetGhostEnabled(bOverlayEnabled_);

	Super::BeginPlay();

	GhostOverlay->SetGhostVisible(bOverlayEnabled_);

	if (bOverlayEnabled_) {
		LeftGraspIndicator->Initialize(GhostOverlay, ComLink, 0);
		RightGraspIndicator->Initialize(GhostOverlay, ComLink, 1);

		LeftWorkspaceBoundary->Initialize(GhostOverlay, 0);
		RightWorkspaceBoundary->Initialize(GhostOverlay, 1);
		if (Config->Overlay.bIntentHighlight) {
			IntentHighlight->RadiusCm = Config->Overlay.IntentHighlightRadiusCm;
			IntentHighlight->GoalRadiusCm = Config->Overlay.IntentHighlightGoalRadiusCm;
			IntentHighlight->GoalLiftM = Config->Overlay.IntentHighlightGoalLiftM;
			IntentHighlight->MinProb  = Config->Overlay.IntentHighlightMinProb;
			IntentHighlight->HoldS    = Config->Overlay.IntentHighlightHoldS;
			IntentHighlight->Tint     = Config->Overlay.IntentHighlightTint;
			IntentHighlight->Initialize(GhostOverlay);
		}
		if (Config->Overlay.bAlignmentPanel) {
			for (UAlignmentPanelComponent* Panel : { LeftAlignmentPanel.Get(), RightAlignmentPanel.Get() }) {
				Panel->SizeCm       = Config->Overlay.AlignmentPanelSizeCm;
				Panel->SideOffsetCm = Config->Overlay.AlignmentPanelSideOffsetCm;
				Panel->UpOffsetCm   = Config->Overlay.AlignmentPanelUpOffsetCm;
				Panel->FollowTauS   = Config->Overlay.AlignmentPanelFollowTauS;
				Panel->bAutoScale   = Config->Overlay.bAlignmentPanelAutoScale;
				Panel->bViewAligned = Config->Overlay.bAlignmentPanelViewAligned;
			}
			LeftAlignmentPanel->Initialize(GhostOverlay, 0);
			RightAlignmentPanel->Initialize(GhostOverlay, 1);
		}
		if (Config->Overlay.bGraspRuler) {
			const auto& O = Config->Overlay;
			FGraspRulerSettings RS;
			RS.TableZ = O.GraspRulerTableZ;
			RS.XAtY = O.GraspRulerXAtY;
			RS.XFrom = O.GraspRulerXFrom;
			RS.XTo = O.GraspRulerXTo;
			RS.XOuter = O.GraspRulerXOuter >= 0.f ? 1.f : -1.f;
			RS.YAtX = O.GraspRulerYAtX;
			RS.YFrom = O.GraspRulerYFrom;
			RS.YTo = O.GraspRulerYTo;
			RS.YOuter = O.GraspRulerYOuter >= 0.f ? 1.f : -1.f;
			RS.TapeX = O.GraspRulerTapeX;
			RS.TapeY = O.GraspRulerTapeY;
			RS.TapeHeight = O.GraspRulerTapeHeightM;
			RS.BandWidth = O.GraspRulerBandWidthM;
			RS.TapeBandWidth = O.GraspRulerTapeWidthM;
			RS.LoupeGain = O.GraspRulerLoupeGain;
			RS.LoupeHalfRange = O.GraspRulerLoupeHalfRangeM;
			RS.BaseAlpha = O.GraspRulerBaseAlpha;
			RS.UVBits = O.GraspRulerUVBits;
			LeftGraspRuler->Initialize(GhostOverlay, RS, true);
			RightGraspRuler->Initialize(GhostOverlay, RS, false);
		}
	}

	if (Config->Stream.bStereo) {
		VideoFeed->SetGhostTextures(GhostOverlay->GetRenderTargetLeft(), GhostOverlay->GetRenderTargetRight());
	}

	VoiceAnnotator->OnAnnotationReceived.AddUObject(this, &AOperatorPawn::HandleVoiceAnnotation);
	ComLink->OnArmFault.AddUObject(this, &AOperatorPawn::HandleArmFault);

	if (APlayerController* PC = Cast<APlayerController>(GetController())) {
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
			Subsystem->AddMappingContext(InputMappingContext, 0);
		}
	}

	LeftTracked->bDrawDebugRay = true;
	RightTracked->bDrawDebugRay = true;

	if (Config->Hud.Placement == TEXT("world"))      HudPlacement_ = EHudPlacement::WorldLocked;
	else if (Config->Hud.Placement == TEXT("head"))  HudPlacement_ = EHudPlacement::HeadLocked;
	else HudPlacement_ = bLookAroundEnabled_ ? EHudPlacement::WorldLocked : EHudPlacement::HeadLocked;
	if (HudPlacement_ == EHudPlacement::WorldLocked && !bLookAroundEnabled_) {
		UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: HUD placement world needs look-around -- frame stays head-locked"));
		HudPlacement_ = EHudPlacement::HeadLocked;
	}

	const bool bWorldHud = HudPlacement_ == EHudPlacement::WorldLocked;
	FrameSize_        = FVector2D(Config->Hud.UIWidgetWidth, Config->Hud.UIWidgetHeight);
	HudFrameDistance_ = Config->Hud.UIPlaneDistance;

	UIBinder->Initialize(UIWidgetClass, VRCamera, FrameSize_, HudFrameDistance_, 2);
	Hud_.Frame = UIBinder;
	if (AlertWidgetClass && AlertBinder) {
		AlertBinder->bHideLayerWhenEmpty = true;
		AlertBinder->Initialize(AlertWidgetClass, VRCamera,
			FVector2D(Config->Hud.AlertsWidgetWidth, Config->Hud.AlertsWidgetHeight), Config->Hud.AlertsPlaneDistance, 4);
		UIBinder->SetOccluder(AlertBinder);
		Hud_.Alerts = AlertBinder;
	}
	Hud_.BuildMirrors();
	Hud_.SetStateDriven(FName("startButton"));
	Hud_.SetStateDriven(FName("engageButton"));
	ApplyControlPanelSet(false);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: HUD frame %.0fx%.0f, %s, near control panel %s, alerts %s"),
		FrameSize_.X, FrameSize_.Y,
		bWorldHud ? TEXT("world-locked while ENGAGED") : TEXT("always head-locked"),
		UIBinder->HasWidget(FName("system_control_tray_near")) ? TEXT("found") : TEXT("not found"),
		Hud_.Alerts ? TEXT("loaded") : TEXT("not loaded"));
	ResolveAuthorityGlyphs();
	AuditWidgetNames();
	PiPBaseSlotPos_ = Hud_.GetWidgetSlotPosition(FName("pip_canvas"));
	PiPNormalSize_  = Hud_.GetWidgetSize(FName("pip_canvas"));
	PiPCurrentSize_ = PiPNormalSize_;
	Hud_.SetVisibility(FName("statsPanel"), false);
	Hud_.SetVisibility(FName("pip_canvas"), false);
	Hud_.SetVisibility(FName("cameraMenu"), false);
	Hud_.SetVisibility(FName("resetMenu"), false);
	Hud_.SetVisibility(FName("episodeAnnotationCanvas"), false);
	Hud_.SetVisibility(FName("settings_canvas"), false);
	// Banners start hidden; the Tick handlers show them once their feed has been
	// heard from.
	Hud_.SetVisibility(FName("fault_status_canvas"), false);
	Hud_.SetVisibility(FName("armStaleInfo"), false);
	Hud_.SetVisibility(FName("videoLostInfo"), false);
	// Intervention UI stays hidden until the session is armed. Both widgets follow
	// bInterventionArmed_.
	Hud_.SetVisibility(FName("interventionPanel"),     bInterventionArmed_);
	Hud_.SetVisibility(FName("authority_pill_canvas"), bInterventionArmed_);
	// Start from a known visual state rather than the UMG default brush.
	Hud_.SetButtonToggled(FName("muteButton"), bSoundMuted_);
	SoundFeedback->SetMuted(bSoundMuted_);

	// Main-view avatar/twin label. Needs a TextBlock named viewmode_label; SetText
	// no-ops if it is missing.
	if (Config->Stream.bHasTwinStream)
		Hud_.SetText(FName("viewmode_label"), *kAvatarMainLabel); // avatar is always the initial active source

	for (const FPiPStreamConfig& S : Config->Stream.PiPStreams) {
		TUniquePtr<IVideoSource> Src;
		if (S.bLocalPreview) {
			Src = MakeUnique<FLocalPreviewSource>(S.Port);
		} else {
			FReceiverConfig Cfg;
			Cfg.Port             = S.Port;
			Cfg.FeedbackPort     = S.FeedbackPort;
			Cfg.SenderIP         = Config->Stream.RemoteIP;
			Cfg.ReportIntervalMs = Config->Stream.ReportIntervalMs;
			Cfg.StatusPort       = S.StatusPort;
			Cfg.JitterBufferMs   = Config->Stream.JitterBufferMs;
			Src = MakeUnique<FGStreamerSource>(Cfg);
		}
		Src->Initialize();
		Src->Start();
		PiPSources_.Add(MoveTemp(Src));
		PiPTextures_.Add(nullptr);
		PiPSourceNames_.Add(S.Name);
	}

	// TWIN in the PiP menu reuses the running main-view twin decode instead of
	// opening a second receiver on the same port, so it is only added to the menu
	// names. Lowercase to match the other PiP entries.
	if (bHasTwinMainStream_) {
		TwinPiPEntryName_ = TwinMainStreamLabel_.ToLower();
		PiPSourceNames_.Add(TwinPiPEntryName_);
	}

	bWristViewEnabled_ = Config->Hud.bWristViewEnabled;
	WristViewMode_     = Config->Hud.WristViewMode;
	if (bWristViewEnabled_) {
		FWristViewSettings WS;
		WS.WidthCm      = Config->Hud.WristViewWidthCm;
		WS.GapCm        = Config->Hud.WristViewGapCm;
		WS.UpCm         = Config->Hud.WristViewUpCm;
		WS.FollowTauS   = Config->Hud.WristViewFollowTauS;
		WS.ExpandScale  = Config->Hud.WristViewExpandScale;
		WS.ShrinkDelayS = Config->Hud.WristViewShrinkDelayS;
		for (uint8 Arm = 0; Arm < 2; ++Arm) {
			const FString StreamName = Config->Hud.WristViewStreams.IsValidIndex(Arm) ? Config->Hud.WristViewStreams[Arm] : FString();
			const int32 Idx = StreamName.IsEmpty() ? INDEX_NONE : PiPSourceNames_.IndexOfByKey(StreamName);
			WristStreamIdx_[Arm] = (Idx != INDEX_NONE && PiPSources_.IsValidIndex(Idx)) ? Idx : INDEX_NONE;
			if (UWristViewComponent* V = WristView(Arm)) V->Initialize(VRCamera, 3, Arm == 0 ? -1.f : 1.f, WS);
			UE_LOG(LogTemp, Log, TEXT("OperatorPawn: wrist view arm %d -> stream \"%s\" %s (mode %s)"),
				Arm, *StreamName, WristStreamIdx_[Arm] != INDEX_NONE ? TEXT("found") : TEXT("NOT FOUND (enable it in stream.json pip_streams)"), *WristViewMode_);
		}
	}

	const float LatencyWarn = Config->Hud.LatencyWarningMs;
	Hud_.BindPlot(FName("videoLatencyPlot"), LatencyHistory.GetSamplesPtr(), nullptr, LatencyHistory.Capacity(), LatencyHistory.GetHeadPtr(), 0.0f, LatencyWarn * 2.0f);
	Hud_.SetPlotThreshold(FName("videoLatencyPlot"), LatencyWarn);

	const float DataLatencyWarn = Config->Hud.DataLatencyWarningMs;
	Hud_.BindPlot(FName("dataLatencyPlot"), JitterHistory.GetSamplesPtr(), nullptr, JitterHistory.Capacity(), JitterHistory.GetHeadPtr(), 0.0f, DataLatencyWarn * 2.0f);
	Hud_.SetPlotThreshold(FName("dataLatencyPlot"), DataLatencyWarn);

	Hud_.BindPlot(FName("dataFpsPlot"), FpsHistory.GetSamplesPtr(), nullptr, FpsHistory.Capacity(), FpsHistory.GetHeadPtr(), 0.0f, 60.0f);
	Hud_.SetPlotThreshold(FName("dataFpsPlot"), 30.0f);

	// CPU and GPU utilization — 0-100%, threshold at 80%.
	// GPU is normalised against a 90 Hz VR frame budget (11.11 ms = 100%).
	Hud_.BindPlot(FName("dataCpuPlot"), CpuHistory.GetSamplesPtr(), nullptr, CpuHistory.Capacity(), CpuHistory.GetHeadPtr(), 0.0f, 100.0f);
	Hud_.SetPlotThreshold(FName("dataCpuPlot"), 80.0f);
	Hud_.BindPlot(FName("dataGpuPlot"), GpuHistory.GetSamplesPtr(), nullptr, GpuHistory.Capacity(), GpuHistory.GetHeadPtr(), 0.0f, 100.0f);
	Hud_.SetPlotThreshold(FName("dataGpuPlot"), 90.0f);  // GPU utilisation %

	Hud_.BindPlot(FName("dataGpuTempPlot"), GpuTempHistory.GetSamplesPtr(), nullptr, GpuTempHistory.Capacity(), GpuTempHistory.GetHeadPtr(), 0.0f, 100.0f);
	Hud_.SetPlotThreshold(FName("dataGpuTempPlot"), 83.0f);  // GPU temp °C — throttle onset

	// Authority echo from the avatar, sent on edges, including changes the avatar
	// makes on its own (staleness watchdog). Runs on the receive thread, so atomics
	// only.
	ComLink->RegisterHandler("authority_state", [this](const FReliableEnvelope& Env) {
		std::map<std::string, msgpack::object> Fields;
		Env.payload.convert(Fields);
		auto DevIt  = Fields.find("device");
		auto AuthIt = Fields.find("authority");
		if (DevIt == Fields.end() || AuthIt == Fields.end()) return;

		const std::string Device = DevIt->second.as<std::string>();
		const uint8 Raw = AuthIt->second.as<uint8_t>();
		if (Device == "arm_left")       LeftAuthority_.Store(Raw);
		else if (Device == "arm_right") RightAuthority_.Store(Raw);

		// Anything but UNSET means an intervention session is already running. See
		// MaybeAutoArmIntervention.
		if (Raw != static_cast<uint8>(EControlAuthority::Unset))
			bAuthorityLiveSeen_.store(true, std::memory_order_relaxed);
	});

	// The orchestrator's policy readouts, relayed by the avatar. Receive thread,
	// atomics only.
	ComLink->RegisterHandler("policy_status", [this](const FReliableEnvelope& Env) {
		std::map<std::string, msgpack::object> Fields;
		Env.payload.convert(Fields);
		auto InfIt   = Fields.find("inference_ms");
		auto AgreeIt = Fields.find("agree");
		if (InfIt != Fields.end())   PolicyInferenceMs_.store(InfIt->second.as<float>());
		if (AgreeIt != Fields.end()) PolicyAgree_.store(AgreeIt->second.as<float>());
		PolicyStatusTime_.store(FPlatformTime::Seconds());
	});

	ComLink->RegisterHandler("device_event", [this](const FReliableEnvelope& Env) {
		std::map<std::string, msgpack::object> Fields;
		Env.payload.convert(Fields);
		auto DevIt = Fields.find("device");
		auto EvtIt = Fields.find("event");
		if (DevIt == Fields.end() || EvtIt == Fields.end()) return;

		std::string Device = DevIt->second.as<std::string>();
		std::string Event = EvtIt->second.as<std::string>();

		if (Event == "reset_complete") {
			if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESET_COMPLETE device=%s"), UTF8_TO_TCHAR(Device.c_str())));
			if (Device == "arm_left") {
				LeftTracked->CaptureOrigin();
				if (CommandThread_) CommandThread_->RequestCaptureOrigin(0);
				SendArmResume("arm_left");
				LeftArmResetState_ = EArmResetState::Idle;
				if (GhostOverlay) {
					if (ComLink->HasNewArmState(0)) {
						ArmStateMsg S = ComLink->ReadArmState(0);
						GhostOverlay->SeedIntentPose(0, S.position, S.quaternion);
					} else {
						GhostOverlay->UnseedIntentPose(0);
					}
				}
			}
			if (Device == "arm_right") {
				RightTracked->CaptureOrigin();
				if (CommandThread_) CommandThread_->RequestCaptureOrigin(1);
				SendArmResume("arm_right");
				RightArmResetState_ = EArmResetState::Idle;
				if (GhostOverlay) {
					if (ComLink->HasNewArmState(1)) {
						ArmStateMsg S = ComLink->ReadArmState(1);
						GhostOverlay->SeedIntentPose(1, S.position, S.quaternion);
					} else {
						GhostOverlay->UnseedIntentPose(1);
					}
				}
			}
			UpdateButtonStates();
			SoundFeedback->Play(ESoundType::Transition);
		}
		});

	UpdateButtonStates();

	float AspectRatio = 1280.f / 720.f;
	if (VideoFeed->IsAngularMapping())
		FGazeProjection::ComputeQuadSize(VideoFeed->PlaneDistance, 1.f, AspectRatio, VideoQuadWidth_, VideoQuadHeight_, Config->Overlay.StereoCaptureFOV);
	else
		FGazeProjection::ComputeQuadSize(VideoFeed->PlaneDistance, VideoFeed->FOVCoverage, AspectRatio, VideoQuadWidth_, VideoQuadHeight_, VideoFeed->HmdHFovDeg);
	if (Config->Stream.bVideoLogEnabled && (Config->Stream.bVideoLogRaw || Config->Stream.bVideoLogAttention)) {
		FActorSpawnParameters Params;
		Params.Owner = this;
		VideoLogger_ = GetWorld()->SpawnActor<AVideoLogger>(AVideoLogger::StaticClass(), FTransform::Identity, Params);
		if (VideoLogger_) {
			VideoLogger_->bEnableRawVideo       = Config->Stream.bVideoLogRaw;
			VideoLogger_->bEnableAttentionVideo = Config->Stream.bVideoLogAttention;
			VideoLogger_->CaptureFPS            = static_cast<float>(Config->Stream.VideoLogFps);
		}
	}

	if (VideoLogger_) {
		FVector2D VideoUVOffset = FVector2D::ZeroVector;
		FVector2D VideoUVSize   = FVector2D::UnitVector;
		if (VideoFeed->IsStereoMode()) {
			VideoUVOffset = FVector2D(0.0f, 0.0f);
			VideoUVSize   = FVector2D(0.5f, 1.0f);
		}
		VideoLogger_->bOperatorView = Config->Stream.bVideoLogOperatorView;

		const float LogPlaneD = VideoFeed->PlaneDistance;
		auto VideoSizeCm = [this]() -> FVector2D { return GetVideoQuadSize(); };
		auto PanelSizeCm = [LogPlaneD](const UWidgetBinder* B, FVector2D Size) -> FVector2D {
			const float Dist = B ? B->GetLayerDistance() : 0.f;
			return Dist > 0.f ? Size * (LogPlaneD / Dist) : FVector2D::ZeroVector;
		};

		VideoLogger_->AddLayerSource([this]() -> UTexture* { return VideoFeed->GetVideoTexture(); }, 0, VideoUVOffset, VideoUVSize, true, false, VideoSizeCm);
		if (VideoFeed->IsStereoMode()) {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return GhostOverlay->IsGhostVisible() ? static_cast<UTexture*>(GhostOverlay->GetRenderTargetLeft()) : nullptr; }, 1, FVector2D::ZeroVector, FVector2D::UnitVector, true, true, VideoSizeCm);
		} else {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return GhostOverlay->IsGhostVisible() ? static_cast<UTexture*>(GhostOverlay->GetRenderTarget()) : nullptr; }, 1, FVector2D::ZeroVector, FVector2D::UnitVector, true, true, VideoSizeCm);
		}
		VideoLogger_->AddLayerSource(
			[this]() -> UTexture* { return UIBinder ? static_cast<UTexture*>(UIBinder->GetRenderTarget()) : nullptr; },
			2, FVector2D::ZeroVector, FVector2D::UnitVector, true, false,
			[this, PanelSizeCm]() -> FVector2D { return UIBinder ? PanelSizeCm(UIBinder, UIBinder->GetQuadSize()) : FVector2D::ZeroVector; },
			[this]() -> bool { return !(UIBinder && UIBinder->GetPlacement() == EHudPlacement::HeadLocked && bVideoLayersWorldLocked_); });
		for (uint8 Arm = 0; Arm < 2; ++Arm) {
			if (!bWristViewEnabled_ || WristStreamIdx_[Arm] == INDEX_NONE) continue;
			VideoLogger_->AddLayerSource(
				[this, Arm]() -> UTexture* { UWristViewComponent* V = WristView(Arm); return (V && V->IsShown()) ? static_cast<UTexture*>(V->GetTexture()) : nullptr; },
				3, FVector2D::ZeroVector, FVector2D::UnitVector, true, false,
				[this, Arm]() -> FVector2D { UWristViewComponent* V = WristView(Arm); return V ? V->GetSizeCm() : FVector2D::ZeroVector; },
				nullptr,
				[this, Arm]() -> FVector2D { UWristViewComponent* V = WristView(Arm); return V ? FVector2D(V->GetCenterCm().X, -V->GetCenterCm().Y) : FVector2D::ZeroVector; });
		}
		if (Hud_.Alerts)
			VideoLogger_->AddLayerSource(
				[this]() -> UTexture* { return (AlertBinder && AlertBinder->IsLayerShown()) ? static_cast<UTexture*>(AlertBinder->GetRenderTarget()) : nullptr; },
				4, FVector2D::ZeroVector, FVector2D::UnitVector, false, false,
				[this, PanelSizeCm]() -> FVector2D { return AlertBinder ? PanelSizeCm(AlertBinder, AlertBinder->GetQuadSize()) : FVector2D::ZeroVector; },
				[this]() -> bool { return !bVideoLayersWorldLocked_; });

		{
			const FVector2D Q = GetVideoQuadSize();
			FVector2D Half = Q * 0.5;
			auto Grow = [&Half](FVector2D SizeCm) { Half.X = FMath::Max(Half.X, SizeCm.X * 0.5); Half.Y = FMath::Max(Half.Y, SizeCm.Y * 0.5); };
			Grow(PanelSizeCm(UIBinder, FrameSize_));
			if (Hud_.Alerts) Grow(PanelSizeCm(AlertBinder, AlertBinder->GetQuadSize()));
			const float VideoWidthPx = 1280.f;
			if (Q.X > 0.0) VideoLogger_->SetCanvas(Half, VideoWidthPx / static_cast<float>(Q.X), VideoWidthPx);
		}

		VideoLogger_->QuadWidth = VideoQuadWidth_;
		VideoLogger_->QuadHeight = VideoQuadHeight_;
		VideoLogger_->PlaneDistance = VideoFeed->PlaneDistance;

		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: VideoLogger spawned"));
	}

	Logger_ = MakeUnique<FTeleOpLogger>();
	FString SessionDir = Logger_->Open(FPaths::ProjectDir() / LogBaseDirectory);

	if (Config->bCommandThreadEnabled) {
		CommandThread_ = MakeUnique<FTeleopCommandThread>(ComLink, Logger_.Get(), CommandThreadRateHz);
		CommandThread_->SetSwapHands(bSwapControllers_);
		CommandThread_->StartThread();
	}
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: logging to %s"), *SessionDir);
	NudgeReceiver->Open(Config->Network.NudgePort, LeftTracked, RightTracked, SoundFeedback, Logger_.Get(),
	                    Config->Overlay.bIntentHighlight ? IntentHighlight.Get() : nullptr);
	if (bOverlayEnabled_ && Config->Overlay.bAlignmentPanel)
		NudgeReceiver->SetAlignmentPanels(LeftAlignmentPanel, RightAlignmentPanel);
	if (bOverlayEnabled_ && Config->Overlay.bGraspRuler)
		NudgeReceiver->SetGraspRulers(LeftGraspRuler, RightGraspRuler);

	if (VideoLogger_) {
		VideoLogger_->StartLogging(SessionDir);
	}
	if (AudioRx_ && bAudioLog_) AudioRx_->StartLog(SessionDir);
	if (Config->Network.EpisodeEventPort > 0) {
		EpisodeEvents_ = MakeUnique<FEpisodeEventReceiver>();
		if (!EpisodeEvents_->Open(Config->Network.EpisodeEventPort)) EpisodeEvents_.Reset();
	}

	SessionStartTime_ = FPlatformTime::Seconds();
}

// Episode boundaries come from the avatar so every producer logs into the same <session_id>/<episode>/ folder.
void AOperatorPawn::PollEpisodeEvents() {
	if (!EpisodeEvents_) return;
	FEpisodeEvent Ev;
	while (EpisodeEvents_->Pop(Ev)) {
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("EPISODE_EVENT type=%s session=%s index=%d reason=%s"),
			*Ev.Type, *Ev.SessionId, Ev.EpisodeIndex, *Ev.Reason));
		if (Ev.Type != TEXT("episode_start") || !Logger_) continue;
		const FString Dir = Logger_->Rotate(FPaths::ProjectDir() / LogBaseDirectory / Ev.EpisodeDir());
		if (VideoLogger_) {
			VideoLogger_->StopLogging(TEXT("episode_start"));
			VideoLogger_->StartLogging(Dir);
		}
		if (AudioRx_ && bAudioLog_) AudioRx_->StartLog(Dir);
		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: episode %d of session %s -> %s"), Ev.EpisodeIndex, *Ev.SessionId, *Dir);
	}
}


void AOperatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	FlushRenderingCommands();
	for (auto& Src : PiPSources_) if (Src) Src->Stop();
	if (VideoLogger_) VideoLogger_->StopLogging(TEXT("EndPlay"));
	if (AudioRx_) { AudioRx_->Shutdown(); AudioRx_.Reset(); }
	if (CommandThread_) CommandThread_->StopThread();
	if (EpisodeEvents_) { EpisodeEvents_->Close(); EpisodeEvents_.Reset(); }
	if (Logger_) Logger_->Close();
	if (bRecordingActive_) SendRecordingSignal(false);
	if (RecordingSocket_) RecordingSocket_->Close();
	Super::EndPlay(EndPlayReason);
}

void AOperatorPawn::SendRecordingSignal(bool bStart) {
	if (!RecordingSocket_) return;
	const uint64 TsNs = FTeleOpLogger::NowNs();
	const FString Msg = FString::Printf(TEXT("{\"cmd\":\"%s\",\"ts_ns\":%llu}"),
		bStart ? TEXT("start") : TEXT("stop"), TsNs);
	FTCHARToUTF8 Converted(*Msg);
	RecordingSocket_->Send(Converted.Get(), Converted.Length());
}


void AOperatorPawn::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);

	PollEpisodeEvents();
	if (AudioRx_ && VideoFeed->GetActiveSourceName() == kAvatarMainSourceName) {
		const uint64 FrameId = VideoFeed->GetLastFrameId();
		if (FrameId != LastAudioSyncFrameId_) {
			LastAudioSyncFrameId_ = FrameId;
			AudioRx_->ReportVideoSenderTimeNs(VideoFeed->GetSenderTimeNs());
		}
	}
	UpdateLookAround(DeltaTime);
	UpdateStateMachine();

	FVideoSourceStats Stats = VideoFeed->GetStreamStats();
	LatencyHistory.Push(Stats.OneWayLatencyMs);
	// Same getter as the info-bar pill: network one-way delay, 0 while no commands
	// are flowing.
	JitterHistory.Push(ComLink->GetNetworkDelayMs());
	LossHistory.Push(Stats.PacketLossPercent);
	FpsHistory.Push(static_cast<float>(Stats.CurrentFPS));

	if (++PerfSampleCounter_ >= 15) {
		PerfSampleCounter_ = 0;
		CpuHistory.Push(SampleSystemCpuPercent());
		float GpuUtilPct = 0.f, GpuTempC = 0.f;
		if (SampleGpuNvml(GpuUtilPct, GpuTempC)) {
			GpuHistory.Push(GpuUtilPct);
			GpuTempHistory.Push(GpuTempC);
		} else {
			constexpr float kVrFrameBudgetMs = 1000.f / 90.f;
			const float GpuMs = FPlatformTime::ToMilliseconds(GGPUFrameTime);
			GpuHistory.Push(FMath::Clamp(GpuMs / kVrFrameBudgetMs * 100.f, 0.f, 150.f));
		}
	}

	const FGazeData& GazeData = Gaze->GetGazeData();
	Hud_.SetGazeInput(GazeData);

	if (ComLink->GetAvatarState() == ESysState::Engaged) SetDeviceColor(FName("avatar_torso"), FLinearColor::Green);
	else if (OperatorState_ == ESysState::Homing || OperatorState_ == ESysState::Awaiting)
		SetDeviceColor(FName("avatar_torso"), kWarnAmber);
	else SetDeviceColor(FName("avatar_torso"), FLinearColor::Red);

	SetDeviceColor(FName("avatar_eye"), VideoFeed->IsReceiving() ? FLinearColor::Green : FLinearColor::Red);
	SetDeviceColor(FName("avatar_left_arm"), ComLink->GetArmRemoteState(0) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	SetDeviceColor(FName("avatar_right_arm"), ComLink->GetArmRemoteState(1) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	SetDeviceColor(FName("avatar_head"), ComLink->IsHeadAlive() ? FLinearColor::Green : FLinearColor::Red);
	Hud_.SetVisibility(FName("videoLostInfo"), !VideoFeed->IsReceiving());

	// --- stale-state alarm ---
	// The 200 Hz state thread keeps sending even when the 1 kHz control thread has
	// faulted, so every link indicator stays green. sample_time_ns is stamped by the
	// control thread and stops advancing, so alarm audibly on its age.
	{
		const bool bStale = ComLink->IsArmStateStale(0, kArmStateStaleWarnMs);
		Hud_.SetVisibility(FName("armStaleInfo"), bStale);
		if (bStale && ComLink->GetArmRemoteState(0) != SysState::FAULT) {
			SetDeviceColor(FName("avatar_left_arm"), kWarnAmber);
		}
		// Edge-triggered: one warning per stale episode, not one per tick.
		if (bStale && !bArmStateWasStale_) {
			SoundFeedback->Play(ESoundType::Warning);
			if (Logger_) {
				Logger_->LogEvent(FString::Printf(
					TEXT("ARM_STATE_STALE side=left age_ms=%.1f remote_state=%d msg_rate_hz=%.1f"),
					ComLink->GetArmStateAgeMs(0),
					static_cast<int32>(ComLink->GetArmRemoteState(0)),
					ComLink->GetArmMsgRateHz(0)));
			}
		} else if (!bStale && bArmStateWasStale_ && Logger_) {
			Logger_->LogEvent(TEXT("ARM_STATE_FRESH side=left"));
		}
		bArmStateWasStale_ = bStale;
	}

	// --- remote fault ---
	// An explicit FAULT reported by the avatar, complementing the staleness alarm
	// above.
	{
		bool bNewFault      = false;
		bool bFaultLatched  = false;
		for (int32 Index = 0; Index < 2; ++Index) {
			const int32 FaultValue = PendingArmFault_[Index].Exchange(-1);
			if (FaultValue >= 0) {
				bNewFault = true;
				if (Logger_) {
					Logger_->LogEvent(FString::Printf(
						TEXT("ARM_REMOTE_FAULT side=%s fault_code=%d remote_state=%d"),
						Index == 0 ? TEXT("left") : TEXT("right"),
						FaultValue,
						static_cast<int32>(ComLink->GetArmRemoteState(static_cast<uint8>(Index)))));
				}
				UE_LOG(LogTemp, Error, TEXT("OperatorPawn: remote arm %d reported FAULT (code %d)"),
					Index, FaultValue);
			}
			// Reset watchdog: a recovery that re-faults never sends reset_complete
			// and would leave the arm stuck in Recovering. The re-fault check waits a
			// grace period, since the avatar stays in FAULT briefly after the
			// request.
			EArmResetState& ResetState = (Index == 0) ? LeftArmResetState_ : RightArmResetState_;
			if (ResetState != EArmResetState::Idle) {
				const double NowS = FPlatformTime::Seconds();
				if (ArmResetRequestTime_[Index] <= 0.0) {
					ArmResetRequestTime_[Index] = NowS;
				}
				const double Elapsed = NowS - ArmResetRequestTime_[Index];
				const bool bRefaulted = Elapsed > kArmResetGraceSec
					&& ComLink->GetArmRemoteState(static_cast<uint8>(Index)) == SysState::FAULT;
				const bool bTimedOut = Elapsed > kArmResetTimeoutSec;
				if (bRefaulted || bTimedOut) {
					ResetState = EArmResetState::Idle;
					ArmResetRequestTime_[Index] = 0.0;
					// Refresh the widgets so an aborted reset no longer shows as
					// locked.
					UpdateButtonStates();
					if (Logger_) {
						Logger_->LogEvent(FString::Printf(
							TEXT("ARM_RESET_ABORTED side=%s reason=%s elapsed_s=%.1f"),
							Index == 0 ? TEXT("left") : TEXT("right"),
							bRefaulted ? TEXT("refaulted") : TEXT("timeout"), Elapsed));
					}
					UE_LOG(LogTemp, Warning,
						TEXT("OperatorPawn: reset of arm %d aborted after %.1f s (%s) - button re-armed"),
						Index, Elapsed, bRefaulted ? TEXT("arm faulted again") : TEXT("no reset_complete"));
				}
			} else {
				ArmResetRequestTime_[Index] = 0.0;
			}

			if (ComLink->GetArmRemoteState(static_cast<uint8>(Index)) == SysState::FAULT) {
				bFaultLatched = true;
			}
		}
		if (bNewFault) {
			SoundFeedback->Play(ESoundType::Warning);
		}
		// The widget is called fault_status_canvas; armFaultInfo is kept in case the
		// asset adds it.
		Hud_.SetVisibility(FName("fault_status_canvas"), bFaultLatched);
		Hud_.SetVisibility(FName("armFaultInfo"), bFaultLatched);
		if (bFaultLatched) {
			const bool bLeft  = ComLink->GetArmRemoteState(0) == SysState::FAULT;
			const bool bRight = ComLink->GetArmRemoteState(1) == SysState::FAULT;
			Hud_.SetText(FName("fault_status_text"),
				(bLeft && bRight) ? TEXT("BOTH ARMS FAULTED")
				: bLeft           ? TEXT("LEFT ARM FAULTED")
				                  : TEXT("RIGHT ARM FAULTED"));
		}
	}

	const bool bGazeFresh = Gaze->IsGazeFresh();
	SetDeviceColor(FName("operator_eye"), bGazeFresh ? FLinearColor::Green : FLinearColor::Red);
	Hud_.SetVisibility(FName("gazeOfflineInfo"), !bGazeFresh);
	SetDeviceColor(FName("operator_left_arm"), LeftTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	SetDeviceColor(FName("operator_right_arm"), RightTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	SetDeviceColor(FName("operator_head"), bHMDOriginValid_ ? FLinearColor::Green : FLinearColor::Red);

	ESysState av_state = ComLink->GetAvatarState();
	if (av_state == ESysState::Engaged) SetDeviceColor(FName("operator_torso"), FLinearColor::Green);
	else if (av_state == ESysState::Homing || av_state == ESysState::Awaiting)
		SetDeviceColor(FName("operator_torso"), kWarnAmber);
	else SetDeviceColor(FName("operator_torso"), FLinearColor::Red);
	UpdateDeviceGraphOpacity(DeltaTime);

	static const TArray<FString> HealthLabels = { TEXT("NO SIGNAL"), TEXT("NORMAL"), TEXT("DEGRADED"), TEXT("CRITICAL"), TEXT("STALE") };
	static const TArray<FLinearColor> HealthColors = { FLinearColor::Gray, FLinearColor::Green, FLinearColor::Yellow, FLinearColor::Red, FLinearColor::Gray };

	uint8 HealthIdx = Stats.StreamHealthState;
	if (HealthIdx < HealthLabels.Num()) {
		Hud_.SetText(FName("stream_health_value"), *HealthLabels[HealthIdx]);
		Hud_.SetTextColor(FName("stream_health_value"), HealthColors[HealthIdx]);
	}

	// Every tick and before UpdateAuthority, so the tick that arms the session keeps
	// its clutch edge, and before UpdateInfoBar, so the pill shows this frame's
	// authority.
	MaybeAutoArmIntervention();
	UpdateAuthority();
	UpdatePolicyStats(DeltaTime);
	// After UpdateAuthority, so a release and a resume chord in the same frame apply
	// in order: HOLD, then POLICY.
	PollResumeButton();

	UpdateInfoBar();

	if (!ActivePiPStreamName_.IsEmpty()) {
		if (bHasTwinMainStream_ && ActivePiPStreamName_ == TwinPiPEntryName_) {
			// Shared decode with the main view -- see VideoFeedComponent::
			// UpdateSourceTexture. No separate receiver, no extra decode.
			VideoFeed->UpdateSourceTexture(TwinMainStreamKey_, PiPSharedTwinTexture_);
			const bool bReceiving = VideoFeed->GetSourceStats(TwinMainStreamKey_).bIsReceiving;
			Hud_.SetVisibility(FName("pip_canvas"), bReceiving);
			if (bReceiving && PiPSharedTwinTexture_)
				Hud_.SetImageTexture(FName("pip_image"), PiPSharedTwinTexture_);
		} else {
			const int32 Idx = PiPSourceNames_.IndexOfByKey(ActivePiPStreamName_);
			if (Idx != INDEX_NONE && PiPSources_.IsValidIndex(Idx)) {
				PiPSources_[Idx]->UpdateTexture(PiPTextures_[Idx]);
				const bool bReceiving = PiPSources_[Idx]->GetStats().bIsReceiving;
				Hud_.SetVisibility(FName("pip_canvas"), bReceiving);
				if (bReceiving && PiPTextures_[Idx])
					Hud_.SetImageTexture(FName("pip_image"), PiPTextures_[Idx]);
			}
		}
	}

	if (!ActivePiPStreamName_.IsEmpty() && PiPNormalSize_.X > 0.f) {
		const bool bOver    = Hud_.IsGazeOverWidget(FName("pip_canvas"), PiPGazeInnerMarginPx);
		const bool bFarAway = !Hud_.IsGazeOverWidget(FName("pip_canvas"), PiPGazeOuterMarginPx);
		if (!bPiPExpanded_ && bOver)   { bPiPExpanded_ = true;  PiPDwellTimer_ = 0.f; }
		if (bPiPExpanded_ && bFarAway) { PiPDwellTimer_ += DeltaTime; if (PiPDwellTimer_ >= PiPShrinkDwellTime) bPiPExpanded_ = false; }
		else if (bPiPExpanded_)         { PiPDwellTimer_ = 0.f; }
		const FVector2D Target = bPiPExpanded_ ? PiPNormalSize_ * PiPExpandScale : PiPNormalSize_;
		PiPCurrentSize_.X = FMath::FInterpTo(PiPCurrentSize_.X, Target.X, DeltaTime, PiPLerpSpeed);
		PiPCurrentSize_.Y = FMath::FInterpTo(PiPCurrentSize_.Y, Target.Y, DeltaTime, PiPLerpSpeed);
		// Render-transform scale so all children (background, rim, image) scale together.
		// Pivot is derived from ExpandDirection: (1,1)→top-left fixed, (-1,-1)→bottom-right fixed.
		FVector2D Scale(PiPCurrentSize_.X / FMath::Max(PiPNormalSize_.X, 1.f), PiPCurrentSize_.Y / FMath::Max(PiPNormalSize_.Y, 1.f));
		FVector2D Pivot = FVector2D(0.5f, 0.5f) - PiPExpandDirection * 0.5f;
		Hud_.SetWidgetRenderScale(FName("pip_canvas"), Scale, Pivot);
	}

	UpdateWristViews(DeltaTime);

	bool bLeftGrasping  = LeftTracked->IsGraspHeld();
	bool bRightGrasping = RightTracked->IsGraspHeld();

	if (bLeftGrasping && !bLeftWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, LeftController->GetComponentLocation());
	}
	if (bRightGrasping && !bRightWasGrasping) {
		SoundFeedback->PlayAtLocation(ESoundType::Confirm, RightController->GetComponentLocation());
	}

	if (Logger_) {
		// --- gear change events ---
		float LeftGear  = LeftTracked->GetScaleFactor();
		float RightGear = RightTracked->GetScaleFactor();
		if (LeftGear != PrevLeftGear_) {
			Logger_->LogEvent(FString::Printf(TEXT("GEAR_CHANGE side=left gear=%.1f"), LeftGear));
			PrevLeftGear_ = LeftGear;
			Hud_.SetText(FName("gear_left_value"), FString::Printf(TEXT("%.1f"), LeftGear));
		}
		if (RightGear != PrevRightGear_) {
			Logger_->LogEvent(FString::Printf(TEXT("GEAR_CHANGE side=right gear=%.1f"), RightGear));
			PrevRightGear_ = RightGear;
			Hud_.SetText(FName("gear_right_value"), FString::Printf(TEXT("%.1f"), RightGear));
		}

		// --- clutch transition events ---
		bool bLeftClutch  = LeftTracked->IsFullClutch();
		bool bRightClutch = RightTracked->IsFullClutch();
		if (bLeftClutch != bPrevLeftClutch_) {
			Logger_->LogEvent(FString::Printf(TEXT("CLUTCH side=left state=%s"), bLeftClutch ? TEXT("engaged") : TEXT("disengaged")));
			bPrevLeftClutch_ = bLeftClutch;
		}
		if (bRightClutch != bPrevRightClutch_) {
			Logger_->LogEvent(FString::Printf(TEXT("CLUTCH side=right state=%s"), bRightClutch ? TEXT("engaged") : TEXT("disengaged")));
			bPrevRightClutch_ = bRightClutch;
		}

		// --- grasp transition events ---
		if (bLeftGrasping != bLeftWasGrasping) {
			Logger_->LogEvent(FString::Printf(TEXT("GRASP side=left state=%s"),
				bLeftGrasping ? TEXT("held") : TEXT("released")));
		}
		if (bRightGrasping != bRightWasGrasping) {
			Logger_->LogEvent(FString::Printf(TEXT("GRASP side=right state=%s"),
				bRightGrasping ? TEXT("held") : TEXT("released")));
		}

		// --- stream row ---
		FStreamRow Row;
		Row.TimestampNs   = FTeleOpLogger::NowNs();
		Row.OperatorState = static_cast<uint8>(OperatorState_);

		Row.LeftClutch = LeftTracked->GetClutchFactor();
		Row.LeftGear   = LeftGear;
		Row.LeftGrasp  = bLeftGrasping ? 1.f : 0.f;

		Row.RightClutch = RightTracked->GetClutchFactor();
		Row.RightGear   = RightGear;
		Row.RightGrasp  = bRightGrasping ? 1.f : 0.f;

		// Latest command on the wire (protocol frame, yaw-corrected), sampled at
		// frame rate. command.csv has the full-rate record.
		if (CommandThread_) {
			const FCommandThreadOutput Cmd = CommandThread_->ReadOutput();
			Row.LeftPx = Cmd.Position[0][0];   Row.LeftPy = Cmd.Position[0][1];   Row.LeftPz = Cmd.Position[0][2];
			Row.LeftQw = Cmd.Quaternion[0][0]; Row.LeftQx = Cmd.Quaternion[0][1];
			Row.LeftQy = Cmd.Quaternion[0][2]; Row.LeftQz = Cmd.Quaternion[0][3];

			Row.RightPx = Cmd.Position[1][0];   Row.RightPy = Cmd.Position[1][1];   Row.RightPz = Cmd.Position[1][2];
			Row.RightQw = Cmd.Quaternion[1][0]; Row.RightQx = Cmd.Quaternion[1][1];
			Row.RightQy = Cmd.Quaternion[1][2]; Row.RightQz = Cmd.Quaternion[1][3];

			Row.CommandRateHz  = Cmd.SendRateHz;
			Row.CommandJitterMs = Cmd.LoopJitterMs;
			Row.CommandDuplicatePct = Cmd.DuplicatePct;
		}

		Row.HeadPan  = LastHeadPan_;
		Row.HeadTilt = LastHeadTilt_;

		Row.VideoLatencyMs = Stats.OneWayLatencyMs;
		Row.VideoJitterMs  = Stats.JitterMs;
		Row.VideoLossPct   = Stats.PacketLossPercent;
		Row.VideoFps       = Stats.CurrentFPS;

		Row.DataLatencyMs = ComLink->GetArmStateLatencyMs(0);
		{
			const float NetMs = ComLink->GetNetworkDelayMs();
			Row.NetDelayMs = (NetMs > 0.f) ? NetMs : -1.f;
		}
		Row.DataMsgRateHz = ComLink->GetArmMsgRateHz(0);

		ArmStateMsg LeftState = ComLink->PeekArmState(0);
		Row.LeftGripperWidth = LeftState.gripper_width;
		Row.LeftGripperGraspState = static_cast<uint8>(LeftGraspIndicator->GetDisplayState());

		ArmStateMsg RightState = ComLink->PeekArmState(1);
		Row.RightGripperWidth = RightState.gripper_width;
		Row.RightGripperGraspState = static_cast<uint8>(RightGraspIndicator->GetDisplayState());

		// --- remote health ---
		// The avatar's own state, as opposed to the link health above.
		Row.ArmRemoteState    = static_cast<uint8>(ComLink->GetArmRemoteState(0));
		Row.ArmRemoteFault    = static_cast<uint8>(ComLink->GetArmRemoteFault(0));
		Row.ArmDroppedPackets = static_cast<uint32>(ComLink->GetArmDroppedPackets(0));
		Row.ArmStateAgeMs     = ComLink->GetArmStateAgeMs(0);

		// Which source VideoLatencyMs refers to; avatar and twin latency differ a
		// lot.
		Row.VideoSourceName = VideoFeed ? VideoFeed->GetActiveSourceName() : FString();

		// FrameMs is the game-thread period and therefore the command period.
		// The other three say which thread owns it: whichever is closest to
		// FrameMs is the limiter.
		Row.FrameMs        = static_cast<float>(FApp::GetDeltaTime()) * 1000.f;
		Row.GameThreadMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
		Row.RenderThreadMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		Row.GpuMs          = FPlatformTime::ToMilliseconds(GGPUFrameTime);

		Row.LeftTrackState  = static_cast<uint8>(LeftTracked->GetTrackingState());
		Row.RightTrackState = static_cast<uint8>(RightTracked->GetTrackingState());

		Logger_->WriteStreamRow(Row);
	}

	bLeftWasGrasping  = bLeftGrasping;
	bRightWasGrasping = bRightGrasping;

	if (VideoLogger_ && VideoLogger_->IsLogging()) {
		VideoLogger_->SubmitFrame(BuildFrameBundle());
	}
}

void AOperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}


// ============================================================
// State machine
// ============================================================
void AOperatorPawn::UpdateStateMachine() {
	if (CheckEmergencyStop()) {
		TransitionTo(ESysState::Idle);
		ComLink->SendStateRequest(SysState::IDLE);
		return;
	}

	if (OperatorState_ != ESysState::Offline && !ComLink->IsAvatarAlive()) {
		TransitionTo(ESysState::Offline);
		SoundFeedback->Play(ESoundType::Warning);
		return;
	}

	FName ButtonPressed = Hud_.ConsumePress();
	FName ButtonRejected = Hud_.ConsumeRejection();
	if (ButtonRejected != FName()) {
		SoundFeedback->Play(ESoundType::Reject);
		if (Logger_) Logger_->LogEvent(TEXT("BUTTON_REJECTED button=") + ButtonRejected.ToString());
	}
	if (ButtonPressed != FName() && Logger_) {
		Logger_->LogEvent(TEXT("BUTTON_PRESS button=") + ButtonPressed.ToString());
	}
	ESysState AvatarState = ComLink->GetAvatarState();


	switch (OperatorState_) {
	case ESysState::Offline:
		if (ComLink->IsAvatarAlive() && Gaze->IsTrackerConnected()) {
			if (bPendingVoiceReengage_) {
				ComLink->SendStateRequest(SysState::HOMING);
				TransitionTo(ESysState::Homing);
			} else {
				TransitionTo(ESysState::Idle);
			}
		}
		break;

	case ESysState::Idle:
		if (!ComLink->IsAvatarAlive()) {
			TransitionTo(ESysState::Offline);
		}
		else if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::HOMING);
			TransitionTo(ESysState::Homing);
		}
		else if (AvatarState == ESysState::Homing || AvatarState == ESysState::Awaiting
			|| AvatarState == ESysState::Engaged || AvatarState == ESysState::Paused) {
			static double LastIdleResync = 0.0;
			const double NowS = FPlatformTime::Seconds();
			if (NowS - LastIdleResync > 1.0) {
				LastIdleResync = NowS;
				ComLink->SendStateRequest(SysState::IDLE);
				if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("IDLE_RESYNC avatar_state=%s"), *StateToString(AvatarState)));
			}
		}
		break;

	case ESysState::Homing:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (AvatarState == ESysState::Awaiting) {
			CaptureControllerOrigins();
			if (bPendingVoiceReengage_) {
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				bPendingVoiceReengage_ = false;
				TransitionTo(ESysState::Engaged);
			} else {
				TransitionTo(ESysState::Awaiting);
			}
		}
		break;

	case ESysState::Awaiting:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (bPendingVoiceReengage_ || ButtonPressed == FName("engageButton")) {
			CaptureControllerOrigins();
			ComLink->SendStateRequest(SysState::ENGAGED);
			bAvatarConfirmedEngaged_ = false;
			bPendingVoiceReengage_ = false;
			TransitionTo(ESysState::Engaged);
		}
		break;

	case ESysState::Engaged:
		if (!bAvatarConfirmedEngaged_ && AvatarState == ESysState::Engaged) {
			bAvatarConfirmedEngaged_ = true;
		}

		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			ComLink->SendStateRequest(SysState::PAUSED);
			TransitionTo(ESysState::Paused);
		}
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(0)) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			if (bResetMenuOpen_) {
				Hud_.HideMenu();
				Hud_.SetButtonToggled(FName("resetButton"), false);
				bResetMenuOpen_ = false;
			} else {
				Hud_.ShowResetMenu();
				Hud_.SetButtonToggled(FName("resetButton"), true);
				bResetMenuOpen_ = true;
			}
		}
		else if (ButtonPressed == FName("homeButton") && !bAnnotationPending_) {
			bAnnotationPending_ = true;
			Hud_.SetVisibility(FName("episodeAnnotationCanvas"), true);
		}
		else if (bAvatarConfirmedEngaged_ && AvatarState == ESysState::Awaiting) {
			CaptureControllerOrigins();
			bAvatarConfirmedEngaged_ = false;
			TransitionTo(ESysState::Awaiting);
		}
		else {
			SendArmCommands();
			SendHeadCommand();
			SendGazeSample();
			SendOperatorState();
		}
		break;

	case ESysState::Paused:
		if (ButtonPressed == FName("startButton")) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		}
		else if (ButtonPressed == FName("engageButton")) {
			CaptureControllerOrigins();
			ComLink->SendStateRequest(SysState::ENGAGED);
			bAvatarConfirmedEngaged_ = false;
			TransitionTo(ESysState::Engaged);
		}
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(0)) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			if (bResetMenuOpen_) {
				Hud_.HideMenu();
				Hud_.SetButtonToggled(FName("resetButton"), false);
				bResetMenuOpen_ = false;
			} else {
				Hud_.ShowResetMenu();
				Hud_.SetButtonToggled(FName("resetButton"), true);
				bResetMenuOpen_ = true;
			}
		}
		else if (ButtonPressed == FName("homeButton") && !bAnnotationPending_) {
			bAnnotationPending_ = true;
			Hud_.SetVisibility(FName("episodeAnnotationCanvas"), true);
		}
		break;

	default:
		break;
	}

	if (ButtonPressed == FName("statisticsButton")) {
		bStatsVisible_ = !bStatsVisible_;
		Hud_.SetVisibility(FName("statsPanel"), bStatsVisible_);
	}

	if (ButtonPressed == FName("settingButton")) {
		bSettingsVisible_ = !bSettingsVisible_;
		Hud_.SetVisibility(FName("settings_canvas"), bSettingsVisible_);
	}

	// Arms or disarms the intervention session.
	// Arming sets HOLD on every arm, so nothing moves until a trigger (HUMAN) or
	// RESUME (POLICY). Disarming hands every arm to the operator (HUMAN); there is no
	// way back to UNSET.
	if (ButtonPressed == FName("interventionButton")) {
		bInterventionArmed_ = !bInterventionArmed_;
		Hud_.SetVisibility(FName("interventionPanel"),     bInterventionArmed_);
		Hud_.SetVisibility(FName("authority_pill_canvas"), bInterventionArmed_);
		Hud_.SetButtonToggled(FName("interventionButton"), bInterventionArmed_);

		const EControlAuthority Target = bInterventionArmed_ ? EControlAuthority::Hold
		                                                      : EControlAuthority::Human;
		// Straight to ComLink: arming is not a takeover, so no retarget re-anchor.
		// Whole-body sends one request for all arms, so they never sit on different
		// sides of the switch.
		if (bWholeBodyAuthority) {
			ComLink->SendAuthorityRequest(0, Target, /*bAllDevices=*/true);
		}
		else {
			for (uint8 Arm = 0; Arm < 2; ++Arm) ComLink->SendAuthorityRequest(Arm, Target);
		}
		// Both triggers are almost certainly released right now; seed the edge
		// detector with reality so arming does not synthesise a takeover.
		if (LeftTracked)  bPrevAuthClutch_[0] = LeftTracked->IsFullClutch();
		if (RightTracked) bPrevAuthClutch_[1] = RightTracked->IsFullClutch();

		if (Logger_)
			Logger_->LogEvent(FString::Printf(TEXT("INTERVENTION_%s"),
				bInterventionArmed_ ? TEXT("ARMED") : TEXT("DISARMED")));
	}

	// Hands every arm the operator is not holding back to the policy. Only reachable
	// while armed.
	// Tests 'not HUMAN' rather than 'is HOLD', so it also recovers from UNSET when
	// the authority echo never arrived. In whole-body mode the head follows through
	// IsOperatorHoldingHead.
	if (ButtonPressed == FName("resumePolicyButton") && bInterventionArmed_) {
		ResumePolicy(TEXT("button"));
	}

	// Global mute. The acknowledgement plays on the audible side of the toggle:
	// before muting, after unmuting.
	if (ButtonPressed == FName("muteButton")) {
		bSoundMuted_ = !bSoundMuted_;
		if (bSoundMuted_) SoundFeedback->Play(ESoundType::Click);
		SoundFeedback->SetMuted(bSoundMuted_);
		if (!bSoundMuted_) SoundFeedback->Play(ESoundType::Confirm);

		Hud_.SetButtonToggled(FName("muteButton"), bSoundMuted_);
		Hud_.PushMessage(bSoundMuted_ ? TEXT("SOUND MUTED") : TEXT("SOUND ON"), 2.0f);
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("MUTE state=%s"),
			bSoundMuted_ ? TEXT("on") : TEXT("off")));
	}

	if (ButtonPressed == FName("viewpointButton")) {
		if (bMenuOpen_) {
			Hud_.HideMenu();
			bMenuOpen_ = false;
			Hud_.SetButtonToggled(FName("viewpointButton"), !ActivePiPStreamName_.IsEmpty());
		} else {
			CurrentMenuStreams_ = PiPSourceNames_;
			Hud_.ShowCameraMenu(CurrentMenuStreams_, ActivePiPStreamName_);
			bMenuOpen_ = true;
			Hud_.SetButtonToggled(FName("viewpointButton"), true);
		}
	}

	// Avatar/twin main-view toggle, a cheap pointer swap between two running sources.
	// No-op without a twin_stream.
	if (ButtonPressed == FName("viewmodeButton") && bHasTwinMainStream_) {
		const bool bCurrentlyAvatar = (VideoFeed->GetActiveSourceName() == kAvatarMainSourceName);
		// SourceKey selects which registered source becomes active (must match
		// RegisterSource's key); Label is the separate user-facing text.
		const FString TargetSourceKey = bCurrentlyAvatar ? TwinMainStreamKey_   : kAvatarMainSourceName;
		const FString TargetLabel     = bCurrentlyAvatar ? TwinMainStreamLabel_ : kAvatarMainLabel;
		if (VideoFeed->SetActiveSource(TargetSourceKey)) {
			Hud_.SetText(FName("viewmode_label"), *TargetLabel);
			Hud_.SetButtonToggled(FName("viewmodeButton"), bCurrentlyAvatar);
			GhostOverlay->SetGhostVisible(!bCurrentlyAvatar);
		}
	}

	if (ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("__menu_"))) {
		if (ButtonPressed == FName("__menu_off")) {
			ActivePiPStreamName_ = TEXT("");
			Hud_.SetVisibility(FName("pip_canvas"), false);
			Hud_.SetButtonToggled(FName("viewpointButton"), false);
		} else {
			for (int32 i = 0; i < CurrentMenuStreams_.Num(); ++i) {
				if (ButtonPressed == FName(*FString::Printf(TEXT("__menu_%d"), i))) {
					ActivePiPStreamName_ = CurrentMenuStreams_[i];
					break;
				}
			}
			Hud_.SetButtonToggled(FName("viewpointButton"), true);
		}
		Hud_.HideMenu();
		bMenuOpen_ = false;
	}

	bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
	if (bCanReset && ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("__reset_"))) {
		if (ButtonPressed == FName("__reset_left") && LeftArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(0)) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("__reset_right") && RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("__reset_all")) {
			SendResetAll();
			if (LeftArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(0)) LeftArmResetState_ = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		bResetMenuOpen_ = false;
		Hud_.HideMenu();
		Hud_.SetButtonToggled(FName("resetButton"), false);
	}

	if (bAnnotationPending_ && ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("episode_"))) {
		FString Label;
		if      (ButtonPressed == FName("episode_success")) Label = TEXT("success");
		else if (ButtonPressed == FName("episode_partial")) Label = TEXT("partial");
		else if (ButtonPressed == FName("episode_failure")) Label = TEXT("failure");

		if (!Label.IsEmpty()) {
			Hud_.SetButtonToggled(ButtonPressed, false);
			Hud_.SetButtonToggled(FName("homeButton"), false);
			SendResetAll();
			if (LeftArmResetState_  == EArmResetState::Idle && ComLink->IsArmAlive(0)) LeftArmResetState_  = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) RightArmResetState_ = EArmResetState::Recovering;
			SendEpisodeRestart(Label);
			++EpisodeCount_;
			bAnnotationPending_ = false;
			Hud_.SetVisibility(FName("episodeAnnotationCanvas"), false);
			UpdateButtonStates();
		}
	}
}

void AOperatorPawn::UpdateInfoBar() {
	Hud_.SetText(FName("ep_value"), FString::Printf(TEXT("%03d"), EpisodeCount_));

	int32 ElapsedSec = static_cast<int32>(FPlatformTime::Seconds() - SessionStartTime_);
	int32 Hours      = ElapsedSec / 3600;
	int32 Minutes    = (ElapsedSec % 3600) / 60;
	int32 Seconds    = ElapsedSec % 60;
	Hud_.SetText(FName("session_time_value"), FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Seconds));

	// Network one-way delay, same getter as the OWD plot. Shows '--' while no
	// commands are flowing.
	const float LatencyMs = ComLink->GetNetworkDelayMs();
	if (LatencyMs > 0.f)
		Hud_.SetText(FName("latency_value"), FString::Printf(TEXT("%.1f ms"), LatencyMs));
	else
		Hud_.SetText(FName("latency_value"), TEXT("-- ms"));

	FLinearColor DotColor;
	if      (LatencyMs <= 0.f) DotColor = FLinearColor::Gray;
	else if (LatencyMs < 30.f) DotColor = FLinearColor::Green;
	else if (LatencyMs < 80.f) DotColor = FLinearColor::Yellow;
	else                       DotColor = FLinearColor::Red;
	Hud_.SetTextColor(FName("latency_dot"), DotColor);

	if (!bInterventionArmed_) return;

	// Whole-body handover has a single state, so there is no per-arm readout. The
	// per-limb form below is used when bWholeBodyAuthority is off.
	FLinearColor Accent;
	FString AuthorityLabel;

	if (bWholeBodyAuthority) {
		const EControlAuthority A = GetEffectiveAuthority();
		Accent         = AuthorityColor(A);
		AuthorityLabel = AuthorityToString(A);
	}
	else {
		const EControlAuthority L = GetArmAuthority(0);
		const EControlAuthority R = GetArmAuthority(1);
		const bool bAgree = (L == R);
		Accent = bAgree ? AuthorityColor(L) : kAuthorityMixed;

		// Agreeing: the word (POLICY / HUMAN / HOLD). Disagreeing: a five-character
		// positional code, e.g. "L= RH" is left HOLD, right HUMAN (P policy, H human,
		// = hold, ? unclaimed), so it fits the same box. The grey colour marks the
		// split.
		AuthorityLabel = bAgree
			? AuthorityToString(L)
			: FString::Printf(TEXT("L%s R%s"), *AuthorityToInitial(L), *AuthorityToInitial(R));
	}

	Hud_.SetText(FName("authority_pill_state"), AuthorityLabel);
	Hud_.SetTextColor(FName("authority_pill_state"), Accent);
	// All three setters on purpose: UWidgetBinder is keyed by widget type and no-ops
	// on a mismatch.
	SetWidgetAccent(FName("authority_pill_dot"), Accent);

	// Same string as the pill, so the two readouts cannot disagree.
	Hud_.SetText(FName("intervention_authority_text"), AuthorityLabel);

	// Pill: accent on a dark background. Panel: solid accent fill with contrasting
	// glyph and text.
	const FLinearColor OnFill = ForegroundOn(Accent);
	SetWidgetAccent(FName("intervention_authority_fill"), Accent);
	SetWidgetAccent(FName("intervention_authority_text"), OnFill);
	// The glyph takes the contrast colour, like the text.
	SetWidgetAccent(FName("authority_glyph"), OnFill);

	// Swap the icon itself to show who is driving. Only on change, since
	// SetImageTexture builds a new brush.
	if (UTexture2D* Glyph = AuthorityGlyphFor(bWholeBodyAuthority
			? GetEffectiveAuthority() : GetArmAuthority(0))) {
		if (Glyph != LastAuthorityGlyph_) {
			Hud_.SetImageTexture(FName("authority_glyph"), Glyph);
			LastAuthorityGlyph_ = Glyph;
			// SetImageTexture resets the tint, so re-apply it.
			SetWidgetAccent(FName("authority_glyph"), OnFill);
		}
	}

	Hud_.SetText(FName("interv_value"), FString::Printf(TEXT("%d"), InterventionCount_));
}

void AOperatorPawn::UpdatePolicyStats(float DeltaTime) {
	// Keyed on EpisodeCount_ rather than hooked into each place that bumps it,
	// so every route to a new episode (buttons, voice) resets the same way.
	if (EpisodeCount_ != StatsEpisode_) {
		StatsEpisode_      = EpisodeCount_;
		InterventionCount_ = 0;
		AutonPolicySec_    = 0.0;
		AutonEngagedSec_   = 0.0;
	}

	// AUTON: share of engaged arm-time the policy held. Averaged over both arms
	// so a per-arm intervention counts half.
	if (ComLink && ComLink->GetAvatarState() == ESysState::Engaged) {
		const int32 NPolicy = (GetArmAuthority(0) == EControlAuthority::Policy ? 1 : 0)
		                    + (GetArmAuthority(1) == EControlAuthority::Policy ? 1 : 0);
		AutonEngagedSec_ += DeltaTime;
		AutonPolicySec_  += DeltaTime * 0.5 * NPolicy;
	}
	const int32 AutonPct = AutonEngagedSec_ > 0.0
		? FMath::RoundToInt(100.0 * AutonPolicySec_ / AutonEngagedSec_) : 0;
	Hud_.SetText(FName("auton_value"), FString::Printf(TEXT("%d%%"), AutonPct));

	const bool  bFresh = FPlatformTime::Seconds() - PolicyStatusTime_.load() < kPolicyStatusStaleSec;
	const float InfMs  = PolicyInferenceMs_.load();
	Hud_.SetText(FName("inference_value"),
		bFresh && InfMs >= 0.f ? FString::Printf(TEXT("%.0fms"), InfMs) : FString(TEXT("--")));

	// Policy liveness: green while predictions arrive, grey once they stop.
	SetWidgetAccent(FName("intervention_rec_dot"), bFresh ? kAuthorityHuman : kPipOff);

	const float Agree = PolicyAgree_.load();
	const int32 Lit   = bFresh && Agree >= 0.f
		? FMath::Clamp(FMath::RoundToInt(Agree * kAgreePips), 0, kAgreePips) : 0;
	for (int32 i = 0; i < kAgreePips; ++i)
		SetWidgetAccent(FName(*FString::Printf(TEXT("agree_pip_%d"), i)), i < Lit ? kAuthorityPolicy : kPipOff);
}

void AOperatorPawn::AuditWidgetNames() const {
	const TArray<FName> Dupes = Hud_.DuplicateNames();
	if (Dupes.Num()) {
		TArray<FString> DupeStr;
		for (const FName& N : Dupes) DupeStr.Add(N.ToString());
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: %d widget name(s) exist in BOTH WBP_HudFrame and WBP_HudAlerts -- only the alerts copy is driven: %s"),
			Dupes.Num(), *FString::Join(DupeStr, TEXT(", ")));
	}
	// Startup check: report every widget this class drives that the UMG asset lacks,
	// or whose type no colour setter can touch. UWidgetBinder silently no-ops on
	// both, so this list is the contract the asset has to meet.

	// Text / value sinks -- SetText only, so any TextBlock will do.
	static const TCHAR* ExpectedText[] = {
		TEXT("intervention_authority_text"), TEXT("authority_pill_state"),
		TEXT("interv_value"), TEXT("auton_value"), TEXT("inference_value"),
		TEXT("ep_value"), TEXT("session_time_value"), TEXT("latency_value"),
		TEXT("stream_health_value"), TEXT("viewmode_label"),
		TEXT("gear_left_value"), TEXT("gear_right_value"), TEXT("fault_status_text"),
	};
	// Panels toggled with SetVisibility -- any widget type is fine.
	static const TCHAR* ExpectedPanel[] = {
		TEXT("interventionPanel"), TEXT("authority_pill_canvas"),
		TEXT("fault_status_canvas"), TEXT("armStaleInfo"), TEXT("videoLostInfo"),
		TEXT("gazeOfflineInfo"), TEXT("statsPanel"), TEXT("settings_canvas"),
		TEXT("episodeAnnotationCanvas"),
	};
	// Buttons -- must be UButton for lock/toggle styling to work at all.
	static const TCHAR* ExpectedButton[] = {
		TEXT("interventionButton"), TEXT("resumePolicyButton"),
	};
	// ACCENT-driven: coloured every tick via SetWidgetAccent, which only bites
	// on a TextBlock, an Image or a Border. Anything else is a silent no-op.
	static const TCHAR* ExpectedAccent[] = {
		TEXT("authority_pill_dot"), TEXT("authority_pill_state"),
		TEXT("intervention_authority_text"), TEXT("intervention_authority_fill"),
		TEXT("authority_glyph"), TEXT("intervention_rec_dot"), TEXT("latency_dot"),
	};

	TArray<FString> Missing, Untintable, NotAButton;

	auto CheckPresent = [&](const TCHAR* Name) {
		if (!Hud_.HasWidget(FName(Name))) Missing.Add(FString(Name));
	};
	for (const TCHAR* N : ExpectedText)   CheckPresent(N);
	for (const TCHAR* N : ExpectedPanel)  CheckPresent(N);
	for (const TCHAR* N : ExpectedButton) CheckPresent(N);
	for (const TCHAR* N : ExpectedAccent) CheckPresent(N);
	for (int32 i = 0; i < kAgreePips; ++i) {
		const FString Pip = FString::Printf(TEXT("agree_pip_%d"), i);
		if (!Hud_.HasWidget(FName(*Pip))) Missing.Add(Pip);
	}

	for (const TCHAR* Name : ExpectedButton) {
		const FString Cls = Hud_.GetWidgetClassName(FName(Name));
		if (!Cls.IsEmpty() && Cls != TEXT("Button"))
			NotAButton.Add(FString::Printf(TEXT("%s (is %s)"), Name, *Cls));
	}

	TArray<FString> Tinted;
	auto CheckTintable = [&](const FString& Name) {
		const FString Cls = Hud_.GetWidgetClassName(FName(*Name));
		if (Cls.IsEmpty()) return;   // already reported as missing
		if (Cls != TEXT("TextBlock") && Cls != TEXT("Image") && Cls != TEXT("Border")) {
			Untintable.Add(FString::Printf(TEXT("%s (is %s)"), *Name, *Cls));
			return;
		}
		// Right type, but a non-white design tint multiplies into every runtime
		// colour.
		const FLinearColor Base = Hud_.GetDesignTint(FName(*Name));
		if (!Base.Equals(FLinearColor::White, 0.02f))
			Tinted.Add(FString::Printf(TEXT("%s (design tint %.2f,%.2f,%.2f)"),
				*Name, Base.R, Base.G, Base.B));
	};
	for (const TCHAR* N : ExpectedAccent) CheckTintable(FString(N));
	for (int32 i = 0; i < kAgreePips; ++i)
		CheckTintable(FString::Printf(TEXT("agree_pip_%d"), i));

	if (Tinted.Num())
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: %d accent widget(s) carry a NON-WHITE design tint -- UMG's tint "
				 "MULTIPLIES the runtime accent, so these can only ever render that colour. "
				 "Set their Brush Tint / Color to white (1,1,1,1) in the asset: %s"),
			Tinted.Num(), *FString::Join(Tinted, TEXT(", ")));

	if (Missing.Num() == 0 && Untintable.Num() == 0 && NotAButton.Num() == 0 && Tinted.Num() == 0) {
		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: HUD widget audit clean."));
		return;
	}
	if (Missing.Num())
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: %d HUD widget(s) MISSING from the bound asset -- every setter "
				 "targeting them silently does nothing: %s"),
			Missing.Num(), *FString::Join(Missing, TEXT(", ")));
	if (Untintable.Num())
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: %d accent widget(s) present but NOT TINTABLE -- SetWidgetAccent "
				 "only affects TextBlock, Image and Border, so these keep their design-time "
				 "colour: %s"),
			Untintable.Num(), *FString::Join(Untintable, TEXT(", ")));
	if (NotAButton.Num())
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: %d expected button(s) are not UButton -- lock/toggle styling "
				 "will not apply: %s"),
			NotAButton.Num(), *FString::Join(NotAButton, TEXT(", ")));
}

void AOperatorPawn::ResolveAuthorityGlyphs() {
	// Unassigned icons are looked up by name; each miss is reported.
	static const TCHAR* Folders[] = {
		TEXT("/Game/UI/Icons"), TEXT("/Game/UI"), TEXT("/Game/Textures"), TEXT("/Game/Icons"),
	};
	auto Resolve = [](TObjectPtr<UTexture2D>& Slot, const TCHAR* AssetName) {
		if (Slot) return;
		for (const TCHAR* Folder : Folders) {
			const FString Path = FString::Printf(TEXT("%s/%s.%s"), Folder, AssetName, AssetName);
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path)) {
				Slot = Tex;
				UE_LOG(LogTemp, Log, TEXT("OperatorPawn: authority glyph '%s' loaded from %s"),
					AssetName, *Path);
				return;
			}
		}
		UE_LOG(LogTemp, Warning,
			TEXT("OperatorPawn: authority glyph '%s' not found -- assign it on the pawn "
				 "(Teleop|Intervention) or move the texture under /Game/UI/Icons. The DAGGER "
				 "panel will keep whatever icon the asset has."), AssetName);
	};
	Resolve(AuthorityIconHuman,  TEXT("authority_human"));
	Resolve(AuthorityIconPolicy, TEXT("authority_policy"));
	Resolve(AuthorityIconHold,   TEXT("authority_hold"));
}

UTexture2D* AOperatorPawn::AuthorityGlyphFor(EControlAuthority A) const {
	switch (A) {
	case EControlAuthority::Policy: return AuthorityIconPolicy;
	case EControlAuthority::Human:  return AuthorityIconHuman;
	// Hold covers Unset too: nobody is driving in either case.
	default:                        return AuthorityIconHold;
	}
}

void AOperatorPawn::SetWidgetAccent(FName WidgetName, const FLinearColor& Color) const {
	// Setters are keyed by widget type and no-op on a miss, so call all of them.
	Hud_.SetTextColor(WidgetName, Color);
	Hud_.SetImageColor(WidgetName, Color);
	Hud_.SetBorderColor(WidgetName, Color);
}

EControlAuthority AOperatorPawn::GetArmAuthority(uint8 ArmIndex) const {
	const uint8 Raw = (ArmIndex == 0) ? LeftAuthority_.Load() : RightAuthority_.Load();
	switch (Raw) {
	case 0:  return EControlAuthority::Policy;
	case 1:  return EControlAuthority::Human;
	case 2:  return EControlAuthority::Hold;
	default: return EControlAuthority::Unset;
	}
}

void AOperatorPawn::RequestArmAuthority(uint8 ArmIndex, EControlAuthority Requested) {
	if (ArmIndex > 1 || !ComLink) return;

	const EControlAuthority Current = bWholeBodyAuthority
		? GetEffectiveAuthority()
		: GetArmAuthority(ArmIndex);
	// Whole-body: one request without a device key covers every arm, so the arms
	// change hands in the same message.
	ComLink->SendAuthorityRequest(ArmIndex, Requested, bWholeBodyAuthority);

	// On a takeover from POLICY or HOLD the avatar re-origins onto the current pose,
	// so the banked delta here has to be reset at the same time, or the arm steps by
	// the distance the policy moved it.
	// Not from UNSET: that is plain teleoperation, and a reset would snap the arm
	// back to the engage origin.
	if (Requested == EControlAuthority::Human && CommandThread_
		&& (Current == EControlAuthority::Policy || Current == EControlAuthority::Hold)) {
		// -1 re-anchors both retargets for a whole-body takeover.
		CommandThread_->RequestCaptureOrigin(bWholeBodyAuthority ? -1 : static_cast<int32>(ArmIndex));
		SyncGraspToMeasured(bWholeBodyAuthority ? -1 : static_cast<int32>(ArmIndex));
		++InterventionCount_;
	}

	if (Logger_)
		Logger_->LogEvent(FString::Printf(TEXT("AUTHORITY_REQUEST arm=%s from=%s requested=%s"),
			bWholeBodyAuthority ? TEXT("all") : (ArmIndex == 0 ? TEXT("left") : TEXT("right")),
			*AuthorityToString(Current), *AuthorityToString(Requested)));
}

void AOperatorPawn::SyncGraspToMeasured(int32 ArmIndex) {
	// Re-seed the grasp latch from the gripper the operator inherits, so a takeover
	// does not apply a stale grasp command. 0.04 m matches run.py's
	// _GRIPPER_CLOSE_THRESHOLD_M, the midpoint of 0.0 and 0.08.
	static constexpr float kGripperClosedBelowM = 0.04f;
	for (uint8 Arm = 0; Arm < 2; ++Arm) {
		if (ArmIndex >= 0 && Arm != static_cast<uint8>(ArmIndex)) continue;
		UTrackedControllerComponent* Tracked = (Arm == 0) ? LeftTracked : RightTracked;
		if (!Tracked || !ComLink->IsArmAlive(Arm)) continue;

		const ArmStateMsg S = ComLink->PeekArmState(Arm);
		const bool bClosed = S.gripper_width < kGripperClosedBelowM;
		if (bClosed != Tracked->IsGraspHeld()) {
			Tracked->SetGraspHeld(bClosed);
			if (Logger_)
				Logger_->LogEvent(FString::Printf(TEXT("GRASP_RESYNC side=%s measured_m=%.4f state=%s"),
					Arm == 0 ? TEXT("left") : TEXT("right"), S.gripper_width,
					bClosed ? TEXT("held") : TEXT("released")));
		}
	}
}

EControlAuthority AOperatorPawn::GetEffectiveAuthority() const {
	if (!bWholeBodyAuthority) return EControlAuthority::Unset;

	const EControlAuthority L = GetArmAuthority(0);
	const EControlAuthority R = GetArmAuthority(1);
	if (L == R) return L;

	// Should never disagree in whole-body mode. If they do, report Human if either
	// arm is human, else Hold, never Policy.
	if (L == EControlAuthority::Human || R == EControlAuthority::Human)
		return EControlAuthority::Human;
	return EControlAuthority::Hold;
}

void AOperatorPawn::MaybeAutoArmIntervention() {
	// Arms the session when the avatar already reports live authority. In
	// intervention mode the orchestrator parks both arms in HOLD at startup, which
	// also refuses the operator until the session is armed here. No request is sent;
	// the orchestrator decides the opening state.
	if (bInterventionArmed_ || !bAuthorityLiveSeen_.load(std::memory_order_relaxed))
		return;

	bInterventionArmed_ = true;
	Hud_.SetVisibility(FName("interventionPanel"),     true);
	Hud_.SetVisibility(FName("authority_pill_canvas"), true);
	Hud_.SetButtonToggled(FName("interventionButton"), true);
	// Seed the clutch edge detector from reality, exactly as the button does,
	// so arming cannot synthesise a takeover from a trigger already held.
	if (LeftTracked)  bPrevAuthClutch_[0] = LeftTracked->IsFullClutch();
	if (RightTracked) bPrevAuthClutch_[1] = RightTracked->IsFullClutch();

	Hud_.PushMessage(TEXT("INTERVENTION ARMED - POLICY CONNECTED"), 3.0f);
	if (Logger_) Logger_->LogEvent(TEXT("INTERVENTION_ARMED source=avatar_authority"));
}

void AOperatorPawn::UpdateAuthority() {
	if (!LeftTracked || !RightTracked) return;

	UTrackedControllerComponent* Tracked[2] = { LeftTracked, RightTracked };

	// Unarmed or not engaged: only track the clutch level, so arming never inherits a
	// stale edge.
	if (!bInterventionArmed_ || OperatorState_ != ESysState::Engaged) {
		for (uint8 i = 0; i < 2; ++i) bPrevAuthClutch_[i] = Tracked[i]->IsFullClutch();
		return;
	}

	// Note the polarity throughout: bFullClutch TRUE means DECOUPLED. Pulling
	// the trigger past 0.55 sets it false, which is the hand taking the arm.
	if (bWholeBodyAuthority) {
		// Whole-body: the first trigger down takes the robot, and it is only given
		// back once both are released, so the policy never takes over while a hand is
		// still driving.
		const bool bDecoupled = Tracked[0]->IsFullClutch() && Tracked[1]->IsFullClutch();
		const bool bPrevDecoupled = bPrevAuthClutch_[0] && bPrevAuthClutch_[1];
		for (uint8 i = 0; i < 2; ++i) bPrevAuthClutch_[i] = Tracked[i]->IsFullClutch();
		if (bDecoupled == bPrevDecoupled) return;

		// The arm index is ignored here; the request covers all arms.
		RequestArmAuthority(0, bDecoupled ? EControlAuthority::Hold : EControlAuthority::Human);
		return;
	}

	for (uint8 i = 0; i < 2; ++i) {
		const bool bDecoupled = Tracked[i]->IsFullClutch();
		if (bDecoupled == bPrevAuthClutch_[i]) continue;
		bPrevAuthClutch_[i] = bDecoupled;

		// Released goes to HOLD, never straight to POLICY. Handing back is an
		// explicit RESUME.
		RequestArmAuthority(i, bDecoupled ? EControlAuthority::Hold : EControlAuthority::Human);
	}
}

void AOperatorPawn::ResumePolicy(const TCHAR* Source) {
	if (!bInterventionArmed_) return;

	// Give back every arm without a hand on it.
	// Tests the local clutch rather than the authority echo, which lags a round trip
	// (~70 ms) behind a release. IsFullClutch() true means decoupled, i.e. no hand on
	// it.
	UTrackedControllerComponent* Tracked[2] = { LeftTracked, RightTracked };
	auto HandOff = [&](uint8 Arm) {
		return !Tracked[Arm] || Tracked[Arm]->IsFullClutch();
	};

	bool bAnyRequested = false;
	if (bWholeBodyAuthority) {
		// One robot: a hand on either arm keeps all of it.
		if (HandOff(0) && HandOff(1)) {
			RequestArmAuthority(0, EControlAuthority::Policy);
			bAnyRequested = true;
		}
	}
	else {
		for (uint8 Arm = 0; Arm < 2; ++Arm) {
			if (HandOff(Arm)) {
				RequestArmAuthority(Arm, EControlAuthority::Policy);
				bAnyRequested = true;
			}
		}
	}

	// The chord has no button to light up, so both outcomes get audio feedback.
	if (bAnyRequested) {
		SoundFeedback->Play(ESoundType::Confirm);
		Hud_.PushMessage(TEXT("POLICY RESUMED"), 2.0f);
	}
	else {
		SoundFeedback->Play(ESoundType::Reject);
		Hud_.PushMessage(TEXT("STILL HELD - RELEASE TO RESUME"), 2.0f);
	}

	if (Logger_)
		Logger_->LogEvent(FString::Printf(TEXT("RESUME_POLICY source=%s applied=%s"),
			Source, bAnyRequested ? TEXT("yes") : TEXT("no")));
}

void AOperatorPawn::PollResumeButton() {
	// Hand-grip tap on either controller, see
	// UTrackedControllerComponent::OnHandGripReleased. Consumed even when unarmed, so
	// an old tap is not spent on arming.
	bool bTap = false;
	if (LeftTracked && LeftTracked->IsResumeRequested()) {
		LeftTracked->ConsumeResumePress();
		bTap = true;
	}
	if (RightTracked && RightTracked->IsResumeRequested()) {
		RightTracked->ConsumeResumePress();
		bTap = true;
	}
	if (bTap && bInterventionArmed_) ResumePolicy(TEXT("handgrip_tap"));
}

bool AOperatorPawn::IsOperatorHoldingHead() const {
	// Unarmed: the head always belongs to the operator.
	if (!bInterventionArmed_) return true;
	if (bWholeBodyAuthority) return GetEffectiveAuthority() == EControlAuthority::Human;
	// Per-limb: either hand driving gives the operator the view.
	return GetArmAuthority(0) == EControlAuthority::Human
		|| GetArmAuthority(1) == EControlAuthority::Human;
}

void AOperatorPawn::TransitionTo(ESysState NewState) {
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: %d -> %d"), static_cast<int>(OperatorState_), static_cast<int>(NewState));
	if (Logger_) {
		Logger_->LogEvent(FString::Printf(TEXT("STATE_TRANSITION old=%s new=%s"),*StateToString(OperatorState_), *StateToString(NewState)));
	}
	if (NewState == ESysState::Idle)    bPendingVoiceReengage_ = false;
	if (NewState == ESysState::Idle || NewState == ESysState::Offline) {
		LeftArmResetState_  = EArmResetState::Idle;
		RightArmResetState_ = EArmResetState::Idle;
		ArmResetRequestTime_[0] = 0.0;
		ArmResetRequestTime_[1] = 0.0;
		if (bResetMenuOpen_) {
			Hud_.HideMenu();
			bResetMenuOpen_ = false;
		}
		bAnnotationPending_ = false;
		Hud_.SetVisibility(FName("episodeAnnotationCanvas"), false);
		Hud_.SetButtonToggled(FName("homeButton"), false);
	}
	if (OperatorState_ == ESysState::Idle && NewState == ESysState::Homing) {
		SendRecordingSignal(true);
		bRecordingActive_ = true;
	} else if (NewState == ESysState::Idle && OperatorState_ != ESysState::Idle && OperatorState_ != ESysState::Offline) {
		SendRecordingSignal(false);
		bRecordingActive_ = false;
	}

	SoundFeedback->Play(ESoundType::Transition);
	OperatorState_ = NewState;
	UpdateButtonStates();
}

void AOperatorPawn::UpdateButtonStates() {
	bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
	bool bAnyRecovering = LeftArmResetState_ == EArmResetState::Recovering
		|| RightArmResetState_ == EArmResetState::Recovering;

	auto ApplyResetButton = [&](FName Button, FName Label, EArmResetState ResetState, const TCHAR* Side, uint8 Index) {
		switch (ResetState) {
		case EArmResetState::Idle:
			Hud_.SetButtonToggled(Button, false);
			Hud_.SetButtonLocked(Button, !bCanReset || !ComLink->IsArmAlive(Index));
			Hud_.SetText(Label, FString::Printf(TEXT("Reset %s"), Side));
			break;
		case EArmResetState::Recovering:
		case EArmResetState::AwaitingResume:
			Hud_.SetButtonToggled(Button, true);
			Hud_.SetButtonLocked(Button, true);
			Hud_.SetText(Label, FString::Printf(TEXT("Resetting %s..."), Side));
			break;
		}
		};

	ApplyResetButton(FName("resetButtonLeft"), FName("resetLabelLeft"), LeftArmResetState_, TEXT("L"), 0);
	ApplyResetButton(FName("resetButtonRight"), FName("resetLabelRight"), RightArmResetState_, TEXT("R"), 1);

	bool bBothIdle = LeftArmResetState_ == EArmResetState::Idle && RightArmResetState_ == EArmResetState::Idle;
	bool bResetting = !bBothIdle;
	Hud_.SetButtonToggled(FName("resetButton"), bResetMenuOpen_ || bResetting);
	Hud_.SetButtonLocked(FName("resetButton"), !bCanReset || bResetting);
	Hud_.SetText(FName("resetLabel"), TEXT("Reset"));

	Hud_.SetButtonLocked(FName("homeButton"), !bCanReset || bAnyRecovering || bAnnotationPending_);

	switch (OperatorState_) {
	case ESysState::Offline:
		Hud_.SetButtonLocked(FName("startButton"), true);
		Hud_.SetButtonLocked(FName("engageButton"), true);
		Hud_.SetText(FName("startLabel"), TEXT("Start"));
		Hud_.SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Idle:
		Hud_.SetButtonLocked(FName("startButton"), false);
		Hud_.SetButtonLocked(FName("engageButton"), true);
		Hud_.SetText(FName("startLabel"), TEXT("Start"));
		Hud_.SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Homing:
		Hud_.SetButtonLocked(FName("startButton"), false);
		Hud_.SetButtonLocked(FName("engageButton"), true);
		Hud_.SetText(FName("startLabel"), TEXT("Stop"));
		Hud_.SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Awaiting:
		Hud_.SetButtonLocked(FName("startButton"), false);
		Hud_.SetButtonLocked(FName("engageButton"), false);
		Hud_.SetText(FName("startLabel"), TEXT("Stop"));
		Hud_.SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Engaged:
		Hud_.SetButtonLocked(FName("startButton"), bAnyRecovering);
		Hud_.SetButtonLocked(FName("engageButton"), bAnyRecovering);
		Hud_.SetText(FName("startLabel"), TEXT("Stop"));
		Hud_.SetText(FName("engageLabel"), TEXT("Pause"));
		break;

	case ESysState::Paused:
		Hud_.SetButtonLocked(FName("startButton"), bAnyRecovering);
		Hud_.SetButtonLocked(FName("engageButton"), bAnyRecovering);
		Hud_.SetText(FName("startLabel"), TEXT("Stop"));
		Hud_.SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	default:
		break;
	}

	const bool bSessionRunning = OperatorState_ == ESysState::Homing || OperatorState_ == ESysState::Awaiting
		|| OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused;
	Hud_.SetButtonToggled(FName("startButton"), bSessionRunning);
	Hud_.SetButtonToggled(FName("engageButton"), OperatorState_ == ESysState::Engaged);
}

bool AOperatorPawn::CheckEmergencyStop() {
	if (OperatorState_ == ESysState::Offline || OperatorState_ == ESysState::Idle) {
		return false;
	}
	// Either menu button stops.
	bool bStop = LeftTracked->IsMenuPressed() || RightTracked->IsMenuPressed();
	if (bStop) {
		if (Logger_) Logger_->LogEvent(TEXT("EMERGENCY_STOP"));
		SoundFeedback->Play(ESoundType::Warning);
		LeftTracked->ConsumeMenuPress();
		RightTracked->ConsumeMenuPress();
	}
	return bStop;
}

void AOperatorPawn::CaptureControllerOrigins() {
	LeftTracked->CaptureOrigin();
	RightTracked->CaptureOrigin();
	if (CommandThread_) CommandThread_->RequestCaptureOrigin();

	if (VRCamera) {
		HMDOrigin_ = VRCamera->GetComponentTransform();
		bHMDOriginValid_ = true;
		// Re-anchor the head base on the neck's current pose, not zero: the wire
		// carries absolute joint angles (base plus HMD delta).
		const HeadStateMsg S = ComLink->PeekHeadState();
		HeadBasePan_  = S.pan;
		HeadBaseTilt_ = S.tilt;

	}

	if (GhostOverlay) {
		for (uint8 Arm = 0; Arm < 2; ++Arm) {
			// ReadArmState returns the latest cached pose, so seed from it whenever
			// the arm is alive; zero would offset the ghost origin.
			if (ComLink->IsArmAlive(Arm)) {
				ArmStateMsg S = ComLink->ReadArmState(Arm);
				GhostOverlay->SeedIntentPose(Arm, S.position, S.quaternion);
			} else {
				GhostOverlay->UnseedIntentPose(Arm);
			}
		}
	}
}

void AOperatorPawn::SendArmCommands() {
	if (!CommandThread_) return;

	// The game thread owns everything Enhanced Input produces; the command
	// thread owns the pose integration and the send. Publish, do not send.
	FOperatorInputSnapshot In;
	In.bArmActive[0] = LeftArmResetState_  == EArmResetState::Idle && ComLink->GetArmRemoteState(0) == SysState::ENGAGED;
	In.bArmActive[1] = RightArmResetState_ == EArmResetState::Idle && ComLink->GetArmRemoteState(1) == SysState::ENGAGED;
	In.bFullClutch[0] = LeftTracked->IsFullClutch();
	In.bFullClutch[1] = RightTracked->IsFullClutch();
	In.bGraspHeld[0]  = LeftTracked->IsGraspHeld();
	In.bGraspHeld[1]  = RightTracked->IsGraspHeld();
	In.ScaleFactor[0] = LeftTracked->GetScaleFactor();
	In.ScaleFactor[1] = RightTracked->GetScaleFactor();
	In.ControlPointOffset[0] = LeftTracked->ControlPointOffset;
	In.ControlPointOffset[1] = RightTracked->ControlPointOffset;
	if (bHMDOriginValid_) {
		const float CaptureYaw = HMDOrigin_.GetRotation().Rotator().Yaw;
		In.HMDYawQuat = FQuat(FRotator(0.f, CaptureYaw, 0.f));
		In.bHMDOriginValid = true;
	}

	In.bHandValid[0] = LeftTracked->IsTracking();
	In.bHandValid[1] = RightTracked->IsTracking();
	In.HandPose[0]   = LeftTracked->GetTrackedTransform();
	In.HandPose[1]   = RightTracked->GetTrackedTransform();

	// GetTrackingSpace() and GetDisplayTime() read pipelined frame state and
	// assert off the game thread, so they are sampled here and ferried across.
	if (GEngine && GEngine->XRSystem.IsValid()) {
		In.TrackingToWorld = GEngine->XRSystem->GetTrackingToWorldTransform();
		if (IOpenXRHMD* Xr = GEngine->XRSystem->GetIOpenXRHMD()) {
			In.XrTrackingSpace = reinterpret_cast<uint64>(Xr->GetTrackingSpace());
			In.XrDisplayTimeNs = static_cast<int64>(Xr->GetDisplayTime());
		}
	}

	CommandThread_->PublishInput(In);

	// The ghost follows the command that was actually sent, so it stays true
	// to the wire even though it only repaints at frame rate.
	const FCommandThreadOutput Out = CommandThread_->ReadOutput();
	if (GhostOverlay) {
		GhostOverlay->SetIntentPose(0, Out.Position[0], Out.Quaternion[0], Out.Gripper[0], Out.bFullClutch[0]);
		GhostOverlay->SetIntentPose(1, Out.Position[1], Out.Quaternion[1], Out.Gripper[1], Out.bFullClutch[1]);
	}
}

void AOperatorPawn::SendHeadCommand() {
	if (!bHMDOriginValid_ || !VRCamera) return;

	// --- head handover ---
	// The neck changes hands with the arms, since the policy is conditioned on its
	// own camera view.
	const bool bHeld = IsOperatorHoldingHead();
	if (bHeld != bHeadHeld_) {
		bHeadHeld_ = bHeld;
		if (bHeld) {
			// Re-anchor like the arms: zero the HMD delta and use the neck's current
			// angles as the base, so taking the head back does not snap it.
			HMDOrigin_ = VRCamera->GetComponentTransform();
			const HeadStateMsg S = ComLink->PeekHeadState();
			HeadBasePan_  = S.pan;
			HeadBaseTilt_ = S.tilt;
		}
		if (Logger_)
			Logger_->LogEvent(FString::Printf(TEXT("HEAD_AUTHORITY held=%s base_pan=%.3f base_tilt=%.3f"),
				bHeld ? TEXT("human") : TEXT("policy"), HeadBasePan_, HeadBaseTilt_));
	}

	if (!bHeld) {
		// Keep the logged head angles following the real neck during autonomous
		// stretches.
		const HeadStateMsg S = ComLink->PeekHeadState();
		LastHeadPan_  = S.pan;
		LastHeadTilt_ = S.tilt;
		return;
	}

	FTransform CurrentHMD = VRCamera->GetComponentTransform();
	FQuat DeltaQuat = HMDOrigin_.GetRotation().Inverse() * CurrentHMD.GetRotation();
	FRotator DeltaRot = DeltaQuat.Rotator();

	HeadCommandMsg Msg{};
	Msg.pan  = HeadBasePan_  + static_cast<float>(FMath::DegreesToRadians(-DeltaRot.Yaw));
	Msg.tilt = HeadBaseTilt_ + static_cast<float>(FMath::DegreesToRadians(DeltaRot.Pitch));
	LastHeadPan_  = Msg.pan;
	LastHeadTilt_ = Msg.tilt;
	ComLink->SendHeadCommand(Msg);
}

void AOperatorPawn::SendArmReset(const std::string& DeviceName) {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"device", DeviceName}});
	ComLink->SendReliable("arm_reset", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: arm_reset -> %s"), UTF8_TO_TCHAR(DeviceName.c_str()));
	if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESET device=%s"), UTF8_TO_TCHAR(DeviceName.c_str())));
}

void AOperatorPawn::SendArmResume(const std::string& DeviceName) {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"device", DeviceName}});
	ComLink->SendReliable("arm_resume", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: arm_resume -> %s"), UTF8_TO_TCHAR(DeviceName.c_str()));
	if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("ARM_RESUME device=%s"), UTF8_TO_TCHAR(DeviceName.c_str())));
}

void AOperatorPawn::SendResetAll() {
	msgpack::sbuffer Buf;
	msgpack::pack(Buf, std::map<std::string, std::string>{{"reason", "operator_reset_all"}});
	ComLink->SendReliable("reset_all", Buf, true);
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: reset_all"));
	if (Logger_) Logger_->LogEvent(TEXT("ARM_RESET device=all"));
}

void AOperatorPawn::SendEpisodeRestart(const FString& Label) {
	std::string LabelStr = TCHAR_TO_UTF8(*Label);

	AnnotationMsg Ann{};
	Ann.timestamp  = FPlatformTime::Seconds();
	Ann.label      = LabelStr;
	Ann.atype      = 1;
	Ann.confidence = 1.0f;
	Ann.score      = 0.0f;
	Ann.frame_id   = 0;
	{
		msgpack::sbuffer Buf;
		msgpack::pack(Buf, Ann);
		ComLink->SendReliable("annotation", Buf, true);
	}

	{
		msgpack::sbuffer Buf;
		msgpack::pack(Buf, std::map<std::string, std::string>{{"label", LabelStr}});
		ComLink->SendReliable("episode_restart", Buf, true);
	}

	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: episode_restart label=%s"), *Label);
	if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("EPISODE_RESTART label=%s"), *Label));
}

// Wrist-pivot calibration helpers. Inert unless WITH_PIVOT_CALIBRATION is enabled in
// TrackedControllerComponent.cpp (ArmPivotCalibration becomes a no-op stub otherwise).
void AOperatorPawn::CalibrateWristPivotRight() {
	if (RightTracked) RightTracked->ArmPivotCalibration();
}

void AOperatorPawn::CalibrateWristPivotLeft() {
	if (LeftTracked) LeftTracked->ArmPivotCalibration();
}

bool AOperatorPawn::IsArmGraspHeld(uint8 ArmIndex) const {
	const UTrackedControllerComponent* T = (ArmIndex == 0) ? LeftTracked : RightTracked;
	return T && T->IsGraspHeld();
}

void AOperatorPawn::SendOperatorState() {
	if (!ComLink) return;
	msgpack::sbuffer Buf;
	msgpack::packer<msgpack::sbuffer> Pk(Buf);
	Pk.pack_map(4);
	Pk.pack(std::string("clutch_left"));  Pk.pack(static_cast<uint8_t>(LeftTracked  && LeftTracked->IsFullClutch()  ? 1 : 0));
	Pk.pack(std::string("clutch_right")); Pk.pack(static_cast<uint8_t>(RightTracked && RightTracked->IsFullClutch() ? 1 : 0));
	Pk.pack(std::string("engaged"));      Pk.pack(static_cast<uint8_t>(1));
	Pk.pack(std::string("timestamp_ns")); Pk.pack(static_cast<uint64_t>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000000000ULL + static_cast<uint64_t>(FDateTime::UtcNow().GetMillisecond()) * 1000000ULL);
	ComLink->SendReliable("operator_state", Buf, false);
}

void AOperatorPawn::SendGazeSample(){
	uint64 FrameId = VideoFeed->GetLastFrameId();
	if (FrameId == 0) return;

	const FGazeData& GazeData = Gaze->GetGazeData();
	if (!GazeData.bIsValid) return;

	if (!VRCamera) return;

	FVector2D GazeUV;
	const FVector2D QuadSize = GetVideoQuadSize();
	if (!FGazeProjection::ProjectOnQuad(GazeData, VideoQuadWorld_, QuadSize.X, QuadSize.Y, GazeUV))
		return;

	UTexture2D* Tex = VideoFeed->GetVideoTexture();
	float W = Tex ? static_cast<float>(Tex->GetSizeX()) : 1280.f;
	float H = Tex ? static_cast<float>(Tex->GetSizeY()) : 720.f;
	if (VideoFeed->IsStereoMode() && Tex) W *= 0.5f;

	const FDateTime NowUtc = FDateTime::UtcNow();
	uint64_t NowNs = static_cast<uint64_t>(NowUtc.ToUnixTimestamp()) * 1000000000ULL + static_cast<uint64_t>(NowUtc.GetMillisecond()) * 1000000ULL;

	FGazeSampleMsg Msg;
	Msg.frame_id     = static_cast<uint64_t>(FrameId);
	Msg.gaze_px_x    = GazeUV.X * W;
	Msg.gaze_px_y    = GazeUV.Y * H;
	Msg.timestamp_ns = NowNs;
	Msg.image_w      = static_cast<uint32_t>(W);
	Msg.image_h      = static_cast<uint32_t>(H);

	msgpack::sbuffer Buf;
	msgpack::pack(Buf, Msg);
	ComLink->SendReliable("gaze_sample", Buf, false);
}

void AOperatorPawn::HandleArmFault(uint8 DeviceIndex, FaultCode Code) {
	// Called on ComLink's receive thread. Do not touch UI, audio or the logger
	// here -- all three assume the game thread. Record and let Tick consume it.
	if (DeviceIndex < 2) {
		PendingArmFault_[DeviceIndex].Store(static_cast<int32>(Code));
	}
}

void AOperatorPawn::HandleVoiceAnnotation(const FVoiceAnnotation& Ann) {
	if (Ann.Type == 0) {
		if (Ann.Label.Equals(TEXT("start"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Idle) {
				ComLink->SendStateRequest(SysState::HOMING);
				TransitionTo(ESysState::Homing);
			}
		} else if (Ann.Label.Equals(TEXT("engage"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Awaiting) {
				CaptureControllerOrigins();
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				TransitionTo(ESysState::Engaged);
			} else if (OperatorState_ == ESysState::Paused) {
				CaptureControllerOrigins();
				ComLink->SendStateRequest(SysState::ENGAGED);
				bAvatarConfirmedEngaged_ = false;
				TransitionTo(ESysState::Engaged);
			}
		} else if (Ann.Label.Equals(TEXT("stop"), ESearchCase::IgnoreCase)) {
			ComLink->SendStateRequest(SysState::IDLE);
			TransitionTo(ESysState::Idle);
		} else if (Ann.Label.Equals(TEXT("pause"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Engaged) {
				ComLink->SendStateRequest(SysState::PAUSED);
				TransitionTo(ESysState::Paused);
			}
		} else if (Ann.Label.Equals(TEXT("reset"), ESearchCase::IgnoreCase)) {
			if (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused) {
				SendResetAll();
				if (LeftArmResetState_  == EArmResetState::Idle && ComLink->IsArmAlive(0)) LeftArmResetState_  = EArmResetState::Recovering;
				if (RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) RightArmResetState_ = EArmResetState::Recovering;
				bPendingVoiceReengage_ = true;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("reset-left"), ESearchCase::IgnoreCase)) {
			bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanReset && LeftArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(0)) {
				SendArmReset("arm_left");
				LeftArmResetState_ = EArmResetState::Recovering;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("reset-right"), ESearchCase::IgnoreCase)) {
			bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanReset && RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) {
				SendArmReset("arm_right");
				RightArmResetState_ = EArmResetState::Recovering;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("home"), ESearchCase::IgnoreCase)) {
			// Open the episode annotation panel — same as pressing homeButton.
			bool bCanAnnotate = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanAnnotate && !bAnnotationPending_) {
				bAnnotationPending_ = true;
				Hud_.SetVisibility(FName("episodeAnnotationCanvas"), true);
			}
		} else if (Ann.Label.Equals(TEXT("success"), ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("partial"),  ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("failure"),  ESearchCase::IgnoreCase)) {
			// One-shot episode annotation: works with or without "home" first.
			bool bCanAnnotate = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanAnnotate) {
				FString Label = Ann.Label;  // already "success" / "partial" / "failure"
				Hud_.SetButtonToggled(FName("homeButton"), false);
				Hud_.SetVisibility(FName("episodeAnnotationCanvas"), false);
				SendResetAll();
				if (LeftArmResetState_  == EArmResetState::Idle && ComLink->IsArmAlive(0)) LeftArmResetState_  = EArmResetState::Recovering;
				if (RightArmResetState_ == EArmResetState::Idle && ComLink->IsArmAlive(1)) RightArmResetState_ = EArmResetState::Recovering;
				SendEpisodeRestart(Label);
				++EpisodeCount_;
				bAnnotationPending_ = false;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("statistics"), ESearchCase::IgnoreCase)) {
			bStatsVisible_ = !bStatsVisible_;
			Hud_.SetVisibility(FName("statsPanel"), bStatsVisible_);
		} else if (Ann.Label.Equals(TEXT("settings"), ESearchCase::IgnoreCase)) {
			bSettingsVisible_ = !bSettingsVisible_;
			Hud_.SetVisibility(FName("settings_canvas"), bSettingsVisible_);
		} else if (Ann.Label.Equals(TEXT("viewpoint"), ESearchCase::IgnoreCase)) {
			// Toggle the camera-stream picker menu.
			if (bMenuOpen_) {
				Hud_.HideMenu();
				bMenuOpen_ = false;
				Hud_.SetButtonToggled(FName("viewpointButton"), !ActivePiPStreamName_.IsEmpty());
			} else {
				CurrentMenuStreams_ = PiPSourceNames_;
				Hud_.ShowCameraMenu(CurrentMenuStreams_, ActivePiPStreamName_);
				bMenuOpen_ = true;
				Hud_.SetButtonToggled(FName("viewpointButton"), true);
			}
		} else if (Ann.Label.Equals(TEXT("camera-left"),  ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("camera-right"), ESearchCase::IgnoreCase)) {
			// Select the PiP stream whose name contains "left" or "right".
			const FString Side = Ann.Label.Equals(TEXT("camera-left"), ESearchCase::IgnoreCase)
			                     ? TEXT("left") : TEXT("right");
			for (const FString& StreamName : PiPSourceNames_) {
				if (StreamName.Contains(Side, ESearchCase::IgnoreCase)) {
					ActivePiPStreamName_ = StreamName;
					Hud_.SetButtonToggled(FName("viewpointButton"), true);
					if (bMenuOpen_) { Hud_.HideMenu(); bMenuOpen_ = false; }
					break;
				}
			}
		} else if (Ann.Label.Equals(TEXT("camera-off"), ESearchCase::IgnoreCase)) {
			// Turn off the picture-in-picture stream.
			ActivePiPStreamName_ = TEXT("");
			Hud_.SetVisibility(FName("pip_canvas"), false);
			Hud_.SetButtonToggled(FName("viewpointButton"), false);
			if (bMenuOpen_) {
				Hud_.HideMenu();
				bMenuOpen_ = false;
			}
		}
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("VOICE_CMD label=%s conf=%.2f"), *Ann.Label, Ann.Confidence));
	} else {
		AnnotationMsg Msg;
		Msg.timestamp  = Ann.Timestamp;
		Msg.label      = TCHAR_TO_UTF8(*Ann.Label);
		Msg.atype      = Ann.Type;
		Msg.confidence = Ann.Confidence;
		Msg.score      = Ann.Score;
		Msg.frame_id   = static_cast<uint64_t>(VideoFeed->GetLastFrameId());

		msgpack::sbuffer Buf;
		msgpack::pack(Buf, Msg);
		ComLink->SendReliable("annotation", Buf, false);
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("VOICE_ANN label=%s type=%d conf=%.2f"), *Ann.Label, Ann.Type, Ann.Confidence));
	}
}

FFrameBundle AOperatorPawn::BuildFrameBundle() const {
	FFrameBundle Bundle;

	const FDateTime NowUtc = FDateTime::UtcNow();
	const int64 UnixSec = NowUtc.ToUnixTimestamp();
	Bundle.UnixTime = static_cast<double>(UnixSec)
		+ static_cast<double>(NowUtc.GetMillisecond()) / 1000.0;

	Bundle.SenderTimeNs = VideoFeed->GetSenderTimeNs();
	Bundle.FrameIdx = 0;

	const FGazeData& GazeData = Gaze->GetGazeData();
	FVector2D GazeUV(0.5f, 0.5f);
	bool bHit = false;

	if (VRCamera) {
		const FVector2D QuadSize = GetVideoQuadSize();
		bHit = FGazeProjection::ProjectOnQuad(GazeData, VideoQuadWorld_, QuadSize.X, QuadSize.Y, GazeUV);
	}

	Bundle.GazeUV = GazeUV;
	Bundle.GazeConfidence = GazeData.Confidence;
	Bundle.bGazeValid = bHit && GazeData.bIsValid;
	if (GazeData.bIsValid && GazeData.Confidence > 0.f) {
		const FTransform Quad(VideoQuadWorld_.GetRotation(), VideoQuadWorld_.GetLocation());
		const FVector O   = Quad.InverseTransformPosition(GazeData.Origin);
		const FVector Dir = Quad.InverseTransformVectorNoScale(GazeData.Direction).GetSafeNormal();
		if (!FMath::IsNearlyZero(Dir.X)) {
			const double T = -O.X / Dir.X;
			if (T > 0.0) {
				Bundle.GazePlaneCm  = FVector2D(O.Y + Dir.Y * T, -(O.Z + Dir.Z * T));
				Bundle.bGazeOnPlane = true;
			}
		}
	}
	Bundle.bHudWorldLocked = UIBinder && UIBinder->GetPlacement() == EHudPlacement::WorldLocked;
	Bundle.ViewYawDeg   = ViewYawDeg_;
	Bundle.ViewPitchDeg = ViewPitchDeg_;
	{
		const FVector2D Q = GetVideoQuadSize();
		const float D = VideoFeed ? VideoFeed->PlaneDistance : 0.f;
		if (Q.X > 0.f && Q.Y > 0.f) {
			Bundle.ViewShiftUV = FVector2D(
				 D * FMath::Tan(FMath::DegreesToRadians(ViewYawDeg_))   / Q.X,
				-D * FMath::Tan(FMath::DegreesToRadians(ViewPitchDeg_)) / Q.Y);
			Bundle.ViewShiftCm = FVector2D(
				 D * FMath::Tan(FMath::DegreesToRadians(ViewYawDeg_)),
				-D * FMath::Tan(FMath::DegreesToRadians(ViewPitchDeg_)));
		}
	}

	return Bundle;
}

FVector2D AOperatorPawn::GetVideoQuadSize() const {
	const FVector2D Live = VideoFeed ? VideoFeed->GetQuadSize() : FVector2D::ZeroVector;
	if (Live.X > 0.f && Live.Y > 0.f) return Live;
	return FVector2D(VideoQuadWidth_, VideoQuadHeight_);
}

void AOperatorPawn::UpdateHudFrame(bool bWorldLocked, const FTransform& FramePose) {
	if (HudPlacement_ != EHudPlacement::WorldLocked || !UIBinder) return;
	if (bWorldLocked) {
		UIBinder->SetWorldPose(FramePose);
		if (UIBinder->GetPlacement() != EHudPlacement::WorldLocked) {
			UIBinder->SetPlacement(EHudPlacement::WorldLocked);
			ApplyControlPanelSet(true);
		}
	} else if (UIBinder->GetPlacement() != EHudPlacement::HeadLocked) {
		UIBinder->SetPlacement(EHudPlacement::HeadLocked);
		ApplyControlPanelSet(false);
	}
}

void AOperatorPawn::SetDeviceColor(FName Name, const FLinearColor& Color) {
	FLinearColor* Prev = DeviceColors_.Find(Name);
	if (!Prev || !Prev->Equals(Color, 0.01f)) {
		DeviceColors_.Add(Name, Color);
		LastDeviceChangeSec_ = FPlatformTime::Seconds();
	}
	Hud_.SetImageColor(Name, Color);
}

void AOperatorPawn::UpdateDeviceGraphOpacity(float DeltaTime) {
	const bool bArmTrouble =
		ComLink->GetArmRemoteState(0) == SysState::FAULT || ComLink->GetArmRemoteState(1) == SysState::FAULT
		|| ComLink->IsArmStateStale(0, kArmStateStaleWarnMs);
	const bool bRecentChange = FPlatformTime::Seconds() - LastDeviceChangeSec_ < kDeviceGraphHoldSec;
	const bool bShow = OperatorState_ != ESysState::Engaged || bRecentChange || bArmTrouble;
	const float Target = bShow ? 1.f : kDeviceGraphDimOpacity;
	DeviceGraphOpacity_ = bShow ? 1.f : FMath::FInterpTo(DeviceGraphOpacity_, Target, DeltaTime, kDeviceGraphFadeSpeed);
	Hud_.SetOpacity(FName("status_canvas"), DeviceGraphOpacity_);
}

void AOperatorPawn::UpdateWristViews(float DeltaTime) {
	if (!bWristViewEnabled_ || !GhostOverlay || !VideoFeed || !Gaze) return;

	const FVector2D Q = GetVideoQuadSize();
	const float D = VideoFeed->PlaneDistance;
	const FGazeData& G = Gaze->GetGazeData();
	const bool bModeWants = WristViewMode_ == TEXT("always");

	UTexture2D* Tex[2]      = { nullptr, nullptr };
	bool        bReceiving[2] = { false, false };
	bool        bAnchor[2]    = { false, false };
	FVector2D   AnchorCm[2]   = { FVector2D::ZeroVector, FVector2D::ZeroVector };

	for (uint8 Arm = 0; Arm < 2; ++Arm) {
		const int32 Idx = WristStreamIdx_[Arm];
		if (Idx != INDEX_NONE && PiPSources_.IsValidIndex(Idx) && PiPSources_[Idx]) {
			PiPSources_[Idx]->UpdateTexture(PiPTextures_[Idx]);
			Tex[Arm] = PiPTextures_[Idx];
			bReceiving[Arm] = PiPSources_[Idx]->GetStats().bIsReceiving;
		}

		if (ComLink->IsArmAlive(Arm) && Q.X > 0.0 && Q.Y > 0.0) {
			const ArmStateMsg S = ComLink->PeekArmState(Arm);
			FVector2D UV;
			if (GhostOverlay->ProjectProtocolPointToUV(FVector(S.position[0], S.position[1], S.position[2]), UV)) {
				AnchorCm[Arm] = FVector2D((UV.X - 0.5) * Q.X, (0.5 - UV.Y) * Q.Y);
				bAnchor[Arm]  = true;
			}
		}
	}

	for (uint8 Arm = 0; Arm < 2; ++Arm)
		if (UWristViewComponent* V = WristView(Arm))
			V->UpdateView(Tex[Arm], bModeWants && bReceiving[Arm], bAnchor[Arm], AnchorCm[Arm], VideoQuadWorld_, D, bVideoLayersWorldLocked_, G, DeltaTime);
}

void AOperatorPawn::ApplyControlPanelSet(bool bWorldLocked) {
	if (!UIBinder || !UIBinder->HasWidget(FName("system_control_tray_near"))) return;
	UIBinder->SetVisibility(FName("system_control_tray"), bWorldLocked);
	UIBinder->SetVisibility(FName("system_control_tray_near"), !bWorldLocked);
}

void AOperatorPawn::UpdateLookAround(float DeltaTime) {
	if (!VRCamera || !VideoFeed) return;

	const FTransform Head(VRCamera->GetComponentQuat(), VRCamera->GetComponentLocation());
	const FQuat HeadQ = Head.GetRotation();
	const float D = VideoFeed->PlaneDistance;

	const bool bWantActive = bLookAroundEnabled_ && OperatorState_ == ESysState::Engaged;
	if (bWantActive && !bLookActive_) {
		LookAround_.Anchor(Head);
		bLookActive_     = true;
		LookReturnAlpha_ = 1.f;
		const FRotator Home = LookAround_.HomeRot.Rotator();
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("LOOK_AROUND_ANCHOR yaw=%.1f pitch=%.1f"), Home.Yaw, Home.Pitch));
	} else if (!bWantActive && bLookActive_) {
		bLookActive_         = false;
		LookReturnStartRel_  = HeadQ.Inverse() * VideoQuadWorld_.GetRotation();
		LookReturnAlpha_     = 0.f;
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("LOOK_AROUND_RELEASE view_yaw=%.1f view_pitch=%.1f"), ViewYawDeg_, ViewPitchDeg_));
	}

	bool bWorldLocked = false;
	if (bLookActive_) {
		VideoQuadWorld_ = LookAround_.ComputeQuadWorld(Head, D, ViewYawDeg_, ViewPitchDeg_);
		bWorldLocked = true;
	} else if (LookReturnAlpha_ < 1.f) {
		LookReturnAlpha_ = (LookReturnSec_ > 0.f) ? FMath::Min(1.f, LookReturnAlpha_ + DeltaTime / LookReturnSec_) : 1.f;
		const float S = FMath::SmoothStep(0.f, 1.f, LookReturnAlpha_);
		const FQuat Rel = FQuat::Slerp(LookReturnStartRel_, FQuat::Identity, S);
		const FQuat QuadRot = HeadQ * Rel;
		VideoQuadWorld_ = FTransform(QuadRot, Head.GetLocation() + QuadRot.RotateVector(FVector(D, 0.f, 0.f)));
		const FRotator View = Rel.Inverse().Rotator();
		ViewYawDeg_   = static_cast<float>(View.Yaw);
		ViewPitchDeg_ = static_cast<float>(View.Pitch);
		bWorldLocked = LookReturnAlpha_ < 1.f;
	}

	if (!bWorldLocked) {
		VideoQuadWorld_ = FVideoLookAround::FaceLockedQuadWorld(Head, D);
		ViewYawDeg_   = 0.f;
		ViewPitchDeg_ = 0.f;
	}

	if (!bLookAroundEnabled_) return;

	if (VideoFeed->IsAngularMapping())
		VideoFeed->SetLookBasis(bWorldLocked, VideoQuadWorld_.GetRotation());

	const bool bModeChanged = bWorldLocked != bVideoLayersWorldLocked_;
	bVideoLayersWorldLocked_ = bWorldLocked;

	FVideoLookAround::ApplyToLayer(VideoFeed->GetMonoLayer(), bWorldLocked, bModeChanged, VideoQuadWorld_, VRCamera, VideoFeed->PlaneDistance, 0);
	if (GhostOverlay)
		FVideoLookAround::ApplyToLayer(GhostOverlay->GetMonoLayer(), bWorldLocked, bModeChanged, VideoQuadWorld_, VRCamera, GhostOverlay->PlaneDistance, 1);

	const FQuat FrameRot = VideoQuadWorld_.GetRotation();
	UpdateHudFrame(bWorldLocked, FTransform(FrameRot, Head.GetLocation() + FrameRot.RotateVector(FVector(HudFrameDistance_, 0.f, 0.f))));
}
