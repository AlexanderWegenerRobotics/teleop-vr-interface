#pragma once

#include "CoreMinimal.h"

struct FWinkGesture {
	float BlinkThreshold = 0.15f;
	float CooldownDuration = 0.4f;

	bool Update(bool bRightEyeClosed, float DeltaTime) {
		if (CooldownRemaining > 0.0f) {
			CooldownRemaining -= DeltaTime;
			return false;
		}

		if (bRightEyeClosed) {
			BlinkAccumulator += DeltaTime;
			if (BlinkAccumulator >= BlinkThreshold && !bFired) {
				bFired = true;
				CooldownRemaining = CooldownDuration;
				return true;
			}
		}
		else {
			BlinkAccumulator = 0.0f;
			bFired = false;
		}
		return false;
	}

	bool IsBlinking() const { return BlinkAccumulator > 0.0f || bFired; }

private:
	float BlinkAccumulator = 0.0f;
	float CooldownRemaining = 0.0f;
	bool bFired = false;
};