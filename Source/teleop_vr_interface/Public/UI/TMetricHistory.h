#pragma once

#include "CoreMinimal.h"

/**
 * TMetricHistory
 *
 * Generic ring buffer for time-series metric visualization.
 * Holds N samples of a primary value and an optional envelope channel
 * (e.g. jitter around latency, variance around torque).
 *
 * Completely decoupled from any data source — the owning panel is
 * responsible for calling Push() each tick with whatever values it holds.
 *
 * Reusable for any scalar metric: latency, loss, torque, force, velocity, etc.
 */
template<int32 N = 128>
struct TMetricHistory
{
    static_assert(N > 1, "TMetricHistory requires at least 2 samples");

    // -----------------------------------------------------------------------
    // Configuration — set once before first use
    // -----------------------------------------------------------------------

    /** Fixed Y axis minimum. Never auto-scales — operator must re-learn scale otherwise. */
    float RangeMin = 0.f;

    /** Fixed Y axis maximum. */
    float RangeMax = 100.f;

    /** Lower bound of the acceptable operating band (drawn as background tint). */
    float ThresholdLo = 0.f;

    /** Upper bound of the acceptable operating band. */
    float ThresholdHi = 80.f;

    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    /** Push a new sample. EnvelopeValue is optional (pass 0 if unused). */
    void Push(float Value, float EnvelopeValue = 0.f)
    {
        Samples_[Head_]  = Value;
        Envelope_[Head_] = EnvelopeValue;
        Head_            = (Head_ + 1) % N;
        bHasData_        = true;
    }

    /** Fill OutSamples[N] and optionally OutEnvelope[N] in chronological order. */
    void GetOrdered(float* OutSamples, float* OutEnvelope = nullptr) const
    {
        for (int32 i = 0; i < N; ++i)
        {
            const int32 Idx = (Head_ + i) % N;
            OutSamples[i]   = Samples_[Idx];
            if (OutEnvelope)
                OutEnvelope[i] = Envelope_[Idx];
        }
    }

    /** Most recently pushed primary value. */
    float Latest() const
    {
        const int32 Idx = (Head_ - 1 + N) % N;
        return Samples_[Idx];
    }

    /** Most recently pushed envelope value. */
    float LatestEnvelope() const
    {
        const int32 Idx = (Head_ - 1 + N) % N;
        return Envelope_[Idx];
    }

    bool HasData()    const { return bHasData_; }
    int32 Capacity()  const { return N; }

    // Raw pointer accessors for UTimeSeriesWidget::BindHistory().
    // The widget holds non-owning pointers — lifetime is owned by this struct.
    const float* GetSamplesPtr()  const { return Samples_; }
    const float* GetEnvelopePtr() const { return Envelope_; }
    const int32* GetHeadPtr()     const { return &Head_; }

    /** Normalize a value to [0, 1] using RangeMin/RangeMax. Clamped. */
    float Normalize(float Value) const
    {
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
