// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ViveOpenXRPassthrough.h"
#include "PassthroughHandle.generated.h"

USTRUCT(BlueprintType)
struct FPassthroughHandle
{
	GENERATED_BODY()

public:

	// Constructor initializing values to safe defaults
	FPassthroughHandle()
		: Handle(XR_NULL_HANDLE)  // Set to null handle initially
		, Valid(false)  // Handle is not valid by default
	{}

	// OpenXR handle for passthrough
	XrPassthroughHTC Handle;

	// Validity flag, exposed to Blueprints
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ViveOpenXR|Passthrough")
	bool Valid;
};
