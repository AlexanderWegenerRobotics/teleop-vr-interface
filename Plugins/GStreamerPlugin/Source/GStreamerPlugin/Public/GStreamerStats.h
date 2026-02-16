#pragma once

#include "CoreMinimal.h"
#include "GStreamerStats.generated.h"

USTRUCT(BlueprintType)
struct GSTREAMERPLUGIN_API FGStreamerStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 FramesPushed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 FramesLost = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float AverageJitterMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 RetransmissionCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float PipelineLatencyMs = 0.0f;  // Renamed for clarity

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float JitterBufferLatencyMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    float PacketLossPercent = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int32 CurrentFPS = 0;
    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 SRTPacketsReceived = 0;
    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    int64 SRTPacketsLost = 0;
    UPROPERTY(BlueprintReadOnly, Category = "GStreamer Stats")
    double SRTRoundTripMs = 0.0;
};