#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shared/AvatarTypes.h"
#include "udpClient.h"
#include "ComLink.generated.h"


DECLARE_MULTICAST_DELEGATE_OneParam(FOnAvatarStateReceived, const FAvatarStateMsg&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnArmStateReceived, const FArmStateMsg&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeadStateReceived, const FHeadStateMsg&);

UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UComLink : public UActorComponent{
    GENERATED_BODY()

public:

    UComLink();
    virtual ~UComLink() override = default;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY() FString RemoteIP = TEXT("127.0.0.1");
    UPROPERTY() int32 AvatarSendPort = 7000;
    UPROPERTY() int32 AvatarReceivePort = 8000;
    UPROPERTY() int32 ArmLeftSendPort = 7001;
    UPROPERTY() int32 ArmLeftReceivePort = 8001;
    UPROPERTY() int32 ArmRightSendPort = 7002;
    UPROPERTY() int32 ArmRightReceivePort = 8002;
    UPROPERTY() int32 HeadSendPort = 7003;
    UPROPERTY() int32 HeadReceivePort = 8003;

    void SendAvatarCommand(ESysState RequestedState, uint8 SessionId = 0);
    void SendArmCommand(const FArmCommandMsg& Msg);
    void SendHeadCommand(const FHeadCommandMsg& Msg);

    FOnAvatarStateReceived OnAvatarStateReceived;
    FOnArmStateReceived    OnArmStateReceived;
    FOnHeadStateReceived   OnHeadStateReceived;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsAvatarAlive()    const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsArmLeftAlive()   const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsArmRightAlive()  const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    bool IsHeadAlive()      const;

    UFUNCTION(BlueprintCallable, Category = "ComLink")
    ESysState GetAvatarState() const { return LastAvatarState_; }

private:

    void DrainAvatar();
    void DrainArmLeft();
    void DrainArmRight();
    void DrainHead();

    static uint64 NowNs();

    TUniquePtr<udpClient> AvatarSocket_;
    TUniquePtr<udpClient> ArmLeftSocket_;
    TUniquePtr<udpClient> ArmRightSocket_;
    TUniquePtr<udpClient> HeadSocket_;

    ESysState LastAvatarState_ = ESysState::Offline;
};