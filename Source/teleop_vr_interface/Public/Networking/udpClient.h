#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "HAL/PlatformTime.h"


class udpClient : public FRunnable {
public:
	udpClient();
	udpClient(FString ip_address, bool send_enabled, int32 sPort, bool receive_enabled, int32 rPort);
	~udpClient();

	void stop();
	bool send_raw(const uint8* Data, int32 Size);


	bool has_new_data() const { return bNewData; }
	TArray<uint8> get_raw_data();
	bool isConnectionAlive() const;
	int64_t get_sender_time();

private:
	virtual uint32 Run() override;

	ISocketSubsystem* SocketSubsystem = nullptr;
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> RemoteAddr;
	TSharedPtr<FInternetAddr> LocalAddr;
	FRunnableThread* ReceiverThread = nullptr;

	bool bWriteIsEnabled = false;
	bool bReceiveIsEnabled = false;
	FString ip;
	int32 sendPort = 0;
	int32 receivePort = 0;
	int32 BufferSize;

	// Receive state
	TArray<uint8> ReceiveBuffer;
	TArray<uint8> LatestPacket;
	FCriticalSection MsgLock;
	TAtomic<bool> bNewData;
	TAtomic<bool> bStop{ false };
	double last_recv_time = 0.0;
	int64_t sender_time = 0;
};