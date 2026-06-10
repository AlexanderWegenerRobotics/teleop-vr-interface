#pragma once

#include "CoreMinimal.h"
#include "TeleOpConfig.generated.h"


USTRUCT()
struct FPortPair
{
    GENERATED_BODY()
    int32 Send = 0;
    int32 Receive = 0;
};

USTRUCT()
struct FNetworkConfig
{
    GENERATED_BODY()
    FString   RemoteIP = TEXT("127.0.0.1");
    FPortPair Avatar;
    FPortPair ArmLeft;
    FPortPair ArmRight;
    FPortPair Head;
};

USTRUCT()
struct FPiPStreamConfig
{
    GENERATED_BODY()
    FString Name;
    int32   Port             = 5010;
    int32   FeedbackPort     = 5011;
    int32   StatusPort       = 5012;
};

USTRUCT()
struct FStreamConfig
{
    GENERATED_BODY()
    FString RemoteIP = TEXT("127.0.0.1");
    bool    bStereo          = false;
    int32   Port             = 5004;
    int32   FeedbackPort     = 5005;
    int32   TimestampPort    = 5006;
    int32   StatusPort       = 5007;
    int32   ReportIntervalMs = 500;
    int32   RightPort             = 5006;
    int32   RightFeedbackPort     = 5008;
    int32   RightStatusPort       = 5009;
    TArray<FPiPStreamConfig> PiPStreams;
};

USTRUCT()
struct FHudConfig
{
    GENERATED_BODY()
    float LatencyWarningMs = 140.0f;
    float DataLatencyWarningMs = 120.0f;
    float LossWarningPercent = 0.001f;
    float FpsWarningFloor = 20.0f;
    float MetricEntryWindowSec = 5.0f;
    float MetricExitWindowSec = 3.0f;
    float ConfirmationDurationSec = 3.0f;
    float UIWidgetWidth = 1280.0f;
    float UIWidgetHeight = 800.0f;
    float UIPlaneDistance = 690.0f;
};

USTRUCT()
struct FRobotConfig
{
    GENERATED_BODY()
    float WorkspaceLowerBoundZ    = 0.435f;
    float WorkspaceBoundaryMargin = 0.05f;
    float WorkspaceMinX =  0.4f;   // torso boundary
    float WorkspaceMaxY =  0.4f;   // left workspace edge
    float WorkspaceMinY = -0.4f;   // right workspace edge
    FVector WristPivotRight = FVector(-5.9f,  2.1f, 3.4f);
    FVector WristPivotLeft  = FVector(-5.9f, -2.1f, 3.4f);

    // Controller->EE orientation retarget (protocol frame), per arm. Must match the
    // arm's controller_axis_map (R_ctrl_to_ee_) so ghost orientation tracks the arm.
    // Stored as FQuat(X,Y,Z,W); JSON supplies [w, x, y, z]. Defaults reproduce the
    // values previously hardcoded in GhostOverlayComponent::SetIntentPose.
    FQuat ControllerToEEQuatRight = FQuat(0.5f, -0.5f, 0.5f,  0.5f);
    FQuat ControllerToEEQuatLeft  = FQuat(0.5f,  0.5f, 0.5f, -0.5f);
};

USTRUCT()
struct FOverlayConfig
{
    GENERATED_BODY()
    float NearThresholdM = 0.03f;
    float FarThresholdM  = 0.15f;
    float MinOpacity     = 0.15f;
    float MaxOpacity     = 0.85f;
    float LatencyOkMs      = 100.0f;
    float LatencyWarnMs    = 150.0f;
    float LatencyBadMs     = 300.0f;
    float LatencyBadExitMs = 200.0f;
    FVector HeadBasePosition = FVector(0.f, 0.f, 1.844f);
    FVector CamOffsetInHead  = FVector(0.05f, 0.f, 0.035f);
    float CaptureFOV         = 75.2f;
    float StereoCaptureFOV   = 75.2f;
    float StereoEyeOffsetCm  = 3.05f;
    int32 RenderTargetWidth  = 1280;
    int32 RenderTargetHeight = 960;
    float PlaneDistance      = 700.0f;
    float FOVCoverage        = 0.85f;
    float BoundaryPlaneWidthM  = 1.8f;
    float BoundaryPlaneHeightM = 0.96f;
};

UCLASS()
class TELEOP_VR_INTERFACE_API UTeleOpConfig : public UObject
{
    GENERATED_BODY()

public:

    FNetworkConfig Network;
    FStreamConfig  Stream;
    FHudConfig     Hud;
    FRobotConfig   Robot;
    FOverlayConfig Overlay;

    bool Load(const FString& RootConfigPath);
    static FString DefaultConfigPath();

private:

    bool LoadNetwork(const FString& Path);
    bool LoadStream(const FString& Path);
    bool LoadHud(const FString& Path);
    bool LoadRobot(const FString& Path);
    bool LoadOverlay(const FString& Path);

    static bool ReadJsonFile(const FString& Path, TSharedPtr<class FJsonObject>& OutObject);
    static bool RequireString(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, FString& OutValue, const FString& Context);
    static bool RequireInt(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, int32& OutValue, const FString& Context);
    static bool RequireFloat(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, float& OutValue, const FString& Context);
    static bool RequireObject(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, TSharedPtr<class FJsonObject>& OutObject, const FString& Context);
    static bool ReadPortPair(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, FPortPair& OutPair, const FString& Context);
};