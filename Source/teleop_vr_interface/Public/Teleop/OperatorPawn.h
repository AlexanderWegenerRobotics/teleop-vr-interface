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
#include "UI/TMetricHistory.h"
#include "UI/SoundFeedback.h"
#include "Video/VideoLogger.h"
#include "Video/GazeProjection.h"
#include "Teleop/TeleOpLogger.h"
#include "UI/VoiceAnnotatorComponent.h"
#include "Video/GhostOverlayComponent.h"
#include "Video/GraspIndicatorComponent.h"
#include "Video/WorkspaceBoundaryComponent.h"
#include "Networking/UdpSocket.h"
#include "Teleop/CommandThread.h"

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
	UPROPERTY() TObjectPtr<USoundFeedback> SoundFeedback;

	UPROPERTY() TObjectPtr<UWidgetBinder> TrayBinder;
	UPROPERTY() TSubclassOf<UUserWidget> TrayWidgetClass;
	UPROPERTY() AVideoLogger* VideoLogger_ = nullptr;
	UPROPERTY() TObjectPtr<UVoiceAnnotatorComponent> VoiceAnnotator;
	UPROPERTY() TObjectPtr<UGhostOverlayComponent>  GhostOverlay;
	UPROPERTY() TObjectPtr<UGraspIndicatorComponent> LeftGraspIndicator;
	UPROPERTY() TObjectPtr<UGraspIndicatorComponent> RightGraspIndicator;
	UPROPERTY() TObjectPtr<UWorkspaceBoundaryComponent> LeftWorkspaceBoundary;
	UPROPERTY() TObjectPtr<UWorkspaceBoundaryComponent> RightWorkspaceBoundary;

	UPROPERTY(EditAnywhere, Category = "Logging")
	FString LogBaseDirectory = TEXT("Saved/Logs/TeleOp/");

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
	void HandleVoiceAnnotation(const FVoiceAnnotation& Ann);

	// Remote arm reported FAULT in its message header. Called from ComLink's
	// receive thread, so it only records intent; the audible cue and HUD work
	// happen on the game thread in Tick.
	void HandleArmFault(uint8 DeviceIndex, FaultCode Code);
	// Set by HandleArmFault, consumed and cleared in Tick. One slot per arm.
	TAtomic<int32> PendingArmFault_[2];    // -1 = none, else FaultCode value
	FFrameBundle BuildFrameBundle() const;

	// Tells the local operator-side RealSense recorder to start/stop, in step with the
	// startButton toggle (Idle<->Homing). Same-machine loopback, so no clock sync needed —
	// the receiver just uses the ts_ns in the message as its own recording clock reference.
	void SendRecordingSignal(bool bStart);
	TUniquePtr<UdpSocket> RecordingSocket_;
	bool bRecordingActive_ = false;

	FTransform HMDOrigin_;
	bool bHMDOriginValid_  = false;
	bool bLeftWasGrasping  = false;
	bool bRightWasGrasping = false;
	// True once avatar confirms ENGAGED after VR entered ENGAGED; prevents
	// dropping back to AWAITING on stale pre-transition avatar state.
	bool bAvatarConfirmedEngaged_ = false;

	// Stale-state alarm, see UpdateHudIndicators. Edge-triggered so the
	// operator gets one warning per episode rather than a continuous tone.
	bool bArmStateWasStale_ = false;
	// 5x the avatar's 200 Hz state publish period. Tight enough to catch a
	// dead control loop within ~25 ms, loose enough not to fire on ordinary
	// scheduling jitter or a single dropped packet.
	static constexpr float kArmStateStaleWarnMs = 60.f;

	bool bPendingVoiceReengage_   = false;
	bool bResetMenuOpen_          = false;
	bool bAnnotationPending_      = false;

	float VideoQuadWidth_  = 0.f;
	float VideoQuadHeight_ = 0.f;

	void UpdateInfoBar();

	// ── Intervention / control authority ────────────────────────────────────
	// Watches each trigger for a clutch edge and claims or releases that arm.
	// Per arm on purpose: the clutch already is, so the operator can correct one
	// hand while the policy keeps driving the other.
	//
	// Does nothing unless the intervention session is armed. That is the whole
	// safety story for ordinary teleoperation: unarmed, not a single authority
	// request goes out, the avatar's gate stays UNSET, and every command path
	// behaves exactly as it did before any of this existed.
	void UpdateAuthority();
	// Arms the intervention session when the avatar reports authority already
	// in use -- i.e. when the orchestrator has claimed it. Without this, an
	// intervention run starts with both arms in HOLD and no way for the
	// operator to take them, because UpdateAuthority is inert while unarmed.
	void MaybeAutoArmIntervention();
	// Sends one arm's request and, on a takeover, re-anchors that arm's
	// retarget at the same instant the avatar re-origins it.
	void RequestArmAuthority(uint8 ArmIndex, EControlAuthority Requested);
	// Authority as the AVATAR last reported it (authority_state on the command
	// channel), NOT as we requested it. Everything the HUD draws comes from
	// here, so the pill can never claim an arm whose request the avatar refused.
	EControlAuthority GetArmAuthority(uint8 ArmIndex) const;
	// The one authority the operator is actually working under.
	//
	// Whole-body: both arms move together by construction, so this is simply
	// their shared value -- and if they ever disagree (a refused claim, a
	// packet lost on one channel) it reports the SAFER of the two rather than
	// averaging them, because the HUD saying POLICY while a hand drives an arm
	// is the worst sentence this interface could write.
	//
	// Per-limb: the arms are legitimately independent, so there is no single
	// answer and this returns Unset, which is the caller's cue to render the
	// split form.
	EControlAuthority GetEffectiveAuthority() const;
	// Re-seeds the grasp toggle from the measured gripper width at a takeover,
	// so the operator inherits the gripper the policy left rather than the one
	// they last set before handing over. ArmIndex -1 = both arms.
	void SyncGraspToMeasured(int32 ArmIndex);
	// True while the operator's HMD is allowed to drive the neck. False means
	// the policy owns the head and SendHeadCommand sends nothing at all --
	// silence, not a held pose, so the operator's HMD motion cannot leak into
	// the observations the policy is conditioning on.
	bool IsOperatorHoldingHead() const;
	// Hands back every arm no hand is currently on. Shared by the RESUME button
	// and the controller chord so the two can never diverge; Source is for the
	// event log only.
	void ResumePolicy(const TCHAR* Source);
	// Drains the left Menu button latch. The shortcut exists
	// because RESUME is the most frequent action in an intervention session and
	// gaze-dwelling a button at the edge of the panel costs a glance away from
	// the thing being corrected, every single time.
	void PollResumeButton();
	// Applies one colour to a widget without knowing whether it is a TextBlock,
	// an Image or a Border. UWidgetBinder's per-type setters no-op on a miss, so
	// calling all three is type-agnostic by construction -- which is what stops a
	// widget silently keeping its design-time colour when its type in UMG is not
	// the one the C++ guessed.
	void SetWidgetAccent(FName WidgetName, const FLinearColor& Color) const;
	void SendEpisodeRestart(const FString& Label);

	// Session tracking
	double SessionStartTime_ = 0.0;
	int32  EpisodeCount_     = 0;

	// Logging
	TUniquePtr<FTeleOpLogger> Logger_;
	float LastHeadPan_   = 0.f;
	float LastHeadTilt_  = 0.f;
	// Head authority as SendHeadCommand last acted on it, for edge detection.
	// Starts true so an unarmed session never sees an edge and behaves exactly
	// as it did before the head joined the handover.
	bool  bHeadHeld_     = true;
	// Neck angles at the instant the operator took the head back. Every
	// command is this plus the HMD delta since the re-anchor, so a takeover
	// continues from where the policy was looking instead of snapping.
	float HeadBasePan_   = 0.f;
	float HeadBaseTilt_  = 0.f;
	float PrevLeftGear_  = -1.0f;
	float PrevRightGear_ = -1.0f;
	bool  bPrevLeftClutch_  = false;
	bool  bPrevRightClutch_ = false;
	bool  bStatsVisible_       = false;
	bool  bSettingsVisible_    = false;
	// Intervention (DAgger) session armed. Drives the authority pill and the
	// intervention panel, which are one feature and appear together, AND gates
	// the authority requests themselves.
	//
	// The button arms the SESSION; the trigger takes the ROBOT. Keeping those
	// apart is what lets ordinary teleoperation stay untouched: while this is
	// false no authority request is ever sent, so the avatar never leaves
	// UNSET and never gates anything.
	bool  bInterventionArmed_ = false;
	// Set from the command-link receive thread the first time the avatar
	// reports any arm as not-UNSET. Consumed on the game thread.
	std::atomic<bool> bAuthorityLiveSeen_{ false };
	// Whole-body handover: one trigger takes the WHOLE robot -- both arms and
	// the head -- and one RESUME hands all of it back.
	//
	// The per-arm machinery underneath is untouched and still does the actual
	// work; this only decides how many arms a single request covers and whether
	// the HUD is allowed to say "left" and "right" at all. Flipping it to false
	// restores per-limb handover with no code changes, which is the point:
	// session 002 showed per-limb yields no trainable chunks under ACT's joint
	// action vector, but that is a property of the current policy head, not a
	// permanent verdict, so the path back stays open.
	//
	// The head follows this flag rather than carrying its own: a head that
	// keeps taking operator input while the arms are policy-driven would put
	// the policy's camera somewhere the policy did not choose, and the images
	// it is conditioning on stop matching what it asked for.
	UPROPERTY(EditAnywhere, Category = "Teleop|Intervention")
	bool  bWholeBodyAuthority = true;
	// Authority as the avatar last reported it, one per arm. Written from the
	// command-link receive thread, read on the game thread.
	TAtomic<uint8> LeftAuthority_{ static_cast<uint8>(EControlAuthority::Unset) };
	TAtomic<uint8> RightAuthority_{ static_cast<uint8>(EControlAuthority::Unset) };
	// Clutch level last seen by the authority edge detector. Deliberately not
	// bPrevLeftClutch_/bPrevRightClutch_: those belong to the periodic stream-row
	// block and only advance when that block runs, which would put an unknown
	// number of frames between a trigger pull and the takeover.
	bool  bPrevAuthClutch_[2] = { true, true };
	// Takeovers this episode, for the panel's INTERV readout. Counted on the
	// request rather than on the avatar's echo so a refused claim still shows
	// the operator tried. Reset with the other per-episode stats in
	// UpdatePolicyStats; the session total is in the event log.
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
	// Operator-chosen global mute. USoundFeedback is the only thing in this
	// interface that plays audio, so this silences everything the operator
	// hears. Controller haptics are deliberately NOT affected -- they are the
	// only feedback channel left once audio is off.
	bool  bSoundMuted_         = false;
	int32 PerfSampleCounter_ = 0;

	TArray<TUniquePtr<IVideoSource>> PiPSources_;
	TArray<UTexture2D*>              PiPTextures_;
	TArray<FString>                  PiPSourceNames_;
	FString                  ActivePiPStreamName_;
	TArray<FString>          CurrentMenuStreams_;
	bool                     bMenuOpen_            = false;

	// Cached out of Config (a BeginPlay-local, not a member -- see
	// AOperatorPawn::BeginPlay) so the viewmodeButton handler in
	// UpdateStateMachine can use them without holding onto UTeleOpConfig.
	bool    bHasTwinMainStream_ = false;
	bool    bOverlayEnabled_    = true;

	// Pose sampling and arm-command send run here, not on the game tick, so
	// the command rate stops being whatever the renderer happens to manage.
	TUniquePtr<FTeleopCommandThread> CommandThread_;

	// 120 rather than 90: above the display rate, comfortably inside the
	// tracker's demonstrated >=90 Hz update rate, and 12% of a core in the
	// pacing spin. Higher buys under a millisecond against a 70 ms link.
	UPROPERTY(EditAnywhere, Category = "Teleop")
	float CommandThreadRateHz = 120.f;

	FString TwinMainStreamKey_;   // VideoFeedComponent registration key (== Config->Stream.TwinStream.Name)
	FString TwinMainStreamLabel_; // viewmode_label text when twin is active ("TWIN", as configured)
	FString TwinPiPEntryName_;    // PiP menu entry text (TwinMainStreamLabel_.ToLower(), e.g. "twin")

	// PiP's "TWIN" entry shares the already-running main-view TWIN source's
	// decode (VideoFeedComponent::UpdateSourceTexture) instead of opening a
	// second receiver -- two independent GStreamer receivers can't both bind
	// the same UDP port anyway, and a second decode would cost GPU we don't
	// have spare. Own texture pointer since PiPTextures_ is only for entries
	// that own their own IVideoSource.
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