#include "UI/VoiceAnnotatorComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

UVoiceAnnotatorComponent::UVoiceAnnotatorComponent() {
    PrimaryComponentTick.bCanEverTick = true;
}

void UVoiceAnnotatorComponent::BeginPlay() {
    Super::BeginPlay();

    Socket_ = MakeUnique<UdpSocket>();
    Socket_->OnDataReceived.BindLambda([this](const uint8* Data, int32 Size) {
        HandleReceive(Data, Size);
    });

    UdpSocket::Config Cfg;
    Cfg.ReceivePort = VoicePort;
    if (!Socket_->Open(Cfg)) {
        UE_LOG(LogTemp, Error, TEXT("VoiceAnnotator: failed to open recv port %d"), VoicePort);
    }

    if (bAutoLaunch && !VoiceScriptPath.IsEmpty()) {
        FString ScriptPath = VoiceScriptPath;
        if (FPaths::IsRelative(ScriptPath))
            ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / ScriptPath);

        // Check both halves before launching. CreateProc fails identically for
        // a missing interpreter and a missing script, and the difference is the
        // whole diagnosis -- a deleted conda env looks exactly like a moved
        // script in the old log line.
        if (!FPaths::FileExists(ScriptPath)) {
            UE_LOG(LogTemp, Warning,
                TEXT("VoiceAnnotator: script not found at %s - voice commands disabled"),
                *ScriptPath);
        }
        else if (!FPaths::FileExists(PythonExe)) {
            UE_LOG(LogTemp, Warning,
                TEXT("VoiceAnnotator: interpreter not found at %s - voice commands disabled. ")
                TEXT("See ThirdParty/voice_annotator/README.md to recreate the env."),
                *PythonExe);
        }
        else {
            FString Args = FString::Printf(TEXT("\"%s\" --host 127.0.0.1 --port %d"), *ScriptPath, VoicePort);
            if (!AudioDevice.IsEmpty())
                Args += FString::Printf(TEXT(" --device-index %s"), *AudioDevice);

            uint32 ProcId = 0;
            ProcHandle_ = FPlatformProcess::CreateProc(
                *PythonExe, *Args,
                /*bLaunchDetached=*/false, /*bLaunchHidden=*/false, /*bLaunchReallyHidden=*/false,
                &ProcId, 0, nullptr, nullptr);

            if (ProcHandle_.IsValid()) {
                UE_LOG(LogTemp, Log, TEXT("VoiceAnnotator: launched sidecar pid=%u  %s"), ProcId, *ScriptPath);
            } else {
                UE_LOG(LogTemp, Warning, TEXT("VoiceAnnotator: failed to launch %s %s"), *PythonExe, *ScriptPath);
            }
        }
    }
}

void UVoiceAnnotatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (Socket_) Socket_->Close();

    if (ProcHandle_.IsValid()) {
        FPlatformProcess::TerminateProc(ProcHandle_);
        FPlatformProcess::CloseProc(ProcHandle_);
    }

    Super::EndPlay(EndPlayReason);
}

void UVoiceAnnotatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    TArray<FVoiceAnnotation> Local;
    {
        FScopeLock Lock(&QueueLock_);
        Local = MoveTemp(PendingAnnotations_);
    }
    for (const FVoiceAnnotation& Ann : Local) {
        OnAnnotationReceived.Broadcast(Ann);
    }
}

void UVoiceAnnotatorComponent::HandleReceive(const uint8* Data, int32 Size) {
    TArray<uint8> Buf(Data, Size);
    Buf.Add(0);
    FString JsonStr = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buf.GetData()));

    UE_LOG(LogTemp, Log, TEXT("VoiceAnnotator: UDP recv %d bytes: %s"), Size, *JsonStr);

    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid()) {
        UE_LOG(LogTemp, Warning, TEXT("VoiceAnnotator: JSON parse failed"));
        return;
    }

    FVoiceAnnotation Ann;
    double Val = 0.0;
    if (JsonObj->TryGetNumberField(TEXT("timestamp"), Val))  Ann.Timestamp  = Val;
    JsonObj->TryGetStringField(TEXT("label"), Ann.Label);
    Val = 0.0;
    if (JsonObj->TryGetNumberField(TEXT("type"), Val))       Ann.Type       = static_cast<uint8>(Val);
    Val = 0.0;
    if (JsonObj->TryGetNumberField(TEXT("confidence"), Val)) Ann.Confidence = static_cast<float>(Val);
    Val = 0.0;
    if (JsonObj->TryGetNumberField(TEXT("score"), Val))      Ann.Score      = static_cast<float>(Val);

    UE_LOG(LogTemp, Log, TEXT("VoiceAnnotator: queuing annotation label=%s type=%d conf=%.2f"),
        *Ann.Label, Ann.Type, Ann.Confidence);

    FScopeLock Lock(&QueueLock_);
    PendingAnnotations_.Add(Ann);
}
