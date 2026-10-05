#pragma once

#include "CoreMinimal.h"

// Right-eye wink detector: fires once when the eye has been reported closed
// for BlinkThreshold seconds, then waits CooldownDuration before it can fire
// again.
//
// DropoutTolerance: the tracker's per-eye "closed" flag is thresholded eye
// openness, and during a deliberate wink the lid sits near that threshold,
// so the flag flickers open for a frame or two. Previously a single "open"
// frame reset the accumulator, the 150 ms had to start over, and the wink
// usually ended before it got there -- a press that simply didn't happen.
// Open gaps shorter than DropoutTolerance are now bridged; only a sustained
// open (> DropoutTolerance) counts as the eye actually reopening.
// 0 restores the old reset-on-any-open-frame behaviour.
struct FWinkGesture {
	float BlinkThreshold   = 0.15f;
	float CooldownDuration = 0.4f;
	float DropoutTolerance = 0.06f;

	bool Update(bool bRightEyeClosed, float DeltaTime) {
		if (CooldownRemaining > 0.0f) {
			CooldownRemaining -= DeltaTime;
			return false;
		}

		if (bRightEyeClosed) {
			OpenGap = 0.0f;
			BlinkAccumulator += DeltaTime;
			if (BlinkAccumulator >= BlinkThreshold && !bFired) {
				bFired = true;
				CooldownRemaining = CooldownDuration;
				return true;
			}
		}
		else if (BlinkAccumulator > 0.0f || bFired) {
			// Bridge short "open" flickers inside a wink; the closed time
			// before the gap still counts.
			OpenGap += DeltaTime;
			if (OpenGap > DropoutTolerance) {
				BlinkAccumulator = 0.0f;
				OpenGap = 0.0f;
				bFired = false;
			}
		}
		return false;
	}

	bool IsBlinking() const { return BlinkAccumulator > 0.0f || bFired; }

private:
	float BlinkAccumulator = 0.0f;
	float OpenGap = 0.0f;
	float CooldownRemaining = 0.0f;
	bool bFired = false;
};
