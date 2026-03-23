#include "Input/TrackedControllerComponent.h"
#include "EnhancedInputComponent.h"
#include "DrawDebugHelpers.h"

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

    if (bDrawDebugRay && MotionController && TrackingState == EControllerTrackingState::Tracking) {
        FVector Pos = MotionController->GetComponentLocation();
        FVector Fwd = MotionController->GetForwardVector();
        FColor RayColor = bIsClutching ? FColor::Orange : (bOriginValid ? FColor::Green : FColor::Yellow);
        DrawDebugLine(GetWorld(), Pos, Pos + Fwd * DebugRayLength, RayColor, false, -1.0f, 0, 0.5f);

        if (bOriginValid) {
            DrawDebugPoint(GetWorld(), Origin.GetLocation(), 8.0f, FColor::Red, false, -1.0f);
        }
    }

    if (bPrintDebugInfo && FPlatformTime::Seconds() - LastLogTime > 0.5) {
        FString Hand = MotionController ? MotionController->GetName() : TEXT("Unknown");
        FControllerDeltaPose Delta = GetDeltaPose();
        UE_LOG(LogTemp, Log, TEXT("Controller[%s]: tracking=%d clutch=%d trigger=%.2f grip=%d origin_valid=%d delta_t=(%.2f,%.2f,%.2f)"),
            *Hand,
            static_cast<int>(TrackingState),
            bIsClutching,
            TriggerValue,
            bGripHeld,
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


}

void UTrackedControllerComponent::OnTrigger(const FInputActionValue& Value) {
    TriggerValue = Value.Get<float>();
}

void UTrackedControllerComponent::OnGripPressed(const FInputActionValue& Value) {
    bGripHeld = true;
}

void UTrackedControllerComponent::OnGripReleased(const FInputActionValue& Value) {
    bGripHeld = false;
}

void UTrackedControllerComponent::OnMenuPressed(const FInputActionValue& Value) {
    bMenuPressed = true;
}

void UTrackedControllerComponent::OnMenuReleased(const FInputActionValue& Value) {
    bMenuPressed = false;
}

void UTrackedControllerComponent::CaptureOrigin() {
    BankedTranslation = FVector::ZeroVector;
    BankedRotation = FQuat::Identity;

    if (SampleBuffer.Num() == 0) {
        if (MotionController) {
            Origin = MotionController->GetComponentTransform();
            bOriginValid = true;
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
}

FControllerDeltaPose UTrackedControllerComponent::GetDeltaPose() const {
    FControllerDeltaPose Delta;
    if (!bOriginValid || !MotionController) return Delta;

    FTransform Current = MotionController->GetComponentTransform();
    FVector LiveTranslation = Current.GetLocation() - Origin.GetLocation();
    FQuat LiveRotation = Current.GetRotation() * Origin.GetRotation().Inverse();

    Delta.Translation = BankedTranslation + LiveTranslation;
    Delta.Rotation = LiveRotation * BankedRotation;

    return Delta;
}


bool UTrackedControllerComponent::IsTracking() const {
    return TrackingState == EControllerTrackingState::Tracking;
}

EControllerTrackingState UTrackedControllerComponent::GetTrackingState() const {
    return TrackingState;
}

void UTrackedControllerComponent::UpdateClutch() {
    if (bGripHeld && bOriginValid && TrackingState == EControllerTrackingState::Tracking) {
        if (!bIsClutching) {
            FTransform Current = MotionController->GetComponentTransform();
            FVector LiveTranslation = Current.GetLocation() - Origin.GetLocation();
            FQuat LiveRotation = Current.GetRotation() * Origin.GetRotation().Inverse();

            BankedTranslation += LiveTranslation;
            BankedRotation = LiveRotation * BankedRotation;
            BankedRotation.Normalize();

            Origin = Current;
        }
        bIsClutching = true;
        Origin = MotionController->GetComponentTransform();
    }
    else if (bWasClutching && !bGripHeld) {
        bIsClutching = false;
        Origin = MotionController->GetComponentTransform();
    }
    bWasClutching = bIsClutching;
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