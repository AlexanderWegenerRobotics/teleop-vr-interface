#pragma once

#include "CoreMinimal.h"
#include "Networking/UdpSocket.h"
#include "Shared/protocol.hpp"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnStreamFault, FaultCode);

template<typename TRecv, typename TSend>
class TDeviceStream {
    static_assert(TIsTrivial<TRecv>::Value, "TRecv must be trivial");
    static_assert(TIsTrivial<TSend>::Value, "TSend must be trivial");

public:
    TDeviceStream() = default;
    ~TDeviceStream() { Close(); }

    bool Open(const FString& RemoteIP, int32 SendPort, int32 RecvPort) {
        UdpSocket::Config Cfg;
        Cfg.RemoteIP    = RemoteIP;
        Cfg.SendPort    = SendPort;
        Cfg.ReceivePort = RecvPort;

        Socket_ = MakeUnique<UdpSocket>();
        Socket_->OnDataReceived.BindRaw(this, &TDeviceStream::HandleReceive);
        return Socket_->Open(Cfg);
    }

    void Close() {
        if (Socket_) {
            Socket_->Close();
            Socket_.Reset();
        }
    }

    void SetState(SysState State, FaultCode Fault = FaultCode::NONE) {
        StickyState_ = State;
        StickyFault_ = Fault;
    }

    void Send(TSend& Msg) {
        if (!Socket_) return;
        Msg.header.sequence     = ++SendSeq_;
        Msg.header.timestamp_ns = timestamp_ns();
        Msg.header.state        = StickyState_;
        Msg.header.fault_code   = StickyFault_;
        Socket_->Send(&Msg, sizeof(TSend));
    }

    bool HasNew() const { return bHasNew_; }

    TRecv Read() {
        FScopeLock Lock(&Mutex_);
        bHasNew_ = false;
        return LastRecv_;
    }

    bool IsAlive(double TimeoutSec = 0.5) const {
        return Socket_ && Socket_->IsAlive(TimeoutSec);
    }

    SysState GetRemoteState() const {
        FScopeLock Lock(&Mutex_);
        return LastRecv_.header.state;
    }

    FaultCode GetRemoteFault() const {
        FScopeLock Lock(&Mutex_);
        return LastRecv_.header.fault_code;
    }

    uint32 DroppedPackets()    const { return DroppedCount_; }
    float  GetStateLatencyMs() const { return LatencyMs_; }
    float  GetMsgRateHz()      const { return MsgRateHz_; }

    FOnStreamFault OnFaultDetected;

private:
    void HandleReceive(const uint8* Data, int32 Size) {
        if (Size != sizeof(TRecv)) return;

        TRecv Msg;
        FMemory::Memcpy(&Msg, Data, sizeof(TRecv));

        uint32 Seq = Msg.header.sequence;
        if (Seq > LastRecvSeq_ + 1 && LastRecvSeq_ > 0) {
            DroppedCount_ += (Seq - LastRecvSeq_ - 1);
        }

        if (Seq > LastRecvSeq_ || LastRecvSeq_ == 0) {
            FScopeLock Lock(&Mutex_);
            LastRecv_ = Msg;
            bHasNew_  = true;
            LastRecvSeq_ = Seq;

            if (Msg.header.timestamp_ns > 0) {
                uint64_t NowNs = timestamp_ns();
                if (NowNs > Msg.header.timestamp_ns) {
                    float LatMs = static_cast<float>((NowNs - Msg.header.timestamp_ns) / 1000000.0);
                    if (LatMs < 5000.f) {
                        LatencyMs_ = 0.1f * LatMs + 0.9f * LatencyMs_;
                    }
                }
            }

            if (Msg.header.state == SysState::FAULT && OnFaultDetected.IsBound()) {
                OnFaultDetected.Broadcast(Msg.header.fault_code);
            }
        }

        ++RecvCountInWindow_;
        double NowSec = FPlatformTime::Seconds();
        double Elapsed = NowSec - WindowStartTime_;
        if (Elapsed >= 1.0) {
            MsgRateHz_ = static_cast<float>(RecvCountInWindow_) / static_cast<float>(Elapsed);
            RecvCountInWindow_ = 0;
            WindowStartTime_   = NowSec;
        }
    }

    TUniquePtr<UdpSocket> Socket_;
    mutable FCriticalSection Mutex_;

    TRecv    LastRecv_{};
    uint32   SendSeq_     = 0;
    uint32   LastRecvSeq_ = 0;
    uint32   DroppedCount_= 0;
    TAtomic<bool> bHasNew_{false};

    float  LatencyMs_        = 0.f;
    float  MsgRateHz_        = 0.f;
    uint32 RecvCountInWindow_= 0;
    double WindowStartTime_  = 0.0;

    SysState  StickyState_ = SysState::OFFLINE;
    FaultCode StickyFault_ = FaultCode::NONE;
};

using ArmStream  = TDeviceStream<ArmStateMsg, ArmCommandMsg>;
using HeadStream = TDeviceStream<HeadStateMsg, HeadCommandMsg>;
