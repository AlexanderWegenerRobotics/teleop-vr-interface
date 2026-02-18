#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IVideoSource.h"
#include "VideoFeedComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UCameraComponent;

UCLASS(ClassGroup = (TeleOp), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UVideoFeedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVideoFeedComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;


	/** Register a named video source. Component takes ownership. */
	void RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source);

	/** Switch to a registered source by name. Stops the previous, starts the new one. */
	bool SetActiveSource(const FString& Name);

	/** Get the name of the currently active source. */
	FString GetActiveSourceName() const;

	/** Get stats from the active source (for HUD display). */
	FVideoSourceStats GetActiveStats() const;

	/** Is any source actively receiving frames? */
	bool IsReceiving() const;

	// --- Configuration ---

	/** Distance from camera to display plane in cm. */
	UPROPERTY(EditAnywhere, Category = "VideoFeed")
	float PlaneDistance = 500.0f;

	/** Horizontal FOV coverage (0.0 to 1.0, where 1.0 = full HMD FOV). */
	UPROPERTY(EditAnywhere, Category = "VideoFeed", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float FOVCoverage = 0.85f;

private:

	void CreateDisplayPlane();
	void UpdatePlaneScale(int32 Width, int32 Height);
	void EnsureTextureSize(int32 Width, int32 Height);

	// Display
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DisplayPlane;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY()
	TObjectPtr<UTexture2D> VideoTexture;

	// Source management
	TMap<FString, TUniquePtr<IVideoSource>> Sources;
	FString ActiveSourceName;
	IVideoSource* ActiveSource = nullptr;

	// Texture state
	int32 CurrentTextureWidth = 0;
	int32 CurrentTextureHeight = 0;

	// Camera reference (resolved in BeginPlay)
	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraRef;
};
