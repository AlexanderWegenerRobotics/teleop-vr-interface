#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Networking/DeviceStream.h"
#include "Networking/CommandLink.h"
#include "Shared/AvatarTypes.h"
#include "ComLink.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConnectionChanged, bool, bConnected);

UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UComLink : public UActorComponent {
    GENERATED_BODY()

public:
    UComLink();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- configuration (set before BeginPlay) ---
    UPROPERTY() FString RemoteIP = TEXT("127.0.0.1");
    UPROPERTY() int32 AvatarSendPort    = 7000;
    UPROPERTY() int32 AvatarReceivePort = 8000;
    UPROPERTY() int32 ArmLeftSendPort   = 7001;
    UPROPERTY() int32 ArmLeftReceivePort  = 8001;
    UPROPERTY() int32 ArmRightSendPort  = 7002;
    UPROPERTY() int32 ArmRightReceivePort = 8002;
    UPROPERTY() int32 HeadSendPort      = 7003;
    UPROPERTY() int32 HeadReceivePort   = 8003;

    // --- twin configuration (set before BeginPlay, independent of the
    // avatar fields above -- see docs/twin_concept.md and network_twin.json).
    // Twin is a second, independent peer: commands mirror to it (the "local
    // tee" the twin's predictive display relies on), but avatar-facing state
    // getters below (GetAvatarState/IsAvatarAlive/etc.) are untouched -- they
    // keep reflecting the avatar only. Use IsTwinAlive()/GetTwinState() etc.
    // to query the twin's link independently. ---
    UPROPERTY() FString TwinIP = TEXT("127.0.0.1");
    UPROPERTY() int32 TwinSendPort    = 7500;
    UPROPERTY() int32 TwinReceivePort = 8500;
    UPROPERTY() int32 TwinArmLeftSendPort    = 7001;
    UPROPERTY() int32 TwinArmLeftReceivePort = 8001;
    UPROPERTY() int32 TwinArmRightSendPort    = 7002;
    UPROPERTY() int32 TwinArmRightReceivePort = 8002;
    UPROPERTY() int32 TwinHeadSendPort    = 7003;
    UPROPERTY() int32 TwinHeadReceivePort = 8003;

    // --- device streams ---
    // Sends to both the avatar and the twin (when twin channels are open --
    // see BeginPlay). Avatar-facing reads (HasNewArmState/ReadArmState/...)
    // are unaffected: they still read from the avatar streams only.
    void SendArmCommand(ArmCommandMsg& Msg, uint8 DeviceIndex = 0);
    void SendHeadCommand(HeadCommandMsg& Msg);

    bool HasNewArmState(uint8 DeviceIndex = 0) const;
    bool HasNewHeadState() const;
    ArmStateMsg ReadArmState(uint8 DeviceIndex = 0);
    ArmStateMsg PeekArmState(uint8 DeviceIndex = 0) const;
    HeadStateMsg ReadHeadState();

    // --- command link (avatar system channel) ---
    // SendStateRequest/SendReliable also mirror to the twin's CommandLink
    // (independent sequence numbers/retries/ack tracking per peer -- exactly
    // what's wanted since avatar and twin are separate machines/processes).
    // RegisterHandler is intentionally avatar-only: it drives existing app
    // state/HUD logic that should keep reflecting the avatar, not double-fire
    // per peer. Use the Twin* status accessors below for twin-specific reads.
    void SendStateRequest(SysState RequestedState);
    void SendReliable(const std::string& MsgType, const msgpack::sbuffer& Payload, bool AckRequested = false);
    void RegisterHandler(const std::string& MsgType, FMsgHandler Handler);

    // --- status ---
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsAvatarAlive() const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsArmAlive(uint8 DeviceIndex = 0) const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsHeadAlive() const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    ESysState GetAvatarState() const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    uint8 GetAvatarFaultCode() const;

    // --- twin status (independent of the avatar getters above) ---
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsTwinAlive() const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    ESysState GetTwinState() const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsTwinArmAlive(uint8 DeviceIndex = 0) const;

    SysState  GetArmRemoteState(uint8 DeviceIndex = 0) const;
    FaultCode GetArmRemoteFault(uint8 DeviceIndex = 0) const;
    SysState  GetHeadRemoteState() const;
    FaultCode GetHeadRemoteFault() const;

    float GetArmStateLatencyMs(uint8 DeviceIndex = 0) const;
    float GetArmMsgRateHz(uint8 DeviceIndex = 0) const;

    // Freshness of the remote's data, as opposed to freshness of the packets.
    // -1 = the sender does not report sample time (older build).
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    float GetArmStateAgeMs(uint8 DeviceIndex = 0) const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    int32 GetArmDroppedPackets(uint8 DeviceIndex = 0) const;

    // True when packets are still arriving but the payload has stopped
    // changing -- the failure mode that looked like a perfectly healthy link
    // on 2026-08-09. Threshold defaults to 5x the 200 Hz publish period.
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsArmStateStale(uint8 DeviceIndex = 0, float ThresholdMs = 25.f) const;

    UPROPERTY(BlueprintAssignable) FOnConnectionChanged OnAvatarConnectionChanged;

    // Fires when an arm stream receives a packet whose header reports FAULT.
    //
    // TDeviceStream has broadcast OnFaultDetected since the fault path was
    // written, but nothing ever bound to it, so a remote fault reached the
    // operator only as a small colour swatch that had to be noticed. This
    // re-exposes it at ComLink level where the pawn can actually subscribe.
    // Fires from the receive thread -- handlers must be cheap and thread-safe,
    // or marshal to the game thread.
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnArmFault, uint8 /*DeviceIndex*/, FaultCode);
    FOnArmFault OnArmFault;

private:
    TUniquePtr<ArmStream>   ArmStreams_[2];
    TUniquePtr<HeadStream>  HeadStream_;
    TUniquePtr<CommandLink>  CmdLink_;

    // Twin's own set of peers -- separate sockets/streams, opened only if
    // TwinIP/Twin*Port are non-zero (see BeginPlay). Mirrors of the avatar
    // set above, never merged with it.
    TUniquePtr<ArmStream>   TwinArmStreams_[2];
    TUniquePtr<HeadStream>  TwinHeadStream_;
    TUniquePtr<CommandLink>  TwinCmdLink_;

    bool bWasAvatarAlive_ = false;
    float HeartbeatAccum_ = 0.0f;
};
