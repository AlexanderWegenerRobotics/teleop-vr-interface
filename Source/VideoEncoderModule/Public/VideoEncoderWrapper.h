#pragma once

#include "CoreMinimal.h"

struct FEncoderHandle;

VIDEOENCODERMODULE_API FEncoderHandle* VideoEncoder_Create(int32 Width, int32 Height, int32 FPS,
                                     const FString& OutputPath);
VIDEOENCODERMODULE_API void            VideoEncoder_SubmitYUV420P(FEncoderHandle* Handle,
                                           const uint8* Y, const uint8* U, const uint8* V,
                                           int32 Width, int32 Height,
                                           uint32 FrameID, int64 TimestampUs);
VIDEOENCODERMODULE_API void            VideoEncoder_Destroy(FEncoderHandle* Handle);