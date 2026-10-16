#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Queue.h"
#include "Networking/UdpSocket.h"
#include "NudgeReceiver.generated.h"

class UTrackedControllerComponent;
class USoundFeedback;
class FTeleOpLogger;
class UIntentHighlightComponent;

// One cue command from the nudge engine (teleop-nudge CueCommand.to_wire()).
struct FCueCommand {
    uint32  Seq = 0;
    FString Nudge;
    FString Channel;     // haptic_left | haptic_right | audio | visual
    FString Primitive;   // pulse | buzz | click | tone | ring | highlight
    FString Arm;         // left | right | none
    float   Intensity = 0.f;
    float   DurationS = 0.f;
    uint64  IssueNs = 0;
};

// Receives cue commands over UDP (msgpack) and renders them as primitives. Holds no
// thresholds and makes no decisions: the engine says what, this only says how.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UNudgeReceiver : public UActorComponent {
    GENERATED_BODY()

public:
    UNudgeReceiver();

    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ReceivePort <= 0 leaves the receiver disabled.
    void Open(int32 ReceivePort, UTrackedControllerComponent* Left, UTrackedControllerComponent* Right,
              USoundFeedback* Sound, FTeleOpLogger* Logger, UIntentHighlightComponent* Highlight = nullptr);

    int32 Received() const { return Received_; }

private:
    void HandleDatagram(const uint8* Data, int32 Size);
    void Render(const FCueCommand& Cue);

    TUniquePtr<UdpSocket> Socket_;
    TQueue<FCueCommand, EQueueMode::Mpsc> Queue_;
    UTrackedControllerComponent* Left_  = nullptr;
    UTrackedControllerComponent* Right_ = nullptr;
    USoundFeedback* Sound_   = nullptr;
    FTeleOpLogger*  Logger_  = nullptr;
    UIntentHighlightComponent* Highlight_ = nullptr;
    int32 Received_ = 0;
};
