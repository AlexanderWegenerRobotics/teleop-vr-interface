#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TeleOpTypes.h"
#include "HAL/PlatformTime.h"
#include <msgpack.hpp>
#include "udpClient.h"
#include "ComLink.generated.h"


// ============================================================================
// Delegates for incoming messages — components bind to these
// ============================================================================
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRobotStateReceived, const FWireRobotState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPanTiltStateReceived, const FWirePanTiltState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSystemStatusReceived, const FWireSystemStatus&);

/**
 * ComLink
 *
 * Communication bridge between the VR operator interface and the remote system
 * (simulator or real hardware). Wraps UDP transport with typed message API.
 *
 * Usage:
 *   - Call SendHeadPose / SendHandPose / SendModeCommand to transmit
 *   - Bind to OnRobotStateReceived / OnPanTiltStateReceived to get incoming data
 *   - Connection health is monitored automatically
 *
 * The component is transport-agnostic from the caller's perspective.
 * Internally it uses UDP for high-frequency streams.
 * TCP for reliable commands will be added later.
 */
UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UComLink : public UActorComponent
{
	GENERATED_BODY()

public:
	UComLink();
	virtual ~UComLink();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Configuration ---
	UPROPERTY(EditAnywhere, Category = "ComLink")
	FString RemoteIP = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, Category = "ComLink")
	int32 SendPort = 5010;

	UPROPERTY(EditAnywhere, Category = "ComLink")
	int32 ReceivePort = 6010;

	// --- Send API (typed, clean — caller never touches serialization) ---

	/** Send head pose. Converts from Unreal coords to protocol coords internally. */
	void SendHeadPose(const FTrackedPose& Pose);

	/** Send hand pose with trigger value. Specify left or right. */
	void SendHandPose(const FTrackedPose& Pose, float TriggerValue, bool bIsLeft);

	/** Send a mode transition command. */
	void SendModeCommand(EOpMode Mode);

	// --- Receive delegates (other components bind to these) ---
	FOnRobotStateReceived OnRobotStateReceived;
	FOnPanTiltStateReceived OnPanTiltStateReceived;
	FOnSystemStatusReceived OnSystemStatusReceived;

	// --- Connection health ---
	bool IsConnected() const;
	float GetLatencyMs() const { return LastLatencyMs; }
	double GetLastReceiveTime() const { return LastReceiveTime; }

private:

	void ProcessIncoming();
	void RouteMessage(const char* Data, int32 Size);

	uint32 NextSequence();

	TUniquePtr<udpClient> Socket;

	uint32 SequenceCounter = 0;
	double LastReceiveTime = 0.0;
	float LastLatencyMs = 0.0f;
	float ConnectionTimeout = 1.5f;
};