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

    // --- device streams ---
    void SendArmCommand(ArmCommandMsg& Msg, uint8 DeviceIndex = 0);
    void SendHeadCommand(HeadCommandMsg& Msg);

    bool HasNewArmState(uint8 DeviceIndex = 0) const;
    bool HasNewHeadState() const;
    ArmStateMsg ReadArmState(uint8 DeviceIndex = 0);
    HeadStateMsg ReadHeadState();

    // --- command link (avatar system channel) ---
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

    SysState GetArmRemoteState(uint8 DeviceIndex = 0) const;
    FaultCode GetArmRemoteFault(uint8 DeviceIndex = 0) const;
    SysState GetHeadRemoteState() const;
    FaultCode GetHeadRemoteFault() const;

    UPROPERTY(BlueprintAssignable) FOnConnectionChanged OnAvatarConnectionChanged;

private:
    TUniquePtr<ArmStream>   ArmStreams_[2];
    TUniquePtr<HeadStream>  HeadStream_;
    TUniquePtr<CommandLink>  CmdLink_;

    bool bWasAvatarAlive_ = false;
    float HeartbeatAccum_ = 0.0f;
};
