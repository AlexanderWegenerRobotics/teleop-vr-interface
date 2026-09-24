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
        StickyState_.Store(static_cast<uint8>(State));
        StickyFault_.Store(static_cast<uint8>(Fault));
    }

    void Send(TSend& Msg) {
        if (!Socket_) return;
        // Send() runs on the command thread, SetState() on the game thread.
        Msg.header.sequence     = SendSeq_.IncrementExchange() + 1;
        Msg.header.timestamp_ns = timestamp_ns();
        Msg.header.state        = static_cast<SysState>(StickyState_.Load());
        Msg.header.fault_code   = static_cast<FaultCode>(StickyFault_.Load());
        Socket_->Send(&Msg, sizeof(TSend));
    }

    bool HasNew() const { return bHasNew_; }

    TRecv Read() {
        FScopeLock Lock(&Mutex_);
        bHasNew_ = false;
        return LastRecv_;
    }

    TRecv Peek() const {
        FScopeLock Lock(&Mutex_);
        return LastRecv_;
    }

    TRecv Peek(uint64& OutRecvNs) const {
        FScopeLock Lock(&Mutex_);
        OutRecvNs = LastRecvNs_;
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

    // Age of the newest received sample, in milliseconds: how long ago the
    // REMOTE actually read its hardware, not how long the packet spent on the
    // wire. GetStateLatencyMs() answers the second question and is blind to
    // the first, because a sender whose control loop has died keeps emitting
    // packets with fresh send timestamps.
    //
    // Returns -1 if the sender does not populate sample_time_ns (older build),
    // so callers can distinguish "unknown" from "fresh". Callers should treat
    // negative as unknown and NOT alarm on it.
    // Measured on this machine's clock only: time since a packet carrying a
    // NEW sample_time_ns arrived. Independent of the clock offset between hosts.
    float GetStateAgeMs() const {
        uint64 AdvanceNs;
        {
            FScopeLock Lock(&Mutex_);
            AdvanceNs = LastSampleAdvanceNs_;
        }
        if (AdvanceNs == 0) return -1.f;
        const uint64 NowNs = timestamp_ns();
        if (NowNs <= AdvanceNs) return 0.f;
        return static_cast<float>((NowNs - AdvanceNs) / 1000000.0);
    }

    FOnStreamFault OnFaultDetected;

    // Called on the receive thread for every accepted packet, with the local receive time.
    TFunction<void(const TRecv&, uint64)> OnReceived;

private:
    void HandleReceive(const uint8* Data, int32 Size) {
        if (Size != sizeof(TRecv)) return;

        TRecv Msg;
        FMemory::Memcpy(&Msg, Data, sizeof(TRecv));

        const uint64 RecvNs = timestamp_ns();
        uint32 Seq = Msg.header.sequence;
        if (LastRecvSeq_ > 0 && Seq + 1000 < LastRecvSeq_) {
            LastRecvSeq_ = 0;
            LatencyMs_   = 0.f;
        }
        if (Seq > LastRecvSeq_ + 1 && LastRecvSeq_ > 0) {
            DroppedCount_ += (Seq - LastRecvSeq_ - 1);
        }

        bool bAccepted = false;
        if (Seq > LastRecvSeq_ || LastRecvSeq_ == 0) {
            FScopeLock Lock(&Mutex_);
            bAccepted = true;
            LastRecv_ = Msg;
            bHasNew_  = true;
            LastRecvSeq_ = Seq;
            LastRecvNs_  = RecvNs;
            if (Msg.header.sample_time_ns != 0 && Msg.header.sample_time_ns != LastSampleNs_) {
                LastSampleNs_        = Msg.header.sample_time_ns;
                LastSampleAdvanceNs_ = RecvNs;
            }

            if (Msg.header.timestamp_ns > 0) {
                uint64_t NowNs = timestamp_ns();
                if (NowNs > Msg.header.timestamp_ns) {
                    float LatMs = static_cast<float>((NowNs - Msg.header.timestamp_ns) / 1000000.0);
                    if (LatMs < 5000.f) {
                        LatencyMs_ = 0.1f * LatMs + 0.9f * LatencyMs_;
                    }
                }
            }

            if (Msg.header.state != SysState::FAULT) {
                bRemoteFaulted_ = false;
                LastFaultCode_  = FaultCode::NONE;
            } else if (!bRemoteFaulted_ || Msg.header.fault_code != LastFaultCode_) {
                bRemoteFaulted_ = true;
                LastFaultCode_  = Msg.header.fault_code;
                if (OnFaultDetected.IsBound()) {
                    OnFaultDetected.Broadcast(Msg.header.fault_code);
                }
            }
        }
        if (bAccepted && OnReceived) OnReceived(Msg, RecvNs);

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
    TAtomic<uint32> SendSeq_{0};
    uint32   LastRecvSeq_ = 0;
    uint64   LastRecvNs_  = 0;
    uint64   LastSampleNs_ = 0;
    uint64   LastSampleAdvanceNs_ = 0;
    uint32   DroppedCount_= 0;
    TAtomic<bool> bHasNew_{false};

    float  LatencyMs_        = 0.f;
    float  MsgRateHz_        = 0.f;
    uint32 RecvCountInWindow_= 0;
    double WindowStartTime_  = 0.0;

    TAtomic<uint8> StickyState_{static_cast<uint8>(SysState::OFFLINE)};
    TAtomic<uint8> StickyFault_{static_cast<uint8>(FaultCode::NONE)};

    bool      bRemoteFaulted_ = false;
    FaultCode LastFaultCode_  = FaultCode::NONE;
};

using ArmStream  = TDeviceStream<ArmStateMsg, ArmCommandMsg>;
using HeadStream = TDeviceStream<HeadStateMsg, HeadCommandMsg>;
