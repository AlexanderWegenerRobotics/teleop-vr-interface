/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Components/NDITriCasterExtComponent.h>

#include <Structures/NDIXml.h>

#include <Misc/EngineVersionComparison.h>

#include <EngineUtils.h>


/**
	Parsers for TriCasterExt metadata
*/

class NDIXmlElementParser_tricaster_ext : public NDIXmlElementParser
{
public:
	NDIXmlElementParser_tricaster_ext(UTriCasterExtComponent* TriCasterExtComponentIn)
		: TriCasterExtComponent(TriCasterExtComponentIn)
	{}

	virtual bool ProcessOpen(const TCHAR* ElementName, const TCHAR* ElementData)
	{
		TCData.Value = FString();
		TCData.KeyValues.Empty();

		return true;
	}

	virtual bool ProcessAttribute(const TCHAR* AttributeName, const TCHAR* AttributeValue) override
	{
		if(FCString::Strcmp(TEXT("name"), AttributeName) == 0)
		{}
		else if(FCString::Strcmp(TEXT("value"), AttributeName) == 0)
		{
			TCData.Value = FString(AttributeValue);
		}
		else
		{
			TCData.KeyValues.Add(FName(AttributeName), FString(AttributeValue));
		}

		return true;
	}

	virtual bool ProcessClose(const TCHAR* ElementName) override
	{
		if(TCData.Value == "ndiio")
		{
			FString* ActorNamePtr = TCData.KeyValues.Find("actor");
			FString* PropertyNamePtr = TCData.KeyValues.Find("property");
			FString* PropertyValueStrPtr = TCData.KeyValues.Find("propertyvalue");
			FString* ComponentNamePtr = TCData.KeyValues.Find("component");
			FString* EasingDurationPtr = TCData.KeyValues.Find("easing");

			if((ActorNamePtr != nullptr) && (PropertyNamePtr != nullptr) && (PropertyValueStrPtr != nullptr))
			{
				FString PropertyBaseName, PropertyElementName;
				if(!PropertyNamePtr->Split(TEXT(":"), &PropertyBaseName, &PropertyElementName))
					PropertyBaseName = *PropertyNamePtr;

				FTimespan EasingDuration = 0;
				if(EasingDurationPtr != nullptr)
				{
					double Seconds = FCString::Atod(**EasingDurationPtr);
					EasingDuration = FTimespan::FromSeconds(Seconds);
				}

				for(TActorIterator<AActor> ActorItr(TriCasterExtComponent->GetWorld()); ActorItr; ++ActorItr)
				{
					AActor* Actor = *ActorItr;
					if(Actor->GetName() == *ActorNamePtr)
					{
						UObject* FoundObject = nullptr;
						FProperty* FoundProperty = nullptr;

						if(ComponentNamePtr != nullptr)
						{
							TInlineComponentArray<UActorComponent*> PrimComponents;
							Actor->GetComponents(PrimComponents, true);
							for(auto& CompIt : PrimComponents)
							{
								if(CompIt->GetName() == *ComponentNamePtr)
								{
									FProperty* Property = CompIt->GetClass()->FindPropertyByName(*PropertyBaseName);
									if(Property)
									{
										FoundObject = CompIt;
										FoundProperty = Property;
										break;
									}
								}
							}
						}
						else
						{
							FProperty* ActorProperty = Actor->GetClass()->FindPropertyByName(*PropertyBaseName);
							if(ActorProperty)
							{
								FoundObject = Actor;
								FoundProperty = ActorProperty;
							}
							else
							{
								TInlineComponentArray<UActorComponent*> PrimComponents;
								Actor->GetComponents(PrimComponents, true);

								for(auto& CompIt : PrimComponents)
								{
									FProperty* CompProperty = CompIt->GetClass()->FindPropertyByName(*PropertyBaseName);
									if(CompProperty)
									{
										FoundObject = CompIt;
										FoundProperty = CompProperty;
										break;
									}
								}
							}
						}

						if(FoundObject && FoundProperty)
						{
							TriCasterExtComponent->TriCasterExt(Actor, FoundObject, FoundProperty, PropertyElementName, *PropertyValueStrPtr, EasingDuration);
							break;
						}
					}
				}
			}
		}

		TriCasterExtComponent->TriCasterExtCustom(TCData);

		return true;
	}

protected:
	UTriCasterExtComponent* TriCasterExtComponent;

	FTriCasterExt TCData;
};
// <tricaster_ext name="net1" value="ndiio" actor="LightSource" property="Intensity" propertyvalue="1.234" />
// <tricaster_ext name="net1" value="ndiio" actor="LightSource" component="LightComponent0" property="Intensity" propertyvalue="1.234" />
// <tricaster_ext name="net1" value="ndiio" actor="LightSource" property="RelativeLocation" propertyvalue="(X=1,Y=2,Z=3)" />
// <tricaster_ext name="net1" value="ndiio" actor="LightSource" property="RelativeLocation" propertyvalue="(X=1)" />
// <tricaster_ext name="net1" value="ndiio" actor="LightSource" property="RelativeLocation:Y" propertyvalue="2" easing="5.3"/>



UTriCasterExtComponent::UTriCasterExtComponent()
{
	this->bWantsInitializeComponent = true;

	this->PrimaryComponentTick.bAllowTickOnDedicatedServer = false;
	this->PrimaryComponentTick.bCanEverTick = true;
	this->PrimaryComponentTick.bHighPriority = true;
	this->PrimaryComponentTick.bRunOnAnyThread = false;
	this->PrimaryComponentTick.bStartWithTickEnabled = true;
	this->PrimaryComponentTick.bTickEvenWhenPaused = true;

	this->NDIMetadataParser = MakeShareable(new NDIXmlParser());
	NDIMetadataParser->AddElementParser("tricaster_ext", MakeShareable(new NDIXmlElementParser_tricaster_ext(this)));
}

UTriCasterExtComponent::~UTriCasterExtComponent()
{}

void UTriCasterExtComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (IsValid(NDIMediaSource))
	{
		// Ensure the TriCasterExt component is subscribed to the sender receiving metadata
		this->NDIMediaSource->OnSenderMetaDataReceived.RemoveAll(this);
		this->NDIMediaSource->OnSenderMetaDataReceived.AddDynamic(this, &UTriCasterExtComponent::ReceiveMetaDataFromSender);
	}
}

bool UTriCasterExtComponent::Initialize(UNDIMediaSender* InMediaSource)
{
	// is the media source already set?
	if (this->NDIMediaSource == nullptr && InMediaSource != nullptr)
	{
		// we passed validation, so set the media source
		this->NDIMediaSource = InMediaSource;

		// validate the Media Source object
		if (IsValid(NDIMediaSource))
		{
			// Ensure the TriCasterExt component is subscribed to the sender receiving metadata
			this->NDIMediaSource->OnSenderMetaDataReceived.RemoveAll(this);
			this->NDIMediaSource->OnSenderMetaDataReceived.AddDynamic(this, &UTriCasterExtComponent::ReceiveMetaDataFromSender);
		}
	}

	// did we pass validation
	return InMediaSource != nullptr && InMediaSource == NDIMediaSource;
}

void UTriCasterExtComponent::TriCasterExt(AActor* Actor, UObject* Object, FProperty* Property, FString PropertyElementName, FString PropertyValueStr, FTimespan EasingDuration)
{
	if(Actor && Object && Property)
	{
		FTriCasterExtInterp Interp;
		Interp.Actor = Actor;
		Interp.Object = Object;
		Interp.Property = Property;
		Interp.PropertyElementName = PropertyElementName;
		Interp.PropertyValueStr = PropertyValueStr;
		Interp.EasingDuration = EasingDuration.GetTotalSeconds();
		Interp.EasingRemaining = Interp.EasingDuration;

		TriCasterExtInterp.Add(Interp);
	}

	OnTriCasterExt.Broadcast(Actor, Object, PropertyElementName, PropertyValueStr, EasingDuration);
}

void UTriCasterExtComponent::TriCasterExtCustom(const FTriCasterExt& TCData)
{
	OnTriCasterExtCustom.Broadcast(TCData);
}


void UTriCasterExtComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	for(int32 i = 0; i < TriCasterExtInterp.Num(); ++i)
	{
		FTriCasterExtInterp& Interp = TriCasterExtInterp[i];

		float EasingDelta = FMath::Min(Interp.EasingRemaining, DeltaTime);

		void* Data = Interp.Property->ContainerPtrToValuePtr<void>(Interp.Object);
		if(Data)
		{
			bool Done = false;

#if WITH_EDITOR
			Interp.Object->PreEditChange(Interp.Property);
			Interp.Actor->PreEditChange(Interp.Property);
#endif

			if(FNumericProperty* NumericProperty = CastField<FNumericProperty>(Interp.Property))
			{
				double PropertyValue = NumericProperty->GetFloatingPointPropertyValue(Data);
				double TargetValue = FCString::Atod(*Interp.PropertyValueStr);

				double EasingFrac = (Interp.EasingRemaining > 0) ? (EasingDelta / Interp.EasingRemaining) : 1;
				double EasingInterp = 3*EasingFrac - 3*EasingFrac*EasingFrac + EasingFrac*EasingFrac*EasingFrac;

				double NewValue = PropertyValue * (1 - EasingInterp) + TargetValue * EasingInterp;
				NumericProperty->SetFloatingPointPropertyValue(Data, NewValue);
				Done = true;
			}
			else if(FStructProperty* StructProperty = CastField<FStructProperty>(Interp.Property))
			{
				FProperty* FieldProperty = FindFProperty<FProperty>(StructProperty->Struct, *(Interp.PropertyElementName));
				if(FNumericProperty* StructNumericProperty = CastField<FNumericProperty>(FieldProperty))
				{
					void* FieldData = FieldProperty->ContainerPtrToValuePtr<void>(Data);
					double PropertyValue = StructNumericProperty->GetFloatingPointPropertyValue(FieldData);
					double TargetValue = FCString::Atod(*Interp.PropertyValueStr);

					double EasingFrac = (Interp.EasingRemaining > 0) ? (EasingDelta / Interp.EasingRemaining) : 1;
					double EasingInterp = 3*EasingFrac - 3*EasingFrac*EasingFrac + EasingFrac*EasingFrac*EasingFrac;

					double NewValue = PropertyValue * (1 - EasingInterp) + TargetValue * EasingInterp;
					StructNumericProperty->SetFloatingPointPropertyValue(FieldData, NewValue);
					Done = true;
				}
			}

			if(!Done)
			{
				FString ImportText;
				if(!Interp.PropertyElementName.IsEmpty())
					ImportText = "(" + Interp.PropertyElementName + "=" + Interp.PropertyValueStr + ")";
				else
					ImportText = Interp.PropertyValueStr;
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 1))	// 5.1 and later
				Interp.Property->ImportText_Direct(*ImportText, Data, Interp.Object, 0);
#else
				Interp.Property->ImportText(*ImportText, Data, 0, Interp.Object);
#endif
			}

			UActorComponent* ActorComponent = Cast<UActorComponent>(Interp.Object);
			if(ActorComponent)
			{
				if((Interp.Property->GetFName() == TEXT("RelativeLocation")) ||
				   (Interp.Property->GetFName() == TEXT("RelativeRotation")) ||
				   (Interp.Property->GetFName() == TEXT("RelativeScale3D")))
				{
					ActorComponent->UpdateComponentToWorld();
				}
			}
#if (ENGINE_MAJOR_VERSION < 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION < 3))	// Before 5.3
			if(Interp.Property->HasAnyPropertyFlags(CPF_Interp))
				Interp.Object->PostInterpChange(Interp.Property);
#endif

#if WITH_EDITOR
			TArray<const UObject*> ModifiedObjects;
			ModifiedObjects.Add(Interp.Actor);
			FPropertyChangedEvent PropertyChangedEvent(Interp.Property, EPropertyChangeType::ValueSet, MakeArrayView(ModifiedObjects));
			FEditPropertyChain PropertyChain;
			PropertyChain.AddHead(Interp.Property);
			FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

			Interp.Object->PostEditChangeChainProperty(PropertyChangedChainEvent);
			Interp.Actor->PostEditChangeChainProperty(PropertyChangedChainEvent);
#endif
		}

		Interp.EasingRemaining -= EasingDelta;
		if(Interp.EasingRemaining == 0)
			TriCasterExtInterp.RemoveAtSwap(i);
	}
}


void UTriCasterExtComponent::ReceiveMetaDataFromSender(UNDIMediaSender* Sender, FString Data)
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
