#include "ComLink.h"
#include "HAL/PlatformTime.h"

UComLink::UComLink()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Tick early so incoming data is available for other components this frame
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UComLink::~UComLink() = default;

void UComLink::BeginPlay()
{
	Super::BeginPlay();

	// Create UDP socket — send and receive on the same socket
	Socket = MakeUnique<udpClient>(RemoteIP, true, SendPort, true, ReceivePort);

	if (Socket)
	{
		UE_LOG(LogTemp, Log, TEXT("ComLink: Initialized — send to %s:%d, receive on :%d"),
			*RemoteIP, SendPort, ReceivePort);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ComLink: Failed to create UDP socket"));
	}
}

void UComLink::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Socket)
	{
		Socket->stop();
		Socket.Reset();
	}

	UE_LOG(LogTemp, Log, TEXT("ComLink: Stopped"));
	Super::EndPlay(EndPlayReason);
}

void UComLink::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ProcessIncoming();
}

// ============================================================================
// Send API
// ============================================================================

void UComLink::SendHeadPose(const FTrackedPose& Pose)
{
	if (!Socket || !Pose.bIsValid) return;

	FWirePose Wire;
	Wire.Timestamp = static_cast<uint64>(FPlatformTime::Seconds() * 1e9);
	Wire.Sequence = NextSequence();
	Wire.Type = static_cast<uint8>(EMsgType::HeadPose);

	CoordConvert::UnrealToProtocol(Pose.Position, Wire.PX, Wire.PY, Wire.PZ);
	CoordConvert::UnrealToProtocolQuat(Pose.Orientation, Wire.QX, Wire.QY, Wire.QZ, Wire.QW);

	msgpack::sbuffer buf = Wire.Pack();
	Socket->send_raw(reinterpret_cast<const uint8*>(buf.data()), buf.size());
}

void UComLink::SendHandPose(const FTrackedPose& Pose, float TriggerValue, bool bIsLeft)
{
	if (!Socket || !Pose.bIsValid) return;

	FWirePose Wire;
	Wire.Timestamp = static_cast<uint64>(FPlatformTime::Seconds() * 1e9);
	Wire.Sequence = NextSequence();
	Wire.Type = static_cast<uint8>(bIsLeft ? EMsgType::HandLeft : EMsgType::HandRight);
	Wire.TriggerValue = static_cast<double>(TriggerValue);

	CoordConvert::UnrealToProtocol(Pose.Position, Wire.PX, Wire.PY, Wire.PZ);
	CoordConvert::UnrealToProtocolQuat(Pose.Orientation, Wire.QX, Wire.QY, Wire.QZ, Wire.QW);

	msgpack::sbuffer buf = Wire.Pack();
	Socket->send_raw(reinterpret_cast<const uint8*>(buf.data()), buf.size());
}

void UComLink::SendModeCommand(EOpMode Mode)
{
	if (!Socket) return;

	FWireModeCommand Wire;
	Wire.Timestamp = static_cast<uint64>(FPlatformTime::Seconds() * 1e9);
	Wire.Sequence = NextSequence();
	Wire.Mode = static_cast<uint8>(Mode);

	msgpack::sbuffer buf = Wire.Pack();
	Socket->send_raw(reinterpret_cast<const uint8*>(buf.data()), buf.size());
}

// ============================================================================
// Receive
// ============================================================================

void UComLink::ProcessIncoming()
{
	if (!Socket || !Socket->has_new_data()) return;

	TArray<uint8> RawData = Socket->get_raw_data();
	if (RawData.Num() > 0)
	{
		RouteMessage(reinterpret_cast<const char*>(RawData.GetData()), RawData.Num());
	}
}

void UComLink::RouteMessage(const char* Data, int32 Size)
{
	try
	{
		auto oh = msgpack::unpack(Data, Size);
		const msgpack::object& obj = oh.get();

		if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 3) return;

		uint8 MsgType = obj.via.array.ptr[2].as<uint8>();
		double Now = FPlatformTime::Seconds();

		switch (static_cast<EMsgType>(MsgType))
		{
		case EMsgType::RobotStateRight:
		case EMsgType::RobotStateLeft:
		{
			FWireRobotState State;
			if (State.Unpack(obj))
			{
				LastReceiveTime = Now;
				LastLatencyMs = static_cast<float>((Now * 1e9 - State.Timestamp) / 1e6);
				OnRobotStateReceived.Broadcast(State);
			}
			break;
		}
		case EMsgType::PanTiltState:
		{
			FWirePanTiltState State;
			if (State.Unpack(obj))
			{
				LastReceiveTime = Now;
				OnPanTiltStateReceived.Broadcast(State);
			}
			break;
		}
		case EMsgType::SystemStatus:
		{
			FWireSystemStatus Status;
			if (Status.Unpack(obj))
			{
				LastReceiveTime = Now;
				OnSystemStatusReceived.Broadcast(Status);
			}
			break;
		}
		default:
			UE_LOG(LogTemp, Warning, TEXT("ComLink: Unknown message type 0x%02X"), MsgType);
			break;
		}
	}
	catch (const std::exception& ex)
	{
		UE_LOG(LogTemp, Error, TEXT("ComLink: Unpack failed — %s"), UTF8_TO_TCHAR(ex.what()));
	}
}

bool UComLink::IsConnected() const
{
	return (FPlatformTime::Seconds() - LastReceiveTime) < ConnectionTimeout;
}

uint32 UComLink::NextSequence()
{
	return ++SequenceCounter;
}