#include "HUDComponent.h"
#include "OperatorPawn.h"
#include "VideoFeedComponent.h"
#include "ComLink.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "MotionControllerComponent.h"

UHUDComponent::UHUDComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UHUDComponent::BeginPlay()
{
	Super::BeginPlay();

	AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwner());
	if (!Pawn)
	{
		UE_LOG(LogTemp, Error, TEXT("HUDComponent: Owner is not an OperatorPawn."));
		SetComponentTickEnabled(false);
		return;
	}

	CameraRef = Pawn->GetVRCamera();
	VideoFeedRef = Pawn->VideoFeed;
	ComLinkRef = Pawn->ComLink;

	if (!CameraRef)
	{
		UE_LOG(LogTemp, Error, TEXT("HUDComponent: No VR camera found."));
		SetComponentTickEnabled(false);
		return;
	}

	SetupInteraction();

	UE_LOG(LogTemp, Log, TEXT("HUDComponent: Initialized with %d panel(s)."), Panels.Num());
}

void UHUDComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Future: attention-based opacity, gaze proximity checks, etc.
}

// ============================================================================
// Panel Management
// ============================================================================

UHUDPanelBase* UHUDComponent::RegisterPanel(
	FName PanelID,
	TSubclassOf<UHUDPanelBase> WidgetClass,
	EHUDPlacement Placement,
	FVector2D DrawSize,
	bool bStartVisible)
{
	if (!CameraRef || !WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("HUDComponent: Cannot register panel '%s' — no camera or null class."), *PanelID.ToString());
		return nullptr;
	}

	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	// Create the WidgetComponent in world space
	UWidgetComponent* WidgetComp = NewObject<UWidgetComponent>(Owner, *FString::Printf(TEXT("HUD_%s"), *PanelID.ToString()));
	if (!WidgetComp) return nullptr;

	WidgetComp->SetWidgetClass(WidgetClass);
	WidgetComp->SetDrawSize(DrawSize);
	WidgetComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WidgetComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	WidgetComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	WidgetComp->SetGenerateOverlapEvents(false);
	WidgetComp->SetPivot(FVector2D(0.5f, 0.5f));

	// World space widget for 3D interaction
	WidgetComp->SetWidgetSpace(EWidgetSpace::World);

	// Scale to physical size
	WidgetComp->SetWorldScale3D(FVector(WorldScale));

	// Attach to camera (face-locked)
	WidgetComp->AttachToComponent(CameraRef, FAttachmentTransformRules::KeepRelativeTransform);

	// Position based on placement
	FVector Offset = GetPlacementOffset(Placement);
	WidgetComp->SetRelativeLocation(FVector(HUDDistance, Offset.Y, Offset.Z));

	// Rotate to face the camera
	WidgetComp->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	
	WidgetComp->RegisterComponent();

	// Get the widget instance and configure it
	UHUDPanelBase* WidgetInstance = Cast<UHUDPanelBase>(WidgetComp->GetWidget());
	if (WidgetInstance)
	{
		WidgetInstance->PanelID = PanelID;
		WidgetInstance->SetDataContext(VideoFeedRef, ComLinkRef);

		if (!bStartVisible)
		{
			WidgetInstance->HidePanel();
		}
	}

	// Store entry
	FHUDPanelEntry Entry;
	Entry.PanelID = PanelID;
	Entry.WidgetClass = WidgetClass;
	Entry.WidgetComponent = WidgetComp;
	Entry.WidgetInstance = WidgetInstance;
	Entry.Placement = Placement;
	Entry.Size = DrawSize;
	Entry.bStartVisible = bStartVisible;
	Panels.Add(Entry);

	UE_LOG(LogTemp, Log, TEXT("HUDComponent: Registered panel '%s' at %s"),
		*PanelID.ToString(),
		*UEnum::GetValueAsString(Placement));

	return WidgetInstance;
}

void UHUDComponent::ShowPanel(FName PanelID)
{
	for (auto& Entry : Panels)
	{
		if (Entry.PanelID == PanelID && Entry.WidgetInstance)
		{
			Entry.WidgetInstance->ShowPanel();
			return;
		}
	}
}

void UHUDComponent::HidePanel(FName PanelID)
{
	for (auto& Entry : Panels)
	{
		if (Entry.PanelID == PanelID && Entry.WidgetInstance)
		{
			Entry.WidgetInstance->HidePanel();
			return;
		}
	}
}

void UHUDComponent::TogglePanel(FName PanelID)
{
	for (auto& Entry : Panels)
	{
		if (Entry.PanelID == PanelID && Entry.WidgetInstance)
		{
			if (Entry.WidgetInstance->bPanelActive)
				Entry.WidgetInstance->HidePanel();
			else
				Entry.WidgetInstance->ShowPanel();
			return;
		}
	}
}

void UHUDComponent::ShowPreset(const TArray<FName>& PanelIDs)
{
	// Hide all first
	HideAll();

	// Show requested panels
	for (const FName& ID : PanelIDs)
	{
		ShowPanel(ID);
	}
}

void UHUDComponent::HideAll()
{
	for (auto& Entry : Panels)
	{
		if (Entry.WidgetInstance)
		{
			Entry.WidgetInstance->HidePanel();
		}
	}
}

UHUDPanelBase* UHUDComponent::GetPanel(FName PanelID) const
{
	for (const auto& Entry : Panels)
	{
		if (Entry.PanelID == PanelID)
		{
			return Entry.WidgetInstance;
		}
	}
	return nullptr;
}

// ============================================================================
// Interaction
// ============================================================================

void UHUDComponent::SetupInteraction()
{
	AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwner());
	if (!Pawn) return;

	UMotionControllerComponent* RightController = Pawn->GetRightController();
	if (!RightController) return;

	// Create widget interaction component on the right controller
	RightHandInteraction = NewObject<UWidgetInteractionComponent>(GetOwner(), TEXT("RightHandWidgetInteraction"));
	if (RightHandInteraction)
	{
		RightHandInteraction->AttachToComponent(RightController, FAttachmentTransformRules::KeepRelativeTransform);
		RightHandInteraction->InteractionDistance = 500.0f;
		RightHandInteraction->bShowDebug = true;  // Shows ray in editor — disable later
		RightHandInteraction->RegisterComponent();

		UE_LOG(LogTemp, Log, TEXT("HUDComponent: Widget interaction set up on right controller."));
	}
}

// ============================================================================
// Positioning
// ============================================================================

FVector UHUDComponent::GetPlacementOffset(EHUDPlacement Placement) const
{
	// Returns (X=0, Y=horizontal, Z=vertical) offsets
	// X is always 0 because distance is set separately
	switch (Placement)
	{
	case EHUDPlacement::Left:		return FVector(0, -SideOffset, 0);
	case EHUDPlacement::Right:		return FVector(0, SideOffset, 0);
	case EHUDPlacement::Top:		return FVector(0, 0, VerticalOffset);
	case EHUDPlacement::Bottom:		return FVector(0, 0, -VerticalOffset);
	case EHUDPlacement::TopLeft:	return FVector(0, -SideOffset, VerticalOffset);
	case EHUDPlacement::TopRight:	return FVector(0, SideOffset, VerticalOffset);
	case EHUDPlacement::BottomLeft:	return FVector(0, -SideOffset, -VerticalOffset);
	case EHUDPlacement::BottomRight:return FVector(0, SideOffset, -VerticalOffset);
	case EHUDPlacement::Center:		return FVector(0, 0, 0);
	default:						return FVector::ZeroVector;
	}
}

FRotator UHUDComponent::GetPlacementRotation(EHUDPlacement Placement) const
{
	// All panels face the camera — no angling for now
	return FRotator(0.0f, 180.0f, 0.0f);
}

/* 
void UHUDComponent::PressUI(){
	UE_LOG(LogTemp, Log, TEXT("Signal moved to Press UI"));
	if (RightHandInteraction){
		UE_LOG(LogTemp, Log, TEXT("We have an hand itneraction"));
		RightHandInteraction->PressPointerKey(EKeys::LeftMouseButton);
	}
}
*/

void UHUDComponent::PressUI() {
	if (RightHandInteraction) {
		AActor* HitActor = RightHandInteraction->GetHoveredWidgetComponent()
			? RightHandInteraction->GetHoveredWidgetComponent()->GetOwner()
			: nullptr;
		UE_LOG(LogTemp, Warning, TEXT("PressUI | Hovering: %s"),
			HitActor ? *HitActor->GetName() : TEXT("NOTHING"));
		RightHandInteraction->PressPointerKey(EKeys::LeftMouseButton);
	}
}

void UHUDComponent::ReleaseUI(){
	if (RightHandInteraction){
		RightHandInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
	}
}