#pragma once

#include "CoreMinimal.h"
#include "GStreamerStats.generated.h"

USTRUCT(BlueprintType)
struct GSTREAMERPLUGIN_API FGStreamerStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int32 CurrentFPS = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float PacketLossPercent = 0.0f;      // pre-FEC loss %

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float PostFecLossPercent = 0.0f;     // unrecoverable loss %

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float OneWayLatencyMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float JitterMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float FrameIntervalVarianceMs = 0.0f; // std dev of inter-frame intervals (ms)

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 PacketsReceived = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 PacketsLost = 0;               // pre-FEC

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 PacketsRecovered = 0;          // saved by FEC

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 PacketsLostPostFec = 0;        // unrecoverable

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 FramesDecoded = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    bool bIsReceiving = false;
};
