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
#include "IOpenXRHMD.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
// Same reason as VideoEncoderWrapper.cpp: the A/W function macros outlive
// Hide...Types.h and leak down the unity blob. GetSystemTimes, the only Windows
// call in this file, is not one of them.
#include "Shared/WindowsMacroCleanup.h"
#endif

namespace {
// Internal VideoFeedComponent registration key for the avatar main-view
// source. The twin main-view source (when configured) is registered under
// Config->Stream.TwinStream.Name instead -- see AOperatorPawn::BeginPlay and
// the viewmodeButton handler below.
const FString kAvatarMainSourceName = TEXT("AvatarStream");
// Display text for viewmode_label when avatar is active -- kept separate
// from kAvatarMainSourceName above (that one's just VideoFeedComponent's
// internal dictionary key, not meant to be user-facing). Twin's label comes
// straight from stream.json's twin_stream.name instead (currently "TWIN"),
// so this is the one hardcoded half of the pair.
const FString kAvatarMainLabel = TEXT("AVATAR");

// Linear-space equivalent of sRGB #FF800D. SetImageColor feeds
// SetColorAndOpacity, which takes linear 0..1 -- 8-bit channel values there
// clip to white.
const FLinearColor kWarnAmber(1.f, 0.216f, 0.004f);

// Authority accent colours, linear-space equivalents of the sRGB values the
// intervention panel was designed against: POLICY #35C6E4, HUMAN #3FD47A,
// HOLD #E8B21F. Same conversion note as kWarnAmber above.
const FLinearColor kAuthorityPolicy(0.0356f, 0.5647f, 0.7758f);
const FLinearColor kAuthorityHuman (0.0497f, 0.6584f, 0.1946f);
const FLinearColor kAuthorityHold  (0.8070f, 0.4452f, 0.0137f);
// Shown when the two arms are in different states, and when authority has not
// been claimed at all. Deliberately not one of the three: a split is not a
// state, it is the absence of a single answer, and colouring it like one of the
// arms would make the HUD claim something about the other.
const FLinearColor kAuthorityMixed (0.55f, 0.55f, 0.55f);
// AGREE pips: lit in the POLICY accent, unlit near-black.
const FLinearColor kPipOff(0.15f, 0.15f, 0.15f);
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
    MSGPACK_DEFINE_MAP(frame_id, gaze_px_x, gaze_px_y, timestamp_ns)
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
	LeftTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("LeftTracked"));
	RightTracked = CreateDefaultSubobject<UTrackedControllerComponent>(TEXT("RightTracked"));

	VoiceAnnotator = CreateDefaultSubobject<UVoiceAnnotatorComponent>(TEXT("VoiceAnnotator"));
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
	}

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

	static ConstructorHelpers::FClassFinder<UUserWidget> UIClass(TEXT("/Game/UI/WBP_DebugPanel.WBP_DebugPanel_C"));
	if (UIClass.Succeeded()) {
		UIWidgetClass = UIClass.Class;
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("OperatorPawn: WBP_DebugPanel not found"));
	}
}

void AOperatorPawn::BeginPlay() {

	UTeleOpConfig* Config = NewObject<UTeleOpConfig>(this);
	if (!Config->Load(UTeleOpConfig::DefaultConfigPath())) {
		UE_LOG(LogTemp, Fatal, TEXT("OperatorPawn: Config load failed! check Config/TeleOp/ in project directory."));
		return;
	}

	RightTracked->ControlPointOffset = Config->Robot.WristPivotRight;
	LeftTracked->ControlPointOffset  = Config->Robot.WristPivotLeft;

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

	// The video quad and the ghost overlay quad are both face-locked and must be
	// sized identically, or the ghost slides off the image it annotates. These
	// used to be UPROPERTY defaults on VideoFeed that happened to equal
	// overlay.json's values; now both components read the same config fields.
	// FGazeProjection also reads VideoFeed's copies (see below), so a mismatch
	// would mistarget gaze on top of misplacing the ghost.
	VideoFeed->PlaneDistance               = Config->Overlay.PlaneDistance;
	VideoFeed->FOVCoverage                 = Config->Overlay.FOVCoverage;
	VideoFeed->HmdHFovDeg                  = Config->Overlay.HmdHFovDeg;

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

	FReceiverConfig GstConfig;
	GstConfig.Port = Config->Stream.Port;
	GstConfig.FeedbackPort = Config->Stream.FeedbackPort;
	GstConfig.SenderIP         = Config->Stream.RemoteIP;
	GstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
	GstConfig.StatusPort       = Config->Stream.StatusPort;
	VideoFeed->RegisterSource(kAvatarMainSourceName, MakeUnique<FGStreamerSource>(GstConfig));
	VideoFeed->SetStereoMode(Config->Stream.bStereo);

	// Twin's head-cam feed as a second main-view source (viewmodeButton
	// toggles between this and avatar, below) -- optional, see
	// FStreamConfig::TwinStream. Avatar was registered first above so it
	// stays VideoFeedComponent's default active source ("main camera comes
	// from avatar when available").
	if (Config->Stream.bHasTwinStream) {
		FReceiverConfig TwinGstConfig;
		TwinGstConfig.Port             = Config->Stream.TwinStream.Port;
		TwinGstConfig.FeedbackPort     = Config->Stream.TwinStream.FeedbackPort;
		// Twin often runs on a different host than the avatar (e.g. avatar
		// remote, twin on the operator's own machine) -- TwinRemoteIP is
		// optional in twin_stream's JSON; empty falls back to the avatar's
		// RemoteIP for the local dual-run test case where they coincide.
		TwinGstConfig.SenderIP = Config->Stream.TwinRemoteIP.IsEmpty()
			? Config->Stream.RemoteIP
			: Config->Stream.TwinRemoteIP;
		TwinGstConfig.ReportIntervalMs = Config->Stream.ReportIntervalMs;
		TwinGstConfig.StatusPort       = Config->Stream.TwinStream.StatusPort;
		VideoFeed->RegisterSource(Config->Stream.TwinStream.Name, MakeUnique<FGStreamerSource>(TwinGstConfig));

		// Cached for the viewmodeButton handler in UpdateStateMachine, which
		// runs long after this BeginPlay-local Config pointer is gone.
		bHasTwinMainStream_  = true;
		TwinMainStreamKey_   = Config->Stream.TwinStream.Name;
		TwinMainStreamLabel_ = Config->Stream.TwinStream.Name;
	}
	GhostOverlay->SetStereoMode(Config->Stream.bStereo);
	bOverlayEnabled_ = Config->Overlay.bEnabled;
	GhostOverlay->SetGhostEnabled(bOverlayEnabled_);

	Super::BeginPlay();

	GhostOverlay->SetGhostVisible(bOverlayEnabled_);

	if (bOverlayEnabled_) {
		LeftGraspIndicator->Initialize(GhostOverlay, ComLink, 0);
		RightGraspIndicator->Initialize(GhostOverlay, ComLink, 1);

		LeftWorkspaceBoundary->Initialize(GhostOverlay, 0);
		RightWorkspaceBoundary->Initialize(GhostOverlay, 1);
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

	UIBinder->Initialize(UIWidgetClass, VRCamera, FVector2D(Config->Hud.UIWidgetWidth, Config->Hud.UIWidgetHeight), Config->Hud.UIPlaneDistance, 1);
	PiPBaseSlotPos_ = UIBinder->GetWidgetSlotPosition(FName("pip_canvas"));
	PiPNormalSize_  = UIBinder->GetWidgetSize(FName("pip_canvas"));
	PiPCurrentSize_ = PiPNormalSize_;
	UIBinder->SetVisibility(FName("statsPanel"), false);
	UIBinder->SetVisibility(FName("pip_canvas"), false);
	UIBinder->SetVisibility(FName("cameraMenu"), false);
	UIBinder->SetVisibility(FName("resetMenu"), false);
	UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), false);
	UIBinder->SetVisibility(FName("settings_canvas"), false);
	// Warning banners. These are laid out visible in UMG so they can be
	// positioned, and nothing hid them at startup -- fault_status_canvas in
	// particular sat on screen showing its placeholder text for a whole session
	// with no fault anywhere. The Tick handlers below set them correctly from
	// the first frame, but only once the relevant feed has been heard from; this
	// makes the starting state false rather than "whatever the artist left".
	UIBinder->SetVisibility(FName("fault_status_canvas"), false);
	UIBinder->SetVisibility(FName("armStaleInfo"), false);
	UIBinder->SetVisibility(FName("videoLostInfo"), false);
	// Intervention UI. Hidden until the operator arms the session; see the
	// interventionButton handler. Both widgets are driven from the single
	// bInterventionArmed_ flag so they can never disagree about whether the
	// feature is on screen -- or, now, about whether it is live.
	UIBinder->SetVisibility(FName("interventionPanel"),     bInterventionArmed_);
	UIBinder->SetVisibility(FName("authority_pill_canvas"), bInterventionArmed_);
	// Start from a known visual state. Without this the button rests on
	// whatever its UMG Normal brush happens to be, which is only the unmuted
	// icon by convention rather than by anything enforcing it.
	UIBinder->SetButtonToggled(FName("muteButton"), bSoundMuted_);
	SoundFeedback->SetMuted(bSoundMuted_);

	// Main-view avatar/twin label -- debug-simple text readout for now (see
	// the viewmodeButton handler below for the toggle itself). Requires a
	// TextBlock named "viewmode_label" in the widget; SetText no-ops with a
	// warning if it isn't there yet, so this is safe even before that widget
	// exists.
	if (Config->Stream.bHasTwinStream)
		UIBinder->SetText(FName("viewmode_label"), *kAvatarMainLabel); // avatar is always the initial active source

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
			Src = MakeUnique<FGStreamerSource>(Cfg);
		}
		Src->Initialize();
		Src->Start();
		PiPSources_.Add(MoveTemp(Src));
		PiPTextures_.Add(nullptr);
		PiPSourceNames_.Add(S.Name);
	}

	// TWIN in the PiP menu shares the already-running main-view TWIN source's
	// decode instead of opening a second receiver on the same port (see
	// VideoFeedComponent::UpdateSourceTexture and the Tick-time display logic
	// below) -- deliberately NOT added to PiPSources_/PiPTextures_, only to
	// the name list the menu is built from.
	//
	// Menu entry is lowercased ("twin") to match the other PiP entries'
	// casing convention ("camera right", "operator", ...); TwinMainStreamLabel_
	// itself stays as configured ("TWIN") since that's what the persistent
	// viewmode_label status readout uses.
	if (bHasTwinMainStream_) {
		TwinPiPEntryName_ = TwinMainStreamLabel_.ToLower();
		PiPSourceNames_.Add(TwinPiPEntryName_);
	}

	const float LatencyWarn = Config->Hud.LatencyWarningMs;
	UIBinder->BindPlot(FName("videoLatencyPlot"), LatencyHistory.GetSamplesPtr(), nullptr, LatencyHistory.Capacity(), LatencyHistory.GetHeadPtr(), 0.0f, LatencyWarn * 2.0f);
	UIBinder->SetPlotThreshold(FName("videoLatencyPlot"), LatencyWarn);

	const float DataLatencyWarn = Config->Hud.DataLatencyWarningMs;
	UIBinder->BindPlot(FName("dataLatencyPlot"), JitterHistory.GetSamplesPtr(), nullptr, JitterHistory.Capacity(), JitterHistory.GetHeadPtr(), 0.0f, DataLatencyWarn * 2.0f);
	UIBinder->SetPlotThreshold(FName("dataLatencyPlot"), DataLatencyWarn);

	UIBinder->BindPlot(FName("dataFpsPlot"), FpsHistory.GetSamplesPtr(), nullptr, FpsHistory.Capacity(), FpsHistory.GetHeadPtr(), 0.0f, 60.0f);
	UIBinder->SetPlotThreshold(FName("dataFpsPlot"), 30.0f);

	// CPU and GPU utilization — 0-100%, threshold at 80%.
	// GPU is normalised against a 90 Hz VR frame budget (11.11 ms = 100%).
	UIBinder->BindPlot(FName("dataCpuPlot"), CpuHistory.GetSamplesPtr(), nullptr, CpuHistory.Capacity(), CpuHistory.GetHeadPtr(), 0.0f, 100.0f);
	UIBinder->SetPlotThreshold(FName("dataCpuPlot"), 80.0f);
	UIBinder->BindPlot(FName("dataGpuPlot"), GpuHistory.GetSamplesPtr(), nullptr, GpuHistory.Capacity(), GpuHistory.GetHeadPtr(), 0.0f, 100.0f);
	UIBinder->SetPlotThreshold(FName("dataGpuPlot"), 90.0f);  // GPU utilisation %

	UIBinder->BindPlot(FName("dataGpuTempPlot"), GpuTempHistory.GetSamplesPtr(), nullptr, GpuTempHistory.Capacity(), GpuTempHistory.GetHeadPtr(), 0.0f, 100.0f);
	UIBinder->SetPlotThreshold(FName("dataGpuTempPlot"), 83.0f);  // GPU temp °C — throttle onset

	// The avatar's authority echo. It publishes authority to the orchestrator
	// inside SceneObjectsMsg every tick, but that is a different socket and a
	// different consumer, and re-asserting on the reliable command channel at
	// 100 Hz would be a hundred acks a second. So the interface gets edges.
	//
	// Edges specifically, not just replies to our own requests: ArmControl
	// changes authority on its own when the staleness watchdog fires, and an
	// echo driven from the request handler would never report that.
	//
	// Runs on the command-link receive thread, so it only touches atomics.
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

		// Anything other than UNSET means SOMEONE is gating this arm, so an
		// intervention session is already running whether or not this interface
		// has been told about it. See MaybeAutoArmIntervention.
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
	FGazeProjection::ComputeQuadSize(VideoFeed->PlaneDistance, VideoFeed->FOVCoverage, AspectRatio, VideoQuadWidth_, VideoQuadHeight_, VideoFeed->HmdHFovDeg);
	if (Config->Stream.bVideoLogEnabled) {
		FActorSpawnParameters Params;
		Params.Owner = this;
		VideoLogger_ = GetWorld()->SpawnActor<AVideoLogger>(AVideoLogger::StaticClass(), FTransform::Identity, Params);
	}

	if (VideoLogger_) {
		FVector2D VideoUVOffset = FVector2D::ZeroVector;
		FVector2D VideoUVSize   = FVector2D::UnitVector;
		if (VideoFeed->IsStereoMode()) {
			VideoUVOffset = FVector2D(0.0f, 0.0f);
			VideoUVSize   = FVector2D(0.5f, 1.0f);
		}
		VideoLogger_->AddLayerSource([this]() -> UTexture* { return VideoFeed->GetVideoTexture(); }, 0, VideoUVOffset, VideoUVSize);
		if (VideoFeed->IsStereoMode()) {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return GhostOverlay->IsGhostVisible() ? static_cast<UTexture*>(GhostOverlay->GetRenderTargetLeft()) : nullptr; }, 1);
		} else {
			VideoLogger_->AddLayerSource([this]() -> UTexture* { return GhostOverlay->IsGhostVisible() ? static_cast<UTexture*>(GhostOverlay->GetRenderTarget()) : nullptr; }, 1);
		}
		VideoLogger_->AddLayerSource([this]() -> UTexture* { return static_cast<UTexture*>(UIBinder->GetRenderTarget()); }, 2);

		VideoLogger_->QuadWidth = VideoQuadWidth_;
		VideoLogger_->QuadHeight = VideoQuadHeight_;
		VideoLogger_->PlaneDistance = VideoFeed->PlaneDistance;

		UE_LOG(LogTemp, Log, TEXT("OperatorPawn: VideoLogger spawned"));
	}

	Logger_ = MakeUnique<FTeleOpLogger>();
	FString SessionDir = Logger_->Open(FPaths::ProjectDir() / LogBaseDirectory);

	CommandThread_ = MakeUnique<FTeleopCommandThread>(ComLink, Logger_.Get(), CommandThreadRateHz);
	CommandThread_->StartThread();
	UE_LOG(LogTemp, Log, TEXT("OperatorPawn: logging to %s"), *SessionDir);

	if (VideoLogger_) {
		VideoLogger_->StartLogging(SessionDir);
	}

	SessionStartTime_ = FPlatformTime::Seconds();
}


void AOperatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason) {
	FlushRenderingCommands();
	for (auto& Src : PiPSources_) if (Src) Src->Stop();
	if (VideoLogger_) VideoLogger_->StopLogging(TEXT("EndPlay"));
	if (CommandThread_) CommandThread_->StopThread();
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

	UpdateStateMachine();

	FVideoSourceStats Stats = VideoFeed->GetStreamStats();
	LatencyHistory.Push(Stats.OneWayLatencyMs);
	JitterHistory.Push(ComLink->GetArmStateLatencyMs(0));
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
	UIBinder->SetGazeInput(GazeData);

	if (ComLink->GetAvatarState() == ESysState::Engaged) UIBinder->SetImageColor(FName("avatar_torso"), FLinearColor::Green);
	else if (OperatorState_ == ESysState::Homing || OperatorState_ == ESysState::Awaiting)
		UIBinder->SetImageColor(FName("avatar_torso"), kWarnAmber);
	else UIBinder->SetImageColor(FName("avatar_torso"), FLinearColor::Red);

	UIBinder->SetImageColor(FName("avatar_eye"), VideoFeed->IsReceiving() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_left_arm"), ComLink->GetArmRemoteState(0) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_right_arm"), ComLink->GetArmRemoteState(1) == SysState::ENGAGED ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("avatar_head"), ComLink->IsHeadAlive() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetVisibility(FName("videoLostInfo"), !VideoFeed->IsReceiving());

	// --- stale-state alarm (Tick) -------------------------------------------
	// Packets arriving is not the same as the robot being alive. The avatar
	// publishes arm state from its 200 Hz state thread, which keeps running --
	// and keeps stamping fresh send timestamps -- even when the 1 kHz control
	// thread has faulted. Every link indicator above stays green in that case.
	//
	// On 2026-08-09 the avatar's control loop died at t=404.7 s and the
	// operator kept commanding for another 2.5 s: data_msg_rate_hz 200.1,
	// data_latency_ms 50.4, video 28 fps, all nominal.
	//
	// sample_time_ns is stamped by the control thread when it reads the robot,
	// so it is the one quantity that stops advancing. Alarm on its age, and do
	// it audibly -- the existing arm indicators are small passive colour
	// swatches competing with six live numeric plots for attention.
	{
		const bool bStale = ComLink->IsArmStateStale(0, kArmStateStaleWarnMs);
		UIBinder->SetVisibility(FName("armStaleInfo"), bStale);
		if (bStale && ComLink->GetArmRemoteState(0) != SysState::FAULT) {
			UIBinder->SetImageColor(FName("avatar_left_arm"), kWarnAmber);
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

	// --- remote fault, drained from the receive thread ----------------------
	// Distinct from the staleness alarm above: staleness catches a control
	// loop that stopped without saying so, this catches one that explicitly
	// reported FAULT. Either alone would have surfaced the 2026-08-09 failure.
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
			// Reset watchdog. LeftArmResetState_/RightArmResetState_ only ever
			// returned to Idle on a "reset_complete" device_event, so a reset
			// that failed left the arm stuck in Recovering for the rest of the
			// session: the reset button stays locked via bResetting, stops
			// showing hover, and SendArmCommands suppresses that arm entirely.
			// That is what happened to arm_right in session 002 -- the recovery
			// re-faulted 192 ms in, so reset_complete never arrived.
			//
			// The re-fault check is delayed: the avatar is still in FAULT for a
			// moment after the request, before it transitions to RECOVERING, so
			// without a grace period every reset would abort itself instantly.
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
					// The widgets only follow LeftArmResetState_/RightArmResetState_
					// when something calls this. Clearing the state without it left
					// the button logically armed but still drawn locked and reading
					// "Resetting R..." -- which is how a reset that had already been
					// aborted still looked dead.
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
		// The widget in WBP_DebugPanel is called fault_status_canvas, not
		// armFaultInfo. UWidgetBinder::SetVisibility no-ops on a name it does not
		// hold, so this call had been doing nothing since it was written, and the
		// banner sat on screen showing its design-time placeholder text
		// ("RIGHT ARM EXTERNAL FORCE") for the whole session regardless of
		// whether anything had faulted.
		//
		// Both names are driven. The real one does the work; armFaultInfo stays
		// in case the asset ever grows a widget by that name, and costs one
		// failed TMap lookup.
		UIBinder->SetVisibility(FName("fault_status_canvas"), bFaultLatched);
		UIBinder->SetVisibility(FName("armFaultInfo"), bFaultLatched);
		if (bFaultLatched) {
			const bool bLeft  = ComLink->GetArmRemoteState(0) == SysState::FAULT;
			const bool bRight = ComLink->GetArmRemoteState(1) == SysState::FAULT;
			UIBinder->SetText(FName("fault_status_text"),
				(bLeft && bRight) ? TEXT("BOTH ARMS FAULTED")
				: bLeft           ? TEXT("LEFT ARM FAULTED")
				                  : TEXT("RIGHT ARM FAULTED"));
		}
	}

	const bool bGazeFresh = Gaze->IsGazeFresh();
	UIBinder->SetImageColor(FName("operator_eye"), bGazeFresh ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetVisibility(FName("gazeOfflineInfo"), !bGazeFresh);
	UIBinder->SetImageColor(FName("operator_left_arm"), LeftTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("operator_right_arm"), RightTracked->IsTracking() ? FLinearColor::Green : FLinearColor::Red);
	UIBinder->SetImageColor(FName("operator_head"), bHMDOriginValid_ ? FLinearColor::Green : FLinearColor::Red);

	ESysState av_state = ComLink->GetAvatarState();
	if (av_state == ESysState::Engaged) UIBinder->SetImageColor(FName("operator_torso"), FLinearColor::Green);
	else if (av_state == ESysState::Homing || av_state == ESysState::Awaiting)
		UIBinder->SetImageColor(FName("operator_torso"), kWarnAmber);
	else UIBinder->SetImageColor(FName("operator_torso"), FLinearColor::Red);

	static const TArray<FString> HealthLabels = { TEXT("NO SIGNAL"), TEXT("NORMAL"), TEXT("DEGRADED"), TEXT("CRITICAL"), TEXT("STALE") };
	static const TArray<FLinearColor> HealthColors = { FLinearColor::Gray, FLinearColor::Green, FLinearColor::Yellow, FLinearColor::Red, FLinearColor::Gray };

	uint8 HealthIdx = Stats.StreamHealthState;
	if (HealthIdx < HealthLabels.Num()) {
		UIBinder->SetText(FName("stream_health_value"), *HealthLabels[HealthIdx]);
		UIBinder->SetTextColor(FName("stream_health_value"), HealthColors[HealthIdx]);
	}

	// Before UpdateInfoBar, so the pill renders this frame's authority rather
	// than last frame's. Every tick, not on the periodic stream-row cadence:
	// the gap between a trigger pull and the takeover is latency the operator
	// feels directly.
	// Before UpdateAuthority: arming is what lets UpdateAuthority do anything
	// at all, so the tick that discovers the session must not also throw away
	// that tick's clutch edge.
	MaybeAutoArmIntervention();
	UpdateAuthority();
	UpdatePolicyStats(DeltaTime);
	// After UpdateAuthority, so that a release and a chord landing in the same
	// frame are processed in the order they physically happened: the release
	// sends HOLD, then the chord sends POLICY. The reverse order would put a
	// HOLD after the resume and freeze the robot the operator just handed
	// back -- with nothing on screen to say why.
	PollResumeButton();

	UpdateInfoBar();

	if (!ActivePiPStreamName_.IsEmpty()) {
		if (bHasTwinMainStream_ && ActivePiPStreamName_ == TwinPiPEntryName_) {
			// Shared decode with the main view -- see VideoFeedComponent::
			// UpdateSourceTexture. No separate receiver, no extra decode.
			VideoFeed->UpdateSourceTexture(TwinMainStreamKey_, PiPSharedTwinTexture_);
			const bool bReceiving = VideoFeed->GetSourceStats(TwinMainStreamKey_).bIsReceiving;
			UIBinder->SetVisibility(FName("pip_canvas"), bReceiving);
			if (bReceiving && PiPSharedTwinTexture_)
				UIBinder->SetImageTexture(FName("pip_image"), PiPSharedTwinTexture_);
		} else {
			const int32 Idx = PiPSourceNames_.IndexOfByKey(ActivePiPStreamName_);
			if (Idx != INDEX_NONE && PiPSources_.IsValidIndex(Idx)) {
				PiPSources_[Idx]->UpdateTexture(PiPTextures_[Idx]);
				const bool bReceiving = PiPSources_[Idx]->GetStats().bIsReceiving;
				UIBinder->SetVisibility(FName("pip_canvas"), bReceiving);
				if (bReceiving && PiPTextures_[Idx])
					UIBinder->SetImageTexture(FName("pip_image"), PiPTextures_[Idx]);
			}
		}
	}

	if (!ActivePiPStreamName_.IsEmpty() && PiPNormalSize_.X > 0.f) {
		const bool bOver    = UIBinder->IsGazeOverWidget(FName("pip_canvas"), PiPGazeInnerMarginPx);
		const bool bFarAway = !UIBinder->IsGazeOverWidget(FName("pip_canvas"), PiPGazeOuterMarginPx);
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
		UIBinder->SetWidgetRenderScale(FName("pip_canvas"), Scale, Pivot);
	}

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
			UIBinder->SetText(FName("gear_left_value"), FString::Printf(TEXT("%.1f"), LeftGear));
		}
		if (RightGear != PrevRightGear_) {
			Logger_->LogEvent(FString::Printf(TEXT("GEAR_CHANGE side=right gear=%.1f"), RightGear));
			PrevRightGear_ = RightGear;
			UIBinder->SetText(FName("gear_right_value"), FString::Printf(TEXT("%.1f"), RightGear));
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

		// The most recent command the command thread actually put on the wire,
		// already in protocol coordinates and already yaw-corrected. Sampled at
		// frame rate, so this row is a decimated view of a faster stream; the
		// command log is the full-rate record.
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
		Row.DataMsgRateHz = ComLink->GetArmMsgRateHz(0);

		ArmStateMsg LeftState = ComLink->PeekArmState(0);
		Row.LeftGripperWidth = LeftState.gripper_width;
		Row.LeftGripperGraspState = static_cast<uint8>(LeftGraspIndicator->GetDisplayState());

		ArmStateMsg RightState = ComLink->PeekArmState(1);
		Row.RightGripperWidth = RightState.gripper_width;
		Row.RightGripperGraspState = static_cast<uint8>(RightGraspIndicator->GetDisplayState());

		// --- remote health -------------------------------------------------
		// Everything above describes the LINK. These describe the AVATAR. On
		// 2026-08-09 the two disagreed for 2.5 s and nothing recorded it, so
		// afterwards it was impossible to establish from the logs whether the
		// avatar had ever reported its fault.
		Row.ArmRemoteState    = static_cast<uint8>(ComLink->GetArmRemoteState(0));
		Row.ArmRemoteFault    = static_cast<uint8>(ComLink->GetArmRemoteFault(0));
		Row.ArmDroppedPackets = static_cast<uint32>(ComLink->GetArmDroppedPackets(0));
		Row.ArmStateAgeMs     = ComLink->GetArmStateAgeMs(0);

		// Which source VideoLatencyMs actually refers to. The viewmode toggle
		// swaps between the remote avatar feed and the loopback twin feed, and
		// the reported latency changes by ~6x across that switch.
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

	FName ButtonPressed = UIBinder->ConsumePress();
	FName ButtonRejected = UIBinder->ConsumeRejection();
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
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			if (bResetMenuOpen_) {
				UIBinder->HideMenu();
				UIBinder->SetButtonToggled(FName("resetButton"), false);
				bResetMenuOpen_ = false;
			} else {
				UIBinder->ShowResetMenu();
				UIBinder->SetButtonToggled(FName("resetButton"), true);
				bResetMenuOpen_ = true;
			}
		}
		else if (ButtonPressed == FName("homeButton") && !bAnnotationPending_) {
			bAnnotationPending_ = true;
			UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), true);
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
		else if (ButtonPressed == FName("resetButtonLeft") && LeftArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButtonRight") && RightArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("resetButton")) {
			if (bResetMenuOpen_) {
				UIBinder->HideMenu();
				UIBinder->SetButtonToggled(FName("resetButton"), false);
				bResetMenuOpen_ = false;
			} else {
				UIBinder->ShowResetMenu();
				UIBinder->SetButtonToggled(FName("resetButton"), true);
				bResetMenuOpen_ = true;
			}
		}
		else if (ButtonPressed == FName("homeButton") && !bAnnotationPending_) {
			bAnnotationPending_ = true;
			UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), true);
		}
		break;

	default:
		break;
	}

	if (ButtonPressed == FName("statisticsButton")) {
		bStatsVisible_ = !bStatsVisible_;
		UIBinder->SetVisibility(FName("statsPanel"), bStatsVisible_);
	}

	if (ButtonPressed == FName("settingButton")) {
		bSettingsVisible_ = !bSettingsVisible_;
		UIBinder->SetVisibility(FName("settings_canvas"), bSettingsVisible_);
	}

	// Arms or disarms the intervention session. This is the switch that makes
	// authority live at all, so both edges move the robot's ownership and both
	// are deliberate:
	//
	//   arming  -> HOLD on every arm. Latches enforcement on at the avatar and
	//              closes both gates, so nothing moves until the operator pulls
	//              a trigger (HUMAN) or presses RESUME (POLICY). Claiming
	//              POLICY here would start the policy driving on a button press
	//              that only meant "show me the panel".
	//   disarming -> HUMAN on every arm. There is no way back to UNSET by
	//              design, so the honest way to switch the feature off is to
	//              hand the arms to the operator, which is where they were
	//              before any of this.
	if (ButtonPressed == FName("interventionButton")) {
		bInterventionArmed_ = !bInterventionArmed_;
		UIBinder->SetVisibility(FName("interventionPanel"),     bInterventionArmed_);
		UIBinder->SetVisibility(FName("authority_pill_canvas"), bInterventionArmed_);
		UIBinder->SetButtonToggled(FName("interventionButton"), bInterventionArmed_);

		const EControlAuthority Target = bInterventionArmed_ ? EControlAuthority::Hold
		                                                      : EControlAuthority::Human;
		// Straight to ComLink, not RequestArmAuthority: that one re-anchors the
		// retarget on a takeover, and arming is not a takeover.
		//
		// Whole-body sends ONE request with the device key omitted. Two would
		// also work -- the avatar fans each out over every arm -- but one
		// message is the invariant this mode rests on: there is no instant,
		// however brief, at which the two arms sit on different sides of the
		// switch.
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

	// Hands arms back to the policy. Only reachable while armed, because the
	// panel it lives on is hidden otherwise -- and hidden widgets no longer
	// answer the gaze ray (see UWidgetBinder::CacheWidgetRects).
	//
	// Every arm the operator is NOT holding. An arm actively clutched into reads
	// HUMAN and is left alone -- one button that could take it out from under a
	// hand would defeat the point of per-arm authority, since holding one arm
	// while the other runs is the whole feature. An arm already in POLICY is a
	// no-op anyway (setAuthority early-returns on an unchanged value).
	//
	// Tests "not HUMAN" rather than "is HOLD", and the difference matters more
	// than it looks. What is tested here is the avatar's ECHO, so if that echo
	// has not arrived -- older avatar build, arming request lost, or the
	// reliable channel answering a different client -- every arm reads UNSET.
	// Keyed on HOLD, RESUME then silently did nothing, forever, with not even a
	// log line to say why. That is exactly what the first run showed: three
	// presses, zero AUTHORITY_REQUEST entries in the event log. Re-asserting
	// from UNSET is harmless and is the only way out of that hole.
	//
	// It is also why one button is enough rather than one per arm: "give back
	// everything I am no longer holding" is unambiguous, and the operator's hand
	// already says which arms those are.
	//
	// Whole-body collapses that to the same sentence about one robot: hand it
	// back unless a hand is still on it. The head follows without being named
	// -- IsOperatorHoldingHead reads the same authority, so the neck returns to
	// the policy on this press too.
	if (ButtonPressed == FName("resumePolicyButton") && bInterventionArmed_) {
		ResumePolicy(TEXT("button"));
	}

	// Global mute. USoundFeedback::SetMuted has existed since that class was
	// written and nothing ever called it; this is the caller.
	//
	// The acknowledgement has to be played on whichever side of the toggle can
	// still be heard -- before the mute goes on, after it comes off -- or the
	// press that enables mute gives no feedback at all. PlaySound2D has already
	// spawned the sound by the time SetMuted lands, so the first one survives.
	if (ButtonPressed == FName("muteButton")) {
		bSoundMuted_ = !bSoundMuted_;
		if (bSoundMuted_) SoundFeedback->Play(ESoundType::Click);
		SoundFeedback->SetMuted(bSoundMuted_);
		if (!bSoundMuted_) SoundFeedback->Play(ESoundType::Confirm);

		UIBinder->SetButtonToggled(FName("muteButton"), bSoundMuted_);
		UIBinder->PushMessage(bSoundMuted_ ? TEXT("SOUND MUTED") : TEXT("SOUND ON"), 2.0f);
		if (Logger_) Logger_->LogEvent(FString::Printf(TEXT("MUTE state=%s"),
			bSoundMuted_ ? TEXT("on") : TEXT("off")));
	}

	if (ButtonPressed == FName("viewpointButton")) {
		if (bMenuOpen_) {
			UIBinder->HideMenu();
			bMenuOpen_ = false;
			UIBinder->SetButtonToggled(FName("viewpointButton"), !ActivePiPStreamName_.IsEmpty());
		} else {
			CurrentMenuStreams_ = PiPSourceNames_;
			UIBinder->ShowCameraMenu(CurrentMenuStreams_, ActivePiPStreamName_);
			bMenuOpen_ = true;
			UIBinder->SetButtonToggled(FName("viewpointButton"), true);
		}
	}

	// Main-view avatar/twin toggle. Two-way toggle, no menu -- see
	// VideoFeedComponent::SetActiveSource: this is a cheap pointer swap
	// between two already-running sources, safe to call as often as wanted
	// (later: driven by the recommendation system instead of this button).
	// No-op when no twin_stream is configured (stream.json), so avatar-only
	// deployments are unaffected.
	if (ButtonPressed == FName("viewmodeButton") && bHasTwinMainStream_) {
		const bool bCurrentlyAvatar = (VideoFeed->GetActiveSourceName() == kAvatarMainSourceName);
		// SourceKey selects which registered source becomes active (must match
		// RegisterSource's key); Label is the separate user-facing text.
		const FString TargetSourceKey = bCurrentlyAvatar ? TwinMainStreamKey_   : kAvatarMainSourceName;
		const FString TargetLabel     = bCurrentlyAvatar ? TwinMainStreamLabel_ : kAvatarMainLabel;
		if (VideoFeed->SetActiveSource(TargetSourceKey)) {
			UIBinder->SetText(FName("viewmode_label"), *TargetLabel);
			UIBinder->SetButtonToggled(FName("viewmodeButton"), bCurrentlyAvatar);
			GhostOverlay->SetGhostVisible(!bCurrentlyAvatar);
		}
	}

	if (ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("__menu_"))) {
		if (ButtonPressed == FName("__menu_off")) {
			ActivePiPStreamName_ = TEXT("");
			UIBinder->SetVisibility(FName("pip_canvas"), false);
			UIBinder->SetButtonToggled(FName("viewpointButton"), false);
		} else {
			for (int32 i = 0; i < CurrentMenuStreams_.Num(); ++i) {
				if (ButtonPressed == FName(*FString::Printf(TEXT("__menu_%d"), i))) {
					ActivePiPStreamName_ = CurrentMenuStreams_[i];
					break;
				}
			}
			UIBinder->SetButtonToggled(FName("viewpointButton"), true);
		}
		UIBinder->HideMenu();
		bMenuOpen_ = false;
	}

	bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
	if (bCanReset && ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("__reset_"))) {
		if (ButtonPressed == FName("__reset_left") && LeftArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_left");
			LeftArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("__reset_right") && RightArmResetState_ == EArmResetState::Idle) {
			SendArmReset("arm_right");
			RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		else if (ButtonPressed == FName("__reset_all")) {
			SendResetAll();
			if (LeftArmResetState_ == EArmResetState::Idle) LeftArmResetState_ = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
			UpdateButtonStates();
		}
		bResetMenuOpen_ = false;
		UIBinder->HideMenu();
		UIBinder->SetButtonToggled(FName("resetButton"), false);
	}

	if (bAnnotationPending_ && ButtonPressed != FName() && ButtonPressed.ToString().StartsWith(TEXT("episode_"))) {
		FString Label;
		if      (ButtonPressed == FName("episode_success")) Label = TEXT("success");
		else if (ButtonPressed == FName("episode_partial")) Label = TEXT("partial");
		else if (ButtonPressed == FName("episode_failure")) Label = TEXT("failure");

		if (!Label.IsEmpty()) {
			UIBinder->SetButtonToggled(ButtonPressed, false);
			UIBinder->SetButtonToggled(FName("homeButton"), false);
			SendResetAll();
			if (LeftArmResetState_  == EArmResetState::Idle) LeftArmResetState_  = EArmResetState::Recovering;
			if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
			SendEpisodeRestart(Label);
			++EpisodeCount_;
			bAnnotationPending_ = false;
			UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), false);
			UpdateButtonStates();
		}
	}
}

void AOperatorPawn::UpdateInfoBar() {
	UIBinder->SetText(FName("ep_value"), FString::Printf(TEXT("%03d"), EpisodeCount_));

	int32 ElapsedSec = static_cast<int32>(FPlatformTime::Seconds() - SessionStartTime_);
	int32 Hours      = ElapsedSec / 3600;
	int32 Minutes    = (ElapsedSec % 3600) / 60;
	int32 Seconds    = ElapsedSec % 60;
	UIBinder->SetText(FName("session_time_value"), FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Seconds));

	float RttMs = ComLink->GetArmRttMs(1);
	if (RttMs <= 0.f) RttMs = ComLink->GetArmRttMs(0);
	float LatencyMs = RttMs * 0.5f;
	if (LatencyMs <= 0.f) LatencyMs = ComLink->GetArmStateLatencyMs(1);
	if (LatencyMs <= 0.f) LatencyMs = ComLink->GetArmStateLatencyMs(0);
	if (LatencyMs > 0.f)
		UIBinder->SetText(FName("latency_value"), FString::Printf(TEXT("%.1f ms"), LatencyMs));
	else
		UIBinder->SetText(FName("latency_value"), TEXT("-- ms"));

	FLinearColor DotColor;
	if      (LatencyMs <= 0.f) DotColor = FLinearColor::Gray;
	else if (LatencyMs < 30.f) DotColor = FLinearColor::Green;
	else if (LatencyMs < 80.f) DotColor = FLinearColor::Yellow;
	else                       DotColor = FLinearColor::Red;
	UIBinder->SetTextColor(FName("latency_dot"), DotColor);

	if (!bInterventionArmed_) return;

	// Whole-body handover has no left and no right to show.
	//
	// That is the point of the mode rather than an omission from it: the arms
	// change hands in one message and can only ever be in the same state, so a
	// HUD that still offered a per-arm readout would be inviting the operator
	// to look for a distinction the system no longer makes. One word, one
	// colour, one thing to check.
	//
	// The split form below is not deleted, only unreachable while the flag is
	// set -- flipping bWholeBodyAuthority back brings the whole per-limb
	// presentation with it, which is the deal: per-limb is parked, not removed.
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

		// One string, used by both readouts, and NEVER wider than the widest word it
		// already had to fit.
		//
		// Agreeing, it is the word: POLICY / HUMAN / HOLD, 5-6 characters, which is
		// what the panel and the pill were laid out for. Disagreeing, it is a
		// five-character positional code -- "L= RH" is left HOLD, right HUMAN --
		// where P is policy, H is human, = is hold and ? is unclaimed.
		//
		// The first attempt spelled the split out as "L HOLD  R HUMAN", which is
		// fourteen characters in a box built for five: it burst the panel and pushed
		// the layout outside its own background. Width is not negotiable in a HUD
		// that sits over the video, so the split form is budgeted against the agree
		// form rather than written out.
		//
		// Nothing is lost by the abbreviation. Position carries which arm, the
		// letter carries its state, and the colour going grey -- none of the three
		// state colours -- says "these two differ, read the letters". A single word
		// standing in for two arms is the one thing this must never do: the pill
		// reading POLICY while a hand is driving the left arm is the most dangerous
		// sentence this HUD could write.
		AuthorityLabel = bAgree
			? AuthorityToString(L)
			: FString::Printf(TEXT("L%s R%s"), *AuthorityToInitial(L), *AuthorityToInitial(R));
	}

	UIBinder->SetText(FName("authority_pill_state"), AuthorityLabel);
	UIBinder->SetTextColor(FName("authority_pill_state"), Accent);
	// Dot and fill get all three setters on purpose.
	//
	// UWidgetBinder caches by widget TYPE, and each setter silently no-ops when
	// the name is not in its map -- so calling SetTextColor on a widget that is
	// actually a UImage does nothing at all, and the widget keeps whatever
	// colour it was given in UMG at design time. That is precisely what the
	// first run showed: the pill dot stayed amber and the panel fill stayed
	// green while both were supposed to be reading the same state, so the two
	// halves of the HUD disagreed with each other and with reality.
	//
	// Guessing the type from the outside is how that happens again the next time
	// the asset is edited. Three calls, at most one of which does anything, is
	// cheaper than a HUD that lies.
	SetWidgetAccent(FName("authority_pill_dot"), Accent);

	// Same string as the pill, deliberately. Two readouts of one fact that could
	// word it differently are two chances to disagree with each other, which is
	// how the first version ended up with an amber dot next to a green panel.
	UIBinder->SetText(FName("intervention_authority_text"), AuthorityLabel);
	SetWidgetAccent(FName("intervention_authority_fill"), Accent);

	UIBinder->SetText(FName("interv_value"), FString::Printf(TEXT("%d"), InterventionCount_));
	SetWidgetAccent(FName("intervention_rec_dot"),
		bRecordingActive_ ? FLinearColor(0.8f, 0.05f, 0.05f) : FLinearColor(0.15f, 0.15f, 0.15f));
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
	UIBinder->SetText(FName("auton_value"), FString::Printf(TEXT("%d%%"), AutonPct));

	const bool  bFresh = FPlatformTime::Seconds() - PolicyStatusTime_.load() < kPolicyStatusStaleSec;
	const float InfMs  = PolicyInferenceMs_.load();
	UIBinder->SetText(FName("inference_value"),
		bFresh && InfMs >= 0.f ? FString::Printf(TEXT("%.0fms"), InfMs) : FString(TEXT("--")));

	const float Agree = PolicyAgree_.load();
	const int32 Lit   = bFresh && Agree >= 0.f
		? FMath::Clamp(FMath::RoundToInt(Agree * kAgreePips), 0, kAgreePips) : 0;
	for (int32 i = 0; i < kAgreePips; ++i)
		SetWidgetAccent(FName(*FString::Printf(TEXT("agree_pip_%d"), i)), i < Lit ? kAuthorityPolicy : kPipOff);
}

void AOperatorPawn::SetWidgetAccent(FName WidgetName, const FLinearColor& Color) const {
	// One colour, every surface that could be carrying it. UWidgetBinder's
	// setters are keyed by widget type and no-op on a miss, so this is
	// type-agnostic by construction rather than by us guessing right.
	UIBinder->SetTextColor(WidgetName, Color);
	UIBinder->SetImageColor(WidgetName, Color);
	UIBinder->SetBorderColor(WidgetName, Color);
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
	// Whole-body: one request covers every arm. The avatar reads an omitted
	// "device" key as "all arms", so the two modes differ by a single flag on
	// the wire rather than by two code paths that can drift apart -- and,
	// more importantly, the arms change hands in the SAME message, so there is
	// no window in which one arm is human and the other is still policy.
	ComLink->SendAuthorityRequest(ArmIndex, Requested, bWholeBodyAuthority);

	// Re-anchor this arm's retarget, but ONLY when taking over from something
	// that was actually holding the arm.
	//
	// The commanded pose is T_origin_ (avatar) composed with the banked delta
	// (command thread). On a takeover the avatar re-origins onto where the
	// POLICY left the arm, while the command thread still holds the delta from
	// the operator's last segment -- reset one and not the other and the arm
	// steps by exactly the distance the policy moved it. Reset both at the same
	// instant and it does not move at all.
	//
	// The order works out on its own: the VR gate is still closed while the
	// avatar processes this request, so any command the thread emits in between
	// is discarded, and the gate only opens after reOrigin() has run.
	//
	// The Policy/Hold test is load-bearing. From Unset nothing is being gated,
	// which is ordinary teleoperation -- zeroing the banked delta there would
	// snap the arm back to the operator's engage origin, a jump this code is
	// supposed to prevent.
	if (Requested == EControlAuthority::Human && CommandThread_
		&& (Current == EControlAuthority::Policy || Current == EControlAuthority::Hold)) {
		// -1 re-anchors BOTH retargets, which is what a whole-body takeover
		// needs: the avatar re-origins every arm it just handed over, so every
		// arm's banked delta has to be zeroed in the same breath or the arm
		// that was not re-anchored steps by however far the policy moved it.
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
	// Re-seed the grasp toggle from the gripper the operator is ACTUALLY
	// inheriting. ArmIndex -1 does both arms.
	//
	// bGripHeld is a latch: the trackpad button flips it, and nothing else
	// ever did. It is sent to the avatar on every command-thread tick, so it
	// survives an entire policy phase unchanged while the policy opens and
	// closes the real gripper underneath it.
	//
	// The result is that a takeover applied a grasp state minutes old. In
	// practice the operator almost always ends an intervention having grasped
	// something, so the latch sits true, and the very first frame after every
	// subsequent takeover slammed the gripper shut -- on whatever the policy
	// happened to be reaching for. It never appeared in the event log either,
	// because GRASP is logged on CHANGE and the latch had not changed.
	//
	// Threshold matches run.py's _GRIPPER_CLOSE_THRESHOLD_M: gripper_cmd is a
	// binary flag on the wire and the sim logs 0.0 or 0.08, so 0.04 is the
	// midpoint and the same number decides "closed" on both sides.
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

	// They should never disagree in this mode -- one request covers both arms
	// and the avatar applies it atomically. If they do, something refused a
	// claim or a state echo was lost, and the honest answer is the one that
	// keeps the operator's hands engaged: Human if either arm is human, and
	// Hold otherwise. Reporting Policy here is the one outcome that could put
	// a hand on a moving arm the operator believes is not theirs.
	if (L == EControlAuthority::Human || R == EControlAuthority::Human)
		return EControlAuthority::Human;
	return EControlAuthority::Hold;
}

void AOperatorPawn::MaybeAutoArmIntervention() {
	// Arms the session when the AVATAR says authority is already in use.
	//
	// In intervention mode the orchestrator parks both arms in HOLD at startup
	// (SystemArbitrator::claim_hold) -- before the operator has even engaged,
	// and deliberately, so the policy cannot start driving with nobody
	// watching. But UpdateAuthority returns immediately while unarmed, so until
	// the DAGGER button is pressed this interface never requests HUMAN either,
	// and HOLD refuses the operator exactly as it refuses the policy.
	//
	// The result was a silent lockout: engage, pull the trigger, nothing moves.
	// The 2026-09-23 log shows ten seconds of it -- four clutch cycles on two
	// hands, no motion -- ending only when the panel was armed. Nothing on
	// screen said the arms were held, or by whom.
	//
	// Arming on the avatar's own report fixes it at the source: if authority is
	// live, this IS an intervention session, and the interface should notice
	// rather than wait to be told. No authority request is sent from here --
	// the avatar already has one, and re-asserting would fight the orchestrator
	// over who decides the opening state.
	if (bInterventionArmed_ || !bAuthorityLiveSeen_.load(std::memory_order_relaxed))
		return;

	bInterventionArmed_ = true;
	UIBinder->SetVisibility(FName("interventionPanel"),     true);
	UIBinder->SetVisibility(FName("authority_pill_canvas"), true);
	UIBinder->SetButtonToggled(FName("interventionButton"), true);
	// Seed the clutch edge detector from reality, exactly as the button does,
	// so arming cannot synthesise a takeover from a trigger already held.
	if (LeftTracked)  bPrevAuthClutch_[0] = LeftTracked->IsFullClutch();
	if (RightTracked) bPrevAuthClutch_[1] = RightTracked->IsFullClutch();

	UIBinder->PushMessage(TEXT("INTERVENTION ARMED - POLICY CONNECTED"), 3.0f);
	if (Logger_) Logger_->LogEvent(TEXT("INTERVENTION_ARMED source=avatar_authority"));
}

void AOperatorPawn::UpdateAuthority() {
	if (!LeftTracked || !RightTracked) return;

	UTrackedControllerComponent* Tracked[2] = { LeftTracked, RightTracked };

	// Unarmed, or not ENGAGED: track the clutch level without acting on it, so
	// that arming (or engaging) never inherits a stale edge and synthesises a
	// takeover from a trigger the operator pulled minutes ago in the lobby.
	if (!bInterventionArmed_ || OperatorState_ != ESysState::Engaged) {
		for (uint8 i = 0; i < 2; ++i) bPrevAuthClutch_[i] = Tracked[i]->IsFullClutch();
		return;
	}

	// Note the polarity throughout: bFullClutch TRUE means DECOUPLED. Pulling
	// the trigger past 0.55 sets it false, which is the hand taking the arm.
	if (bWholeBodyAuthority) {
		// One robot, one decision, so the two triggers have to be reduced to a
		// single signal -- and the reduction is not symmetric.
		//
		// Taking: the FIRST trigger down takes everything. An operator who
		// reaches in with one hand to nudge a grasp has taken the robot; making
		// them pull both triggers to do it would mean the moment they most need
		// control is the moment they have to think about a second trigger.
		//
		// Giving back: only when BOTH are released. This is the half that
		// matters. Dropping authority on the first release would hand the robot
		// to the policy while the other hand is still driving it -- the policy
		// would start acting from a pose it did not create, against an arm a
		// human is holding. AND across the decoupled flags gives exactly this:
		// decoupled only when nobody is pulling.
		const bool bDecoupled = Tracked[0]->IsFullClutch() && Tracked[1]->IsFullClutch();
		const bool bPrevDecoupled = bPrevAuthClutch_[0] && bPrevAuthClutch_[1];
		for (uint8 i = 0; i < 2; ++i) bPrevAuthClutch_[i] = Tracked[i]->IsFullClutch();
		if (bDecoupled == bPrevDecoupled) return;

		// Arm index 0 is a formality: RequestArmAuthority sends bAllDevices in
		// this mode, so the avatar applies it to every arm in one message and
		// no arm is ever briefly on the other side of the handover.
		RequestArmAuthority(0, bDecoupled ? EControlAuthority::Hold : EControlAuthority::Human);
		return;
	}

	for (uint8 i = 0; i < 2; ++i) {
		const bool bDecoupled = Tracked[i]->IsFullClutch();
		if (bDecoupled == bPrevAuthClutch_[i]) continue;
		bPrevAuthClutch_[i] = bDecoupled;

		// Released -> HOLD, never straight back to POLICY. The operator
		// un-clutches constantly to reposition their arms, and losing the robot
		// to the policy mid-correction is the first thing that would make this
		// mode unusable. Handing it back is an explicit press of RESUME.
		RequestArmAuthority(i, bDecoupled ? EControlAuthority::Hold : EControlAuthority::Human);
	}
}

void AOperatorPawn::ResumePolicy(const TCHAR* Source) {
	if (!bInterventionArmed_) return;

	// "Give back everything I am no longer holding." An arm with a hand still
	// on it is left alone -- a resume that could take an arm out from under a
	// hand would be worse than no shortcut at all, and that matters more now
	// that this is reachable from the controller, where it can be hit by
	// accident.
	//
	// The test is the CLUTCH, not the avatar's authority echo, and the
	// difference is the whole reason the chord is usable.
	//
	// The echo is a round trip away -- ~70 ms on this link, two or three frames
	// -- so for those frames after a release the avatar still reports HUMAN.
	// Release both triggers, chord immediately, and an echo-based test would
	// answer "you are still holding it" about a trigger already let go. Which
	// is exactly the moment the operator uses this.
	//
	// The trigger is local and instant, and it is also the literal question
	// being asked: is a hand on this arm right now. It has no UNSET hole
	// either, which is what made the echo test fail silently the first time.
	// Polarity: IsFullClutch() TRUE means DECOUPLED, i.e. no hand on it.
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

	// The chord has no button to light up, so it needs its own acknowledgement
	// or a press that did nothing is indistinguishable from one that worked.
	// Both outcomes are reported, because "nothing happened" is the answer the
	// operator most needs when they are still holding a trigger.
	if (bAnyRequested) {
		SoundFeedback->Play(ESoundType::Confirm);
		UIBinder->PushMessage(TEXT("POLICY RESUMED"), 2.0f);
	}
	else {
		SoundFeedback->Play(ESoundType::Reject);
		UIBinder->PushMessage(TEXT("STILL HELD - RELEASE TO RESUME"), 2.0f);
	}

	if (Logger_)
		Logger_->LogEvent(FString::Printf(TEXT("RESUME_POLICY source=%s applied=%s"),
			Source, bAnyRequested ? TEXT("yes") : TEXT("no")));
}

void AOperatorPawn::PollResumeButton() {
	// A hand-grip TAP on either controller -- pressed and released without
	// touching the pad. See UTrackedControllerComponent::OnHandGripReleased.
	//
	// Either hand, deliberately: in whole-body handover the two hands are not
	// doing different jobs, so making the operator recall which one carries the
	// shortcut is a rule with nothing behind it. The grip also sits under the
	// hand at rest, which the menu button at the top of the controller does
	// not -- and a shortcut that needs the hand repositioned is one the
	// operator puts off, which moves every hand-back later than it should be
	// and biases the intervention boundaries in the dataset.
	//
	// Consumed unconditionally, even unarmed: a latch cleared only while armed
	// would bank a tap made minutes earlier and spend it the instant the
	// operator arms intervention.
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
	// Unarmed, the head is the operator's and always was -- no intervention
	// session means no authority anywhere, and ordinary teleoperation must
	// behave exactly as it did before any of this existed.
	if (!bInterventionArmed_) return true;
	if (bWholeBodyAuthority) return GetEffectiveAuthority() == EControlAuthority::Human;
	// Per-limb has no single answer for a single neck. Either hand driving is
	// enough to give the operator the view: a correction they cannot look at
	// is not a correction.
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
		if (bResetMenuOpen_) {
			UIBinder->HideMenu();
			bResetMenuOpen_ = false;
		}
		bAnnotationPending_ = false;
		UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), false);
		UIBinder->SetButtonToggled(FName("homeButton"), false);
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

	auto ApplyResetButton = [&](FName Button, FName Label, EArmResetState ResetState, const TCHAR* Side) {
		switch (ResetState) {
		case EArmResetState::Idle:
			UIBinder->SetButtonToggled(Button, false);
			UIBinder->SetButtonLocked(Button, !bCanReset);
			UIBinder->SetText(Label, FString::Printf(TEXT("Reset %s"), Side));
			break;
		case EArmResetState::Recovering:
		case EArmResetState::AwaitingResume:
			UIBinder->SetButtonToggled(Button, true);
			UIBinder->SetButtonLocked(Button, true);
			UIBinder->SetText(Label, FString::Printf(TEXT("Resetting %s..."), Side));
			break;
		}
		};

	ApplyResetButton(FName("resetButtonLeft"), FName("resetLabelLeft"), LeftArmResetState_, TEXT("L"));
	ApplyResetButton(FName("resetButtonRight"), FName("resetLabelRight"), RightArmResetState_, TEXT("R"));

	bool bBothIdle = LeftArmResetState_ == EArmResetState::Idle && RightArmResetState_ == EArmResetState::Idle;
	bool bResetting = !bBothIdle;
	UIBinder->SetButtonToggled(FName("resetButton"), bResetMenuOpen_ || bResetting);
	UIBinder->SetButtonLocked(FName("resetButton"), !bCanReset || bResetting);
	UIBinder->SetText(FName("resetLabel"), TEXT("Reset"));

	UIBinder->SetButtonLocked(FName("homeButton"), !bCanReset || bAnyRecovering || bAnnotationPending_);

	switch (OperatorState_) {
	case ESysState::Offline:
		UIBinder->SetButtonLocked(FName("startButton"), true);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Start"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Idle:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Start"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Homing:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), true);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Awaiting:
		UIBinder->SetButtonLocked(FName("startButton"), false);
		UIBinder->SetButtonLocked(FName("engageButton"), false);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	case ESysState::Engaged:
		UIBinder->SetButtonLocked(FName("startButton"), bAnyRecovering);
		UIBinder->SetButtonLocked(FName("engageButton"), bAnyRecovering);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Pause"));
		break;

	case ESysState::Paused:
		UIBinder->SetButtonLocked(FName("startButton"), bAnyRecovering);
		UIBinder->SetButtonLocked(FName("engageButton"), bAnyRecovering);
		UIBinder->SetText(FName("startLabel"), TEXT("Stop"));
		UIBinder->SetText(FName("engageLabel"), TEXT("Engage"));
		break;

	default:
		break;
	}
}

bool AOperatorPawn::CheckEmergencyStop() {
	if (OperatorState_ == ESysState::Offline || OperatorState_ == ESysState::Idle) {
		return false;
	}
	// BOTH menu buttons, either hand. Briefly this was right-only, with the
	// left carrying resume -- which made the operator remember which hand was
	// which, and still meant reaching the top of the controller. Resume moved
	// to the hand grip, so the stop gets both hands back.
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
		// Re-anchor the head base on the neck's CURRENT pose, exactly as a
		// takeover does. NOT zero.
		//
		// What goes on the wire is an absolute joint angle -- this base plus
		// the HMD delta since HMDOrigin_ -- so a zero base is not "no offset",
		// it is a command to point the neck at joint zero. Zero was right only
		// while the head channel added q0 for us; it no longer does, and
		// nothing on this side should know what q0 is. Reading the measured
		// pose gets the same answer without duplicating the constant.
		const HeadStateMsg S = ComLink->PeekHeadState();
		HeadBasePan_  = S.pan;
		HeadBaseTilt_ = S.tilt;
	}

	if (GhostOverlay) {
		for (uint8 Arm = 0; Arm < 2; ++Arm) {
			// ReadArmState() returns the latest cached EE pose whether or not a new
			// packet arrived this exact frame, so seed from it whenever the arm is alive.
			// Falling back to zero/identity would plant the ghost origin at the world
			// origin and inject a large bogus offset at engage.
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

	// ── Head handover ───────────────────────────────────────────────────────
	// The neck is part of the robot, so it changes hands with the rest of it.
	// This is not tidiness: the policy is conditioned on the images its own
	// camera returns, and a head that kept following the operator's HMD while
	// the policy drove the arms would be feeding it observations from a
	// viewpoint it never chose. The policy would be acting on one scene and
	// looking at another.
	const bool bHeld = IsOperatorHoldingHead();
	if (bHeld != bHeadHeld_) {
		bHeadHeld_ = bHeld;
		if (bHeld) {
			// Re-anchor, exactly as the arms do on a takeover, and for exactly
			// the same reason.
			//
			// pan/tilt are absolute angles derived from how far the HMD has
			// turned since HMDOrigin_. Take the head back without re-anchoring
			// and the first command sends the neck to wherever the operator's
			// head happened to be pointing relative to an origin captured at
			// engage -- which, after the policy has been looking around for a
			// minute, is a hard snap to somewhere else entirely.
			//
			// Zeroing the operator's delta and adding the neck's CURRENT angles
			// as the base makes the first commanded pose identical to the pose
			// the policy left behind: the head does not move at all until the
			// operator moves their own.
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
		// Keep the logged head angles tracking the real neck rather than
		// freezing at the operator's last command -- otherwise every log row
		// recorded during an autonomous stretch reports a head pose that
		// nothing is holding.
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

void AOperatorPawn::SendGazeSample(){
	uint64 FrameId = VideoFeed->GetLastFrameId();
	if (FrameId == 0) return;

	const FGazeData& GazeData = Gaze->GetGazeData();
	if (!GazeData.bIsValid) return;

	if (!VRCamera) return;

	FVector2D GazeUV;
	if (!FGazeProjection::Project(GazeData, VRCamera->GetComponentTransform(), VideoFeed->PlaneDistance, VideoQuadWidth_, VideoQuadHeight_, GazeUV))
		return;

	UTexture2D* Tex = VideoFeed->GetVideoTexture();
	float W = Tex ? static_cast<float>(Tex->GetSizeX()) : 1280.f;
	float H = Tex ? static_cast<float>(Tex->GetSizeY()) : 720.f;

	const FDateTime NowUtc = FDateTime::UtcNow();
	uint64_t NowNs = static_cast<uint64_t>(NowUtc.ToUnixTimestamp()) * 1000000000ULL + static_cast<uint64_t>(NowUtc.GetMillisecond()) * 1000000ULL;

	FGazeSampleMsg Msg;
	Msg.frame_id     = static_cast<uint64_t>(FrameId);
	Msg.gaze_px_x    = GazeUV.X * W;
	Msg.gaze_px_y    = GazeUV.Y * H;
	Msg.timestamp_ns = NowNs;

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
				if (LeftArmResetState_  == EArmResetState::Idle) LeftArmResetState_  = EArmResetState::Recovering;
				if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
				bPendingVoiceReengage_ = true;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("reset-left"), ESearchCase::IgnoreCase)) {
			bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanReset && LeftArmResetState_ == EArmResetState::Idle) {
				SendArmReset("arm_left");
				LeftArmResetState_ = EArmResetState::Recovering;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("reset-right"), ESearchCase::IgnoreCase)) {
			bool bCanReset = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanReset && RightArmResetState_ == EArmResetState::Idle) {
				SendArmReset("arm_right");
				RightArmResetState_ = EArmResetState::Recovering;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("home"), ESearchCase::IgnoreCase)) {
			// Open the episode annotation panel — same as pressing homeButton.
			bool bCanAnnotate = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanAnnotate && !bAnnotationPending_) {
				bAnnotationPending_ = true;
				UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), true);
			}
		} else if (Ann.Label.Equals(TEXT("success"), ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("partial"),  ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("failure"),  ESearchCase::IgnoreCase)) {
			// One-shot episode annotation: works with or without "home" first.
			bool bCanAnnotate = (OperatorState_ == ESysState::Engaged || OperatorState_ == ESysState::Paused);
			if (bCanAnnotate) {
				FString Label = Ann.Label;  // already "success" / "partial" / "failure"
				UIBinder->SetButtonToggled(FName("homeButton"), false);
				UIBinder->SetVisibility(FName("episodeAnnotationCanvas"), false);
				SendResetAll();
				if (LeftArmResetState_  == EArmResetState::Idle) LeftArmResetState_  = EArmResetState::Recovering;
				if (RightArmResetState_ == EArmResetState::Idle) RightArmResetState_ = EArmResetState::Recovering;
				SendEpisodeRestart(Label);
				++EpisodeCount_;
				bAnnotationPending_ = false;
				UpdateButtonStates();
			}
		} else if (Ann.Label.Equals(TEXT("statistics"), ESearchCase::IgnoreCase)) {
			bStatsVisible_ = !bStatsVisible_;
			UIBinder->SetVisibility(FName("statsPanel"), bStatsVisible_);
		} else if (Ann.Label.Equals(TEXT("settings"), ESearchCase::IgnoreCase)) {
			bSettingsVisible_ = !bSettingsVisible_;
			UIBinder->SetVisibility(FName("settings_canvas"), bSettingsVisible_);
		} else if (Ann.Label.Equals(TEXT("viewpoint"), ESearchCase::IgnoreCase)) {
			// Toggle the camera-stream picker menu.
			if (bMenuOpen_) {
				UIBinder->HideMenu();
				bMenuOpen_ = false;
				UIBinder->SetButtonToggled(FName("viewpointButton"), !ActivePiPStreamName_.IsEmpty());
			} else {
				CurrentMenuStreams_ = PiPSourceNames_;
				UIBinder->ShowCameraMenu(CurrentMenuStreams_, ActivePiPStreamName_);
				bMenuOpen_ = true;
				UIBinder->SetButtonToggled(FName("viewpointButton"), true);
			}
		} else if (Ann.Label.Equals(TEXT("camera-left"),  ESearchCase::IgnoreCase) ||
		           Ann.Label.Equals(TEXT("camera-right"), ESearchCase::IgnoreCase)) {
			// Select the PiP stream whose name contains "left" or "right".
			const FString Side = Ann.Label.Equals(TEXT("camera-left"), ESearchCase::IgnoreCase)
			                     ? TEXT("left") : TEXT("right");
			for (const FString& StreamName : PiPSourceNames_) {
				if (StreamName.Contains(Side, ESearchCase::IgnoreCase)) {
					ActivePiPStreamName_ = StreamName;
					UIBinder->SetButtonToggled(FName("viewpointButton"), true);
					if (bMenuOpen_) { UIBinder->HideMenu(); bMenuOpen_ = false; }
					break;
				}
			}
		} else if (Ann.Label.Equals(TEXT("camera-off"), ESearchCase::IgnoreCase)) {
			// Turn off the picture-in-picture stream.
			ActivePiPStreamName_ = TEXT("");
			UIBinder->SetVisibility(FName("pip_canvas"), false);
			UIBinder->SetButtonToggled(FName("viewpointButton"), false);
			if (bMenuOpen_) {
				UIBinder->HideMenu();
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
		bHit = FGazeProjection::Project(GazeData, VRCamera->GetComponentTransform(), VideoFeed->PlaneDistance, VideoQuadWidth_, VideoQuadHeight_, GazeUV);
	}

	Bundle.GazeUV = GazeUV;
	Bundle.GazeConfidence = GazeData.Confidence;
	Bundle.bGazeValid = bHit && GazeData.bIsValid;

	return Bundle;
}