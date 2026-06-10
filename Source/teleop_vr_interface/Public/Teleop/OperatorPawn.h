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
	FFrameBundle BuildFrameBundle() const;

	FTransform HMDOrigin_;
	bool bHMDOriginValid_  = false;
	bool bLeftWasGrasping  = false;
	bool bRightWasGrasping = false;
	// True once avatar confirms ENGAGED after VR entered ENGAGED; prevents
	// dropping back to AWAITING on stale pre-transition avatar state.
	bool bAvatarConfirmedEngaged_ = false;
	bool bPendingVoiceReengage_   = false;
	bool bResetMenuOpen_          = false;
	bool bAnnotationPending_      = false;

	float VideoQuadWidth_  = 0.f;
	float VideoQuadHeight_ = 0.f;

	void UpdateInfoBar();
	void SendEpisodeRestart(const FString& Label);

	// Session tracking
	double SessionStartTime_ = 0.0;
	int32  EpisodeCount_     = 0;

	// Logging
	TUniquePtr<FTeleOpLogger> Logger_;
	float LastHeadPan_   = 0.f;
	float LastHeadTilt_  = 0.f;
	uint8 PrevLeftGear_  = 255;   // 255 = uninitialized, forces first-tick event
	uint8 PrevRightGear_ = 255;
	bool  bPrevLeftClutch_  = false;
	bool  bPrevRightClutch_ = false;
	bool  bStatsVisible_    = false;
	int32 PerfSampleCounter_ = 0;

	TArray<TUniquePtr<IVideoSource>> PiPSources_;
	TArray<UTexture2D*>              PiPTextures_;
	TArray<FString>                  PiPSourceNames_;
	FString                  ActivePiPStreamName_;
	TArray<FString>          CurrentMenuStreams_;
	bool                     bMenuOpen_            = false;

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