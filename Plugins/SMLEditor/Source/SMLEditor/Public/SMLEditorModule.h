#pragma once
#include "Modules/ModuleManager.h"

class FSMLEditorModule : public FDefaultGameModuleImpl {
public:
	static const FName AssetGeneratorTabName;
	
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
private:
	FDelegateHandle ContentBrowserExtenderDelegateHandler;
};