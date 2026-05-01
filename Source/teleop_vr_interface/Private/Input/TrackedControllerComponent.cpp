#include "Input/TrackedControllerComponent.h"
#include "EnhancedInputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/IInputInterface.h"

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

    if (TrackingState == EControllerTrackingState::Tracking) {
        RecordSample();
    }

    UpdateClutch();
    UpdateScaledTranslation();

    if (bDrawDebugRay && MotionController && TrackingState == EControllerTrackingState::Tracking) {
        FVector Pos = MotionController->GetComponentLocation();
        FVector Fwd = MotionController->GetForwardVector();
        FColor RayColor = bFullClutch ? FColor::Orange : (bOriginValid ? FColor::Green : FColor::Yellow);
        DrawDebugLine(GetWorld(), Pos, Pos + Fwd * DebugRayLength, RayColor, false, -1.0f, 0, 0.5f);

        DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetForwardVector() * 10.f, FColor::Red, false, -1.f, 0, 0.5f);
        DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetRightVector() * 10.f, FColor::Green, false, -1.f, 0, 0.5f);
        DrawDebugLine(GetWorld(), Pos, Pos + MotionController->GetUpVector() * 10.f, FColor::Blue, false, -1.f, 0, 0.5f);

        if (bOriginValid) {
            DrawDebugPoint(GetWorld(), Origin.GetLocation(), 8.0f, FColor::Red, false, -1.0f);
        }
    }

    if (bPrintDebugInfo && FPlatformTime::Seconds() - LastLogTime > 0.5) {
        FString Hand = MotionController ? MotionController->GetName() : TEXT("Unknown");
        FControllerDeltaPose Delta = GetDeltaPose();
        float ClutchScale = GetClutchFactor();
        UE_LOG(LogTemp, Log, TEXT("Controller[%s]: tracking=%d fullclutch=%d clutch_scale=%.2f trigger=%.2f grasp=%d gear=%d origin_valid=%d delta_t=(%.2f,%.2f,%.2f)"),
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
}

void UTrackedControllerComponent::OnTrigger(const FInputActionValue& Value) {
    TriggerValue = Value.Get<float>();
}

void UTrackedControllerComponent::OnGripPressed(const FInputActionValue& Value) {
    bGripHeld = !bGripHeld;
}

void UTrackedControllerComponent::OnGripReleased(const FInputActionValue& Value) {
    //bGripHeld = false;
}

void UTrackedControllerComponent::OnMenuPressed(const FInputActionValue& Value) {
    bMenuPressed = true;
}

void UTrackedControllerComponent::OnMenuReleased(const FInputActionValue& Value) {
    bMenuPressed = false;
}

void UTrackedControllerComponent::OnPadUp(const FInputActionValue& Value) {
    if (ScaleFactor < MaxScale) {
        ScaleFactor++;
    }
}

void UTrackedControllerComponent::OnPadDown(const FInputActionValue& Value) {
    if (ScaleFactor > MinScale) {
        ScaleFactor--;
    }
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

    FVector CurrentLocation = MotionController->GetComponentLocation();

    if (!bPrevLocationValid) {
        PrevTrackedLocation = CurrentLocation;
        bPrevLocationValid = true;
        return;
    }

    FVector TickDelta = CurrentLocation - PrevTrackedLocation;
    float ClutchScale = ComputeClutchScale(TriggerValue);
    ScaledTranslation += TickDelta * ClutchScale * static_cast<float>(ScaleFactor);
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
            PrevTrackedLocation = Origin.GetLocation();
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
    PrevTrackedLocation = AvgPos;
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
            PrevTrackedLocation = Origin.GetLocation();
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