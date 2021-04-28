#include "Toolkit/AssetGeneration/AssetTypeGenerator.h"
#include "FileHelpers.h"
#include "Toolkit/ObjectHierarchySerializer.h"
#include "Toolkit/PropertySerializer.h"

DEFINE_LOG_CATEGORY(LogAssetGenerator)

UAssetTypeGenerator::UAssetTypeGenerator() {
	this->PropertySerializer = CreateDefaultSubobject<UPropertySerializer>(TEXT("PropertySerializer"));
	this->ObjectSerializer = CreateDefaultSubobject<UObjectHierarchySerializer>(TEXT("ObjectSerializer"));
	this->ObjectSerializer->SetPropertySerializer(PropertySerializer);

	this->CurrentStage = EAssetGenerationStage::CONSTRUCTION;
	this->AssetPackage = NULL;
	this->AssetObject = NULL;
	this->bUsingExistingPackage = false;
}

void UAssetTypeGenerator::InitializeInternal(const FString& InPackageBaseDirectory, const FName InPackageName, const TSharedPtr<FJsonObject> RootFileObject) {
	this->PackageBaseDirectory = InPackageBaseDirectory;
	this->PackageName = FName(*RootFileObject->GetStringField(TEXT("PackageName")));
	this->AssetName = FName(*RootFileObject->GetStringField(TEXT("AssetName")));
	checkf(this->PackageName == InPackageName, TEXT("InitializeInternal called with inconsistent package name. Externally provided name was '%s', but internal dump package name is '%s'"),
		*InPackageName.ToString(), *this->PackageName.ToString());

	const TArray<TSharedPtr<FJsonValue>> ObjectHierarchy = RootFileObject->GetArrayField(TEXT("ObjectHierarchy"));
	this->ObjectSerializer->InitializeForDeserialization(ObjectHierarchy);
	this->AssetData = RootFileObject->GetObjectField(TEXT("AssetSerializedData"));
}

EAssetGenerationStage UAssetTypeGenerator::AdvanceGenerationState() {
	if (CurrentStage == EAssetGenerationStage::CONSTRUCTION) {
		UPackage* ExistingPackage = LoadPackage(NULL, *PackageName.ToString(), LOAD_Quiet);
		if (ExistingPackage == NULL) {
			//Make new package if we don't have existing one, make sure asset object is also allocated
			this->AssetPackage = CreateAssetPackage();
			this->AssetObject = FindObjectChecked<UObject>(AssetPackage, *AssetName.ToString());

			//Save package to filesystem so it will be exist not only in memory, but also on the disk
			FEditorFileUtils::PromptForCheckoutAndSave({AssetPackage}, false, false);

		} else {
			//Package already exist, reuse it while making sure out asset is contained within
			this->AssetPackage = ExistingPackage;
			this->AssetObject = FindObject<UObject>(AssetPackage, *AssetName.ToString());

			//We need to verify package exists and provide meaningful error message, so user knows what is wrong
			checkf(AssetObject, TEXT("Existing package %s does not contain an asset named %s, requested by asset dump"), *PackageName.ToString(), *AssetName.ToString());

			//Notify generator we are reusing existing package, so it can do additional cleanup and settings
			this->bUsingExistingPackage = true;
			OnExistingPackageLoaded();
		}

		//Advance asset generation state, next state after construction is data population
		this->CurrentStage = EAssetGenerationStage::DATA_POPULATION;
	}

	if (CurrentStage == EAssetGenerationStage::DATA_POPULATION) {
		this->PopulateAssetWithData();
		this->CurrentStage = EAssetGenerationStage::CDO_FINALIZATION;

		//Save asset with the populated data into filesystem, but only if it's dirty
		//Since we can handle existing packages, sometimes there simply won't be any changes to assets, in which case we do not want to re-save them
		FEditorFileUtils::PromptForCheckoutAndSave({AssetPackage}, true, false);
	}

	if (CurrentStage == EAssetGenerationStage::CDO_FINALIZATION) {
		this->FinalizeAssetCDO();
		this->CurrentStage = EAssetGenerationStage::FINISHED;

		//Save asset with the populated data into filesystem, but only if it's dirty. Most assets do not use CDO Finalization Stage at all.
		FEditorFileUtils::PromptForCheckoutAndSave({AssetPackage}, true, false);
	}

	return CurrentStage;
}

UAssetTypeGenerator* UAssetTypeGenerator::InitializeFromFile(const FString& RootDirectory, const FName PackageName) {
	const FString ShortPackageName = FPackageName::GetShortName(PackageName);
	const FString PackagePath = FPackageName::GetLongPackagePath(PackageName.ToString());

	const FString PackageBaseDirectory = FPaths::Combine(RootDirectory, PackagePath);

	const FString AssetDumpFilename = FPaths::SetExtension(ShortPackageName, TEXT("json"));
	const FString AssetDumpFilePath = FPaths::Combine(PackageBaseDirectory, AssetDumpFilename);

	//Return early if dump file is not found for this asset
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*AssetDumpFilePath)) {
		return NULL;
	}

	FString DumpFileStringContents;
	if (!FFileHelper::LoadFileToString(DumpFileStringContents, *AssetDumpFilePath)) {
		UE_LOG(LogAssetGenerator, Error, TEXT("Failed to load asset dump file %s"), *AssetDumpFilePath);
		return NULL;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DumpFileStringContents);
	TSharedPtr<FJsonObject> RootFileObject;
	if (!FJsonSerializer::Deserialize(Reader, RootFileObject)) {
		UE_LOG(LogAssetGenerator, Error, TEXT("Failed to parse asset dump file %s: invalid json"), *AssetDumpFilePath);
		return NULL;
	}

	const FName AssetClass = FName(*RootFileObject->GetStringField(TEXT("AssetClass")));
	UClass* AssetTypeGenerator = FindGeneratorForClass(AssetClass);
	if (AssetTypeGenerator == NULL) {
		UE_LOG(LogAssetGenerator, Error, TEXT("Asset generator not found for asset class '%s', loaded from %s"), *AssetClass.ToString(), *AssetDumpFilePath);
		return NULL;
	}

	UAssetTypeGenerator* NewGenerator = NewObject<UAssetTypeGenerator>(GetTransientPackage(), AssetTypeGenerator);
	NewGenerator->InitializeInternal(PackageBaseDirectory, PackageName, RootFileObject);
	return NewGenerator;
}

//Static class used to lazily populate serializer registry
class FAssetTypeGeneratorRegistry {
public:
	//Native classes should never get unloaded, so we can get away with using TSubclassOf without weak pointer
	TMap<FName, TWeakObjectPtr<UClass>> Generators;

	//Constructor that will automatically populate registry serializers
	FAssetTypeGeneratorRegistry();

	static const FAssetTypeGeneratorRegistry& Get() {
		static FAssetTypeGeneratorRegistry AssetTypeSerializerRegistry{};
		return AssetTypeSerializerRegistry;
	}
};

FAssetTypeGeneratorRegistry::FAssetTypeGeneratorRegistry() {
	TArray<UClass*> AssetGeneratorClasses;
	GetDerivedClasses(UAssetTypeGenerator::StaticClass(), AssetGeneratorClasses, true);

	//Iterate classes in memory to resolve serializers
	TArray<UAssetTypeGenerator*> OutFoundInitializers;
	for (UClass* Class : AssetGeneratorClasses) {
		//Skip classes that are marked as Abstract
		if (Class->HasAnyClassFlags(CLASS_Abstract)) {
			continue;
		}
		//Skip classes that are not marked as Native
		if (!Class->HasAnyClassFlags(CLASS_Native)) {
			continue;
		}
            
		//Check asset type in class default object
		UAssetTypeGenerator* ClassDefaultObject = CastChecked<UAssetTypeGenerator>(Class->GetDefaultObject());
		if (ClassDefaultObject->GetAssetClass() != NAME_None) {
			OutFoundInitializers.Add(ClassDefaultObject);
		}
	}

	//First register additional asset classes, so primary ones will overwrite them later
	for (UAssetTypeGenerator* Generator : OutFoundInitializers) {
		TArray<FName> OutExtraAssetClasses;
		Generator->GetAdditionallyHandledAssetClasses(OutExtraAssetClasses);
		for (const FName& AssetClass : OutExtraAssetClasses) {
			this->Generators.Add(AssetClass, Generator->GetClass());
		}
	}

	//Now setup primary asset classes and add serializers into array
	for (UAssetTypeGenerator* Generator : OutFoundInitializers) {
		const FName AssetClass = Generator->GetAssetClass();
		this->Generators.Add(AssetClass, Generator->GetClass());
	}
}

TSubclassOf<UAssetTypeGenerator> UAssetTypeGenerator::FindGeneratorForClass(const FName AssetClass) {
	const TWeakObjectPtr<UClass>* Class = FAssetTypeGeneratorRegistry::Get().Generators.Find(AssetClass);
	return Class ? (*Class).Get() : NULL;
}
