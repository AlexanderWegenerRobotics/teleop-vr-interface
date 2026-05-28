#include "Teleop/TeleOpConfig.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString UTeleOpConfig::DefaultConfigPath() {
    return FPaths::ProjectDir() / TEXT("Config/TeleOp/config.json");
}

bool UTeleOpConfig::Load(const FString& RootConfigPath) {
    UE_LOG(LogTemp, Log, TEXT("TeleOpConfig: Loading from %s"), *RootConfigPath);

    TSharedPtr<FJsonObject> Root;
    if (!ReadJsonFile(RootConfigPath, Root)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � root config not found or invalid JSON: %s"), *RootConfigPath);
        return false;
    }

    const FString BaseDir = FPaths::GetPath(RootConfigPath);

    FString NetworkPath, StreamPath, HudPath;
    if (!RequireString(Root, TEXT("network"), NetworkPath, TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("stream"), StreamPath, TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("hud"), HudPath, TEXT("config.json"))) return false;

    if (!LoadNetwork(BaseDir / NetworkPath)) return false;
    if (!LoadStream(BaseDir / StreamPath))   return false;
    if (!LoadHud(BaseDir / HudPath))         return false;

    UE_LOG(LogTemp, Log, TEXT("TeleOpConfig: Loaded successfully"));
    UE_LOG(LogTemp, Log, TEXT("  Network  � remote: %s  avatar: %d/%d"), *Network.RemoteIP, Network.Avatar.Send, Network.Avatar.Receive);
    UE_LOG(LogTemp, Log, TEXT("  Stream(right)  stereo: %s  port: %d  feedback: %d  status: %d"),
        Stream.bStereo ? TEXT("true") : TEXT("false"),
        Stream.RightPort, Stream.RightFeedbackPort, Stream.RightStatusPort);
    UE_LOG(LogTemp, Log, TEXT("  Stream� %s:%d  feedback: %d  timestamp: %d  status: %d"), *Stream.RemoteIP, Stream.Port, Stream.FeedbackPort, Stream.TimestampPort, Stream.StatusPort);
    UE_LOG(LogTemp, Log, TEXT("  HUD      � latency: %.0fms  loss: %.3f%%  fps: %.0f"), Hud.LatencyWarningMs, Hud.LossWarningPercent, Hud.FpsWarningFloor);

    return true;
}

bool UTeleOpConfig::LoadNetwork(const FString& Path) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj))
    {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � cannot read network config: %s"), *Path);
        return false;
    }

    const FString Context = TEXT("network.json");
    if (!RequireString(Obj, TEXT("remote_ip"), Network.RemoteIP, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("avatar"), Network.Avatar, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("arm_left"), Network.ArmLeft, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("arm_right"), Network.ArmRight, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("head"), Network.Head, Context)) return false;

    return true;
}

bool UTeleOpConfig::LoadStream(const FString& Path) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj))
    {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � cannot read stream config: %s"), *Path);
        return false;
    }

    const FString Context = TEXT("stream.json");
    if (!RequireString(Obj, TEXT("remote_ip"), Stream.RemoteIP, Context)) return false;
    if (!RequireInt(Obj, TEXT("port"), Stream.Port, Context)) return false;
    if (!RequireInt(Obj, TEXT("feedback_port"), Stream.FeedbackPort, Context)) return false;
    if (!RequireInt(Obj, TEXT("timestamp_port"), Stream.TimestampPort, Context)) return false;
    if (!RequireInt(Obj, TEXT("report_interval_ms"), Stream.ReportIntervalMs, Context)) return false;
    if (!RequireInt(Obj, TEXT("status_port"), Stream.StatusPort, Context)) return false;

    Obj->TryGetBoolField(TEXT("stereo"), Stream.bStereo);
    Obj->TryGetNumberField(TEXT("right_port"),          Stream.RightPort);
    Obj->TryGetNumberField(TEXT("right_feedback_port"), Stream.RightFeedbackPort);
    Obj->TryGetNumberField(TEXT("right_status_port"),   Stream.RightStatusPort);

    return true;
}

bool UTeleOpConfig::LoadHud(const FString& Path) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj))
    {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � cannot read hud config: %s"), *Path);
        return false;
    }

    const FString Context = TEXT("hud.json");
    if (!RequireFloat(Obj, TEXT("latency_warning_ms"), Hud.LatencyWarningMs, Context)) return false;
    if (!RequireFloat(Obj, TEXT("loss_warning_percent"), Hud.LossWarningPercent, Context)) return false;
    if (!RequireFloat(Obj, TEXT("fps_warning_floor"), Hud.FpsWarningFloor, Context)) return false;
    if (!RequireFloat(Obj, TEXT("metric_entry_window_s"), Hud.MetricEntryWindowSec, Context)) return false;
    if (!RequireFloat(Obj, TEXT("metric_exit_window_s"), Hud.MetricExitWindowSec, Context)) return false;
    if (!RequireFloat(Obj, TEXT("confirmation_duration_s"), Hud.ConfirmationDurationSec, Context)) return false;

    return true;
}

bool UTeleOpConfig::ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject) {
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *Path)) {
        return false;
    }

    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool UTeleOpConfig::RequireString(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FString& OutValue, const FString& Context) {
    if (!Obj->TryGetStringField(Key, OutValue)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � missing or invalid field '%s' in %s"), *Key, *Context);
        return false;
    }
    return true;
}

bool UTeleOpConfig::RequireInt(const TSharedPtr<FJsonObject>& Obj, const FString& Key, int32& OutValue, const FString& Context) {
    if (!Obj->TryGetNumberField(Key, OutValue)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � missing or invalid field '%s' in %s"), *Key, *Context);
        return false;
    }
    return true;
}

bool UTeleOpConfig::RequireFloat(const TSharedPtr<FJsonObject>& Obj, const FString& Key, float& OutValue, const FString& Context) {
    double Temp = 0.0;
    if (!Obj->TryGetNumberField(Key, Temp)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � missing or invalid field '%s' in %s"), *Key, *Context);
        return false;
    }
    OutValue = static_cast<float>(Temp);
    return true;
}

bool UTeleOpConfig::RequireObject(const TSharedPtr<FJsonObject>& Obj, const FString& Key, TSharedPtr<FJsonObject>& OutObject, const FString& Context) {
    const TSharedPtr<FJsonObject>* Found = nullptr;
    if (!Obj->TryGetObjectField(Key, Found) || !Found || !(*Found).IsValid()) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � missing or invalid object '%s' in %s"), *Key, *Context);
        return false;
    }
    OutObject = *Found;
    return true;
}

bool UTeleOpConfig::ReadPortPair(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FPortPair& OutPair, const FString& Context){
    TSharedPtr<FJsonObject> Sub;
    if (!RequireObject(Obj, Key, Sub, Context)) return false;
    if (!RequireInt(Sub, TEXT("send"), OutPair.Send, Context + TEXT(".") + Key)) return false;
    if (!RequireInt(Sub, TEXT("receive"), OutPair.Receive, Context + TEXT(".") + Key)) return false;
    return true;
}