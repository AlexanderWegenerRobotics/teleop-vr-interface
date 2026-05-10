#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "IVideoSource.generated.h"

USTRUCT(BlueprintType)
struct TELEOP_VR_INTERFACE_API FVideoSourceStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int32 CurrentFPS = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    float PacketLossPercent = 0.0f;      // pre-FEC loss %

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    float PostFecLossPercent = 0.0f;     // unrecoverable loss %

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    float OneWayLatencyMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    float JitterMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    float FrameIntervalVarianceMs = 0.0f; // std dev of inter-frame intervals (ms)

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int64 PacketsReceived = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int64 PacketsLost = 0;               // pre-FEC

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int64 PacketsRecovered = 0;          // saved by FEC

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int64 PacketsLostPostFec = 0;        // unrecoverable

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    int64 FramesDecoded = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    bool bIsReceiving = false;
    UPROPERTY(BlueprintReadOnly, Category = "Video Stats")
    uint8 StreamHealthState = 0;
};

class IVideoSource
{
public:
    virtual ~IVideoSource() = default;

    virtual bool Initialize() = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;

    virtual bool UpdateTexture(UTexture2D*& OutTexture) = 0;
    virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const = 0;
    virtual FVideoSourceStats GetStats() const = 0;
    virtual FString GetSourceName() const = 0;
    virtual int64  GetLastSenderTimeNs() const { return 0; }
    virtual uint64 GetLastFrameId()      const { return 0; }
};
