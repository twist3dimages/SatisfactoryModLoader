#include "Toolkit/AssetGeneration/AssetDumpViewWidget.h"
#include "PackageTools.h"

FAssetDumpTreeNode::FAssetDumpTreeNode() {
	this->bIsLeafNode = false;
	this->bIsChecked = false;
	this->bIsOverridingParentState = false;
	this->bChildrenNodesInitialized = false;
}

TSharedPtr<FAssetDumpTreeNode> FAssetDumpTreeNode::MakeChildNode() {
	TSharedRef<FAssetDumpTreeNode> NewNode = MakeShareable(new FAssetDumpTreeNode());
	NewNode->ParentNode = SharedThis(this);
	NewNode->RootDirectory = RootDirectory;
	NewNode->bIsChecked = bIsChecked;
	
	Children.Add(NewNode);
	return NewNode;
}

void FAssetDumpTreeNode::SetupPackageNameFromDiskPath() {
	//Remove extension from the file path (all asset dump files are json files)
	FString PackageNameNew = FPaths::ChangeExtension(DiskPackagePath, TEXT(""));
	
	//Make path relative to root directory (e.g D:\ProjectRoot\DumpRoot\Game\FactoryGame\Asset -> Game\FactoryGame\Asset)
	FPaths::MakePathRelativeTo(PackageNameNew, *RootDirectory);
	
	//Normalize path separators to use / instead of backslashes (Game/FactoryGame/Asset)
	PackageNameNew.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);

	//Make sure package path starts with a forward slash (/Game/FactoryGame/Asset)
	PackageNameNew.InsertAt(0, TEXT('/'));

	//Make sure package name is sanitised before using it
	PackageNameNew = UPackageTools::SanitizePackageName(PackageNameNew);

	this->PackageName = PackageNameNew;
	this->NodeName = FPackageName::GetShortName(PackageNameNew);
}

void FAssetDumpTreeNode::RegenerateChildren() {
	if (bIsLeafNode) {
		return;
	}
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TArray<FString> ChildDirectoryNames;
	TArray<FString> ChildFilenames;
	
	PlatformFile.IterateDirectory(*DiskPackagePath, [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory) {
		if (bIsDirectory) {
			ChildDirectoryNames.Add(FilenameOrDirectory);
		//TODO this should really use a better filtering mechanism than checking file extension, or maybe we could use some custom extension like .uassetdump
		} else if (FPaths::GetExtension(FilenameOrDirectory) == TEXT("json")) {
			ChildFilenames.Add(FilenameOrDirectory);
		}
		return true;
	});

	//Append child directory nodes first, even if they are empty
	for (const FString& ChildDirectoryName : ChildDirectoryNames) {
		const TSharedPtr<FAssetDumpTreeNode> ChildNode = MakeChildNode();
		ChildNode->bIsLeafNode = false;
		ChildNode->DiskPackagePath = ChildDirectoryName;
		ChildNode->SetupPackageNameFromDiskPath();
	}

	//Append filenames then, these represent individual packages
	for (const FString& AssetFilename : ChildFilenames) {
		const TSharedPtr<FAssetDumpTreeNode> ChildNode = MakeChildNode();
		ChildNode->bIsLeafNode = true;
		ChildNode->DiskPackagePath = AssetFilename;
		ChildNode->SetupPackageNameFromDiskPath();
	}
}

void FAssetDumpTreeNode::GetChildrenNodes(TArray<TSharedPtr<FAssetDumpTreeNode>>& OutChildrenNodes) {
	if (!bChildrenNodesInitialized) {
		this->bChildrenNodesInitialized = true;
		RegenerateChildren();
	}
	OutChildrenNodes.Append(Children);
}

void FAssetDumpTreeNode::UpdateSelectedState(bool bIsCheckedNew, bool bIsSetByParent) {
	this->bIsChecked = bIsCheckedNew;
	
	//We reset override state when selected state is updated by parent
	if (bIsSetByParent) {
		this->bIsOverridingParentState = false;
	} else {
		//Otherwise we reset it if it matches parent state or set it to true otherwise
		const TSharedPtr<FAssetDumpTreeNode> ParentNodePinned = ParentNode.Pin();
		const bool bIsParentChecked = ParentNodePinned.IsValid() ? ParentNodePinned->IsChecked() : false;
		//If updated state matches parent state, we should remove override
		if (bIsParentChecked == bIsCheckedNew) {
			this->bIsOverridingParentState = false;
		} else {
			//Otherwise our state differs from the parents one, so we are overriding it
			this->bIsOverridingParentState = true;
		}
	}
	
	//Propagate state update to children widgets
	for (const TSharedPtr<FAssetDumpTreeNode>& ChildNode : Children) {
		ChildNode->UpdateSelectedState(bIsCheckedNew, true);
	}
}

void FAssetDumpTreeNode::PopulateGeneratedPackages(TArray<FName>& OutPackageNames) {
	if (bIsLeafNode) {
		//If we represent leaf node, append our package name if we are checked
		if (bIsChecked) {
			OutPackageNames.Add(*PackageName);
		}
	} else {
		//Otherwise we represent a directory node. Directory nodes ignore checked flag because children nodes
		//can override it, and just delegate function call to their children
		
		//Make sure our children nodes are initialized
		if (!bChildrenNodesInitialized) {
			this->bChildrenNodesInitialized = true;
			RegenerateChildren();
		}

		//And iterate them for them to add generated packages to the list
		for (const TSharedPtr<FAssetDumpTreeNode>& ChildNode : Children) {
			ChildNode->PopulateGeneratedPackages(OutPackageNames);
		}
	}
}

void SAssetDumpViewWidget::Construct(const FArguments& InArgs) {
	ChildSlot[
        SNew(STreeView<TSharedPtr<FAssetDumpTreeNode>>)
        .SelectionMode(ESelectionMode::None)
        .OnGenerateRow_Raw(this, &SAssetDumpViewWidget::OnCreateRow)
        .OnGetChildren_Raw(this, &SAssetDumpViewWidget::GetNodeChildren)
        .TreeItemsSource(&this->RootAssetPaths)
    ];
}

void SAssetDumpViewWidget::SetAssetDumpRootDirectory(const FString& RootDirectory) {
	this->RootNode = MakeShareable(new FAssetDumpTreeNode);
	this->RootNode->bIsLeafNode = false;
	this->RootNode->RootDirectory = RootDirectory;
	this->RootNode->DiskPackagePath = RootDirectory;
	this->RootNode->SetupPackageNameFromDiskPath();

	this->RootAssetPaths.Empty();
	this->RootNode->GetChildrenNodes(RootAssetPaths);
}

void SAssetDumpViewWidget::PopulateSelectedPackages(TArray<FName>& OutPackageNames) const {
	this->RootNode->PopulateGeneratedPackages(OutPackageNames);
}

TSharedRef<ITableRow> SAssetDumpViewWidget::OnCreateRow(const TSharedPtr<FAssetDumpTreeNode> TreeNode,
	const TSharedRef<STableViewBase>& Owner) const {
	return SNew(STableRow<TSharedPtr<FAssetDumpTreeNode>>, Owner)
		.Content()[
			SNew(STextBlock)
			.Text(FText::FromString(TreeNode->NodeName))
		];
}

void SAssetDumpViewWidget::GetNodeChildren(const TSharedPtr<FAssetDumpTreeNode> TreeNode, TArray<TSharedPtr<FAssetDumpTreeNode>>& OutChildren) const {
	TreeNode->GetChildrenNodes(OutChildren);
}