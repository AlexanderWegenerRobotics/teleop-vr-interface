/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Components/NDIPTZControllerComponent.h>
#include <GameFramework/Actor.h>

#include <Structures/NDIXml.h>


/**
	Parsers for PTZ metadata
*/

class NDIXmlElementParser_ntk_ptz_pan_tilt_speed : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_ntk_ptz_pan_tilt_speed(UPTZController* PTZControllerIn)
		: PTZController(PTZControllerIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		PanSpeed = 0.0;
		TiltSpeed = 0.0;

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("pan_speed"), AttributeName) == 0)
		{
			PanSpeed = FCString::Atod(AttributeValue);
		}
		else if(FCString::Strcmp(TEXT("tilt_speed"), AttributeName) == 0)
		{
			TiltSpeed = FCString::Atod(AttributeValue);
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		PTZController->SetPTZPanTiltSpeed(PanSpeed, TiltSpeed);

		return true;
	}

protected:
	UPTZController* PTZController;

	double PanSpeed { 0.0 };
	double TiltSpeed { 0.0 };
};

class NDIXmlElementParser_ntk_ptz_zoom_speed : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_ntk_ptz_zoom_speed(UPTZController* PTZControllerIn)
		: PTZController(PTZControllerIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		ZoomSpeed = 0.0;

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("zoom_speed"), AttributeName) == 0)
		{
			ZoomSpeed = FCString::Atod(AttributeValue);
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		PTZController->SetPTZZoomSpeed(ZoomSpeed);

		return true;
	}

protected:
	UPTZController* PTZController;

	double ZoomSpeed { 0.0 };
};

class NDIXmlElementParser_ntk_ptz_focus : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_ntk_ptz_focus(UPTZController* PTZControllerIn)
		: PTZController(PTZControllerIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		AutoMode = true;
		Distance = 0.5;

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("mode"), AttributeName) == 0)
		{
			if(FCString::Strcmp(TEXT("manual"), AttributeValue) == 0)
				AutoMode = false;
		}
		else if(FCString::Strcmp(TEXT("distance"), AttributeName) == 0)
		{
			Distance = FCString::Atod(AttributeValue);
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		PTZController->SetPTZFocus(AutoMode, Distance);

		return true;
	}

protected:
	UPTZController* PTZController;

	bool AutoMode { true };
	double Distance { 0.5 };
};

class NDIXmlElementParser_ntk_ptz_store_preset : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_ntk_ptz_store_preset(UPTZController* PTZControllerIn)
		: PTZController(PTZControllerIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		StoreIndex = -1;

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("index"), AttributeName) == 0)
		{
			StoreIndex = FCString::Atoi(AttributeValue);
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		if(StoreIndex >= 0)
		{
			PTZController->StorePTZState(StoreIndex);
		}

		return true;
	}

protected:
	UPTZController* PTZController;

	int StoreIndex { -1 };
};

class NDIXmlElementParser_ntk_ptz_recall_preset : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_ntk_ptz_recall_preset(UPTZController* PTZControllerIn)
		: PTZController(PTZControllerIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		RecallIndex = -1;

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("index"), AttributeName) == 0)
		{
			RecallIndex = FCString::Atoi(AttributeValue);
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		if(RecallIndex >= 0)
		{
			PTZController->RecallPTZState(RecallIndex);
		}

		return true;
	}

protected:
	UPTZController* PTZController;

	int RecallIndex { -1 };
};


/**
	PTZ controller component
*/
UPTZController::UPTZController()
{
	this->bWantsInitializeComponent = true;

	this->PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	this->PrimaryComponentTick.bCanEverTick = true;
	this->PrimaryComponentTick.bHighPriority = true;
	this->PrimaryComponentTick.bRunOnAnyThread = false;
	this->PrimaryComponentTick.bStartWithTickEnabled = true;
	this->PrimaryComponentTick.bTickEvenWhenPaused = true;

	this->NDIMetadataParser = MakeShareable(new NDIXmlParser());
	this->NDIMetadataParser->AddElementParser("ntk_ptz_pan_tilt_speed", MakeShareable(new NDIXmlElementParser_ntk_ptz_pan_tilt_speed(this)));
	this->NDIMetadataParser->AddElementParser("ntk_ptz_zoom_speed", MakeShareable(new NDIXmlElementParser_ntk_ptz_zoom_speed(this)));
	this->NDIMetadataParser->AddElementParser("ntk_ptz_focus", MakeShareable(new NDIXmlElementParser_ntk_ptz_focus(this)));
	this->NDIMetadataParser->AddElementParser("ntk_ptz_store_preset", MakeShareable(new NDIXmlElementParser_ntk_ptz_store_preset(this)));
	this->NDIMetadataParser->AddElementParser("ntk_ptz_recall_preset", MakeShareable(new NDIXmlElementParser_ntk_ptz_recall_preset(this)));
}

UPTZController::~UPTZController()
{}

void UPTZController::InitializeComponent()
{
	Super::InitializeComponent();

	if (IsValid(NDIMediaSource))
	{
		// Ensure the PTZ controller is subscribed to the sender receiving metadata
		this->NDIMediaSource->OnSenderMetaDataReceived.RemoveAll(this);
		this->NDIMediaSource->OnSenderMetaDataReceived.AddDynamic(this, &UPTZController::ReceiveMetaDataFromSender);
	}
}

bool UPTZController::Initialize(UNDIMediaSender* InMediaSource)
{
	// is the media source already set?
	if (this->NDIMediaSource == nullptr && InMediaSource != nullptr)
	{
		// we passed validation, so set the media source
		this->NDIMediaSource = InMediaSource;

		// validate the Media Source object
		if (IsValid(NDIMediaSource))
		{
			// Ensure the PTZ controller is subscribed to the sender receiving metadata
			this->NDIMediaSource->OnSenderMetaDataReceived.RemoveAll(this);
			this->NDIMediaSource->OnSenderMetaDataReceived.AddDynamic(this, &UPTZController::ReceiveMetaDataFromSender);
		}
	}

	// did we pass validation
	return InMediaSource != nullptr && InMediaSource == NDIMediaSource;
}

void UPTZController::SetPTZPanTiltSpeed(float PanSpeed, float TiltSpeed)
{
	PTZPanSpeed = PanSpeed;
	PTZTiltSpeed = TiltSpeed;

	OnPTZPanTiltSpeed.Broadcast(PanSpeed, TiltSpeed);
}

void UPTZController::SetPTZZoomSpeed(float ZoomSpeed)
{
	PTZZoomSpeed = ZoomSpeed;

	OnPTZZoomSpeed.Broadcast(ZoomSpeed);
}

void UPTZController::SetPTZFocus(bool AutoMode, float Distance)
{
	FPTZState PTZState = GetPTZStateFromUE();
	PTZState.FocusDistance = Distance;
	PTZState.bAutoFocus = AutoMode;
	SetPTZStateToUE(PTZState);

	OnPTZFocus.Broadcast(AutoMode, Distance);
}

void UPTZController::StorePTZState(int Index)
{
	if((Index >= 0) && (Index < 256))
	{
		FPTZState PTZState = GetPTZStateFromUE();

		if(Index >= PTZStoredStates.Num())
			PTZStoredStates.SetNum(Index+1);
		PTZStoredStates[Index] = PTZState;

		OnPTZStore.Broadcast(Index);
	}
}

void UPTZController::RecallPTZState(int Index)
{
	if((Index >= 0) && (Index < PTZStoredStates.Num()))
	{
		if(PTZRecallEasing > 0)
		{
			PTZStateInterp.PTZTargetState = PTZStoredStates[Index];
			PTZStateInterp.EasingDuration = PTZRecallEasing;
			PTZStateInterp.EasingRemaining = PTZStateInterp.EasingDuration;
		}
		else
		{
			SetPTZStateToUE(PTZStoredStates[Index]);
		}
	}

	OnPTZRecall.Broadcast(Index);
}

FPTZState UPTZController::GetPTZStateFromUE() const
{
	AActor* OwnerActor = GetOwner();

	IPTZControllableInterface* ControllableObject = Cast<IPTZControllableInterface>(OwnerActor);
	if (ControllableObject != nullptr)
	{
		return ControllableObject->GetPTZStateFromUE();
	}
	else
	{
		FPTZState PTZState;

		FTransform Transform = OwnerActor->GetActorTransform();
		FVector Euler = Transform.GetRotation().Euler();
		PTZState.Pan = FMath::DegreesToRadians(Euler[2]);
		PTZState.Tilt = FMath::DegreesToRadians(Euler[1]);
		Transform.SetRotation(FQuat::MakeFromEuler(FVector(Euler[0], 0.f, 0.f)));
		PTZState.CameraTransform = Transform;

		return PTZState;
	}
}

void UPTZController::SetPTZStateToUE(const FPTZState& PTZState)
{
	if (EnablePTZ == true)
	{
		AActor* OwnerActor = GetOwner();

		IPTZControllableInterface* ControllableObject = Cast<IPTZControllableInterface>(OwnerActor);
		if (ControllableObject != nullptr)
		{
			ControllableObject->SetPTZStateToUE(PTZState);
		}
		else
		{
			FTransform Transform = PTZState.CameraTransform;
			FVector Euler = Transform.GetRotation().Euler();
			float Pitch = FMath::RadiansToDegrees(PTZState.Tilt);
			float Yaw = FMath::RadiansToDegrees(PTZState.Pan);
			Transform.SetRotation(FQuat::MakeFromEuler(FVector(Euler[0], Pitch, Yaw)));
			OwnerActor->SetActorTransform(Transform);
		}
	}
}


void UPTZController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bUpdatePTZ = false;

	if(PTZStateInterp.EasingRemaining > 0)
		bUpdatePTZ = true;

	if((PTZPanSpeed != 0) || (PTZTiltSpeed != 0) || (PTZZoomSpeed != 0))
		bUpdatePTZ = true;

	if(bUpdatePTZ)
	{
		FPTZState PTZState = GetPTZStateFromUE();

		if(PTZStateInterp.EasingRemaining > 0)
		{
			float EasingDelta = FMath::Min(PTZStateInterp.EasingRemaining, DeltaTime);

			/** Interpolate from 0 to 1 using polynomial:
					I(F) = a*F^3 + b*F^2 + c*F + d
				with constraints:
					Start and end points: I(0) = 0, I(1) = 1
					Smooth stop at end: I'(1) = 0 (velocity)
										I''(1) = 0 (acceleration)
				Solve to get:
					a = 1, b = -3, c = 3, d = 0
				I(F) = F^3 - 3*F^2 + 3*F
			*/
			float EasingFrac = (PTZStateInterp.EasingRemaining > 0) ? (EasingDelta / PTZStateInterp.EasingRemaining) : 1;
			float EasingInterp = EasingFrac*EasingFrac*EasingFrac - 3*EasingFrac*EasingFrac + 3*EasingFrac;

			PTZState.Pan = PTZState.Pan * (1 - EasingInterp) + PTZStateInterp.PTZTargetState.Pan * EasingInterp;
			PTZState.Tilt = PTZState.Tilt * (1 - EasingInterp) + PTZStateInterp.PTZTargetState.Tilt * EasingInterp;
			PTZState.FieldOfView = PTZState.FieldOfView * (1 - EasingInterp) + PTZStateInterp.PTZTargetState.FieldOfView * EasingInterp;
			PTZState.FocusDistance = PTZState.FocusDistance * (1 - EasingInterp) + PTZStateInterp.PTZTargetState.FocusDistance * EasingInterp;
			PTZState.CameraTransform.BlendWith(PTZStateInterp.PTZTargetState.CameraTransform, EasingInterp);

			PTZStateInterp.EasingRemaining -= EasingDelta;
		}

		PTZState.FieldOfView -= FMath::RadiansToDegrees(PTZZoomSpeed) * DeltaTime;
		if(PTZWithFoVLimit)
		{
			PTZState.FieldOfView = FMath::Clamp(PTZState.FieldOfView, PTZFoVMinLimit, PTZFoVMaxLimit);
		}
		PTZState.FieldOfView = FMath::Clamp(PTZState.FieldOfView, 5.f, 170.f);

		float MovementScale = PTZState.FieldOfView / 90.f;

		PTZState.Pan += PTZPanSpeed * DeltaTime * MovementScale * (bPTZPanInvert ? -1 : 1);
		PTZState.Pan = FMath::Fmod(PTZState.Pan, 2*PI);
		if(PTZWithPanLimit)
		{
			PTZState.Pan = FMath::Clamp(PTZState.Pan, FMath::DegreesToRadians(PTZPanMinLimit), FMath::DegreesToRadians(PTZPanMaxLimit));
		}

		PTZState.Tilt += PTZTiltSpeed * DeltaTime * MovementScale * (bPTZTiltInvert ? -1 : 1);
		PTZState.Tilt = FMath::Fmod(PTZState.Tilt, 2*PI);
		if(PTZWithTiltLimit)
		{
			PTZState.Tilt = FMath::Clamp(PTZState.Tilt, FMath::DegreesToRadians(PTZTiltMinLimit), FMath::DegreesToRadians(PTZTiltMaxLimit));
		}

		SetPTZStateToUE(PTZState);
	}
}


void UPTZController::ReceiveMetaDataFromSender(UNDIMediaSender* Sender, FString Data)
{
	FText OutErrorMessage;
	int32 OutErrorLineNumber;

	FFastXml::ParseXmlFile(this->NDIMetadataParser.Get(),
		nullptr,						// XmlFilePath
		Data.GetCharArray().GetData(),	// XmlFileContents
		nullptr,						// FeedbackContext
		false,							// bShowSlowTaskDialog
		false,							// bShowCancelButton
		OutErrorMessage,				// OutErrorMessage
		OutErrorLineNumber				// OutErrorLineNumber
	);
}
