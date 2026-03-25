#pragma once

#include "CoreMinimal.h"
#include "UI/GazeComponent.h"
#include "UI/WidgetBinder.h"
#include "TrayController.generated.h"

USTRUCT()
struct FTrayController {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Geometry")
    float CollapsedWidth = 24.0f;

    UPROPERTY(EditAnywhere, Category = "Geometry")
    float ExpandedWidth = 220.0f;

    UPROPERTY(EditAnywhere, Category = "Geometry")
    float VerticalOffset = 320.0f;

    UPROPERTY(EditAnywhere, Category = "Geometry")
    float LayerDistance = 690.0f;

    UPROPERTY(EditAnywhere, Category = "Behaviour")
    float GazeEngageThreshold = 0.87f;

    UPROPERTY(EditAnywhere, Category = "Behaviour")
    float GazeReleaseThreshold = 0.78f;

    UPROPERTY(EditAnywhere, Category = "Behaviour")
    float ExpandRate = 1.8f;

    UPROPERTY(EditAnywhere, Category = "Behaviour")
    float CollapseRate = 0.7f;

    UPROPERTY(EditAnywhere, Category = "Appearance")
    float CollapsedOpacity = 0.08f;

    void Update(float DeltaTime, const FGazeData& GazeData, UWidgetBinder* Binder) {
        if (!Binder) return;

        bool bGazeInRegion;
        if (bEngaged_) {
            bGazeInRegion = GazeData.bIsValid && (GazeData.Direction.Y >= GazeReleaseThreshold);
        }
        else {
            bGazeInRegion = GazeData.bIsValid && (GazeData.Direction.Y >= GazeEngageThreshold);
        }
        bEngaged_ = bGazeInRegion;

        if (bGazeInRegion) {
            DwellAccumulator_ = FMath::Min(DwellAccumulator_ + DeltaTime * ExpandRate, 1.0f);
        }
        else {
            DwellAccumulator_ = FMath::Max(DwellAccumulator_ - DeltaTime * CollapseRate, 0.0f);
        }

        float NewExpansion = FMath::SmoothStep(0.0f, 1.0f, DwellAccumulator_);
        if (FMath::IsNearlyEqual(NewExpansion, Expansion_, 0.001f)) return;

        Expansion_ = NewExpansion;
        Binder->SetExpansion(Expansion_);
        Binder->SetLayerOpacity(FMath::Lerp(CollapsedOpacity, 1.0f, Expansion_));
    }

    float GetExpansion() const { return Expansion_; }
    bool IsExpanded() const { return Expansion_ >= 1.0f; }

private:
    float DwellAccumulator_ = 0.0f;
    float Expansion_ = 0.0f;
    bool bEngaged_ = false;
};