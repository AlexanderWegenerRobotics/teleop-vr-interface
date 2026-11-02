#pragma once

#include "CoreMinimal.h"

// Right-eye wink detector: fires once the eye has been reported closed for
// BlinkThreshold seconds, then waits CooldownDuration. Open gaps shorter than
// DropoutTolerance are bridged, since the closed flag flickers while the lid sits
// near the threshold. 0 = reset on any open frame.
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
