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

    FString NetworkPath, NetworkTwinPath, StreamPath, HudPath, RobotPath, GhostPath;
    if (!RequireString(Root, TEXT("network"),      NetworkPath,     TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("network_twin"), NetworkTwinPath, TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("stream"),  StreamPath,  TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("hud"),     HudPath,     TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("robot"),   RobotPath,   TEXT("config.json"))) return false;
    if (!RequireString(Root, TEXT("overlay"), GhostPath,   TEXT("config.json"))) return false;

    if (!LoadNetwork(BaseDir / NetworkPath))         return false;
    if (!LoadNetworkTwin(BaseDir / NetworkTwinPath)) return false;
    if (!LoadStream(BaseDir / StreamPath))   return false;
    if (!LoadHud(BaseDir / HudPath))         return false;
    if (!LoadRobot(BaseDir / RobotPath))     return false;
    if (!LoadOverlay(BaseDir / GhostPath))   return false;

    UE_LOG(LogTemp, Log, TEXT("TeleOpConfig: Loaded successfully"));
    UE_LOG(LogTemp, Log, TEXT("  Network  � remote: %s  avatar: %d/%d"), *Network.RemoteIP, Network.Avatar.Send, Network.Avatar.Receive);
    UE_LOG(LogTemp, Log, TEXT("  NetworkTwin � remote: %s  avatar: %d/%d"), *NetworkTwin.RemoteIP, NetworkTwin.Avatar.Send, NetworkTwin.Avatar.Receive);
    UE_LOG(LogTemp, Log, TEXT("  Stream(right)  stereo: %s  port: %d  feedback: %d  status: %d"),
        Stream.bStereo ? TEXT("true") : TEXT("false"),
        Stream.RightPort, Stream.RightFeedbackPort, Stream.RightStatusPort);
    UE_LOG(LogTemp, Log, TEXT("  Stream� %s:%d  feedback: %d  timestamp: %d  status: %d"), *Stream.RemoteIP, Stream.Port, Stream.FeedbackPort, Stream.TimestampPort, Stream.StatusPort);
    UE_LOG(LogTemp, Log, TEXT("  HUD      � latency: %.0fms  loss: %.3f%%  fps: %.0f"), Hud.LatencyWarningMs, Hud.LossWarningPercent, Hud.FpsWarningFloor);

    return true;
}

bool UTeleOpConfig::LoadNetwork(const FString& Path) {
    return LoadNetworkInto(Path, Network, TEXT("network.json"));
}

bool UTeleOpConfig::LoadNetworkTwin(const FString& Path) {
    return LoadNetworkInto(Path, NetworkTwin, TEXT("network_twin.json"));
}

// Shared parser for both the avatar link (network.json / network_local.json,
// swapped between local/robot per deployment) and the twin link
// (network_twin.json, expected to stay fixed/local -- see ComLink's Twin*
// fields). Same on-disk shape either way; recording_port is only meaningful
// for the avatar file today (the recorder is driven off the avatar link) but
// is still required in both for schema consistency -- point network_twin.json's
// recording_port at the same local recorder if/when that becomes relevant.
bool UTeleOpConfig::LoadNetworkInto(const FString& Path, FNetworkConfig& OutNetwork, const FString& Context) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj))
    {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED � cannot read network config: %s"), *Path);
        return false;
    }

    if (!RequireString(Obj, TEXT("remote_ip"), OutNetwork.RemoteIP, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("avatar"), OutNetwork.Avatar, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("arm_left"), OutNetwork.ArmLeft, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("arm_right"), OutNetwork.ArmRight, Context)) return false;
    if (!ReadPortPair(Obj, TEXT("head"), OutNetwork.Head, Context)) return false;
    if (!RequireInt(Obj, TEXT("recording_port"), OutNetwork.RecordingPort, Context)) return false;

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
    Stream.PiPStreams.Empty();
    const TArray<TSharedPtr<FJsonValue>>* PiPArray = nullptr;
    if (Obj->TryGetArrayField(TEXT("pip_streams"), PiPArray) && PiPArray) {
        for (const auto& Val : *PiPArray) {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Val->TryGetObject(Entry) || !Entry) continue;
            FPiPStreamConfig S;
            (*Entry)->TryGetStringField(TEXT("name"),          S.Name);
            (*Entry)->TryGetNumberField(TEXT("port"),          S.Port);
            (*Entry)->TryGetNumberField(TEXT("feedback_port"), S.FeedbackPort);
            (*Entry)->TryGetNumberField(TEXT("status_port"),   S.StatusPort);
            (*Entry)->TryGetBoolField(TEXT("local_preview"),   S.bLocalPreview);
            Stream.PiPStreams.Add(S);
        }
    }

    // Optional second main-view source (see FStreamConfig::TwinStream). Reuses
    // FPiPStreamConfig's shape (name/port/feedback_port/status_port) even
    // though it isn't a PiP entry -- same fields are needed either way.
    const TSharedPtr<FJsonObject>* TwinObj = nullptr;
    if (Obj->TryGetObjectField(TEXT("twin_stream"), TwinObj) && TwinObj) {
        (*TwinObj)->TryGetStringField(TEXT("name"),          Stream.TwinStream.Name);
        (*TwinObj)->TryGetNumberField(TEXT("port"),          Stream.TwinStream.Port);
        (*TwinObj)->TryGetNumberField(TEXT("feedback_port"), Stream.TwinStream.FeedbackPort);
        (*TwinObj)->TryGetNumberField(TEXT("status_port"),   Stream.TwinStream.StatusPort);
        // Optional -- absent means "same host as the avatar stream" (see
        // FStreamConfig::TwinRemoteIP).
        (*TwinObj)->TryGetStringField(TEXT("remote_ip"),     Stream.TwinRemoteIP);

        // "enabled" lets a session opt out of the twin without deleting the
        // block, for the common case where the twin process simply isn't
        // running. Disabled means the source is never registered at all --
        // no socket, no decode thread, no reconnect timer, and no twin entry
        // in the main-view toggle. Absent defaults to true so existing
        // configs behave as before.
        bool bTwinEnabled = true;
        (*TwinObj)->TryGetBoolField(TEXT("enabled"), bTwinEnabled);

        Stream.bHasTwinStream = bTwinEnabled && !Stream.TwinStream.Name.IsEmpty();

        if (!bTwinEnabled)
            UE_LOG(LogTemp, Log, TEXT("TeleOpConfig: twin stream disabled via \"enabled\": false — avatar-only main view"));
    }

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
    if (!RequireFloat(Obj, TEXT("ui_widget_width"),    Hud.UIWidgetWidth,    Context)) return false;
    if (!RequireFloat(Obj, TEXT("ui_widget_height"),   Hud.UIWidgetHeight,   Context)) return false;
    if (!RequireFloat(Obj, TEXT("ui_plane_distance"),  Hud.UIPlaneDistance,  Context)) return false;

    return true;
}

bool UTeleOpConfig::LoadRobot(const FString& Path) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED — cannot read robot config: %s"), *Path);
        return false;
    }

    const FString Context = TEXT("robot.json");
    if (!RequireFloat(Obj, TEXT("workspace_lower_bound_z"),    Robot.WorkspaceLowerBoundZ,    Context)) return false;
    if (!RequireFloat(Obj, TEXT("workspace_boundary_margin"),  Robot.WorkspaceBoundaryMargin, Context)) return false;
    // Optional lateral limits — fall back to defaults if absent
    Obj->TryGetNumberField(TEXT("workspace_min_x"), Robot.WorkspaceMinX);
    Obj->TryGetNumberField(TEXT("workspace_max_y"), Robot.WorkspaceMaxY);
    Obj->TryGetNumberField(TEXT("workspace_min_y"), Robot.WorkspaceMinY);

    Obj->TryGetNumberField(TEXT("boundary_ttc_horizon_s"),         Robot.BoundaryTtcHorizonS);
    Obj->TryGetNumberField(TEXT("boundary_dist_floor_m"),          Robot.BoundaryDistFloorM);
    Obj->TryGetNumberField(TEXT("boundary_ttc_engage_distance_m"), Robot.BoundaryTtcEngageDistanceM);
    Obj->TryGetNumberField(TEXT("boundary_on_threshold"),          Robot.BoundaryOnThreshold);
    Obj->TryGetNumberField(TEXT("boundary_off_threshold"),         Robot.BoundaryOffThreshold);
    Obj->TryGetNumberField(TEXT("boundary_attack_s"),              Robot.BoundaryAttackS);
    Obj->TryGetNumberField(TEXT("boundary_release_s"),             Robot.BoundaryReleaseS);
    Obj->TryGetNumberField(TEXT("boundary_min_on_s"),              Robot.BoundaryMinOnS);
    Obj->TryGetNumberField(TEXT("boundary_radius_min_cm"),         Robot.BoundaryRadiusMinCm);
    Obj->TryGetNumberField(TEXT("boundary_radius_max_cm"),         Robot.BoundaryRadiusMaxCm);
    Obj->TryGetNumberField(TEXT("boundary_grid_spacing_cm"),       Robot.BoundaryGridSpacingCm);

    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
    if (Obj->TryGetArrayField(TEXT("wrist_pivot_right"), Arr) && Arr && Arr->Num() == 3)
        Robot.WristPivotRight = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
    if (Obj->TryGetArrayField(TEXT("wrist_pivot_left"), Arr) && Arr && Arr->Num() == 3)
        Robot.WristPivotLeft  = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());

    // Controller->EE orientation retarget per arm. JSON layout is [w, x, y, z];
    // FQuat is constructed (X, Y, Z, W). Optional — defaults keep prior behaviour.
    auto ReadQuatWXYZ = [&](const TCHAR* Key, FQuat& Out) {
        const TArray<TSharedPtr<FJsonValue>>* Q = nullptr;
        if (Obj->TryGetArrayField(Key, Q) && Q && Q->Num() == 4) {
            const double W = (*Q)[0]->AsNumber(), X = (*Q)[1]->AsNumber();
            const double Y = (*Q)[2]->AsNumber(), Z = (*Q)[3]->AsNumber();
            Out = FQuat(X, Y, Z, W);
            Out.Normalize();
        }
    };
    ReadQuatWXYZ(TEXT("controller_to_ee_quat_right"), Robot.ControllerToEEQuatRight);
    ReadQuatWXYZ(TEXT("controller_to_ee_quat_left"),  Robot.ControllerToEEQuatLeft);

    return true;
}

bool UTeleOpConfig::LoadOverlay(const FString& Path) {
    TSharedPtr<FJsonObject> Obj;
    if (!ReadJsonFile(Path, Obj)) {
        UE_LOG(LogTemp, Error, TEXT("TeleOpConfig: FAILED — cannot read ghost config: %s"), *Path);
        return false;
    }

    const FString Context = TEXT("ghost.json");
    if (!RequireFloat(Obj, TEXT("near_threshold_m"),   Overlay.NearThresholdM,   Context)) return false;
    if (!RequireFloat(Obj, TEXT("far_threshold_m"),    Overlay.FarThresholdM,    Context)) return false;
    if (!RequireFloat(Obj, TEXT("min_opacity"),        Overlay.MinOpacity,       Context)) return false;
    if (!RequireFloat(Obj, TEXT("max_opacity"),        Overlay.MaxOpacity,       Context)) return false;
    if (!RequireFloat(Obj, TEXT("latency_ok_ms"),      Overlay.LatencyOkMs,      Context)) return false;
    if (!RequireFloat(Obj, TEXT("latency_warn_ms"),    Overlay.LatencyWarnMs,    Context)) return false;
    if (!RequireFloat(Obj, TEXT("latency_bad_ms"),     Overlay.LatencyBadMs,     Context)) return false;
    if (!RequireFloat(Obj, TEXT("latency_bad_exit_ms"),Overlay.LatencyBadExitMs, Context)) return false;
    if (!RequireFloat(Obj, TEXT("capture_fov"),        Overlay.CaptureFOV,       Context)) return false;
    if (!RequireFloat(Obj, TEXT("stereo_capture_fov"), Overlay.StereoCaptureFOV, Context)) return false;
    if (!RequireFloat(Obj, TEXT("stereo_eye_offset_cm"),Overlay.StereoEyeOffsetCm,Context)) return false;
    if (!RequireFloat(Obj, TEXT("plane_distance"),     Overlay.PlaneDistance,    Context)) return false;
    if (!RequireFloat(Obj, TEXT("fov_coverage"),       Overlay.FOVCoverage,      Context)) return false;
    if (!RequireFloat(Obj, TEXT("boundary_plane_width_m"), Overlay.BoundaryPlaneWidthM,  Context)) return false;
    if (!RequireFloat(Obj, TEXT("boundary_plane_height_m"),Overlay.BoundaryPlaneHeightM, Context)) return false;
    if (!RequireInt(Obj,   TEXT("render_target_width"), Overlay.RenderTargetWidth, Context)) return false;
    if (!RequireInt(Obj,   TEXT("render_target_height"),Overlay.RenderTargetHeight,Context)) return false;

    // Optional -- absent keeps the default (110, Vive Pro). Sizes the face-locked
    // quads for both the ghost overlay and the video layer.
    Obj->TryGetNumberField(TEXT("hmd_hfov_deg"), Overlay.HmdHFovDeg);

    // Legacy flat keys. Still read first so pre-"viewpoint" overlay configs load
    // unchanged; the nested block below overrides them when present.
    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
    if (Obj->TryGetArrayField(TEXT("head_base_position"), Arr) && Arr && Arr->Num() == 3)
        Overlay.HeadBasePosition = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
    if (Obj->TryGetArrayField(TEXT("cam_offset_in_head"), Arr) && Arr && Arr->Num() == 3)
        Overlay.CamOffsetInHead  = FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());

    // Optional "viewpoint" block -- selects which camera the ghost is reprojected
    // through. Absent means head_tracked with the flat keys above, i.e. exactly
    // the behaviour that existed before this block was introduced.
    const TSharedPtr<FJsonObject>* ViewObj = nullptr;
    if (Obj->TryGetObjectField(TEXT("viewpoint"), ViewObj) && ViewObj)
    {
        auto ReadVec3 = [](const TSharedPtr<FJsonObject>& Src, const TCHAR* Key, FVector& Out)
        {
            const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
            if (Src->TryGetArrayField(Key, V) && V && V->Num() == 3)
                Out = FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber());
        };

        FString ModeStr;
        (*ViewObj)->TryGetStringField(TEXT("mode"), ModeStr);
        if (ModeStr.Equals(TEXT("static"), ESearchCase::IgnoreCase))
            Overlay.ViewpointMode = EOverlayViewpointMode::Static;
        else if (ModeStr.Equals(TEXT("head_tracked"), ESearchCase::IgnoreCase) || ModeStr.IsEmpty())
            Overlay.ViewpointMode = EOverlayViewpointMode::HeadTracked;
        else
            UE_LOG(LogTemp, Warning,
                TEXT("TeleOpConfig: unknown viewpoint.mode '%s' — falling back to head_tracked"), *ModeStr);

        // Sub-blocks are read regardless of the selected mode: keeping both
        // populated means flipping "mode" is genuinely a one-word edit.
        const TSharedPtr<FJsonObject>* HeadObj = nullptr;
        if ((*ViewObj)->TryGetObjectField(TEXT("head_tracked"), HeadObj) && HeadObj)
        {
            ReadVec3(*HeadObj, TEXT("head_base_position"), Overlay.HeadBasePosition);
            ReadVec3(*HeadObj, TEXT("cam_offset_in_head"), Overlay.CamOffsetInHead);
        }

        const TSharedPtr<FJsonObject>* StaticObj = nullptr;
        if ((*ViewObj)->TryGetObjectField(TEXT("static"), StaticObj) && StaticObj)
        {
            ReadVec3(*StaticObj, TEXT("pos"),     Overlay.StaticCamPos);
            ReadVec3(*StaticObj, TEXT("look_at"), Overlay.StaticCamLookAt);
            ReadVec3(*StaticObj, TEXT("up"),      Overlay.StaticCamUp);
        }
    }

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