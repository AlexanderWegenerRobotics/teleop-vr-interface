#include "Networking/ComLink.h"
#include "HAL/PlatformTime.h"

UComLink::UComLink() {
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UComLink::BeginPlay() {
    Super::BeginPlay();

    int32 ArmSendPorts[]  = { ArmLeftSendPort, ArmRightSendPort };
    int32 ArmRecvPorts[]  = { ArmLeftReceivePort, ArmRightReceivePort };

    for (int32 i = 0; i < 2; ++i) {
        ArmStreams_[i] = MakeUnique<ArmStream>();
        ArmStreams_[i]->Open(RemoteIP, ArmSendPorts[i], ArmRecvPorts[i]);
    }

    HeadStream_ = MakeUnique<HeadStream>();
    HeadStream_->Open(RemoteIP, HeadSendPort, HeadReceivePort);

    CommandLink::Config CmdCfg;
    CmdCfg.RemoteIP    = RemoteIP;
    CmdCfg.SendPort    = AvatarSendPort;
    CmdCfg.ReceivePort = AvatarReceivePort;
    CmdLink_ = MakeUnique<CommandLink>();
    CmdLink_->Open(CmdCfg);

    UE_LOG(LogTemp, Log, TEXT("ComLink: initialized -> %s"), *RemoteIP);
}

void UComLink::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    for (int32 i = 0; i < 2; ++i) {
        if (ArmStreams_[i]) { ArmStreams_[i]->Close(); ArmStreams_[i].Reset(); }
    }
    if (HeadStream_) { HeadStream_->Close(); HeadStream_.Reset(); }
    if (CmdLink_) { CmdLink_->Close(); CmdLink_.Reset(); }

    UE_LOG(LogTemp, Log, TEXT("ComLink: stopped"));
    Super::EndPlay(EndPlayReason);
}

void UComLink::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (CmdLink_) CmdLink_->Tick();

    HeartbeatAccum_ += DeltaTime;
    if (HeartbeatAccum_ >= 0.5f) {
        HeartbeatAccum_ = 0.0f;
        msgpack::sbuffer Buf;
        msgpack::pack(Buf, std::map<std::string, uint8_t>{{}});  // empty payload
        CmdLink_->Send("heartbeat", Buf, false);
    }

    bool bAlive = IsAvatarAlive();
    if (bAlive != bWasAvatarAlive_) {
        OnAvatarConnectionChanged.Broadcast(bAlive);
        bWasAvatarAlive_ = bAlive;
    }
}

void UComLink::SendArmCommand(ArmCommandMsg& Msg, uint8 DeviceIndex) {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex]) {
        Msg.header.device_id = static_cast<DeviceId>(DeviceIndex + 1);
        ArmStreams_[DeviceIndex]->Send(Msg);
    }
}

void UComLink::SendHeadCommand(HeadCommandMsg& Msg) {
    if (HeadStream_) {
        Msg.header.device_id = DeviceId::HEAD;
        HeadStream_->Send(Msg);
    }
}

bool UComLink::HasNewArmState(uint8 DeviceIndex) const {
    return DeviceIndex < 2 && ArmStreams_[DeviceIndex] && ArmStreams_[DeviceIndex]->HasNew();
}

bool UComLink::HasNewHeadState() const {
    return HeadStream_ && HeadStream_->HasNew();
}

ArmStateMsg UComLink::ReadArmState(uint8 DeviceIndex) {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex]) {
        return ArmStreams_[DeviceIndex]->Read();
    }
    return ArmStateMsg{};
}

HeadStateMsg UComLink::ReadHeadState() {
    if (HeadStream_) return HeadStream_->Read();
    return HeadStateMsg{};
}

void UComLink::SendStateRequest(SysState RequestedState) {
    if (!CmdLink_) return;

    msgpack::sbuffer Buf;
    msgpack::pack(Buf, std::map<std::string, uint8_t>{
        {"requested_state", static_cast<uint8_t>(RequestedState)}
    });
    CmdLink_->Send("state_change", Buf, true);
}

void UComLink::SendReliable(const std::string& MsgType,
                             const msgpack::sbuffer& Payload,
                             bool AckRequested) {
    if (CmdLink_) CmdLink_->Send(MsgType, Payload, AckRequested);
}

void UComLink::RegisterHandler(const std::string& MsgType, FMsgHandler Handler) {
    if (CmdLink_) CmdLink_->RegisterHandler(MsgType, MoveTemp(Handler));
}

bool UComLink::IsAvatarAlive() const {
    return CmdLink_ && CmdLink_->IsAlive();
}

bool UComLink::IsArmAlive(uint8 DeviceIndex) const {
    return DeviceIndex < 2 && ArmStreams_[DeviceIndex] && ArmStreams_[DeviceIndex]->IsAlive();
}

bool UComLink::IsHeadAlive() const {
    return HeadStream_ && HeadStream_->IsAlive();
}

ESysState UComLink::GetAvatarState() const {
    if (!CmdLink_) return ESysState::Offline;
    return static_cast<ESysState>(CmdLink_->GetRemoteState());
}

uint8 UComLink::GetAvatarFaultCode() const {
    if (!CmdLink_) return 0;
    return CmdLink_->GetRemoteFaultCode();
}

SysState UComLink::GetArmRemoteState(uint8 DeviceIndex) const {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex]) return ArmStreams_[DeviceIndex]->GetRemoteState();
    return SysState::OFFLINE;
}

FaultCode UComLink::GetArmRemoteFault(uint8 DeviceIndex) const {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex]) return ArmStreams_[DeviceIndex]->GetRemoteFault();
    return FaultCode::NONE;
}

SysState UComLink::GetHeadRemoteState() const {
    if (HeadStream_) return HeadStream_->GetRemoteState();
    return SysState::OFFLINE;
}

FaultCode UComLink::GetHeadRemoteFault() const {
    if (HeadStream_) return HeadStream_->GetRemoteFault();
    return FaultCode::NONE;
}

float UComLink::GetArmStateLatencyMs(uint8 DeviceIndex) const {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex])
        return ArmStreams_[DeviceIndex]->GetStateLatencyMs();
    return 0.f;
}

float UComLink::GetArmMsgRateHz(uint8 DeviceIndex) const {
    if (DeviceIndex < 2 && ArmStreams_[DeviceIndex])
        return ArmStreams_[DeviceIndex]->GetMsgRateHz();
    return 0.f;
}
