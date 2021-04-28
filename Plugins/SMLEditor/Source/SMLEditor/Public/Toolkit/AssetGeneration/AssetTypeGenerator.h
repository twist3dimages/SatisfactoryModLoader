#pragma once
#include "CoreMinimal.h"
#include "AssetTypeGenerator.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetGenerator, Log, All);

class UObjectHierarchySerializer;
class UPropertySerializer;
class FJsonObject;

/** Describes various phases of asset generation, followed by each other */
enum class EAssetGenerationStage {
	CONSTRUCTION,
	DATA_POPULATION,
	CDO_FINALIZATION,
	FINISHED
};

/** Describes a single asset package dependency to be satisfied */
struct SMLEDITOR_API FAssetDependency {
	const FName PackageName;
	const EAssetGenerationStage State;
};

UCLASS()
class SMLEDITOR_API UAssetTypeGenerator : public UObject {
	GENERATED_BODY()
private:
	FString PackageBaseDirectory;
    FName PackageName;
	FName AssetName;
    TSharedPtr<FJsonObject> AssetData;
	EAssetGenerationStage CurrentStage;
	bool bUsingExistingPackage;
	
	UPROPERTY()
    UObjectHierarchySerializer* ObjectSerializer;
	UPROPERTY()
	UPropertySerializer* PropertySerializer;
	UPROPERTY()
	UPackage* AssetPackage;
	UPROPERTY()
	UObject* AssetObject;

	/** Initializes this asset generator instance with the file data */
	void InitializeInternal(const FString& PackageBaseDirectory, FName PackageName, TSharedPtr<FJsonObject> RootFileObject);
protected:
	/** Returns name of the asset object as it is loaded from the dump */
	FORCEINLINE FName GetAssetName() const { return AssetName; }
	FORCEINLINE bool IsUsingExistingPackage() const { return bUsingExistingPackage; }
	FORCEINLINE TSharedPtr<FJsonObject> GetAssetData() const { return AssetData; }
	
	/** Retrieves path to the base directory containing current asset data */
	FORCEINLINE const FString& GetPackageBaseDirectory() const { return PackageBaseDirectory; };

	/** Returns instance of the active property serializer */
	FORCEINLINE UPropertySerializer* GetPropertySerializer() const { return PropertySerializer; }

	/** Returns instance of the object hierarchy serializer associated with this package */
	FORCEINLINE UObjectHierarchySerializer* GetObjectSerializer() const { return ObjectSerializer; }

	/** Allocates new package object and asset object inside of it */
	virtual UPackage* CreateAssetPackage() PURE_VIRTUAL(ConstructAsset, return NULL;);
	virtual void PopulateAssetWithData() {};
	virtual void FinalizeAssetCDO() {}

	/** Called when existing package is loaded from the disk to be used with asset generator. In that case, no CreateAssetPackage call will happen */
	virtual void OnExistingPackageLoaded() {};
public:
	UAssetTypeGenerator();

	/** Returns package name of the asset being generated */
	FORCEINLINE FName GetPackageName() const { return PackageName; }

	/** Returns current stage of the asset generation for this asset */
	FORCEINLINE EAssetGenerationStage GetCurrentStage() const { return CurrentStage; }

	/** Returns asset package created by CreateAssetPackage or loaded from the disk */
	FORCEINLINE UPackage* GetAssetPackage() const { return AssetPackage; }

	/** Populates array with the dependencies required to perform current asset generation stage */
	virtual void PopulateStageDependencies(TArray<FAssetDependency>& OutDependencies) const {}

	/** Attempts to advance asset generation stage. Returns new stage, or finished if generation is finished */
	EAssetGenerationStage AdvanceGenerationState();

	/** Additional asset classes handled by this generator, can be empty, these have lower priority than GetAssetClass */
	virtual void GetAdditionallyHandledAssetClasses(TArray<FName>& OutExtraAssetClasses) {}
	
	/** Determines class of the asset this generator is capable of generating. Will be called on CDO, do not access any state here! */
	virtual FName GetAssetClass() PURE_VIRTUAL(GetAssetClass, return NAME_None;);

	/** Tries to load asset generator state from the asset dump located under the provided root directory and having given package name */
	static UAssetTypeGenerator* InitializeFromFile(const FString& RootDirectory, FName PackageName);

	/** Finds generator capable of generating asset of the given class */
	static TSubclassOf<UAssetTypeGenerator> FindGeneratorForClass(FName AssetClass);
};