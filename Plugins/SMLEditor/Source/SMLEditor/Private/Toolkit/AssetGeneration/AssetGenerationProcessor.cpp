#include "Toolkit/AssetGeneration/AssetGenerationProcessor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#define LOCTEXT_NAMESPACE "SML"

TSharedPtr<FAssetGenerationProcessor> FAssetGenerationProcessor::ActiveAssetGenerator = NULL;

void FAssetGenerationProcessor::RefreshGeneratorDependencies(UAssetTypeGenerator* Generator) {
	const FName PackageName = Generator->GetPackageName();
	TArray<FAssetDependency> GeneratorDependencies;
	Generator->PopulateStageDependencies(GeneratorDependencies);

	TSharedRef<FDependencyList> DependencyList = MakeShareable(new FDependencyList());
	DependencyList->AssetTypeGenerator = Generator;
	
	for (const FAssetDependency& AssetDependency : GeneratorDependencies) {
		const FName DependencyPackageName = AssetDependency.PackageName;

		//Try to find asset generator in progress for the provided dependency package name first
		UAssetTypeGenerator** DependencyGenerator = AssetGenerators.Find(DependencyPackageName);
		if (DependencyGenerator != NULL) {
			const int32 DependencyStageIndex = (int32) AssetDependency.State;
			const int32 CurrentStageIndex = (int32) (*DependencyGenerator)->GetCurrentStage();
			
			//We only want to add dependency if required stage index is higher or equal to current stage index,
			//e.g when we are waiting for the dependency stage advance to happen
			if (DependencyStageIndex >= CurrentStageIndex) {
				DependencyList->PackageDependencies.Add(AssetDependency.PackageName, AssetDependency.State);
				PendingDependencies.FindOrAdd(DependencyPackageName).Add(DependencyList);

				//Log verbose information about the dependency for easier debugging
				UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("Package %s depends on generated package %s (required Stage: %d, Current: %d)"),
					*PackageName.ToString(), *DependencyPackageName.ToString(), DependencyStageIndex, CurrentStageIndex);
			}
			continue;
		}

		//Dependency seems to be unsatisfied (either external package or not-yet-constructed asset generator)
		//Call AddPackage, it will figure out what are we dealing with
		const EAddPackageResult AddPackageResult = AddPackage(DependencyPackageName);

		//Only add dependency if package is going to be generated. We never wait for external packages.
		if (AddPackageResult == EAddPackageResult::PACKAGE_WILL_BE_GENERATED) {
			DependencyList->PackageDependencies.Add(AssetDependency.PackageName, AssetDependency.State);
			PendingDependencies.FindOrAdd(DependencyPackageName).Add(DependencyList);

			//Log verbose information about the dependency for easier debugging
			UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("Package %s depends on generated package %s (required Stage: %d, just started)"),
				*PackageName.ToString(), *DependencyPackageName.ToString(), AssetDependency.State);
		}	
	}

	//If we have no pending asset generator dependencies, add ourselves to the advance list instantly
	//Otherwise we are waiting on dependencies to advance pretty much, nothing else to do here
	if (DependencyList->PackageDependencies.Num() == 0) {
		UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("Dependencies satisfied for package %d (instantly)"), *PackageName.ToString());
		this->GeneratorsReadyToAdvance.Add(Generator);
	}
}

void FAssetGenerationProcessor::OnGeneratorStageAdvanced(UAssetTypeGenerator* Generator) {
	const FName PackageName = Generator->GetPackageName();
	const int32 CurrentStageIndex = (int32) Generator->GetCurrentStage();
	UE_LOG(LogAssetGenerator, Verbose, TEXT("Asset generation advanced to stage %d for asset %s"), CurrentStageIndex, *PackageName.ToString());

	//Notify all dependents that we have advanced by one state
	TArray<TSharedPtr<FDependencyList>>* Dependents = PendingDependencies.Find(PackageName);
	if (Dependents) {
		for (int32 i = Dependents->Num() - 1; i >= 0; i--) {
			const TSharedPtr<FDependencyList> DependencyList = (*Dependents)[i];
			const FName DependentPackageName = DependencyList->AssetTypeGenerator->GetPackageName();
			const int32 DependencyStageIndex = (int32) DependencyList->PackageDependencies.FindChecked(PackageName);
			
			if (CurrentStageIndex > DependencyStageIndex) {
				DependencyList->PackageDependencies.Remove(PackageName);
				
				UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("Dependent package %s has satisfied dependency on %s (dependencies remaining: %d)"),
					*DependentPackageName.ToString(), *PackageName.ToString(), DependencyList->PackageDependencies.Num());

				//Advance dependent generator state if all dependencies have been satisfied
				if (DependencyList->PackageDependencies.Num() == 0) {
					UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("Dependencies satisfied for package %d (instantly)"), *DependentPackageName.ToString());
					this->GeneratorsReadyToAdvance.Add(Generator);
					(*Dependents).RemoveAt(i);
				}
			}
		}

		//Remove dependent entries if all dependents are finished now
		if (Dependents->Num() == 0) {
			this->PendingDependencies.Remove(PackageName);
		}
	}

	//Schedule next stage for the current generator if we're not finished
	if (Generator->GetCurrentStage() != EAssetGenerationStage::FINISHED) {
		RefreshGeneratorDependencies(Generator);
	} else {
		//Otherwise, if we're finished, we should do a cleanup for this generator
		CleanupAssetGenerator(Generator);
	}
}

FAssetGeneratorConfiguration::FAssetGeneratorConfiguration() :
		DumpRootDirectory(FPaths::ProjectDir() + TEXT("AssetDump/")),
		MaxAssetsToAdvancePerTick(4),
		bRefreshExistingAssets(true) {
}

void FAssetGenerationProcessor::InitializeAssetGeneratorInternal(UAssetTypeGenerator* Generator) {
	UE_LOG(LogAssetGenerator, Log, TEXT("Started asset generation: %s"), *Generator->GetPackageName().ToString());
	
	//Root generator so it will not garbage collected
	Generator->AddToRoot();

	//Associate it with the package in question and refresh dependencies
	this->AssetGenerators.Add(Generator->GetPackageName(), Generator);
	RefreshGeneratorDependencies(Generator);
}

void FAssetGenerationProcessor::MarkExternalPackageDependencySatisfied(FName PackageName) {
	this->ExternalPackagesResolved.Add(PackageName);
	UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("External package resolved: %s"), *PackageName.ToString());
}

void FAssetGenerationProcessor::MarkPackageAsNotFound(FName PackageName) {
	this->KnownMissingPackages.Add(PackageName);
	UE_LOG(LogAssetGenerator, Warning, TEXT("Failed to find external package '%s'"), *PackageName.ToString());
}

void FAssetGenerationProcessor::CleanupAssetGenerator(UAssetTypeGenerator* Generator) {
	//Make sure nobody is waiting for us
	check(this->PendingDependencies.Find(Generator->GetPackageName()) == NULL);
	
	UE_LOG(LogAssetGenerator, Log, TEXT("Finished asset generation: %s"), *Generator->GetPackageName().ToString());
	
	//Remove ourselves from the collection of asset generators, mark package as generated
	this->AssetGenerators.Remove(Generator->GetPackageName());
	this->AlreadyGeneratedPackages.Add(Generator->GetPackageName());
	
	//Unroot asset generator, we don't need it anymore
	Generator->RemoveFromRoot();
}

EAddPackageResult FAssetGenerationProcessor::AddPackage(const FName PackageName) {
	//Return PACKAGE_EXISTS if we have already processed this package before
	if (ExternalPackagesResolved.Contains(PackageName) ||
		AlreadyGeneratedPackages.Contains(PackageName) ||
		KnownMissingPackages.Contains(PackageName)) {
		return EAddPackageResult::PACKAGE_EXISTS;
	}

	//Asset is currently being generated, so make it known
	if (AssetGenerators.Contains(PackageName)) {
		return EAddPackageResult::PACKAGE_WILL_BE_GENERATED;
	}
	
	//First, try to extract package from the dump
	UAssetTypeGenerator* AssetTypeGenerator = UAssetTypeGenerator::InitializeFromFile(Configuration.DumpRootDirectory, PackageName);
	if (AssetTypeGenerator != NULL) {
		InitializeAssetGeneratorInternal(AssetTypeGenerator);
		return EAddPackageResult::PACKAGE_WILL_BE_GENERATED;
	}

	//Try to find package in memory, this will handle any /Script/ packages basically
	if (FindPackage(NULL, *PackageName.ToString())) {
		MarkExternalPackageDependencySatisfied(PackageName);
		return EAddPackageResult::PACKAGE_EXISTS;
	}

	//Try to lookup package file on disk, without loading it (because loading is not really needed for the dependency check)
	if (FPackageName::DoesPackageExist(PackageName.ToString())) {
		MarkExternalPackageDependencySatisfied(PackageName);
		return EAddPackageResult::PACKAGE_EXISTS;
	}

	//Package is not found, unlucky
	MarkPackageAsNotFound(PackageName);
	return EAddPackageResult::PACKAGE_NOT_FOUND;
}

bool FAssetGenerationProcessor::GatherNewAssetsForGeneration() {
	while (PackagesToGenerate.IsValidIndex(NextPackageToGenerateIndex)) {
		const FName PackageToGenerate = PackagesToGenerate[NextPackageToGenerateIndex++];

		//Skip package if it has been generated already before
		if (AlreadyGeneratedPackages.Contains(PackageToGenerate)) {
			continue;
		}

		const EAddPackageResult Result = AddPackage(PackageToGenerate);
		if (Result != EAddPackageResult::PACKAGE_WILL_BE_GENERATED) {
			UE_LOG(LogAssetGenerator, Warning, TEXT("AddPackage for package %s returned %d, expected PACKAGE_WILL_BE_GENERATED"), *PackageToGenerate.ToString(), Result);
			continue;
		}
		return true;
	}
	return false;
}

void FAssetGenerationProcessor::TickAssetGeneration() {
	//If we have nothing to advance, but have asset generators waiting, we are definitely in a cyclic dependencies loop
	//Log our full state for debugging purposes and crash
	if (GeneratorsReadyToAdvance.Num() == 0 && AssetGenerators.Num() != 0) {
		PrintStateIntoTheLog();
		UE_LOG(LogAssetGenerator, Fatal, TEXT("Cyclic dependencies were encountered during asset generation"));
	}

	//If asset generators are empty, try to gather some new assets for generation
	if (AssetGenerators.Num() == 0) {
		if (!GatherNewAssetsForGeneration()) {
			//No new assets to generate, we can finish generation now pretty much
			OnAssetGenerationFinished();
			return;
		}
	}

	//Advance pending asset generators (according to configuration)
	const int32 MaxGeneratorsToAdvance = FMath::Min(GeneratorsReadyToAdvance.Num(), Configuration.MaxAssetsToAdvancePerTick);
	
	for (int32 i = 0; i < MaxGeneratorsToAdvance; i++) {
		UAssetTypeGenerator* Generator = GeneratorsReadyToAdvance[i];
		Generator->AdvanceGenerationState();
		OnGeneratorStageAdvanced(Generator);
	}

	//Remove generators that we have already advanced
	GeneratorsReadyToAdvance.RemoveAt(0, MaxGeneratorsToAdvance);

	//Update notification item if it's visible
	UpdateNotificationItem();
}

void FAssetGenerationProcessor::OnAssetGenerationStarted() {
	UE_LOG(LogAssetGenerator, Log, TEXT("Starting asset generator for generating %d assets..."), PackagesToGenerate.Num());
	UE_LOG(LogAssetGenerator, Log, TEXT("To view advanced information about asset generation process in the log, set LogAssetGenerator verbosity to VeryVerbose/Verbose"));

	//Do not spawn notifications while we're running commandlet
	if (!IsRunningCommandlet()) {
		FNotificationInfo NotificationInfo = FNotificationInfo(LOCTEXT("AssetGenerator_Startup", "Asset Generation Starting Up..."));
		NotificationInfo.Hyperlink = FSimpleDelegate::CreateStatic([](){ FGlobalTabmanager::Get()->InvokeTab(FName(TEXT("OutputLog"))); });
		NotificationInfo.HyperlinkText = LOCTEXT("ShowMessageLogHyperlink", "Show Output Log");
		this->NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	}
}

void FAssetGenerationProcessor::OnAssetGenerationFinished() {
	this->bGenerationFinished = true;
	UE_LOG(LogAssetGenerator, Log, TEXT("Asset generation finished successfully, %d packages were generated/refreshed"), PackagesToGenerate.Num());

	if (NotificationItem.IsValid()) {
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("AssetsGenerated"), GetPackagesGenerated());
		this->NotificationItem->SetText(FText::Format(LOCTEXT("AssetGenerator_Finished", "Asset Generation Finished, {AssetsGenerated} Assets Generated"), Arguments));
		this->NotificationItem->SetExpireDuration(10.0f);
		this->NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		this->NotificationItem->ExpireAndFadeout();
		this->NotificationItem.Reset();
	}
}

void FAssetGenerationProcessor::UpdateNotificationItem() {
	if (NotificationItem.IsValid()) {
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("PackagesGenerated"), GetPackagesGenerated());
		Arguments.Add(TEXT("TotalPackages"), GetTotalPackages());
		Arguments.Add(TEXT("PackagesInProgress"), GetPackagesGeneratedCurrently());
		Arguments.Add(TEXT("ProgressPercent"), FMath::RoundToInt(GetPackagesGenerated() / (GetTotalPackages() * 1.0f) * 100.0f));

		NotificationItem->SetText(FText::Format(LOCTEXT("AssetGenerator_Progress",
			"Asset Generation In Progress: {PackagesGenerated}/{TotalPackages} packages, "
			"{ProgressPercent}% done, {PackagesInProgress} generating currently"), Arguments));
	}
}

void FAssetGenerationProcessor::PrintStateIntoTheLog() {
	UE_LOG(LogAssetGenerator, Log, TEXT("------------ ASSET GENERATOR STATE BEGIN ------------"));
	
	if (AssetGenerators.Num()) {
		UE_LOG(LogAssetGenerator, Log, TEXT("Package generators in progress: "));
		for (const TPair<FName, UAssetTypeGenerator*>& Pair : AssetGenerators) {
			UE_LOG(LogAssetGenerator, Log, TEXT(" - %s (Stage: %d)"), *Pair.Key.ToString(), Pair.Value->GetCurrentStage());
		}
	}

	if (PendingDependencies.Num()) {
		UE_LOG(LogAssetGenerator, Log, TEXT("Pending package dependencies: "));
		for (const TPair<FName, TArray<TSharedPtr<FDependencyList>>>& Pair : PendingDependencies) {
			UE_LOG(LogAssetGenerator, Log, TEXT(" - %s Dependents:"), *Pair.Key.ToString());
			for (const TSharedPtr<FDependencyList>& DependentPtr : Pair.Value) {
				UE_LOG(LogAssetGenerator, Log, TEXT("    - %s"), *DependentPtr->AssetTypeGenerator->GetPackageName().ToString());
			}
		}
	}

	if (GeneratorsReadyToAdvance.Num()) {
		UE_LOG(LogAssetGenerator, Log, TEXT("Generators ready to advance: "));
		for (UAssetTypeGenerator* Generator : GeneratorsReadyToAdvance) {
			UE_LOG(LogAssetGenerator, Log, TEXT(" - %s"), *Generator->GetPackageName().ToString());
		}
	}

	if (KnownMissingPackages.Num()) {
		UE_LOG(LogAssetGenerator, Log, TEXT("Missing external packages: "));
		for (const FName& PackageName : KnownMissingPackages) {
			UE_LOG(LogAssetGenerator, Log, TEXT(" - %s"), *PackageName.ToString());
		}
	}

	if (AlreadyGeneratedPackages.Num()) {
		UE_LOG(LogAssetGenerator, Verbose, TEXT("Packages already generated: "));
		for (const FName& PackageName : AlreadyGeneratedPackages) {
			UE_LOG(LogAssetGenerator, Verbose, TEXT(" - %s"), *PackageName.ToString());
		}
	}

	if (ExternalPackagesResolved.Num() && !LogAssetGenerator.IsSuppressed(ELogVerbosity::VeryVerbose)) {
		UE_LOG(LogAssetGenerator, VeryVerbose, TEXT("External packages referenced: "));
		for (const FName& PackageName : ExternalPackagesResolved) {
			UE_LOG(LogAssetGenerator, VeryVerbose, TEXT(" - %s"), *PackageName.ToString());
		}
	}
	
	UE_LOG(LogAssetGenerator, Log, TEXT("------------- ASSET GENERATOR STATE END ------------"));
}

FAssetGenerationProcessor::FAssetGenerationProcessor(const FAssetGeneratorConfiguration& Configuration,
	const TArray<FName>& PackagesToGenerate) {
	this->Configuration = Configuration;
	this->PackagesToGenerate = PackagesToGenerate;
	this->NextPackageToGenerateIndex = 0;
	this->bGenerationFinished = false;
	this->bIsFirstTick = true;
}

TSharedRef<FAssetGenerationProcessor> FAssetGenerationProcessor::CreateAssetGenerator(const FAssetGeneratorConfiguration& Configuration, const TArray<FName>& PackagesToGenerate) {
	check(IsInGameThread());
	check(ActiveAssetGenerator.IsValid() == false);
	check(PackagesToGenerate.Num());

	ActiveAssetGenerator = MakeShareable(new FAssetGenerationProcessor(Configuration, PackagesToGenerate));
	return ActiveAssetGenerator.ToSharedRef();
}

void FAssetGenerationProcessor::Tick(float DeltaTime) {
	if (bIsFirstTick) {
		OnAssetGenerationStarted();
		this->bIsFirstTick = false;
	}
	if (!bGenerationFinished) {
		TickAssetGeneration();
	} else {
		if (ActiveAssetGenerator == SharedThis(this)) {
			this->ActiveAssetGenerator.Reset();
		}
	}
}

TStatId FAssetGenerationProcessor::GetStatId() const {
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAssetGeneratorProcessor, STATGROUP_Game);
}

bool FAssetGenerationProcessor::IsTickableWhenPaused() const {
	return true;
}

bool FAssetGenerationProcessor::IsTickableInEditor() const {
	return true;
}
