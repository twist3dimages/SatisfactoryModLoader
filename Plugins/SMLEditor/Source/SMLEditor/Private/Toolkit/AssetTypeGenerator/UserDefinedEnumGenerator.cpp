#include "Toolkit/AssetTypeGenerator/UserDefinedEnumGenerator.h"
#include "Dom/JsonObject.h"
#include "Engine/UserDefinedEnum.h"

UPackage* UUserDefinedEnumGenerator::CreateAssetPackage() {
	UPackage* NewPackage = CreatePackage(NULL, *GetPackageName().ToString());
	UEnum* NewEnum = NewObject<UUserDefinedEnum>(NewPackage, GetAssetName(), RF_Public | RF_Standalone);

	TArray<TPair<FName, int64>> EmptyNames;
	NewEnum->SetEnums(EmptyNames, UEnum::ECppForm::Namespaced);
	NewEnum->SetMetaData(TEXT("BlueprintType"), TEXT("true"));
	
	PopulateEnumWithData(CastChecked<UUserDefinedEnum>(NewEnum));
	return NewPackage;
}

void UUserDefinedEnumGenerator::OnExistingPackageLoaded() {
	UUserDefinedEnum* ExistingEnum = FindObjectChecked<UUserDefinedEnum>(GetAssetPackage(), *GetAssetName().ToString());

	if (!IsEnumerationUpToDate(ExistingEnum)) {
		UE_LOG(LogAssetGenerator, Display, TEXT("User Defined Enumeration %s is not up to date, regenerating data"), *ExistingEnum->GetPathName());
		
		//Wipe any existing data from the enumeration
		TArray<TPair<FName, int64>> EmptyNames;
		ExistingEnum->SetEnums(EmptyNames, UEnum::ECppForm::Namespaced);
		ExistingEnum->DisplayNameMap.Empty();
	
		PopulateEnumWithData(ExistingEnum);
	}
}

void UUserDefinedEnumGenerator::PopulateEnumWithData(UUserDefinedEnum* Enum) {
	const TArray<TSharedPtr<FJsonValue>>& Names = GetAssetData()->GetArrayField(TEXT("Names"));
	const TArray<TSharedPtr<FJsonValue>>& DisplayNames = GetAssetData()->GetArrayField(TEXT("DisplayNameMap"));

	TArray<TPair<FName, int64>> ResultEnumNames;

	//Last entry should always be a MAX one, just skip it because SetEnums will make one on it's own
	const TSharedPtr<FJsonObject> LastNamePair = Names.Last()->AsObject();
	check(LastNamePair->GetStringField(TEXT("Name")).EndsWith(TEXT("_MAX")));
	
	for (int32 i = 0; i < Names.Num() - 1; i++) {
		const TSharedPtr<FJsonObject> PairObject = Names[i]->AsObject();
		const FString Name = PairObject->GetStringField(TEXT("Name"));
		const int64 Value = PairObject->GetIntegerField(TEXT("Value"));

		ResultEnumNames.Add(TPair<FName, int64>(Name, Value));
	}
	
	Enum->SetEnums(ResultEnumNames, UEnum::ECppForm::Namespaced, true);
	check(LastNamePair->GetIntegerField(TEXT("Value")) == Enum->GetMaxEnumValue());

	//Update display names according to the json ones
	for (const TSharedPtr<FJsonValue> NameValue : DisplayNames) {
		const TSharedPtr<FJsonObject> PairObject = NameValue->AsObject();
		const FName Name = FName(*PairObject->GetStringField(TEXT("Name")));
		const FString DisplayName = PairObject->GetStringField(TEXT("DisplayName"));

		Enum->DisplayNameMap.Add(Name, FText::FromString(DisplayName));
	}
}

bool UUserDefinedEnumGenerator::IsEnumerationUpToDate(UUserDefinedEnum* Enum) const {
	const TArray<TSharedPtr<FJsonValue>>& Names = GetAssetData()->GetArrayField(TEXT("Names"));
	const TArray<TSharedPtr<FJsonValue>>& DisplayNames = GetAssetData()->GetArrayField(TEXT("DisplayNameMap"));

	if (Enum->NumEnums() != Names.Num() ||
		Enum->DisplayNameMap.Num() != DisplayNames.Num()) {
		return false;
	}

	for (int32 i = 0; i < Names.Num(); i++) {
		const TSharedPtr<FJsonObject> PairObject = Names[i]->AsObject();
		const FString Name = PairObject->GetStringField(TEXT("Name"));
		const int64 Value = PairObject->GetIntegerField(TEXT("Value"));

		const int64 ExistingValue = Enum->GetValueByIndex(i);
		const FString ExistingName = Enum->GetNameByIndex(i).ToString();
		if (ExistingName != Name || ExistingValue != Value) {
			return false;
		}
	}

	for (const TSharedPtr<FJsonValue>& NamePair : DisplayNames) {
		const TSharedPtr<FJsonObject> PairObject = NamePair->AsObject();
		const FName Name = FName(*PairObject->GetStringField(TEXT("Name")));
		const FString DisplayName = PairObject->GetStringField(TEXT("DisplayName"));

		const FText* ExistingDisplayName = Enum->DisplayNameMap.Find(Name);
		if (ExistingDisplayName == NULL ||
			ExistingDisplayName->ToString() != DisplayName) {
			return false;
		}
	}
	return true;
}

FName UUserDefinedEnumGenerator::GetAssetClass() {
	return UUserDefinedEnum::StaticClass()->GetFName();
}
