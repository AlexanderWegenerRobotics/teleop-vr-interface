/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Widgets/NDIWidgets.h>
#include <Services/NDIFinderService.h>

#include <DetailLayoutBuilder.h>
#include <DetailWidgetRow.h>
#include <Editor.h>
#include <IDetailChildrenBuilder.h>
#include <IPropertyUtilities.h>
#include <PropertyHandle.h>

#include <atomic>

#define LOCTEXT_NAMESPACE "UNDIWidgets"


/**
	Organizes NDI sources into a tree
*/
struct FNDISourceTreeItem
{
	TArray<TSharedRef<FNDISourceTreeItem> > Children;
	FNDIConnectionInformation NDISource;
	FText DisplayText;
	bool IsExpanded { false };
	bool IsSelected { false };

	FNDISourceTreeItem()
	{}

	FNDISourceTreeItem(const FText& DisplayTextIn)
		: DisplayText(DisplayTextIn)
	{}

	FNDISourceTreeItem(const FNDIConnectionInformation& Source)
		: NDISource(Source)
	{}

	FNDISourceTreeItem(TSharedRef<FNDISourceTreeItem>&& Child)
	{
		Children.Add(Child);
	}

	static const TSharedRef<FNDISourceTreeItem>* FindMachineNode(const FNDISourceTreeItem& RootNode, const FNDIConnectionInformation& SourceItem)
	{
		const TSharedRef<FNDISourceTreeItem>* MachineNode = nullptr;

		if(!SourceItem.MachineName.IsEmpty())
		{
			const FString& SearchName = SourceItem.MachineName;
			MachineNode = RootNode.Children.FindByPredicate([&SearchName](const TSharedRef<FNDISourceTreeItem>& Child)
			{
				if(Child->Children.Num() > 0)
					return Child->Children[0]->NDISource.MachineName == SearchName;
				else
					return false;
			});
		}
		else if(!SourceItem.Url.IsEmpty())
		{
			const FString& SearchName = SourceItem.Url;
			MachineNode = RootNode.Children.FindByPredicate([&SearchName](const TSharedRef<FNDISourceTreeItem>& Child)
			{
				if(Child->Children.Num() > 0)
					return Child->Children[0]->NDISource.Url == SearchName;
				else
					return false;
			});
		}

		return MachineNode;
	}

	static const TSharedRef<FNDISourceTreeItem>* FindStreamNodeInMachineNode(const TSharedRef<FNDISourceTreeItem>& MachineNode, const FNDIConnectionInformation& SourceItem)
	{
		const TSharedRef<FNDISourceTreeItem>* StreamNode = nullptr;

		if(!SourceItem.StreamName.IsEmpty())
		{
			const FString& SearchName = SourceItem.StreamName;
			StreamNode = MachineNode->Children.FindByPredicate([&SearchName](const TSharedRef<FNDISourceTreeItem>& Child)
			{
				return Child->NDISource.StreamName == SearchName;
			});
		}
		else if(!SourceItem.Url.IsEmpty())
		{
			const FString& SearchName = SourceItem.Url;
			StreamNode = MachineNode->Children.FindByPredicate([&SearchName](const TSharedRef<FNDISourceTreeItem>& Child)
			{
				return Child->NDISource.Url == SearchName;
			});
		}

		return StreamNode;
	}

	void SetFromSources(const TArray<FNDIConnectionInformation>& SourceItems, const FText& SearchingTxt, bool StartExpanded)
	{
		FNDISourceTreeItem RootNode;

		//
		// Build new tree
		//

		for(int32 i = 0; i < SourceItems.Num(); ++i)
		{
			const TSharedRef<FNDISourceTreeItem>* MachineNode = FindMachineNode(RootNode, SourceItems[i]);

			if(MachineNode != nullptr)
			{
				FNDISourceTreeItem* NewNode = new FNDISourceTreeItem(SourceItems[i]);
				(*MachineNode)->Children.Add(MakeShareable(NewNode));
			}
			else
			{
				FNDISourceTreeItem* NewNode = new FNDISourceTreeItem(SourceItems[i]);
				FNDISourceTreeItem* NewMachineNode = new FNDISourceTreeItem(MakeShareable(NewNode));
				RootNode.Children.Add(MakeShareable(NewMachineNode));
			}
		}

		//
		// Preserve expansion and selection state by matching with old tree
		//

		for(int32 i = 0; i < RootNode.Children.Num(); ++i)
		{
			const TSharedRef<FNDISourceTreeItem>* OldMachineNode = FindMachineNode(*this, RootNode.Children[i]->Children[0]->NDISource);
			if(OldMachineNode != nullptr)
			{
				RootNode.Children[i]->IsExpanded = (*OldMachineNode)->IsExpanded;

				for(int32 j = 0; j < RootNode.Children[i]->Children.Num(); ++j)
				{
					const TSharedRef<FNDISourceTreeItem>* OldStreamNode = FindStreamNodeInMachineNode(*OldMachineNode, RootNode.Children[i]->Children[j]->NDISource);
					if(OldStreamNode != nullptr)
					{
						RootNode.Children[i]->Children[j]->IsSelected = (*OldStreamNode)->IsSelected;
					}
				}
			}
			else
			{
				RootNode.Children[i]->IsExpanded = StartExpanded;
			}
		}

		if(RootNode.Children.Num() == 0)
		{
			RootNode.Children.Add(MakeShareable(new FNDISourceTreeItem(SearchingTxt)));
		}


		//
		// Set to new tree
		//

		*this = RootNode;
	}
};



/**
	A menu widget containing NDI sources
*/

DECLARE_DELEGATE_OneParam(FOnSourceClicked, FNDIConnectionInformation);

class SNDISourcesMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNDISourcesMenu)
		: _OnSourceClicked()
		{}

	SLATE_EVENT(FOnSourceClicked, OnSourceClicked)

	SLATE_END_ARGS()

	SNDISourcesMenu()
	{}

	virtual ~SNDISourcesMenu()
	{
		FNDIFinderService::EventOnNDISourceCollectionChanged.Remove(SourceCollectionChangedEventHandle);
		SourceCollectionChangedEventHandle.Reset();
	}

	void Construct(const FArguments& InArgs)
	{
		OnSourceClicked = InArgs._OnSourceClicked;

		ChildSlot
		[
			SNew(SComboButton)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(LOCTEXT("NDI Sources Tip", "Currently Available NDI Sources"))
				.Text(LOCTEXT("NDI Sources", "NDI Sources"))
			]
			.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
			{
				FMenuBuilder MenuBuilder(true, nullptr);

				for (const auto& Sources : SourceTreeItems.Children)
					ConstructSourceMenu(MenuBuilder, Sources.Get());

				return MenuBuilder.MakeWidget();
			})
		];

		UpdateSources = true;

		FNDIFinderService::EventOnNDISourceCollectionChanged.Remove(SourceCollectionChangedEventHandle);
		SourceCollectionChangedEventHandle.Reset();
		SourceCollectionChangedEventHandle = FNDIFinderService::EventOnNDISourceCollectionChanged.AddLambda([this]()
		{
			UpdateSources = true;
		});
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime) override
	{
		bool IsDifferent = false;

		if (UpdateSources.exchange(false))
		{
			IsDifferent = FNDIFinderService::UpdateSourceCollection(SourceItems);
		}

		if (SourceItems.Num() == 0)
		{
			FText NewSearchingTxt;

			double WholeTime = 0.0;
			double FracTime = FMath::Modf(CurrentTime, &WholeTime);
			if(FracTime < 1/4.0)
				NewSearchingTxt = FText(LOCTEXT("NDI Sources Searching0", "Searching"));
			else if(FracTime < 2/4.0)
				NewSearchingTxt = FText(LOCTEXT("NDI Sources Searching1", "Searching."));
			else if(FracTime < 3/4.0)
				NewSearchingTxt = FText(LOCTEXT("NDI Sources Searching2", "Searching.."));
			else
				NewSearchingTxt = FText(LOCTEXT("NDI Sources Searching3", "Searching..."));

			if(!NewSearchingTxt.EqualTo(SearchingTxt))
			{
				SearchingTxt = NewSearchingTxt;
				IsDifferent = true;
			}
		}

		if (IsDifferent)
		{
			SourceTreeItems.SetFromSources(SourceItems, SearchingTxt, false);
			Invalidate(EInvalidateWidgetReason::PaintAndVolatility | EInvalidateWidgetReason::ChildOrder);
		}

		SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
	}

protected:
	void ConstructSourceMenu(FMenuBuilder& MenuBuilder, const FNDISourceTreeItem& SourceTreeItem)
	{
		if (SourceTreeItem.NDISource.IsValid())
		{
			MenuBuilder.AddMenuEntry(
				FText::FromString(SourceTreeItem.NDISource.StreamName),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this,&SourceTreeItem]()
				{
					this->OnSourceClicked.ExecuteIfBound(SourceTreeItem.NDISource);
				})),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
		else if (SourceTreeItem.Children.Num() > 0)
		{
			MenuBuilder.AddSubMenu(
				FText::FromString(SourceTreeItem.Children[0]->NDISource.MachineName),
				FText::GetEmpty(),
				FNewMenuDelegate::CreateLambda([this,&SourceTreeItem](FMenuBuilder& MenuBuilder)
				{
					for(const auto& ChildSource : SourceTreeItem.Children)
						ConstructSourceMenu(MenuBuilder, ChildSource.Get());
				})
			);
		}
		else if (!SourceTreeItem.DisplayText.IsEmpty())
		{
			MenuBuilder.AddMenuEntry(
				SourceTreeItem.DisplayText,
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this]
				{
				})),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
	}

private:
	TArray<FNDIConnectionInformation> SourceItems;
	FText SearchingTxt;
	FNDISourceTreeItem SourceTreeItems;

	FDelegateHandle SourceCollectionChangedEventHandle;
	std::atomic_bool UpdateSources { false };

	FOnSourceClicked OnSourceClicked;
};


/**
	Customization of NDIConnectionInformation property
	by including a menu to select from currently available NDI sources
*/

TSharedRef<IPropertyTypeCustomization> FNDIConnectionInformationCustomization::MakeInstance()
{
	return MakeShareable(new FNDIConnectionInformationCustomization);
}

void FNDIConnectionInformationCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SNDISourcesMenu)
		.OnSourceClicked_Lambda([this,PropertyHandle](FNDIConnectionInformation Source)
		{
			TArray<void*> RawData;
			PropertyHandle->AccessRawData(RawData);
			FNDIConnectionInformation* ConnectionInformation = reinterpret_cast<FNDIConnectionInformation*>(RawData[0]);
			if (ConnectionInformation != nullptr)
			{
				ConnectionInformation->Url = "";
				PropertyHandle->GetChildHandle("SourceName")->SetValue(Source.SourceName);
			}
		})
	].IsEnabled(true);
}

void FNDIConnectionInformationCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyUtilities> PropertyUtils = CustomizationUtils.GetPropertyUtilities();
	uint32 NumberOfChild;
	if (PropertyHandle->GetNumChildren(NumberOfChild) == FPropertyAccess::Success)
	{
		for (uint32 Index = 0; Index < NumberOfChild; ++Index)
		{
			TSharedRef<IPropertyHandle> ChildPropertyHandle = PropertyHandle->GetChildHandle(Index).ToSharedRef();
			ChildBuilder.AddProperty(ChildPropertyHandle)
				.ShowPropertyButtons(true)
				.IsEnabled(MakeAttributeLambda([=] { return !PropertyHandle->IsEditConst() && PropertyUtils->IsPropertyEditingEnabled(); }));
		}
	}
}


#undef LOCTEXT_NAMESPACE
