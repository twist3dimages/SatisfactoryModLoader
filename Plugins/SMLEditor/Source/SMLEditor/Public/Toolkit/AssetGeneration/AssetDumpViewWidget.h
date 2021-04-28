#pragma once
#include "Slate.h"

struct SMLEDITOR_API FAssetDumpTreeNode : TSharedFromThis<FAssetDumpTreeNode> {
public:
	/** Root directory path */
	FString RootDirectory;
	/** Whenever this node represents a complete asset path, or just a directory */
	bool bIsLeafNode;
	/** Full Path to the represented asset dump file or directory */
	FString DiskPackagePath;
	/** Canonical package name, if this node represents asset */
	FString PackageName;
	/** Last fragment of the path, representing package short name */
	FString NodeName;

	FAssetDumpTreeNode();
private:
	/** True if this asset and all underlying assets are to be generated */
	bool bIsChecked;
	/** True if our state has been explicitly overriden by the user and we should be listed as a manual override */
	bool bIsOverridingParentState;
	
	/** True when children nodes have already been initialized */
	bool bChildrenNodesInitialized;
	TWeakPtr<FAssetDumpTreeNode> ParentNode;
	TArray<TSharedPtr<FAssetDumpTreeNode>> Children;
	
    void RegenerateChildren();
	TSharedPtr<FAssetDumpTreeNode> MakeChildNode();
public:
	void SetupPackageNameFromDiskPath();

	FORCEINLINE bool IsChecked() const { return bIsChecked; }

	/** Updates selection state of the element and all of it's children */
	void UpdateSelectedState(bool bIsChecked, bool bIsSetByParent);

	/** Retrieves a list of children nodes associated with this node */
	void GetChildrenNodes(TArray<TSharedPtr<FAssetDumpTreeNode>>& OutChildrenNodes);

	/** Appends selected package names to the package list */
	void PopulateGeneratedPackages(TArray<FName>& OutPackageNames);
};

class SML_API SAssetDumpViewWidget : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SAssetDumpViewWidget) {}
	SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
	void SetAssetDumpRootDirectory(const FString& RootDirectory);
	void PopulateSelectedPackages(TArray<FName>& OutPackageNames) const;
protected:
	TSharedPtr<FAssetDumpTreeNode> RootNode;
	TArray<TSharedPtr<FAssetDumpTreeNode>> RootAssetPaths;
	
	TSharedRef<class ITableRow> OnCreateRow(const TSharedPtr<FAssetDumpTreeNode> TreeNode, const TSharedRef<STableViewBase>& Owner) const;
	void GetNodeChildren(const TSharedPtr<FAssetDumpTreeNode> TreeNode, TArray<TSharedPtr<FAssetDumpTreeNode>>& OutChildren) const;
};