#include "Networking/UdpSocket.h"
#include "HAL/PlatformTime.h"

UdpSocket::UdpSocket() {
    SocketSub_ = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
}

UdpSocket::~UdpSocket() {
    Close();
}

bool UdpSocket::Open(const Config& Cfg) {
    Socket_ = SocketSub_->CreateSocket(NAME_DGram, TEXT("UdpSocket"), false);
    if (!Socket_) {
        UE_LOG(LogTemp, Error, TEXT("UdpSocket: Failed to create socket"));
        return false;
    }
    Socket_->SetReuseAddr(true);
    Socket_->SetRecvErr(true);
    Socket_->SetNonBlocking(true);

    if (!Cfg.RemoteIP.IsEmpty() && Cfg.SendPort > 0) {
        RemoteAddr_ = SocketSub_->CreateInternetAddr();
        bool bValid = false;
        RemoteAddr_->SetIp(*Cfg.RemoteIP, bValid);
        RemoteAddr_->SetPort(Cfg.SendPort);
        if (!bValid) {
            UE_LOG(LogTemp, Error, TEXT("UdpSocket: Invalid IP: %s"), *Cfg.RemoteIP);
            Close();
            return false;
        }
    }

    if (Cfg.ReceivePort > 0) {
        TSharedPtr<FInternetAddr> BindAddr = SocketSub_->CreateInternetAddr();
        BindAddr->SetAnyAddress();
        BindAddr->SetPort(Cfg.ReceivePort);
        if (!Socket_->Bind(*BindAddr)) {
            UE_LOG(LogTemp, Error, TEXT("UdpSocket: Failed to bind port %d"), Cfg.ReceivePort);
            Close();
            return false;
        }
        bStop_ = false;
        RecvThread_ = FRunnableThread::Create(this, TEXT("UdpSocketRecv"), 0, TPri_Normal);
    }

    UE_LOG(LogTemp, Log, TEXT("UdpSocket: opened -> %s:%d, recv :%d"),
        *Cfg.RemoteIP, Cfg.SendPort, Cfg.ReceivePort);
    return true;
}

void UdpSocket::Close() {
    if (RecvThread_) {
        bStop_ = true;
        RecvThread_->WaitForCompletion();
        delete RecvThread_;
        RecvThread_ = nullptr;
    }
    if (Socket_) {
        Socket_->Close();
        SocketSub_->DestroySocket(Socket_);
        Socket_ = nullptr;
    }
}

bool UdpSocket::Send(const void* Data, int32 Size) {
    if (!Socket_ || !RemoteAddr_.IsValid()) return false;
    int32 Sent = 0;
    return Socket_->SendTo(static_cast<const uint8*>(Data), Size, Sent, *RemoteAddr_);
}

bool UdpSocket::IsAlive(double TimeoutSec) const {
    return (FPlatformTime::Seconds() - LastRecvTime_) < TimeoutSec;
}

double UdpSocket::SecondsSinceLastRecv() const {
    return FPlatformTime::Seconds() - LastRecvTime_;
}

uint32 UdpSocket::Run() {
    TArray<uint8> Buffer;
    Buffer.SetNumUninitialized(BUFFER_SIZE);
    TSharedRef<FInternetAddr> Sender = SocketSub_->CreateInternetAddr();

    while (!bStop_) {
        uint32 Pending = 0;
        while (Socket_->HasPendingData(Pending)) {
            int32 BytesRead = 0;
            if (Socket_->RecvFrom(Buffer.GetData(), BUFFER_SIZE, BytesRead, *Sender) && BytesRead > 0) {
                LastRecvTime_ = FPlatformTime::Seconds();
                if (OnDataReceived.IsBound()) {
                    OnDataReceived.Execute(Buffer.GetData(), BytesRead);
                }
            }
        }
        FPlatformProcess::Sleep(0.0002f);
    }
    return 0;
}
