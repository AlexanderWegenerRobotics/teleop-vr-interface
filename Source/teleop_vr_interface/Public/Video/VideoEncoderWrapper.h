#pragma once

#include "CoreMinimal.h"

struct FEncoderHandle;

FEncoderHandle* VideoEncoder_Create(int32 Width, int32 Height, int32 FPS,
                                     const FString& OutputPath);
void            VideoEncoder_SubmitYUV420P(FEncoderHandle* Handle,
                                           const uint8* Y, const uint8* U, const uint8* V,
                                           int32 Width, int32 Height);
void            VideoEncoder_Destroy(FEncoderHandle* Handle);
