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
    bool    bPiPEnabled           = false;
    int32   PiPPort               = 5010;
    int32   PiPFeedbackPort       = 5011;
    int32   PiPStatusPort         = 5012;
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
};


UCLASS()
class TELEOP_VR_INTERFACE_API UTeleOpConfig : public UObject
{
    GENERATED_BODY()

public:

    FNetworkConfig Network;
    FStreamConfig  Stream;
    FHudConfig     Hud;

    bool Load(const FString& RootConfigPath);
    static FString DefaultConfigPath();

private:

    bool LoadNetwork(const FString& Path);
    bool LoadStream(const FString& Path);
    bool LoadHud(const FString& Path);

    static bool ReadJsonFile(const FString& Path, TSharedPtr<class FJsonObject>& OutObject);
    static bool RequireString(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, FString& OutValue, const FString& Context);
    static bool RequireInt(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, int32& OutValue, const FString& Context);
    static bool RequireFloat(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, float& OutValue, const FString& Context);
    static bool RequireObject(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, TSharedPtr<class FJsonObject>& OutObject, const FString& Context);
    static bool ReadPortPair(const TSharedPtr<class FJsonObject>& Obj, const FString& Key, FPortPair& OutPair, const FString& Context);
};