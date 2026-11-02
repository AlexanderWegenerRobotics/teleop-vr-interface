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

    // --- twin configuration ---
    // The twin is a second, independent peer: commands are mirrored to it, but the
    // avatar getters below keep reflecting the avatar only. Use
    // IsTwinAlive()/GetTwinState() for the twin link.
    UPROPERTY() FString TwinIP = TEXT("127.0.0.1");
    UPROPERTY() int32 TwinSendPort    = 7500;
    UPROPERTY() int32 TwinReceivePort = 8500;
    UPROPERTY() int32 TwinArmLeftSendPort    = 7001;
    UPROPERTY() int32 TwinArmLeftReceivePort = 8001;
    UPROPERTY() int32 TwinArmRightSendPort    = 7002;
    UPROPERTY() int32 TwinArmRightReceivePort = 8002;
    UPROPERTY() int32 TwinHeadSendPort    = 7003;
    UPROPERTY() int32 TwinHeadReceivePort = 8003;

    // Local orchestrator peer: send-only, no acks. Receives every reliable message the
    // avatar gets (gaze_sample, episode_restart, ...) plus operator_state. Port 0 = off.
    UPROPERTY() FString LocalIP = TEXT("127.0.0.1");
    UPROPERTY() int32 LocalPort = 0;

    // --- device streams ---
    // Sends to the avatar and, if open, the twin. Reads come from the avatar streams
    // only.
    void SendArmCommand(ArmCommandMsg& Msg, uint8 DeviceIndex = 0);
    void SendHeadCommand(HeadCommandMsg& Msg);

    bool HasNewArmState(uint8 DeviceIndex = 0) const;
    bool HasNewHeadState() const;
    ArmStateMsg ReadArmState(uint8 DeviceIndex = 0);
    ArmStateMsg PeekArmState(uint8 DeviceIndex = 0) const;
    HeadStateMsg ReadHeadState();
    // Latest head state without consuming the new-data flag, used by the head
    // handover to re-anchor.
    HeadStateMsg PeekHeadState() const;

    // --- command link ---
    // SendStateRequest/SendReliable are mirrored to the twin with separate sequence
    // numbers and acks. RegisterHandler is avatar-only, so handlers do not fire
    // twice.
    void SendStateRequest(SysState RequestedState);
    // Claims or releases one arm (0 = left, 1 = right); the avatar arbitrates per
    // arm. Sent with an ack request so a lost claim is retried. The HUD renders the
    // avatar's echo, not this request. Not mirrored to the twin. bAllDevices omits
    // the device key, which the avatar reads as all arms.
    void SendAuthorityRequest(uint8 DeviceIndex, EControlAuthority Requested, bool bAllDevices = false);
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

    // Round trip on this machine's clock: command send to the first state packet
    // echoing it in applied_cmd_sequence. Includes up to one avatar state period (5
    // ms). 0 = no sample yet.
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    float GetArmRttMs(uint8 DeviceIndex = 0) const;
    float GetArmLastRttMs(uint8 DeviceIndex = 0) const;

    // Network one-way delay with the avatar's dwell removed: ((recv -
    // send(echo_cmd_sequence)) - echo_hold_us) / 2. Clock-offset free, unlike
    // GetArmStateLatencyMs. Only measurable while commands flow; 0 = unknown, shown
    // as "--".
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    float GetArmNetworkDelayMs(uint8 DeviceIndex = 0) const;   // EWMA, for display
    float GetArmLastNetworkDelayMs(uint8 DeviceIndex = 0) const;  // raw last sample, for logs; -1 = unknown

    // The value the HUD shows and stream.csv logs: the worse of the two arms' fresh
    // values. 0 = unknown.
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    float GetNetworkDelayMs() const;

    ArmStateMsg PeekArmStateWithRecvTime(uint8 DeviceIndex, uint64& OutRecvNs) const;

    // True when packets still arrive but the payload stopped changing. Threshold
    // defaults to 5x the 200 Hz publish period.
    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsArmStateStale(uint8 DeviceIndex = 0, float ThresholdMs = 25.f) const;

    UPROPERTY(BlueprintAssignable) FOnConnectionChanged OnAvatarConnectionChanged;

    // Fires when an arm stream enters FAULT or reports a different fault code, not
    // once per packet. Fires from the receive thread, so handlers must be cheap and
    // thread-safe.
    DECLARE_MULTICAST_DELEGATE_TwoParams(FOnArmFault, uint8 /*DeviceIndex*/, FaultCode);
    FOnArmFault OnArmFault;

private:
    TUniquePtr<ArmStream>   ArmStreams_[2];
    TUniquePtr<HeadStream>  HeadStream_;
    TUniquePtr<CommandLink>  CmdLink_;

    // Twin peers: separate sockets and streams, opened only if the twin ports are
    // set.
    TUniquePtr<ArmStream>   TwinArmStreams_[2];
    TUniquePtr<HeadStream>  TwinHeadStream_;
    TUniquePtr<CommandLink>  TwinCmdLink_;
    TUniquePtr<CommandLink>  LocalCmdLink_;

    bool bWasAvatarAlive_ = false;
    float HeartbeatAccum_ = 0.0f;

    static constexpr uint32 kRttRing = 1024;
    struct FArmRtt {
        FCriticalSection Mutex;
        uint32 SentSeq[kRttRing] = {};
        uint64 SentNs[kRttRing]  = {};
        uint32 LastEcho  = 0;
        float  LastRttMs = 0.f;
        float  RttMs     = 0.f;
        // Network-only delay from echo_cmd_sequence / echo_hold_us.
        uint32 LastNetEcho       = 0;
        float  LastNetDelayMs    = 0.f;
        float  NetDelayMs        = 0.f;
        double NetDelayUpdateSec = 0.0;   // FPlatformTime::Seconds() of the last sample
    };
    static constexpr double kNetDelayStaleSec = 1.0;
    FArmRtt ArmRtt_[2];
    void OnArmStateReceived(uint8 DeviceIndex, const ArmStateMsg& Msg, uint64 RecvNs);
};
