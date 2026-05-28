#pragma once

#include "CoreMinimal.h"
#include "Networking/UdpSocket.h"
#include "Shared/protocol.hpp"

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <msgpack.hpp>

struct FReliableEnvelope {
    uint32_t    sequence      = 0;
    uint64_t    timestamp_ns  = 0;
    uint8_t     state         = static_cast<uint8_t>(SysState::OFFLINE);
    uint8_t     fault_code    = static_cast<uint8_t>(FaultCode::NONE);
    std::string msg_type;
    bool        ack_requested = false;
    msgpack::object payload;

    MSGPACK_DEFINE_MAP(sequence, timestamp_ns, state, fault_code,
                       msg_type, ack_requested, payload)
};

using FMsgHandler = TFunction<void(const FReliableEnvelope&)>;

class TELEOP_VR_INTERFACE_API CommandLink {
public:
    struct Config {
        FString RemoteIP;
        int32   SendPort       = 0;
        int32   ReceivePort    = 0;
        int32   DefaultRetryMs = 200;
        int32   MaxRetries     = 10;
    };

    CommandLink();
    ~CommandLink();

    bool Open(const Config& Cfg);
    void Close();
    void Tick();

    void RegisterHandler(const std::string& MsgType, FMsgHandler Handler);

    void Send(const std::string& MsgType,
              const msgpack::sbuffer& PayloadBuf,
              bool AckRequested = false);

    void SetState(SysState State, FaultCode Fault = FaultCode::NONE);

    bool IsAlive(double TimeoutSec = 2.0) const;
    SysState GetRemoteState() const { return RemoteState_; }
    uint8 GetRemoteFaultCode() const { return RemoteFaultCode_; }

private:
    struct PendingMsg {
        TArray<uint8> PackedData;
        uint32        Sequence;
        int32         RetryMs;
        int32         MaxRetries;
        int32         Attempts;
        double        LastSendTime;
    };

    void HandleReceive(const uint8* Data, int32 Size);
    void HandleAck(uint32 AckSeq);
    void ProcessRetries();
    TArray<uint8> PackEnvelope(const std::string& MsgType,
                               const msgpack::sbuffer& PayloadBuf,
                               bool AckRequested);

    TUniquePtr<UdpSocket> Socket_;
    Config Config_;

    FCriticalSection SendMtx_;
    uint32 SendSeq_ = 0;
    SysState CurrentState_ = SysState::OFFLINE;
    FaultCode CurrentFault_ = FaultCode::NONE;

    FCriticalSection RecvMtx_;
    TArray<FReliableEnvelope> IncomingQueue_;
    SysState RemoteState_ = SysState::OFFLINE;
    uint8 RemoteFaultCode_ = 0;

    FCriticalSection PendingMtx_;
    TArray<PendingMsg> PendingAcks_;

    TMap<FString, FMsgHandler> Handlers_;
    TArray<TUniquePtr<msgpack::object_handle>> IncomingZones_;
    UdpSocket::Config SocketCfg;
};
