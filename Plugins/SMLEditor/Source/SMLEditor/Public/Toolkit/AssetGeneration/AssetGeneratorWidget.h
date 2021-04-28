#pragma once
#include "AssetGenerationProcessor.h"
#include "Slate.h"

class SAssetGeneratorWidget : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SAssetGeneratorWidget) {}
	SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
protected:
	TSharedPtr<class SAssetDumpViewWidget> AssetDumpViewWidget;
	TSharedPtr<class SEditableTextBox> InputDumpPathText;
	FAssetGeneratorConfiguration AssetGeneratorSettings;

	FString GetAssetDumpFolderPath() const;
	void UpdateDumpViewRootDirectory();
	void SetAssetDumpFolderPath(const FString& InDumpFolderPath);
	static FString GetDefaultAssetDumpPath();
	
	FReply OnBrowseOutputPathPressed();
	FReply OnGenerateAssetsButtonPressed();
};