#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Networking/UdpSocket.h"
#include "VoiceAnnotatorComponent.generated.h"

USTRUCT(BlueprintType)
struct FVoiceAnnotation {
    GENERATED_BODY()
    UPROPERTY() double  Timestamp  = 0.0;
    UPROPERTY() FString Label;
    UPROPERTY() uint8   Type       = 0;
    UPROPERTY() float   Confidence = 0.0f;
    UPROPERTY() float   Score      = 0.0f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnVoiceAnnotation, const FVoiceAnnotation&)

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TELEOP_VR_INTERFACE_API UVoiceAnnotatorComponent : public UActorComponent {
    GENERATED_BODY()

public:
    UVoiceAnnotatorComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, Category = "VoiceAnnotator")
    int32 VoicePort = 7778;

    UPROPERTY(EditAnywhere, Category = "VoiceAnnotator")
    FString PythonExe = TEXT("C:\\Users\\ceti\\miniconda3\\envs\\voice-control\\python.exe");

    // Relative to the project directory: the repo in the editor, and
    // <Package>/teleop_vr_interface/ in a packaged build, where the folder is
    // staged by DirectoriesToAlwaysStageAsNonUFS. An absolute path set on the
    // pawn is still honoured.
    //
    // This used to be an absolute path into the repo, so a packaged build ran
    // whatever happened to be in the working tree -- editing the script changed
    // the behaviour of a build that was supposed to be frozen, and the package
    // could not be moved to another machine at all.
    UPROPERTY(EditAnywhere, Category = "VoiceAnnotator")
    FString VoiceScriptPath = TEXT("ThirdParty/voice_annotator/voice_annotator.py");

    UPROPERTY(EditAnywhere, Category = "VoiceAnnotator")
    FString AudioDevice;

    UPROPERTY(EditAnywhere, Category = "VoiceAnnotator")
    bool bAutoLaunch = true;

    FOnVoiceAnnotation OnAnnotationReceived;

private:
    void HandleReceive(const uint8* Data, int32 Size);

    TUniquePtr<UdpSocket> Socket_;
    FProcHandle           ProcHandle_;

    FCriticalSection         QueueLock_;
    TArray<FVoiceAnnotation> PendingAnnotations_;
};
