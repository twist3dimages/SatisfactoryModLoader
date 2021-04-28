#include "Toolkit/AssetGeneration/AssetGeneratorWidget.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Toolkit/AssetGeneration/AssetDumpViewWidget.h"
#define LOCTEXT_NAMESPACE "SML"

void SAssetGeneratorWidget::Construct(const FArguments& InArgs) {
	ChildSlot[
		SNew(SVerticalBox)
		+SVerticalBox::Slot().AutoHeight()[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center)[
				SNew(STextBlock)
				.Text(LOCTEXT("AssetGenerator_DumpPath", "Dump Root Folder Path: "))
			]
			+SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[
				SAssignNew(InputDumpPathText, SEditableTextBox)
				.HintText(LOCTEXT("AssetGenerator_DumpPath", "Enter path to the dump root folder here..."))
				.Text(FText::FromString(GetDefaultAssetDumpPath()))
				.OnTextCommitted_Lambda([this](const FText&) { UpdateDumpViewRootDirectory(); })
			]
			+SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center)[
				SNew(SButton)
				.Text(INVTEXT("..."))
				.OnClicked_Raw(this, &SAssetGeneratorWidget::OnBrowseOutputPathPressed)
			]
		]
		+SVerticalBox::Slot().AutoHeight()[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center)[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					const FText SourceText = LOCTEXT("AssetGenerator_AssetsPerTick", "Assets To Generate Per Tick ({Assets}): ");
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Assets"), AssetGeneratorSettings.MaxAssetsToAdvancePerTick);
					
					return FText::Format(SourceText, Arguments);
				})
			]
			+SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[
				SNew(SSlider)
				.Orientation(Orient_Horizontal)
				.MinValue(1.0f)
				.MaxValue(32.0f)
				.StepSize(1.0f)
				.Value(AssetGeneratorSettings.MaxAssetsToAdvancePerTick)
				.OnValueChanged_Lambda([this](float NewValue) {
					AssetGeneratorSettings.MaxAssetsToAdvancePerTick = (int32) NewValue;
				})
			]
		]
		+SVerticalBox::Slot().AutoHeight()[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center)[
				SNew(STextBlock)
				.Text(LOCTEXT("AssetGenerator_RefreshAssets", "Refresh Existing Assets: "))
            ]
           +SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center)[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() {
					return AssetGeneratorSettings.bRefreshExistingAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
					AssetGeneratorSettings.bRefreshExistingAssets = NewState == ECheckBoxState::Checked;
				})
           ]
		]
		+SVerticalBox::Slot().AutoHeight()[
			SAssignNew(AssetDumpViewWidget, SAssetDumpViewWidget)
		]
		+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).VAlign(VAlign_Center)[
			SNew(SButton)
			.OnClicked_Raw(this, &SAssetGeneratorWidget::OnGenerateAssetsButtonPressed)
			.IsEnabled_Lambda([]() { return !FAssetGenerationProcessor::GetActiveAssetGenerator().IsValid(); })
		]
	];
}


FReply SAssetGeneratorWidget::OnGenerateAssetsButtonPressed() {
	TArray<FName> SelectedAssetPackages;
	AssetDumpViewWidget->PopulateSelectedPackages(SelectedAssetPackages);

	if (SelectedAssetPackages.Num() == 0) {
		return FReply::Handled();
	}
	
	FAssetGenerationProcessor::CreateAssetGenerator(AssetGeneratorSettings, SelectedAssetPackages);
	return FReply::Handled();
}

FString SAssetGeneratorWidget::GetAssetDumpFolderPath() const {
	FString FolderPath = InputDumpPathText->GetText().ToString();
	FPaths::NormalizeDirectoryName(FolderPath);
	
	//Return default path to asset dump if we failed to validate the typed path
	if (FolderPath.IsEmpty() || !FPaths::ValidatePath(*FolderPath)) {
		return GetDefaultAssetDumpPath();
	}
	return FolderPath;
}

void SAssetGeneratorWidget::UpdateDumpViewRootDirectory() {
	this->AssetDumpViewWidget->SetAssetDumpRootDirectory(GetDefaultAssetDumpPath());
}

void SAssetGeneratorWidget::SetAssetDumpFolderPath(const FString& InDumpFolderPath) {
	FString NewDumpFolderPath = InDumpFolderPath;
	FPaths::NormalizeDirectoryName(NewDumpFolderPath);
	this->InputDumpPathText->SetText(FText::FromString(NewDumpFolderPath));
	UpdateDumpViewRootDirectory();
}

FString SAssetGeneratorWidget::GetDefaultAssetDumpPath() {
	return FPaths::ProjectDir() + TEXT("AssetDump/");
}

FReply SAssetGeneratorWidget::OnBrowseOutputPathPressed() {
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(SharedThis(this));
	const FText DialogTitle = LOCTEXT("AssetGenerator_SelectDumpPath", "Select Asset Dump Root Folder");
	
	//Make sure selected directory exists, or directory dialog will fallback to user's root directory
	//Might be a better idea to navigate to parent directory instead, but let's leave it as-is
	const FString CurrentAssetDumpPath = GetAssetDumpFolderPath();
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*CurrentAssetDumpPath);
	
	FString OutDirectoryPath;
	if (DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, DialogTitle.ToString(), CurrentAssetDumpPath, OutDirectoryPath)) {
		SetAssetDumpFolderPath(OutDirectoryPath);
	}
	return FReply::Handled();
}
