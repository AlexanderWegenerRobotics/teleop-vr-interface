#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "MotionControllerComponent.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "Input/TrackedControllerComponent.h"
#include "Video/VideoFeedComponent.h"
#include "Video/IVideoSource.h"
#include "Networking/ComLink.h"
#include "UI/GazeComponent.h"
#include "UI/WidgetBinder.h"
#include "UI/HudPanels.h"
#include "UI/TMetricHistory.h"
#include "UI/SoundFeedback.h"
#include "Video/VideoLogger.h"
#include "Video/GazeProjection.h"
#include "Video/VideoLookAround.h"
#include "Teleop/TeleOpLogger.h"
#include "UI/VoiceAnnotatorComponent.h"
#include "UI/NudgeReceiver.h"
#include "Video/IntentHighlightComponent.h"
#include "Video/GhostOverlayComponent.h"
#include "Video/GraspIndicatorComponent.h"
#include "Video/WorkspaceBoundaryComponent.h"
#include "Video/AlignmentPanelComponent.h"
#include "Video/WristViewComponent.h"
#include "Video/GraspRulerComponent.h"
#include "Networking/UdpSocket.h"
#include "Teleop/CommandThread.h"
#include "Teleop/EpisodeEventReceiver.h"
#include "GStreamerAudioReceiver.h"
// UPROPERTY(TObjectPtr<UTexture2D>) needs the complete type for the generated
// reflection code.
#include "Engine/Texture2D.h"

#include <atomic>

#include "OperatorPawn.generated.h"

UCLASS()
class TELEOP_VR_INTERFACE_API AOperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	AOperatorPawn();
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UCameraComponent*    GetVRCamera()   const { return VRCamera; }
	UComLink*            GetComLink()    const { return ComLink; }
	UVideoFeedComponent* GetVideoFeed()  const { return VideoFeed; }
	void SendResetAll();

	// Console: arm a one-shot wrist-pivot calibration on a controller. Type in the UE
	// console, then hold that controller's grip, rotate in place, release to solve.
	UFUNCTION(Exec) void CalibrateWristPivotRight();
	UFUNCTION(Exec) void CalibrateWristPivotLeft();

	// Grasp (grip) state per arm for overlay/HUD Blueprints. ArmIndex: 0 = left, 1 = right.
	// True while the controller grip is held — the same signal sent as the gripper command.
	UFUNCTION(BlueprintPure, Category = "Teleop|Gripper")
	bool IsArmGraspHeld(uint8 ArmIndex) const;

protected:
	UPROPERTY() TObjectPtr<USceneComponent> VROrigin;
	UPROPERTY() TObjectPtr<UCameraComponent> VRCamera;
	UPROPERTY() TObjectPtr<UMotionControllerComponent> LeftController;
	UPROPERTY() TObjectPtr<UMotionControllerComponent> RightController;

	UPROPERTY() UTrackedControllerComponent* LeftTracked = nullptr;
	UPROPERTY() UTrackedControllerComponent* RightTracked = nullptr;
	UPROPERTY() UInputMappingContext* InputMappingContext = nullptr;

	UPROPERTY() TObjectPtr<UVideoFeedComponent> VideoFeed;
	UPROPERTY() TObjectPtr<UComLink> ComLink;
	UPROPERTY() TObjectPtr<UGazeComponent> Gaze;
	UPROPERTY() TObjectPtr<UWidgetBinder> UIBinder;
	UPROPERTY() TSubclassOf<UUserWidget> UIWidgetClass;
	UPROPERTY() TObjectPtr<UWidgetBinder> AlertBinder;
	UPROPERTY() TSubclassOf<UUserWidget> AlertWidgetClass;
	UPROPERTY() TObjectPtr<USoundFeedback> SoundFeedback;

	UPROPERTY() TObjectPtr<UWidgetBinder> TrayBinder;
	UPROPERTY() TSubclassOf<UUserWidget> TrayWidgetClass;
	UPROPERTY() AVideoLogger* VideoLogger_ = nullptr;
	UPROPERTY() TObjectPtr<UVoiceAnnotatorComponent> VoiceAnnotator;
	UPROPERTY() TObjectPtr<UNudgeReceiver> NudgeReceiver;
	UPROPERTY() TObjectPtr<UIntentHighlightComponent> IntentHighlight;
	UPROPERTY() TObjectPtr<UGhostOverlayComponent>  GhostOverlay;
	UPROPERTY() TObjectPtr<UGraspIndicatorComponent> LeftGraspIndicator;
	UPROPERTY() TObjectPtr<UGraspIndicatorComponent> RightGraspIndicator;
	UPROPERTY() TObjectPtr<UWorkspaceBoundaryComponent> LeftWorkspaceBoundary;
	UPROPERTY() TObjectPtr<UWorkspaceBoundaryComponent> RightWorkspaceBoundary;
	UPROPERTY() TObjectPtr<UWristViewComponent> LeftWristView;
	UPROPERTY() TObjectPtr<UWristViewComponent> RightWristView;
	UPROPERTY() TObjectPtr<UAlignmentPanelComponent> LeftAlignmentPanel;
	UPROPERTY() TObjectPtr<UAlignmentPanelComponent> RightAlignmentPanel;
	UPROPERTY() TObjectPtr<UGraspRulerComponent> LeftGraspRuler;
	UPROPERTY() TObjectPtr<UGraspRulerComponent> RightGraspRuler;

	UPROPERTY(EditAnywhere, Category = "Logging")
	FString LogBaseDirectory = TEXT("Saved/Logs/TeleOp/");
	TUniquePtr<FEpisodeEventReceiver> EpisodeEvents_;
	void PollEpisodeEvents();

	TMetricHistory<128> LatencyHistory;
	TMetricHistory<128> JitterHistory;
	TMetricHistory<128> LossHistory;
	TMetricHistory<128> FpsHistory;
	TMetricHistory<128> CpuHistory;
	TMetricHistory<128> GpuHistory;
	TMetricHistory<128> GpuTempHistory;

private:
	ESysState OperatorState_ = ESysState::Offline;

	enum class EArmResetState : uint8 { Idle, Recovering, AwaitingResume };
	EArmResetState LeftArmResetState_ = EArmResetState::Idle;
	EArmResetState RightArmResetState_ = EArmResetState::Idle;
	// Watchdog on a reset that never completes -- see the reset block in Tick.
	double ArmResetRequestTime_[2] = { 0.0, 0.0 };
	static constexpr double kArmResetGraceSec   = 2.0;
	static constexpr double kArmResetTimeoutSec = 15.0;

	void UpdateStateMachine();
	void TransitionTo(ESysState NewState);
	void UpdateButtonStates();
	void CaptureControllerOrigins();
	void SendArmCommands();
	void SendHeadCommand();
	bool CheckEmergencyStop();
	void SendArmReset(const std::string& DeviceName);
	void SendArmResume(const std::string& DeviceName);
	void SendGazeSample();
	void SendOperatorState();
	void HandleVoiceAnnotation(const FVoiceAnnotation& Ann);

	// Remote arm reported FAULT. Called from ComLink's receive thread, so it only
	// records intent; cue and HUD run on the game thread in Tick.
	void HandleArmFault(uint8 DeviceIndex, FaultCode Code);
	// Set by HandleArmFault, consumed and cleared in Tick. One slot per arm.
	TAtomic<int32> PendingArmFault_[2];    // -1 = none, else FaultCode value
	FFrameBundle BuildFrameBundle() const;

	// Starts/stops the local RealSense recorder with the startButton toggle.
	// Same-machine loopback, so the message ts_ns is the recorder's clock reference.
	void SendRecordingSignal(bool bStart);
	TUniquePtr<UdpSocket> RecordingSocket_;
	bool bRecordingActive_ = false;

	FTransform HMDOrigin_;
	bool bSwapControllers_ = false;
	bool bHMDOriginValid_  = false;
	bool bLeftWasGrasping  = false;
	bool bRightWasGrasping = false;
	// True once avatar confirms ENGAGED after VR entered ENGAGED; prevents
	// dropping back to AWAITING on stale pre-transition avatar state.
	bool bAvatarConfirmedEngaged_ = false;

	// Stale-state alarm, see UpdateHudIndicators. Edge-triggered so the
	// operator gets one warning per episode rather than a continuous tone.
	bool bArmStateWasStale_ = false;
	// 5x the avatar's 200 Hz state period: catches a dead control loop within ~25 ms
	// without firing on jitter or a single dropped packet.
	static constexpr float kArmStateStaleWarnMs = 25.f;

	bool bPendingVoiceReengage_   = false;
	bool bResetMenuOpen_          = false;
	bool bAnnotationPending_      = false;

	float VideoQuadWidth_  = 0.f;
	float VideoQuadHeight_ = 0.f;

	void UpdateLookAround(float DeltaTime);
	FVector2D GetVideoQuadSize() const;
	FVideoLookAround LookAround_;
	bool bLookAroundEnabled_      = false;
	bool bVideoLayersWorldLocked_ = false;
	bool bLookActive_             = false;
	float LookReturnSec_          = 0.4f;
	float LookReturnAlpha_        = 1.f;
	FQuat LookReturnStartRel_     = FQuat::Identity;
	FTransform VideoQuadWorld_;
	float ViewYawDeg_   = 0.f;
	float ViewPitchDeg_ = 0.f;

	FHudPanels Hud_;
	EHudPlacement HudPlacement_ = EHudPlacement::HeadLocked;
	float HudFrameDistance_ = 690.f;
	FVector2D FrameSize_ = FVector2D(1280.f, 800.f);
	void UpdateHudFrame(bool bWorldLocked, const FTransform& FramePose);
	void ApplyControlPanelSet(bool bWorldLocked);

	bool    bWristViewEnabled_ = false;
	FString WristViewMode_     = TEXT("always");
	int32   WristStreamIdx_[2] = { INDEX_NONE, INDEX_NONE };
	UWristViewComponent* WristView(uint8 Arm) const { return Arm == 0 ? LeftWristView.Get() : RightWristView.Get(); }
	void UpdateWristViews(float DeltaTime);

	TMap<FName, FLinearColor> DeviceColors_;
	double LastDeviceChangeSec_ = 0.0;
	float  DeviceGraphOpacity_  = 1.f;
	static constexpr float kDeviceGraphDimOpacity = 0.2f;
	static constexpr float kDeviceGraphHoldSec    = 4.0f;
	static constexpr float kDeviceGraphFadeSpeed  = 6.0f;
	void SetDeviceColor(FName Name, const FLinearColor& Color);
	void UpdateDeviceGraphOpacity(float DeltaTime);

	void UpdateInfoBar();

	// --- intervention / control authority ---
	// Watches each trigger for a clutch edge and claims or releases that arm, so the
	// operator can correct one hand while the policy drives the other. Does nothing
	// unless the session is armed, so ordinary teleoperation never sends an authority
	// request.
	void UpdateAuthority();
	// Arms the session when the avatar reports authority already in use (claimed by
	// the orchestrator), since UpdateAuthority is inert while unarmed.
	void MaybeAutoArmIntervention();
	// Sends one arm's request and, on a takeover, re-anchors that arm's
	// retarget at the same instant the avatar re-origins it.
	void RequestArmAuthority(uint8 ArmIndex, EControlAuthority Requested);
	// Authority as the avatar last reported it, not as requested, so the HUD never
	// shows a claim the avatar refused.
	EControlAuthority GetArmAuthority(uint8 ArmIndex) const;
	// The authority the operator is working under. Whole-body: the shared value, or
	// the safer one if the arms disagree. Per-limb: Unset, the cue to render the
	// split form.
	EControlAuthority GetEffectiveAuthority() const;
	// Re-seeds the grasp toggle from the measured gripper width at a takeover.
	// ArmIndex -1 = both arms.
	void SyncGraspToMeasured(int32 ArmIndex);

	// Glyph per authority state inside the DAGGER panel; the icon says who is
	// driving, the colour reinforces it. Falls back to loading by name if unset (see
	// ResolveAuthorityGlyphs).
	UPROPERTY(EditAnywhere, Category = "Teleop|Intervention")
	TObjectPtr<UTexture2D> AuthorityIconHuman = nullptr;
	UPROPERTY(EditAnywhere, Category = "Teleop|Intervention")
	TObjectPtr<UTexture2D> AuthorityIconPolicy = nullptr;
	UPROPERTY(EditAnywhere, Category = "Teleop|Intervention")
	TObjectPtr<UTexture2D> AuthorityIconHold = nullptr;

	void ResolveAuthorityGlyphs();
	UTexture2D* AuthorityGlyphFor(EControlAuthority A) const;
	// Last glyph pushed, so the brush is rebuilt only on a state change rather
	// than every tick -- SetImageTexture allocates a whole FSlateBrush.
	UPROPERTY() TObjectPtr<UTexture2D> LastAuthorityGlyph_ = nullptr;
	// True while the operator's HMD drives the neck. False means the policy owns the
	// head and SendHeadCommand sends nothing, so HMD motion cannot leak into the
	// policy's observations.
	bool IsOperatorHoldingHead() const;
	// Hands back every arm no hand is on. Shared by the RESUME button and the
	// controller chord; Source is for the event log.
	void ResumePolicy(const TCHAR* Source);
	// Drains the left menu button latch, a shortcut for RESUME without looking away
	// from the task.
	void PollResumeButton();
	// Applies one colour whether the widget is a TextBlock, Image or Border; the
	// per-type setters no-op on a miss.
	void SetWidgetAccent(FName WidgetName, const FLinearColor& Color) const;
	// Logs once at startup every widget name this class drives that the bound UMG
	// asset does not contain.
	void AuditWidgetNames() const;
	void SendEpisodeRestart(const FString& Label);

	// Session tracking
	double SessionStartTime_ = 0.0;
	int32  EpisodeCount_     = 0;

	// Logging
	TUniquePtr<FTeleOpLogger> Logger_;
	TUniquePtr<FGStreamerAudioReceiver> AudioRx_;
	bool   bAudioLog_             = false;
	uint64 LastAudioSyncFrameId_  = 0;
	float LastHeadPan_   = 0.f;
	float LastHeadTilt_  = 0.f;
	// Head authority as SendHeadCommand last acted on it, for edge detection. Starts
	// true so an unarmed session never sees an edge.
	bool  bHeadHeld_     = true;
	// Neck angles when the operator took the head back. Commands are this plus the
	// HMD delta since the re-anchor.
	float HeadBasePan_   = 0.f;
	float HeadBaseTilt_  = 0.f;
	float PrevLeftGear_  = -1.0f;
	float PrevRightGear_ = -1.0f;
	bool  bPrevLeftClutch_  = false;
	bool  bPrevRightClutch_ = false;
	bool  bStatsVisible_       = false;
	bool  bSettingsVisible_    = false;
	// Intervention (DAgger) session armed. Drives the authority pill and panel and
	// gates the authority requests. The button arms the session, the trigger takes
	// the robot; while false the avatar stays UNSET.
	bool  bInterventionArmed_ = false;
	// Set from the command-link receive thread the first time the avatar
	// reports any arm as not-UNSET. Consumed on the game thread.
	std::atomic<bool> bAuthorityLiveSeen_{ false };
	// Whole-body handover: one trigger takes both arms and the head, one RESUME hands
	// them back. The per-arm machinery underneath is unchanged; false restores
	// per-limb handover. The head follows this flag so the policy's camera is never
	// moved by the operator while the policy drives.
	UPROPERTY(EditAnywhere, Category = "Teleop|Intervention")
	bool  bWholeBodyAuthority = true;
	// Authority as the avatar last reported it, one per arm. Written from the
	// command-link receive thread, read on the game thread.
	TAtomic<uint8> LeftAuthority_{ static_cast<uint8>(EControlAuthority::Unset) };
	TAtomic<uint8> RightAuthority_{ static_cast<uint8>(EControlAuthority::Unset) };
	// Clutch level last seen by the authority edge detector. Separate from
	// bPrevLeftClutch_/bPrevRightClutch_, which only advance on the stream-row
	// cadence.
	bool  bPrevAuthClutch_[2] = { true, true };
	// Takeovers this episode for the INTERV readout, counted on the request so a
	// refused claim still shows. Reset in UpdatePolicyStats.
	int32 InterventionCount_ = 0;
	// Policy readouts relayed from the orchestrator through the avatar
	// (policy_status). Written on the command-link receive thread.
	std::atomic<float>  PolicyInferenceMs_{ -1.f };
	std::atomic<float>  PolicyAgree_{ -1.f };
	std::atomic<double> PolicyStatusTime_{ 0.0 };
	// Per-episode AUTON accumulators: engaged time, and arm-weighted time under POLICY.
	int32  StatsEpisode_     = -1;
	double AutonPolicySec_   = 0.0;
	double AutonEngagedSec_  = 0.0;
	// Resets per-episode stats and draws INF, AUTON and the AGREE pips.
	void UpdatePolicyStats(float DeltaTime);
	// Global mute for USoundFeedback, the only audio source. Controller haptics stay
	// on.
	bool  bSoundMuted_         = false;
	int32 PerfSampleCounter_ = 0;

	TArray<TUniquePtr<IVideoSource>> PiPSources_;
	TArray<UTexture2D*>              PiPTextures_;
	TArray<FString>                  PiPSourceNames_;
	FString                  ActivePiPStreamName_;
	TArray<FString>          CurrentMenuStreams_;
	bool                     bMenuOpen_            = false;

	// Cached from Config (BeginPlay-local) for the viewmodeButton handler.
	bool    bHasTwinMainStream_ = false;
	bool    bOverlayEnabled_    = true;

	// Pose sampling and arm-command send run here, not on the game tick, so
	// the command rate stops being whatever the renderer happens to manage.
	TUniquePtr<FTeleopCommandThread> CommandThread_;

	// 120 rather than 90: above the display rate and within the tracker's >=90 Hz, at
	// ~12% of a core in the pacing spin.
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float CommandThreadRateHz = 120.f;

	FString TwinMainStreamKey_;   // VideoFeedComponent registration key (== Config->Stream.TwinStream.Name)
	FString TwinMainStreamLabel_; // viewmode_label text when twin is active ("TWIN", as configured)
	FString TwinPiPEntryName_;    // PiP menu entry text (TwinMainStreamLabel_.ToLower(), e.g. "twin")

	// PiP's TWIN entry shares the main-view twin decode; two receivers cannot bind
	// the same UDP port. Own texture pointer, since PiPTextures_ is for entries with
	// their own IVideoSource.
	UTexture2D* PiPSharedTwinTexture_ = nullptr;

	// PiP gaze-driven expand.
	// PiPExpandScale: multiplier applied to the widget's normal size when expanded (1.5 = 50% bigger).
	UPROPERTY(EditAnywhere, Category = "PiP") float     PiPExpandScale        = 1.5f;
	UPROPERTY(EditAnywhere, Category = "PiP") FVector2D PiPExpandDirection    = FVector2D(1.f, 1.f);
	UPROPERTY(EditAnywhere, Category = "PiP") float     PiPGazeInnerMarginPx  = 20.f;
	UPROPERTY(EditAnywhere, Category = "PiP") float     PiPGazeOuterMarginPx  = 15.f;
	UPROPERTY(EditAnywhere, Category = "PiP") float     PiPShrinkDwellTime    = 0.5f;
	UPROPERTY(EditAnywhere, Category = "PiP") float     PiPLerpSpeed          = 8.f;

	FVector2D PiPBaseSlotPos_;
	FVector2D PiPNormalSize_;
	FVector2D PiPCurrentSize_;
	float     PiPDwellTimer_ = 0.f;
	bool      bPiPExpanded_  = false;
};