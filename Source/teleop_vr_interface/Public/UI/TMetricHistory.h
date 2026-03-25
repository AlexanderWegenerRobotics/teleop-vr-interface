#pragma once

#include "CoreMinimal.h"

template<int32 N = 128>
struct TMetricHistory
{
    static_assert(N > 1, "TMetricHistory requires at least 2 samples");

    float RangeMin = 0.f;
    float RangeMax = 100.f;
    float ThresholdLo = 0.f;
    float ThresholdHi = 80.f;

    void Push(float Value, float EnvelopeValue = 0.f){
        Samples_[Head_]  = Value;
        Envelope_[Head_] = EnvelopeValue;
        Head_            = (Head_ + 1) % N;
        bHasData_        = true;
    }

    void GetOrdered(float* OutSamples, float* OutEnvelope = nullptr) const{
        for (int32 i = 0; i < N; ++i){
            const int32 Idx = (Head_ + i) % N;
            OutSamples[i]   = Samples_[Idx];
            if (OutEnvelope)
                OutEnvelope[i] = Envelope_[Idx];
        }
    }

    float Latest() const{
        const int32 Idx = (Head_ - 1 + N) % N;
        return Samples_[Idx];
    }

    float LatestEnvelope() const{
        const int32 Idx = (Head_ - 1 + N) % N;
        return Envelope_[Idx];
    }

    bool HasData()    const { return bHasData_; }
    int32 Capacity()  const { return N; }

    const float* GetSamplesPtr()  const { return Samples_; }
    const float* GetEnvelopePtr() const { return Envelope_; }
    const int32* GetHeadPtr()     const { return &Head_; }

    float Normalize(float Value) const{
        const float Range = RangeMax - RangeMin;
        if (FMath::IsNearlyZero(Range)) return 0.f;
        return FMath::Clamp((Value - RangeMin) / Range, 0.f, 1.f);
    }

private:
    float  Samples_[N]  = {};
    float  Envelope_[N] = {};
    int32  Head_        = 0;
    bool   bHasData_    = false;
};
