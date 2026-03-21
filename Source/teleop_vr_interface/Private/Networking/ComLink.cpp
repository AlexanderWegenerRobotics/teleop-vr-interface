#include "Networking/ComLink.h"
#include "HAL/PlatformTime.h"

UComLink::UComLink() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UComLink::BeginPlay(){
    Super::BeginPlay();

    AvatarSocket_ = MakeUnique<udpClient>(RemoteIP, true, AvatarSendPort, true, AvatarReceivePort);
    ArmLeftSocket_ = MakeUnique<udpClient>(RemoteIP, true, ArmLeftSendPort, true, ArmLeftReceivePort);
    ArmRightSocket_ = MakeUnique<udpClient>(RemoteIP, true, ArmRightSendPort, true, ArmRightReceivePort);
    HeadSocket_ = MakeUnique<udpClient>(RemoteIP, true, HeadSendPort, true, HeadReceivePort);

    UE_LOG(LogTemp, Log, TEXT("ComLink: initialized — remote %s | "
        "avatar %d/%d | armL %d/%d | armR %d/%d | head %d/%d"),
        *RemoteIP,
        AvatarSendPort, AvatarReceivePort,
        ArmLeftSendPort, ArmLeftReceivePort,
        ArmRightSendPort, ArmRightReceivePort,
        HeadSendPort, HeadReceivePort);
}

void UComLink::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (AvatarSocket_) { AvatarSocket_->stop();   AvatarSocket_.Reset(); }
    if (ArmLeftSocket_) { ArmLeftSocket_->stop();  ArmLeftSocket_.Reset(); }
    if (ArmRightSocket_) { ArmRightSocket_->stop(); ArmRightSocket_.Reset(); }
    if (HeadSocket_) { HeadSocket_->stop();     HeadSocket_.Reset(); }

    UE_LOG(LogTemp, Log, TEXT("ComLink: stopped"));
    Super::EndPlay(EndPlayReason);
}

void UComLink::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    DrainAvatar();
    DrainArmLeft();
    DrainArmRight();
    DrainHead();
}

void UComLink::SendAvatarCommand(ESysState RequestedState, uint8 SessionId) {
    if (!AvatarSocket_) return;

    FAvatarCommandMsg Msg{};
    Msg.RequestedState = static_cast<uint8>(RequestedState);
    Msg.SessionId = SessionId;
    Msg.TimestampNs = NowNs();

    AvatarSocket_->send_raw(reinterpret_cast<const uint8*>(&Msg), sizeof(Msg));
}

void UComLink::SendArmCommand(const FArmCommandMsg& Msg){
    udpClient* Socket = (Msg.DeviceId == 0) ? ArmLeftSocket_.Get() : ArmRightSocket_.Get();
    if (!Socket) return;
    Socket->send_raw(reinterpret_cast<const uint8*>(&Msg), sizeof(Msg));
}

void UComLink::SendHeadCommand(const FHeadCommandMsg& Msg){
    if (!HeadSocket_) return;
    HeadSocket_->send_raw(reinterpret_cast<const uint8*>(&Msg), sizeof(Msg));
}

void UComLink::DrainAvatar() {
    if (!AvatarSocket_ || !AvatarSocket_->has_new_data()) return;

    TArray<uint8> Raw = AvatarSocket_->get_raw_data();
    if (Raw.Num() != sizeof(FAvatarStateMsg)) return;

    FAvatarStateMsg Msg{};
    FMemory::Memcpy(&Msg, Raw.GetData(), sizeof(Msg));

    LastAvatarState_ = static_cast<ESysState>(Msg.SystemState);
    OnAvatarStateReceived.Broadcast(Msg);
}

void UComLink::DrainArmLeft() {
    if (!ArmLeftSocket_ || !ArmLeftSocket_->has_new_data()) return;

    TArray<uint8> Raw = ArmLeftSocket_->get_raw_data();
    if (Raw.Num() != sizeof(FArmStateMsg)) return;

    FArmStateMsg Msg{};
    FMemory::Memcpy(&Msg, Raw.GetData(), sizeof(Msg));

    OnArmStateReceived.Broadcast(Msg);
}

void UComLink::DrainArmRight() {
    if (!ArmRightSocket_ || !ArmRightSocket_->has_new_data()) return;

    TArray<uint8> Raw = ArmRightSocket_->get_raw_data();
    if (Raw.Num() != sizeof(FArmStateMsg)) return;

    FArmStateMsg Msg{};
    FMemory::Memcpy(&Msg, Raw.GetData(), sizeof(Msg));

    OnArmStateReceived.Broadcast(Msg);
}

void UComLink::DrainHead() {
    if (!HeadSocket_ || !HeadSocket_->has_new_data()) return;

    TArray<uint8> Raw = HeadSocket_->get_raw_data();
    if (Raw.Num() != sizeof(FHeadStateMsg)) return;

    FHeadStateMsg Msg{};
    FMemory::Memcpy(&Msg, Raw.GetData(), sizeof(Msg));

    OnHeadStateReceived.Broadcast(Msg);
}

bool UComLink::IsAvatarAlive()   const { return AvatarSocket_ && AvatarSocket_->isConnectionAlive(); }
bool UComLink::IsArmLeftAlive()  const { return ArmLeftSocket_ && ArmLeftSocket_->isConnectionAlive(); }
bool UComLink::IsArmRightAlive() const { return ArmRightSocket_ && ArmRightSocket_->isConnectionAlive(); }
bool UComLink::IsHeadAlive()     const { return HeadSocket_ && HeadSocket_->isConnectionAlive(); }

uint64 UComLink::NowNs() {
    return static_cast<uint64>(FPlatformTime::Seconds() * 1e9);
}