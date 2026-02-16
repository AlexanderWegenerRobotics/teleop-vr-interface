#include "udpClient.h"
#include "HAL/PlatformTime.h"

udpClient::udpClient()
	: Socket(nullptr)
	, BufferSize(4096)
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bNewData = false;
	sender_time = 0;
}

udpClient::udpClient(FString ip_address, bool send_enabled, int32 sPort, bool receive_enabled, int32 rPort)
	: Socket(nullptr)
	, BufferSize(4096)
	, bWriteIsEnabled(send_enabled)
	, bReceiveIsEnabled(receive_enabled)
	, ip(ip_address)
	, sendPort(sPort)
	, receivePort(rPort)
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("UdpClientSocket"), false);
	if (!Socket)
	{
		UE_LOG(LogTemp, Error, TEXT("udpClient: Failed to create UDP socket."));
		return;
	}
	Socket->SetReuseAddr(true);
	Socket->SetRecvErr(true);
	Socket->SetNonBlocking(true);

	// Initialize remote address for sending
	if (bWriteIsEnabled)
	{
		RemoteAddr = SocketSubsystem->CreateInternetAddr();
		bool bIsValidIp = false;
		RemoteAddr->SetIp(*ip_address, bIsValidIp);
		RemoteAddr->SetPort(sPort);

		if (!bIsValidIp)
		{
			UE_LOG(LogTemp, Error, TEXT("udpClient: Invalid IP address: %s"), *ip_address);
			stop();
			return;
		}
	}

	if (bReceiveIsEnabled)
	{
		LocalAddr = SocketSubsystem->CreateInternetAddr();
		LocalAddr->SetAnyAddress();
		LocalAddr->SetPort(rPort);

		if (!Socket->Bind(*LocalAddr))
		{
			UE_LOG(LogTemp, Error, TEXT("udpClient: Failed to bind socket to port %d"), rPort);
			stop();
			return;
		}
		ReceiveBuffer.SetNumUninitialized(BufferSize);
		ReceiverThread = FRunnableThread::Create(this, TEXT("UDPRecvThread"), 0, TPri_Normal);

		if (bWriteIsEnabled)
		{
			UE_LOG(LogTemp, Log, TEXT("udpClient: send to %s:%d, receive on :%d"), *ip_address, sPort, rPort);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("udpClient: receive-only on :%d"), rPort);
		}
	}
	bNewData = false;
}

udpClient::~udpClient()
{
	stop();
}

void udpClient::stop()
{
	if (ReceiverThread)
	{
		bStop = true;
		ReceiverThread->WaitForCompletion();
		delete ReceiverThread;
		ReceiverThread = nullptr;
	}

	if (Socket)
	{
		Socket->Close();
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;
	}
	UE_LOG(LogTemp, Log, TEXT("udpClient: Stopped."));
}

// ============================================================================
// Receive thread — stores latest raw packet
// ============================================================================

uint32 udpClient::Run()
{
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(BufferSize);

	while (!bStop)
	{
		int32 BytesRead = 0;
		uint32 Pending = 0;

		while (Socket->HasPendingData(Pending))
		{
			if (Socket->RecvFrom(Buffer.GetData(), BufferSize, BytesRead, *LocalAddr) && BytesRead > 0)
			{
				FScopeLock Lock(&MsgLock);
				LatestPacket.SetNumUninitialized(BytesRead);
				FMemory::Memcpy(LatestPacket.GetData(), Buffer.GetData(), BytesRead);
				bNewData = true;
				last_recv_time = FPlatformTime::Seconds();
			}
		}
		FPlatformProcess::Sleep(0.0005f);	// 0.5ms poll interval
	}
	return 0;
}

// ============================================================================
// Send — raw bytes
// ============================================================================

bool udpClient::send_raw(const uint8* Data, int32 Size)
{
	if (!bWriteIsEnabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("udpClient: Attempting to send on read-only socket"));
		return false;
	}
	if (!Socket || !RemoteAddr.IsValid()) return false;

	int32 BytesSent = 0;
	bool bSuccess = Socket->SendTo(Data, Size, BytesSent, *RemoteAddr);

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("udpClient: Send failed on port %d"), sendPort);
	}
	return bSuccess;
}

// ============================================================================
// Receive — raw bytes
// ============================================================================

TArray<uint8> udpClient::get_raw_data()
{
	FScopeLock Lock(&MsgLock);
	bNewData = false;
	return MoveTemp(LatestPacket);
}

bool udpClient::isConnectionAlive() const
{
	return (FPlatformTime::Seconds() - last_recv_time) < 1.5;
}

int64_t udpClient::get_sender_time()
{
	FScopeLock Lock(&MsgLock);
	return sender_time;
}