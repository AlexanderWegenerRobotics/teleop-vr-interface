#include "Networking/CommandLink.h"
#include "HAL/PlatformTime.h"

CommandLink::CommandLink() = default;

CommandLink::~CommandLink() {
    Close();
}

bool CommandLink::Open(const Config& Cfg) {
    Config_ = Cfg;

    UdpSocket::Config SocketCfg;
    SocketCfg.RemoteIP = Cfg.RemoteIP;
    SocketCfg.SendPort = Cfg.SendPort;
    SocketCfg.ReceivePort = Cfg.ReceivePort;

    Socket_ = MakeUnique<UdpSocket>();
    Socket_->OnDataReceived.BindRaw(this, &CommandLink::HandleReceive);
    return Socket_->Open(SocketCfg);
}

void CommandLink::Close() {
    if (Socket_) {
        Socket_->Close();
        Socket_.Reset();
    }
}

void CommandLink::RegisterHandler(const std::string& MsgType, FMsgHandler Handler) {
    Handlers_.Add(FString(MsgType.c_str()), MoveTemp(Handler));
}

void CommandLink::SetState(SysState State, FaultCode Fault) {
    FScopeLock Lock(&SendMtx_);
    CurrentState_ = State;
    CurrentFault_ = Fault;
}

void CommandLink::Send(const std::string& MsgType,
    const msgpack::sbuffer& PayloadBuf,
    bool AckRequested) {
    TArray<uint8> Packed = PackEnvelope(MsgType, PayloadBuf, AckRequested);
    if (!Socket_) return;

    Socket_->Send(Packed.GetData(), Packed.Num());

    if (AckRequested) {
        FScopeLock Lock(&PendingMtx_);
        uint32 Seq;
        {
            FScopeLock SLock(&SendMtx_);
            Seq = SendSeq_;
        }

        PendingMsg PM;
        PM.PackedData = Packed;
        PM.Sequence = Seq;
        PM.RetryMs = Config_.DefaultRetryMs;
        PM.MaxRetries = Config_.MaxRetries;
        PM.Attempts = 1;
        PM.LastSendTime = FPlatformTime::Seconds();
        PendingAcks_.Add(MoveTemp(PM));
    }
}

TArray<uint8> CommandLink::PackEnvelope(const std::string& MsgType,
    const msgpack::sbuffer& PayloadBuf,
    bool AckRequested) {
    FScopeLock Lock(&SendMtx_);
    ++SendSeq_;

    msgpack::object_handle oh = msgpack::unpack(PayloadBuf.data(), PayloadBuf.size());

    FReliableEnvelope Env;
    Env.sequence = SendSeq_;
    Env.timestamp_ns = timestamp_ns();
    Env.state = static_cast<uint8_t>(CurrentState_);
    Env.fault_code = static_cast<uint8_t>(CurrentFault_);
    Env.msg_type = MsgType;
    Env.ack_requested = AckRequested;
    Env.payload = oh.get();

    msgpack::sbuffer Buf;
    msgpack::pack(Buf, Env);

    TArray<uint8> Result;
    Result.Append(reinterpret_cast<const uint8*>(Buf.data()), static_cast<int32>(Buf.size()));
    return Result;
}

void CommandLink::HandleReceive(const uint8* Data, int32 Size) {
    try {
        TUniquePtr<msgpack::object_handle> Oh = MakeUnique<msgpack::object_handle>(
            msgpack::unpack(reinterpret_cast<const char*>(Data), static_cast<size_t>(Size)));

        FReliableEnvelope Env;
        Oh->get().convert(Env);

        {
            FScopeLock Lock(&SendMtx_);
            RemoteState_ = static_cast<SysState>(Env.state);
            RemoteFaultCode_ = Env.fault_code;
        }

        if (Env.msg_type == "ack") {
            std::map<std::string, msgpack::object> Fields;
            Env.payload.convert(Fields);
            auto It = Fields.find("ack_sequence");
            if (It != Fields.end()) {
                HandleAck(It->second.as<uint32_t>());
            }
            return;
        }

        if (Env.ack_requested && Socket_) {
            msgpack::sbuffer AckPayload;
            msgpack::pack(AckPayload, std::map<std::string, uint32_t>{
                {"ack_sequence", Env.sequence}
            });

            TArray<uint8> AckPacked = PackEnvelope("ack", AckPayload, false);
            Socket_->Send(AckPacked.GetData(), AckPacked.Num());
        }

        {
            FScopeLock Lock(&RecvMtx_);
            IncomingQueue_.Add(MoveTemp(Env));
            IncomingZones_.Add(MoveTemp(Oh));
        }
    }
    catch (const msgpack::type_error&) {
        UE_LOG(LogTemp, Warning, TEXT("CommandLink: msgpack type error"));
    }
    catch (const msgpack::unpack_error&) {
        UE_LOG(LogTemp, Warning, TEXT("CommandLink: msgpack unpack error"));
    }
}

void CommandLink::HandleAck(uint32 AckSeq) {
    FScopeLock Lock(&PendingMtx_);
    PendingAcks_.RemoveAll([AckSeq](const PendingMsg& PM) {
        return PM.Sequence == AckSeq;
        });
}

void CommandLink::Tick() {
    ProcessRetries();

    TArray<FReliableEnvelope> ToDispatch;
    TArray<TUniquePtr<msgpack::object_handle>> Zones;
    {
        FScopeLock Lock(&RecvMtx_);
        ToDispatch = MoveTemp(IncomingQueue_);
        Zones = MoveTemp(IncomingZones_);
        IncomingQueue_.Reset();
        IncomingZones_.Reset();
    }

    for (const FReliableEnvelope& Env : ToDispatch) {
        FString Key(Env.msg_type.c_str());
        if (FMsgHandler* Handler = Handlers_.Find(Key)) {
            (*Handler)(Env);
        }
    }
}

void CommandLink::ProcessRetries() {
    FScopeLock Lock(&PendingMtx_);
    double Now = FPlatformTime::Seconds();

    for (int32 i = PendingAcks_.Num() - 1; i >= 0; --i) {
        PendingMsg& PM = PendingAcks_[i];
        double ElapsedMs = (Now - PM.LastSendTime) * 1000.0;

        if (ElapsedMs >= PM.RetryMs) {
            if (PM.Attempts >= PM.MaxRetries) {
                UE_LOG(LogTemp, Warning, TEXT("CommandLink: message seq=%d failed after %d retries"),
                    PM.Sequence, PM.Attempts);
                PendingAcks_.RemoveAt(i);
                continue;
            }

            if (Socket_) {
                Socket_->Send(PM.PackedData.GetData(), PM.PackedData.Num());
            }
            PM.Attempts++;
            PM.LastSendTime = Now;
        }
    }
}

bool CommandLink::IsAlive(double TimeoutSec) const {
    return Socket_ && Socket_->IsAlive(TimeoutSec);
}