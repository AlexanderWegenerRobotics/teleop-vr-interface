#include "Input/TrackedControllerComponent.h"
#include "EnhancedInputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/IInputInterface.h"

// Wrist-pivot calibration tool. Disabled (0) for normal operation: the capture/solve
// code stays compiled out of the hot path and the console commands become no-ops.
// Set to 1 and recompile to re-run CalibrateWristPivotRight/Left. The resulting offset
// is persisted via ControlPointOffset (see AOperatorPawn ctor) and is applied regardless
// of this flag.
#define WITH_PIVOT_CALIBRATION 0

UTrackedControllerComponent::UTrackedControllerComponent() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTrackedControllerComponent::BeginPlay() {
    Super::BeginPlay();
    SampleBuffer.Reserve(CalibrationSamples);
    BindInputActions();
}

void UTrackedControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UpdateTrackingState();

    // --- Wrist-pivot calibration capture (timed window, no button needed) ---
#if WITH_PIVOT_CALIBRATION
    if (bCalibCapturing) {
        const FString NameStr = MotionController ? MotionController->GetName() : TEXT("?");
        const double NowS = FPlatformTime::Seconds();

        if (MotionController && TrackingState == EControllerTrackingState::Tracking) {
            FTransform T = MotionController->GetComponentTransform();
            const float AdvanceDeg = FMath::RadiansToDegrees(T.GetRotation().AngularDistance(LastCalibQuat));
            if (CalibSamples.Num() == 0 || AdvanceDeg >= CalibAngularStepDeg) {
                CalibSamples.Add(T);
                LastCalibQuat = T.GetRotation();
            }
        }

        // Status every second - reports WHY if no samples (track state / controller).
        if (NowS - LastCalibProgressTime >= 1.0) {
            UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] capturing... %d samples, %.0fs left  [trackState=%d (0=Tracking,1=Lost,2=Stale), haveController=%d]"),
                *NameStr, CalibSamples.Num(),
                FMath::Max(0.0, CalibWindowSec - (NowS - CalibStartTime)),
                static_cast<int>(TrackingState), MotionController ? 1 : 0);
            LastCalibProgressTime = NowS;
        }

        if (NowS - CalibStartTime >= CalibWindowSec || CalibSamples.Num() >= 4000) {
            bCalibCapturing = false;
            SolvePivotCalibration();
        }
    }
#endif // WITH_PIVOT_CALIBRATION

    if (TrackingState == EControllerTrackingState::Tracking) {
        RecordSample();
    }

    UpdateClutch();
    UpdateScaledTranslation();

    if (bDrawDebugRay && MotionController && TrackingState == EControllerTrackingState::Tracking) {
        FVector Pos = MotionController->GetComponentLocation();
        FVector Fwd = MotionController->GetForwardVector();
        FColor RayColor = bFullClutch ? FColor::Orange : (bOriginValid ? FColor::Green : FColor::Yellow);
        //DrawDebugLine(GetWorld(), Pos, Pos + Fwd * DebugRayLength, RayColor, false, -1.0f, 0, 0.5f);

        //DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetForwardVector() * 10.f, FColor::Red, false, -1.f, 0, 0.3f);
        //DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetRightVector() * 10.f, FColor::Green, false, -1.f, 0, 0.3f);
        //DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetUpVector() * 10.f, FColor::Blue, false, -1.f, 0, 0.3f);

        //if (bOriginValid) {
        //    DrawDebugPoint(GetWorld(), Origin.GetLocation(), 8.0f, FColor::Red, false, -1.0f);
        //}
    }

    if (bPrintDebugInfo && FPlatformTime::Seconds() - LastLogTime > 0.5) {
        FString Hand = MotionController ? MotionController->GetName() : TEXT("Unknown");
        FControllerDeltaPose Delta = GetDeltaPose();
        float ClutchScale = GetClutchFactor();
        UE_LOG(LogTemp, Log, TEXT("Controller[%s]: tracking=%d fullclutch=%d clutch_scale=%.2f trigger=%.2f grasp=%d gear=%.1f origin_valid=%d delta_t=(%.2f,%.2f,%.2f)"),
            *Hand,
            static_cast<int>(TrackingState),
            bFullClutch,
            ClutchScale,
            TriggerValue,
            bGripHeld,
            ScaleFactor,
            bOriginValid,
            Delta.Translation.X, Delta.Translation.Y, Delta.Translation.Z);
        LastLogTime = FPlatformTime::Seconds();
    }
}

void UTrackedControllerComponent::BindInputActions() {
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn) return;

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
    if (!EIC) {
        UE_LOG(LogTemp, Warning, TEXT("TrackedController: no EnhancedInputComponent on pawn"));
        return;
    }

    if (IA_Trigger) {
        EIC->BindAction(IA_Trigger, ETriggerEvent::Triggered, this, &UTrackedControllerComponent::OnTrigger);
        EIC->BindAction(IA_Trigger, ETriggerEvent::Completed, this, &UTrackedControllerComponent::OnTrigger);
    }
    if (IA_Grip) {
        EIC->BindAction(IA_Grip, ETriggerEvent::Started, this, &UTrackedControllerComponent::OnGripPressed);
        EIC->BindAction(IA_Grip, ETriggerEvent::Completed, this, &UTrackedControllerComponent::OnGripReleased);
    }
    if (IA_Stop) {
        EIC->BindAction(IA_Stop, ETriggerEvent::Started, this, &UTrackedControllerComponent::OnMenuPressed);
        EIC->BindAction(IA_Stop, ETriggerEvent::Completed, this, &UTrackedControllerComponent::OnMenuReleased);
    }
    if (IA_PadUp) {
        EIC->BindAction(IA_PadUp, ETriggerEvent::Started, this, &UTrackedControllerComponent::OnPadUp);
    }
    if (IA_PadDown) {
        EIC->BindAction(IA_PadDown, ETriggerEvent::Started, this, &UTrackedControllerComponent::OnPadDown);
    }
    if (IA_HandGrip) {
        EIC->BindAction(IA_HandGrip, ETriggerEvent::Started,   this, &UTrackedControllerComponent::OnHandGripPressed);
        EIC->BindAction(IA_HandGrip, ETriggerEvent::Completed, this, &UTrackedControllerComponent::OnHandGripReleased);
    }
}

void UTrackedControllerComponent::OnTrigger(const FInputActionValue& Value) {
    TriggerValue = Value.Get<float>();
}

void UTrackedControllerComponent::OnGripPressed(const FInputActionValue& Value) {
#if WITH_PIVOT_CALIBRATION
    if (bCalibCapturing) return;
#endif
    if (bHandGripHeld) return;
    bGripHeld = !bGripHeld;
}

void UTrackedControllerComponent::OnGripReleased(const FInputActionValue& Value) {
}

void UTrackedControllerComponent::OnHandGripPressed(const FInputActionValue& Value) {
    bHandGripHeld = true;
}

void UTrackedControllerComponent::OnHandGripReleased(const FInputActionValue& Value) {
    bHandGripHeld = false;
}

void UTrackedControllerComponent::OnMenuPressed(const FInputActionValue& Value) {
    bMenuPressed = true;
}

void UTrackedControllerComponent::OnMenuReleased(const FInputActionValue& Value) {
    bMenuPressed = false;
}

void UTrackedControllerComponent::OnPadUp(const FInputActionValue& Value) {
    if (!bHandGripHeld) return;
    ScaleFactor = FMath::Min(ScaleFactor + ScaleStep, MaxScale);
}

void UTrackedControllerComponent::OnPadDown(const FInputActionValue& Value) {
    if (!bHandGripHeld) return;
    ScaleFactor = FMath::Max(ScaleFactor - ScaleStep, MinScale);
}

EControllerHand UTrackedControllerComponent::GetHand() const {
    if (!MotionController) return EControllerHand::Left;
    return MotionController->GetTrackingSource() == EControllerHand::Right
        ? EControllerHand::Right
        : EControllerHand::Left;
}

float UTrackedControllerComponent::ComputeClutchScale(float TriggerRaw) const {
    if (TriggerRaw >= ClutchActiveRangeMax) return 1.0f;
    if (TriggerRaw <= ClutchDeadZoneLow) return 0.0f;

    float t = (TriggerRaw - ClutchDeadZoneLow) / (ClutchActiveRangeMax - ClutchDeadZoneLow);
    float Scale = t * t;
    return FMath::Clamp(Scale, 0.0f, 1.0f);
}

float UTrackedControllerComponent::GetClutchFactor() const {
    if (bFullClutch) return 0.0f;
    return ComputeClutchScale(TriggerValue);
}

void UTrackedControllerComponent::UpdateScaledTranslation() {
    if (!bOriginValid || !MotionController || TrackingState != EControllerTrackingState::Tracking || bFullClutch) {
        return;
    }

    FVector CurrentLocation = ControlPointLocation(MotionController->GetComponentTransform());

    if (!bPrevLocationValid) {
        PrevTrackedLocation = CurrentLocation;
        bPrevLocationValid = true;
        return;
    }

    FVector TickDelta = CurrentLocation - PrevTrackedLocation;
    float ClutchScale = ComputeClutchScale(TriggerValue);
    ScaledTranslation += TickDelta * ClutchScale * ScaleFactor;
    PrevTrackedLocation = CurrentLocation;
}

void UTrackedControllerComponent::CaptureOrigin() {
    ScaledTranslation = FVector::ZeroVector;
    BankedScaledTranslation = FVector::ZeroVector;
    BankedRotation = FQuat::Identity;
    bPrevLocationValid = false;
    bFullClutch = true;

    if (SampleBuffer.Num() == 0) {
        if (MotionController) {
            Origin = MotionController->GetComponentTransform();
            bOriginValid = true;
            PrevTrackedLocation = ControlPointLocation(Origin);
            bPrevLocationValid = true;
        }
        return;
    }

    int32 Count = FMath::Min(SampleBuffer.Num(), CalibrationSamples);
    int32 Start = SampleBuffer.Num() - Count;

    FVector AvgPos = FVector::ZeroVector;
    FQuat AccumQuat = SampleBuffer[Start].GetRotation();
    AvgPos += SampleBuffer[Start].GetLocation();

    for (int32 i = Start + 1; i < SampleBuffer.Num(); ++i) {
        AvgPos += SampleBuffer[i].GetLocation();

        FQuat Q = SampleBuffer[i].GetRotation();
        if ((Q | AccumQuat) < 0.0f) {
            Q = -Q;
        }
        AccumQuat = FQuat::Slerp(AccumQuat, Q, 1.0f / (i - Start + 1));
    }

    AvgPos /= Count;
    AccumQuat.Normalize();

    Origin.SetLocation(AvgPos);
    Origin.SetRotation(AccumQuat);
    Origin.SetScale3D(FVector::OneVector);
    bOriginValid = true;
    PrevTrackedLocation = ControlPointLocation(Origin);
    bPrevLocationValid = true;
}

FControllerDeltaPose UTrackedControllerComponent::GetDeltaPose() const {
    FControllerDeltaPose Delta;
    if (!bOriginValid || !MotionController) return Delta;

    Delta.Translation = BankedScaledTranslation + ScaledTranslation;

    FTransform Current = MotionController->GetComponentTransform();
    FQuat LiveRotation = Origin.GetRotation().Inverse() * Current.GetRotation();
    Delta.Rotation = BankedRotation * LiveRotation;

    return Delta;
}

bool UTrackedControllerComponent::IsTracking() const {
    return TrackingState == EControllerTrackingState::Tracking;
}

EControllerTrackingState UTrackedControllerComponent::GetTrackingState() const {
    return TrackingState;
}

void UTrackedControllerComponent::UpdateClutch() {
    if (!bFullClutch && TriggerValue <= ClutchDisengageThreshold) {
        if (bOriginValid && MotionController && TrackingState == EControllerTrackingState::Tracking) {
            FTransform Current = MotionController->GetComponentTransform();
            FQuat LiveRotation = Origin.GetRotation().Inverse() * Current.GetRotation();
            BankedRotation = BankedRotation * LiveRotation;
            BankedRotation.Normalize();
            BankedScaledTranslation += ScaledTranslation;
            ScaledTranslation = FVector::ZeroVector;
        }
        bFullClutch = true;
        PlayClutchHaptic(ClutchEngageHapticIntensity, ClutchEngageHapticDuration);
        if (MotionController) {
            Origin = MotionController->GetComponentTransform();
        }
    }
    else if (bFullClutch && TriggerValue > ClutchEngageThreshold) {
        bFullClutch = false;
        PlayClutchHaptic(ClutchDisengageHapticIntensity, ClutchDisengageHapticDuration);
        if (MotionController) {
            Origin = MotionController->GetComponentTransform();
            PrevTrackedLocation = ControlPointLocation(Origin);
        }
    }

    if (bFullClutch && MotionController) {
        Origin = MotionController->GetComponentTransform();
        PrevTrackedLocation = Origin.GetLocation();
    }

    bWasFullClutch = bFullClutch;
}

void UTrackedControllerComponent::UpdateTrackingState() {
    if (!MotionController) {
        TrackingState = EControllerTrackingState::Lost;
        return;
    }

    if (!MotionController->IsTracked()) {
        double TimeSinceLast = FPlatformTime::Seconds() - LastTrackingTimestamp;
        TrackingState = (TimeSinceLast > StaleThreshold)
            ? EControllerTrackingState::Lost
            : EControllerTrackingState::Stale;
        return;
    }

    FTransform Current = MotionController->GetComponentTransform();
    LastTrackedTransform = Current;
    LastTrackingTimestamp = FPlatformTime::Seconds();
    TrackingState = EControllerTrackingState::Tracking;
}

void UTrackedControllerComponent::RecordSample() {
    if (!MotionController) return;

    SampleBuffer.Add(MotionController->GetComponentTransform());
    if (SampleBuffer.Num() > CalibrationSamples * 2) {
        SampleBuffer.RemoveAt(0, SampleBuffer.Num() - CalibrationSamples);
    }
}

void UTrackedControllerComponent::PlayClutchHaptic(float Intensity, float Duration) {
    APawn* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn) return;
    APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
    if (!PC) return;

    EControllerHand Hand = GetHand();
    PC->SetHapticsByValue(Intensity, 1.0f, Hand);

    FTimerHandle TimerHandle;
    GetWorld()->GetTimerManager().SetTimer(TimerHandle, [PC, Hand]() {
        PC->SetHapticsByValue(0.0f, 0.0f, Hand);
        }, Duration, false);
}

void UTrackedControllerComponent::ArmPivotCalibration() {
#if !WITH_PIVOT_CALIBRATION
    UE_LOG(LogTemp, Warning, TEXT("[PivotCalib] disabled in this build. Set WITH_PIVOT_CALIBRATION=1 in TrackedControllerComponent.cpp and recompile to re-run."));
    return;
#else
    const FString NameStr = MotionController ? MotionController->GetName() : TEXT("?");

    // Second call while running -> finish early and solve.
    if (bCalibCapturing) {
        bCalibCapturing = false;
        SolvePivotCalibration();
        return;
    }

    // Start capturing immediately (no button needed).
    bCalibCapturing       = true;
    CalibStartTime        = FPlatformTime::Seconds();
    LastCalibProgressTime = CalibStartTime;
    CalibSamples.Reset();
    if (MotionController) LastCalibQuat = MotionController->GetComponentQuat();
    UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] CAPTURING NOW for up to %.0f s - rotate the wand IN PLACE (mix pitch, yaw, roll), keep the wrist fixed. Re-run the command to finish early. trackState=%d (0=Tracking)"),
        *NameStr, CalibWindowSec, static_cast<int>(TrackingState));
#endif // WITH_PIVOT_CALIBRATION
}

// Wrist-pivot (lever-arm) calibration.
// Model: during a pure in-place rotation the wrist pivot W is fixed, while the tracked
// pose (p_i, R_i) swings around it: W = p_i + R_i * c, with c a constant local-frame
// offset (tracked origin -> wrist). Solve the mean-subtracted least squares
//   min_c  sum_i || (p_i - p_mean) + (R_i - R_mean) c ||^2
// => A c = b  with  A = sum dR_i^T dR_i ,  b = - sum dR_i^T dP_i   (3x3 solve).
void UTrackedControllerComponent::SolvePivotCalibration() {
#if WITH_PIVOT_CALIBRATION
    const FString Tag = MotionController ? MotionController->GetName() : TEXT("?");
    const int32 N = CalibSamples.Num();
    if (N < 20) {
        UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] only %d samples - move more / longer. Aborted."), *Tag, N);
        return;
    }

    // Rotation matrix (column-vector convention: v_world = R * v_local) from a quat.
    auto FillR = [](const FQuat& Q, double R[3][3]) {
        const FVector X = Q.GetAxisX(), Y = Q.GetAxisY(), Z = Q.GetAxisZ();
        R[0][0]=X.X; R[1][0]=X.Y; R[2][0]=X.Z;
        R[0][1]=Y.X; R[1][1]=Y.Y; R[2][1]=Y.Z;
        R[0][2]=Z.X; R[1][2]=Z.Y; R[2][2]=Z.Z;
    };

    double pbar[3] = {0,0,0};
    double Rbar[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
    for (const FTransform& T : CalibSamples) {
        const FVector p = T.GetLocation();
        pbar[0]+=p.X; pbar[1]+=p.Y; pbar[2]+=p.Z;
        double R[3][3]; FillR(T.GetRotation(), R);
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) Rbar[i][j]+=R[i][j];
    }
    for (int k=0;k<3;++k) pbar[k]/=N;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) Rbar[i][j]/=N;

    double A[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
    double b[3] = {0,0,0};
    for (const FTransform& T : CalibSamples) {
        const FVector p = T.GetLocation();
        double R[3][3]; FillR(T.GetRotation(), R);
        double dR[3][3], dP[3];
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) dR[i][j]=R[i][j]-Rbar[i][j];
        dP[0]=p.X-pbar[0]; dP[1]=p.Y-pbar[1]; dP[2]=p.Z-pbar[2];
        for (int i=0;i<3;++i) {
            for (int j=0;j<3;++j) { double s=0; for (int k=0;k<3;++k) s+=dR[k][i]*dR[k][j]; A[i][j]+=s; }
            double sb=0; for (int k=0;k<3;++k) sb+=dR[k][i]*dP[k]; b[i]-=sb;
        }
    }

    const double det =
        A[0][0]*(A[1][1]*A[2][2]-A[1][2]*A[2][1])
      - A[0][1]*(A[1][0]*A[2][2]-A[1][2]*A[2][0])
      + A[0][2]*(A[1][0]*A[2][1]-A[1][1]*A[2][0]);
    if (FMath::Abs(det) < 1e-3) {
        UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] ill-conditioned (det=%.3e). Rotate about MORE distinct axes (pitch+yaw+roll). Aborted."), *Tag, det);
        return;
    }
    double inv[3][3];
    inv[0][0]= (A[1][1]*A[2][2]-A[1][2]*A[2][1])/det;
    inv[0][1]=-(A[0][1]*A[2][2]-A[0][2]*A[2][1])/det;
    inv[0][2]= (A[0][1]*A[1][2]-A[0][2]*A[1][1])/det;
    inv[1][0]=-(A[1][0]*A[2][2]-A[1][2]*A[2][0])/det;
    inv[1][1]= (A[0][0]*A[2][2]-A[0][2]*A[2][0])/det;
    inv[1][2]=-(A[0][0]*A[1][2]-A[0][2]*A[1][0])/det;
    inv[2][0]= (A[1][0]*A[2][1]-A[1][1]*A[2][0])/det;
    inv[2][1]=-(A[0][0]*A[2][1]-A[0][1]*A[2][0])/det;
    inv[2][2]= (A[0][0]*A[1][1]-A[0][1]*A[1][0])/det;

    const FVector c(
        inv[0][0]*b[0]+inv[0][1]*b[1]+inv[0][2]*b[2],
        inv[1][0]*b[0]+inv[1][1]*b[1]+inv[1][2]*b[2],
        inv[2][0]*b[0]+inv[2][1]*b[1]+inv[2][2]*b[2]);

    double sse=0;
    for (const FTransform& T : CalibSamples) {
        const FVector p = T.GetLocation();
        double R[3][3]; FillR(T.GetRotation(), R);
        double r[3];
        for (int i=0;i<3;++i) {
            double s = (i==0?p.X-pbar[0]:i==1?p.Y-pbar[1]:p.Z-pbar[2]);
            s += (R[i][0]-Rbar[i][0])*c.X + (R[i][1]-Rbar[i][1])*c.Y + (R[i][2]-Rbar[i][2])*c.Z;
            r[i]=s;
        }
        sse += r[0]*r[0]+r[1]*r[1]+r[2]*r[2];
    }
    const double rms_cm = FMath::Sqrt(sse/N);

    ControlPointOffset = c;  // apply immediately so it can be tested right away

    UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] DONE  N=%d  offset=(%.2f, %.2f, %.2f) cm  residualRMS=%.1f mm  det=%.2e"),
        *Tag, N, c.X, c.Y, c.Z, rms_cm*10.0, det);
    UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] applied. To persist: set ControlPointOffset = (X=%.3f, Y=%.3f, Z=%.3f) on this component's defaults."),
        *Tag, c.X, c.Y, c.Z);
    if (rms_cm > 1.0)
        UE_LOG(LogTemp, Warning, TEXT("[PivotCalib %s] WARN residual %.1f mm is high - the wrist likely translated during capture. Redo, keeping the wrist planted."), *Tag, rms_cm*10.0);
#endif // WITH_PIVOT_CALIBRATION
}