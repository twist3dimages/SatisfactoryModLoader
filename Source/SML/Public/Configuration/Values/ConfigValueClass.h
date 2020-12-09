#pragma once
#include "Configuration/ConfigValue.h"
#include "ConfigValueClass.generated.h"

UCLASS()
class SML_API UConfigValueClass : public UConfigValue {
    GENERATED_BODY()
public:
    /** Updates current class value on this state if class is suitable for this state */
    UFUNCTION(BlueprintCallable, Category = "ModConfig")
    void SetClassValue(UClass* ClassValue);

    /** Retrieves current class value from state */
    UFUNCTION(BlueprintPure, Category = "ModConfig")
    FORCEINLINE UClass* GetClassValue() const;

    /** Returns true if specified class value is valid for this value */
    UFUNCTION(BlueprintPure, Category = "ModConfig")
    bool IsClassValueValid(UClass* Class) const;
    
    virtual FString DescribeValue_Implementation() const override;
    virtual URawFormatValue* Serialize_Implementation(UObject* Outer) const override;
    virtual void Deserialize_Implementation(const URawFormatValue* Value) override;
    void FillConfigStruct_Implementation(const FReflectedObject& ReflectedObject, const FString& VariableName) const override;
private:
    /** Private to avoid bypassing validation */
    UPROPERTY()
    UClass* PrivateClassValue;
};