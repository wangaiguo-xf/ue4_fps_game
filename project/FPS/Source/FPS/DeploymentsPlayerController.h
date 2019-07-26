// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "c_worker.h"
#include "DeploymentsPlayerController.generated.h"


class UFPSLoginWidget;

USTRUCT(BlueprintType)
struct FDeploymentInfo {
	GENERATED_BODY()

		UPROPERTY(BlueprintReadOnly)
		FString DeploymentId;
	UPROPERTY(BlueprintReadOnly)
		FString DeploymentName;
	UPROPERTY(BlueprintReadOnly)
		FString LoginToken;
	UPROPERTY(BlueprintReadOnly)
		int32 PlayerCount = 0;
	UPROPERTY(BlueprintReadOnly)
		int32 MaxPlayerCount = 0;
	UPROPERTY(BlueprintReadOnly)
		bool bAvailable = false;
};
/**
 * 
 */
UCLASS()
class FPS_API ADeploymentsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	ADeploymentsPlayerController();

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void Populate(const Worker_Alpha_LoginTokensResponse* Deployments);
	FString LatestPIToken;
	const char * LatestPITokenData;
	FString LatestLoginToken;

	void QueryDeployments();

	FTimerHandle QueryDeploymentsTimer;

	UFUNCTION(BlueprintCallable)
		void JoinDeployment();

	UFUNCTION(BlueprintCallable)
		void SetLoadingScreen(UUserWidget* LoadingScreen);


protected:

	void QueryPIT();

	TSubclassOf<UFPSLoginWidget> LoginWidgetClass;

	UFPSLoginWidget * LoginWidget;
};
