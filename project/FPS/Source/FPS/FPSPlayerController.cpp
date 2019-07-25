// Fill out your copyright notice in the Description page of Project Settings.
#include "FPSPlayerController.h"
#include "LogMacros.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine.h"
#include "FPSLoginWidget.h"
#include "FPSCharacter.h"
#include "FPSAIController.h"
#include "FPSGameMode.h"

#include "TimerManager.h"
#include "SpatialWorkerConnection.h"
#include "SpatialGameInstance.h"
#include "GDKLogging.h"

const float AFPSPlayerController::GRENADE_FROM_BODY_MAX_DIST = 200.f;

void OnLoginTokens(void* UserData, const Worker_Alpha_LoginTokensResponse* LoginTokens)
{
	AFPSPlayerController* contoller = static_cast<AFPSPlayerController*>(UserData);
	if (LoginTokens->status.code == WORKER_CONNECTION_STATUS_CODE_SUCCESS)
	{
		UE_LOG(LogGDK, Log, TEXT("Success: Login Token Count %d"), LoginTokens->login_token_count);
		contoller->Populate(LoginTokens);
	}
	else
	{
		UE_LOG(LogGDK, Log, TEXT("Failure: Error %s"), UTF8_TO_TCHAR(LoginTokens->status.detail));
	}
}

void OnPlayerIdentityToken(void* UserData, const Worker_Alpha_PlayerIdentityTokenResponse* PIToken)
{
	if (PIToken->status.code == WORKER_CONNECTION_STATUS_CODE_SUCCESS)
	{
		UE_LOG(LogGDK, Log, TEXT("Success: Received PIToken: %s"), UTF8_TO_TCHAR(PIToken->player_identity_token));
		AFPSPlayerController* controller = static_cast<AFPSPlayerController*>(UserData);
		controller->LatestPITokenData = PIToken->player_identity_token;
		controller->LatestPIToken = UTF8_TO_TCHAR(PIToken->player_identity_token);

		if (!controller->GetWorld()->GetTimerManager().IsTimerActive(controller->QueryDeploymentsTimer))
		{
			controller->GetWorld()->GetTimerManager().SetTimer(controller->QueryDeploymentsTimer, controller, &AFPSPlayerController::QueryDeployments, 5.0f, true, 0.0f);
		}
	}
	else
	{
		UE_LOG(LogGDK, Log, TEXT("Failure: Error %s 1111"), UTF8_TO_TCHAR(PIToken->status.detail));
		AFPSPlayerController* controller = static_cast<AFPSPlayerController*>(UserData);

		if (controller->GetWorld()->GetTimerManager().IsTimerActive(controller->QueryDeploymentsTimer))
		{
			controller->GetWorld()->GetTimerManager().ClearTimer(controller->QueryDeploymentsTimer);
		}
	}
}


AFPSPlayerController::AFPSPlayerController()
{
	//DefaultMouseCursor = EMouseCursor::Crosshairs;

	static ConstructorHelpers::FClassFinder<UUserWidget> LoginWidgetBPClass(TEXT("WidgetBlueprint'/Game/FirstPersonCPP/Blueprints/FPSLoginWidgetBP.FPSLoginWidgetBP_C'"));
	if (LoginWidgetBPClass.Class != NULL)
	{
		LoginWidgetClass = LoginWidgetBPClass.Class;
	}
}

void AFPSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	QueryPIT();

	//Before client connect to server successfully, PlayerController's NetMode is NM_Standalone, we only use LoginWidget before client connected to server.
	//if (GetNetMode() == NM_Standalone)
	//{
	FString Text = FString::Printf(TEXT("AFPSPlayerController::BeginPlay"));
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Text);

		if (LoginWidgetClass)
		{
			Text = FString::Printf(TEXT("LoadClass Successful"));
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Text);

			LoginWidget = CreateWidget<UFPSLoginWidget>(this, LoginWidgetClass);
			if (LoginWidget)
			{
				Text = FString::Printf(TEXT("AddToViewport"));
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Text);
				LoginWidget->AddToViewport();
			}
		}

		bShowMouseCursor = true;
	//}
	//else if (GetNetMode() == NM_Client)
	//{
	//	//in NM_Client mode, we need to use mouse to turn and look up/down, so we hide mouse cursor.
	//	bShowMouseCursor = false;
	//}
}

void AFPSPlayerController::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFPSPlayerController, UserName_);
}

UFPSLoginWidget* AFPSPlayerController::GetLoginWidget()
{
	return LoginWidget;
}

FDeploymentInfo Parse(const Worker_Alpha_LoginTokenDetails LoginToken)
{
	FDeploymentInfo DeploymentInfo;

	DeploymentInfo.DeploymentId = UTF8_TO_TCHAR(LoginToken.deployment_id);
	DeploymentInfo.DeploymentName = UTF8_TO_TCHAR(LoginToken.deployment_name);
	DeploymentInfo.LoginToken = UTF8_TO_TCHAR(LoginToken.login_token);

	for (int i = 0; i < (int)LoginToken.tag_count; i++)
	{
		FString tag = UTF8_TO_TCHAR(LoginToken.tags[i]);
		if (tag.StartsWith("max_players_"))
		{
			tag.RemoveFromStart("max_players_");
			DeploymentInfo.MaxPlayerCount = FCString::Atoi(*tag);
		}
		else if (tag.StartsWith("players_"))
		{
			tag.RemoveFromStart("players_");
			DeploymentInfo.PlayerCount = FCString::Atoi(*tag);
		}
		else if (tag.Equals("status_lobby"))
		{
			DeploymentInfo.bAvailable = true;
		}
	}
	return DeploymentInfo;
}

void AFPSPlayerController::Populate(const Worker_Alpha_LoginTokensResponse* Deployments)
{
	TArray<FDeploymentInfo> DeploymentArray;
	for (int i = 0; i < (int)Deployments->login_token_count; i++)
	{
		DeploymentArray.Add(Parse(Deployments->login_tokens[i]));
	}


	DeploymentArray.Sort([](const FDeploymentInfo& lhs, const FDeploymentInfo& rhs)
	{
		return lhs.DeploymentName.Compare(rhs.DeploymentName) < 0;
	});

	for (FDeploymentInfo info : DeploymentArray)
	{
		LatestLoginToken = info.LoginToken;
	}
}

void AFPSPlayerController::QueryDeployments()
{
	Worker_Alpha_LoginTokensRequest* LTParams = new Worker_Alpha_LoginTokensRequest();
	LTParams->player_identity_token = LatestPITokenData;
	LTParams->worker_type = "UnrealClient";
	Worker_Alpha_LoginTokensResponseFuture* LTFuture = Worker_Alpha_CreateDevelopmentLoginTokensAsync("locator.improbable.io", 444, LTParams);
	Worker_Alpha_LoginTokensResponseFuture_Get(LTFuture, nullptr, this, OnLoginTokens);
}

void AFPSPlayerController::JoinDeployment()
{
	FURL TravelURL;
	TravelURL.Host = TEXT("locator.improbable.io");
	TravelURL.Map = TEXT("FirstPersonExampleMap");
	TravelURL.AddOption(TEXT("locator"));
	TravelURL.AddOption(*FString::Printf(TEXT("playeridentity=%s"), *LatestPIToken));
	TravelURL.AddOption(*FString::Printf(TEXT("login=%s"), *LatestLoginToken));

	//OnLoadingStarted.Broadcast();

	ClientTravel(TravelURL.ToString(), TRAVEL_Absolute, false);
}

void AFPSPlayerController::SetLoadingScreen(UUserWidget* LoadingScreen)
{

}

void AFPSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindAction("SwitchWeapon", IE_Pressed, this, &AFPSPlayerController::OnGrenadeEquip);
	InputComponent->BindAction("Draw", IE_Pressed, this, &AFPSPlayerController::OnGrenadeDraw);
	InputComponent->BindAction("Release", IE_Released, this, &AFPSPlayerController::OnGrenadeRelease);

	InputComponent->BindAxis("MoveForward", this, &AFPSPlayerController::MoveForward);
	InputComponent->BindAxis("MoveRight", this, &AFPSPlayerController::MoveRight);

	//handles devices that provide an absolute delta, such as a mouse.
	InputComponent->BindAxis("Turn", this, &AFPSPlayerController::Turn);
	InputComponent->BindAxis("LookUp", this, &AFPSPlayerController::LookUp);
}

void AFPSPlayerController::OnGrenadeEquip()
{
	ServerGrenadeEquip();
}

bool AFPSPlayerController::ServerGrenadeEquip_Validate()
{
	return true;
}

void AFPSPlayerController::ServerGrenadeEquip_Implementation()
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			Char->MulticastEquipWeapon();
		}
	}
}

void AFPSPlayerController::OnGrenadeDraw()
{
	ServerGrenadeDraw();
}

bool AFPSPlayerController::ServerGrenadeDraw_Validate()
{
	return true;
}

void AFPSPlayerController::ServerGrenadeDraw_Implementation()
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			Char->DrawGrenade();
		}
	}
}

void AFPSPlayerController::OnGrenadeRelease()
{
	if (AFPSCharacter* Character = Cast<AFPSCharacter>(GetViewTarget()))
	{
		if (Character->GrenadeEquipFlag())
		{
			FVector GripLocation = Character->GetGripSocketLocation();
			//To avoid the CollisionComponent of Character, becase Physics would be conflicted when spawning grenades inside the CollisionComponent.
			FVector ThrowLocation = GripLocation + GetControlRotation().Vector() * 50.f;
			ServerGrenadeRelease(ThrowLocation);
		}
	}
}

bool AFPSPlayerController::ServerGrenadeRelease_Validate(const FVector& ProjectileAtLocation)
{
	bool Ret = false;
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Character = GameMode->GetOwnCharacter(UserName_))
		{
			float Dist = FVector::Dist(Character->GetActorLocation(), ProjectileAtLocation);
			Ret = Dist < GRENADE_FROM_BODY_MAX_DIST;
		}
	}

	return Ret;
}

void AFPSPlayerController::ServerGrenadeRelease_Implementation(const FVector& ProjectileAtLocation)
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Character = GameMode->GetOwnCharacter(UserName_))
		{
			Character->ServerReleaseGrenade(ProjectileAtLocation);
		}
	}
}

void AFPSPlayerController::MoveForward(float Val)
{
	if (Val != 0.0f)
	{
		if (AFPSCharacter* Char = Cast<AFPSCharacter>(GetViewTarget()))
		{
			if (UCameraComponent* Camera = Char->GetFirstPersonCameraComponent())
			{
				// add movement in that direction
				//ServerMoveForward(Camera->GetForwardVector(), Val);
				FMatrix RotMatrix = FRotationMatrix(Camera->GetComponentRotation());
				FVector ForwardVector = RotMatrix.GetScaledAxis(EAxis::X).GetSafeNormal2D();
				Char->AddMovementInput(ForwardVector, Val);
			}
		}
	}
}

bool AFPSPlayerController::ServerMoveForward_Validate(const FVector& Direction, float Val)
{
	return Val != 0.f;
}

void AFPSPlayerController::ServerMoveForward_Implementation(const FVector& Direction, float Val)
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			// add movement in that direction
			Char->AddMovementInput(Direction, Val);
		}
	}
}

void AFPSPlayerController::MoveRight(float Val)
{
	if (Val != 0.0f)
	{
		if (AFPSCharacter* Char = Cast<AFPSCharacter>(GetViewTarget()))
		{
			if (UCameraComponent* Camera = Char->GetFirstPersonCameraComponent())
			{
				// add movement in that direction
				//ServerMoveRight(Camera->GetRightVector(), Val);
				FMatrix RotMatrix = FRotationMatrix(Camera->GetComponentRotation());
				FVector RightVector = RotMatrix.GetScaledAxis(EAxis::Y).GetSafeNormal2D();
				Char->AddMovementInput(RightVector, Val);
			}
		}
	}
}

bool AFPSPlayerController::ServerMoveRight_Validate(const FVector& Direction, float Val)
{
	return Val != 0.f;
}

void AFPSPlayerController::ServerMoveRight_Implementation(const FVector& Direction, float Val)
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			// add movement in that direction
			Char->AddMovementInput(Direction, Val);
		}
	}
}

void AFPSPlayerController::Turn(float Rate)
{
	// calculate delta for this frame from the rate information
	if (Rate != 0.f)
	{
		AddYawInput(Rate);

		//turn the direction on Server
		ServerTurn(Rate * InputYawScale);
	}
}

bool AFPSPlayerController::ServerTurn_Validate(float Rate)
{
	return Rate != 0.f;
}

void AFPSPlayerController::ServerTurn_Implementation(float Rate)
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(UGameplayStatics::GetGameMode(GetWorld())))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			// add movement in that direction
			//Char->AddActorLocalRotation(FRotator(0.f, Rate, 0.f));
			Char->AddActorWorldRotation(FRotator(0.f, Rate, 0.f));
		}
	}
}

void AFPSPlayerController::LookUp(float Rate)
{
	// calculate delta for this frame from the rate information
	if (Rate != 0.f)
	{
		AddPitchInput(-Rate);

		float CurrPitch = GetControlRotation().Pitch;

		if (CurrPitch > 180)
		{
			CurrPitch = CurrPitch - 360.f;
		}

		ServerLookUp(CurrPitch);
	}
}

bool AFPSPlayerController::ServerLookUp_Validate(float CurrPitch)
{
	return true;
}

void AFPSPlayerController::ServerLookUp_Implementation(float CurrPitch)
{
	if (AFPSGameMode* GameMode = Cast<AFPSGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (AFPSCharacter* Char = GameMode->GetOwnCharacter(UserName_))
		{
			// add movement in that direction
			//Char->AddActorLocalRotation(FRotator(Rate * InputYawScale, 0.f, 0.f));

			//Char->MulticastLookup(Rate);
			Char->SetAimPitch(CurrPitch);
		}
	}
}

void AFPSPlayerController::QueryPIT()
{
	Worker_Alpha_PlayerIdentityTokenRequest* PITParams = new Worker_Alpha_PlayerIdentityTokenRequest();
	// Replace this string with a dev auth token, see docs for information on how to generate one of these
	PITParams->development_authentication_token = "MDQ4NDQ1ZDUtZGE4YS00ODY3LWFiNWUtZDllMjYxZWNhMGQzOjpkZTZhODgzOS03OTM4LTQyYTQtYWU1Yy0zYTdkMGQ0ZjI2NjU=";
	PITParams->player_id = "Player Id";
	PITParams->display_name = "";
	PITParams->metadata = "";
	PITParams->use_insecure_connection = false;

	Worker_Alpha_PlayerIdentityTokenResponseFuture* PITFuture = Worker_Alpha_CreateDevelopmentPlayerIdentityTokenAsync("locator.improbable.io", 444, PITParams);

	if (PITFuture != nullptr)
	{
		Worker_Alpha_PlayerIdentityTokenResponseFuture_Get(PITFuture, nullptr, this, OnPlayerIdentityToken);
	}
}

