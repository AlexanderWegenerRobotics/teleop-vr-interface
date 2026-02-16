#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

/**
 * Video source health / statistics.
 * Backends populate what they can, leave defaults for unsupported fields.
 */
struct FVideoSourceStats
{
	int32 CurrentFPS = 0;
	float PacketLossPercent = 0.0f;
	float LatencyMs = 0.0f;
	float JitterMs = 0.0f;
	float RoundTripMs = 0.0f;
	bool bIsReceiving = false;
};

/**
 * IVideoSource
 *
 * Abstract interface for a video streaming backend.
 * Any source that provides video frames (GStreamer, NDI, simulation render, etc.)
 * implements this interface.
 *
 * The VideoFeedComponent consumes this interface without knowing what backend
 * provides the frames.
 */
class IVideoSource
{
public:
	virtual ~IVideoSource() = default;

	/** Initialize the source with configuration. Returns true on success. */
	virtual bool Initialize() = 0;

	/** Start receiving / producing frames. */
	virtual bool Start() = 0;

	/** Stop receiving / producing frames. */
	virtual void Stop() = 0;

	/**
	 * Update the provided texture with the latest frame.
	 * Returns true if a new frame was written.
	 */
	virtual bool UpdateTexture(UTexture2D* Texture) = 0;

	/** Get current frame dimensions. Returns false if no frame received yet. */
	virtual bool GetDimensions(int32& OutWidth, int32& OutHeight) const = 0;

	/** Get streaming statistics for health monitoring. */
	virtual FVideoSourceStats GetStats() const = 0;

	/** Human-readable name for logging / UI display. */
	virtual FString GetSourceName() const = 0;
};
