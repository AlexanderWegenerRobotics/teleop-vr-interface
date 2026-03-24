#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "SocketSubsystem.h"
#include "Networking.h"

DECLARE_DELEGATE_TwoParams(FOnDataReceived, const uint8*, int32);

class TELEOP_VR_INTERFACE_API UdpSocket : public FRunnable {
public:
    struct Config {
        FString RemoteIP;
        int32   SendPort    = 0;
        int32   ReceivePort = 0;
    };

    UdpSocket();
    ~UdpSocket();

    bool Open(const Config& Cfg);
    void Close();

    bool Send(const void* Data, int32 Size);
    bool IsAlive(double TimeoutSec = 1.5) const;
    double SecondsSinceLastRecv() const;

    FOnDataReceived OnDataReceived;

private:
    virtual uint32 Run() override;

    ISocketSubsystem* SocketSub_ = nullptr;
    FSocket* Socket_ = nullptr;
    TSharedPtr<FInternetAddr> RemoteAddr_;
    FRunnableThread* RecvThread_ = nullptr;

    TAtomic<bool> bStop_{false};
    TAtomic<double> LastRecvTime_{0.0};

    static constexpr int32 BUFFER_SIZE = 65536;
};
