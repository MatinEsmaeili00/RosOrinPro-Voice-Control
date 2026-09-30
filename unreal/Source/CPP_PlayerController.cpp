// Fill out your copyright notice in the Description page of Project Settings.


#include "CPP_PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "CPP_RosOrinPawn.h"
#include "Kismet/GameplayStatics.h"
#include "CPP_RosBridgeComponent.h"
#include "CPP_AICommandComponent.h"



ACPP_PlayerController::ACPP_PlayerController()
{
		}

void ACPP_PlayerController::BeginPlay()
{
	Super::BeginPlay();


	UE_LOG(LogTemp, Warning, TEXT("=== CPP_PlayerController BeginPlay ==="));

	APawn* MyPawn = GetPawn();

	if (MyPawn)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Controller is possessing: %s | Class: %s"),
			*MyPawn->GetName(),
			*MyPawn->GetClass()->GetName()
		);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				10.0f,
				FColor::Green,
				FString::Printf(
					TEXT("Possessing: %s"),
					*MyPawn->GetName()
				)
			);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Controller is NOT possessing any Pawn"));

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				10.0f,
				FColor::Red,
				TEXT("NO PAWN POSSESSED")
			);
		}
	}


	

	AActor* FoundRobot = UGameplayStatics::GetActorOfClass(
	   GetWorld(),
	   ACPP_RosOrinPawn::StaticClass()
   );

	// Find the VR / Camera pawn by tag
	TArray<AActor*> CameraActors;

	UGameplayStatics::GetAllActorsWithTag(
		GetWorld(),
		FName("CameraPawn"),
		CameraActors
	);

	if (CameraActors.Num() > 0)
	{
		CameraPawn = Cast<APawn>(CameraActors[0]);

		if (CameraPawn)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("FOUND CAMERA PAWN: %s"),
				*CameraPawn->GetName()
			);
		}
	}

	RobotPawn = Cast<ACPP_RosOrinPawn>(FoundRobot);

	if (RobotPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("Found RosOrinPawn successfully"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Could NOT find RosOrinPawn in the level"));
	}
	
}

void ACPP_PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(
	EKeys::W,
	IE_Pressed,
	this,
	&ACPP_PlayerController::DebugWPressed
);

	// AI BRIDGE: push-to-talk + emergency stop on the keyboard
	InputComponent->BindKey(
		PushToTalkKey,
		IE_Pressed,
		this,
		&ACPP_PlayerController::OnTalkPressed
	);

	InputComponent->BindKey(
		PushToTalkKey,
		IE_Released,
		this,
		&ACPP_PlayerController::OnTalkReleased
	);

	InputComponent->BindKey(
		AIStopKey,
		IE_Pressed,
		this,
		&ACPP_PlayerController::OnAIStopPressed
	);

	//
	// // only add IMCs for local player controllers
	// if (IsLocalPlayerController())
	// {
	// 	// // Add Input Mapping Contexts
	// 	// if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	// 	// {
	// 	// 	// for (UInputMappingContext* CurrentContext : RobotMappingContext)
	// 	// 	// {
	// 	// 	// 	Subsystem->AddMappingContext(CurrentContext, 0);
	// 	// 	// }
	// 	//
	// 	// 	// Add the IMC that you assigned in BP_PlayerController
	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
				GetLocalPlayer()))
		{
			if (RobotMappingContext)
			{
				Subsystem->AddMappingContext(
					RobotMappingContext,
					1
				);
				UE_LOG(
				   LogTemp,
				   Warning,
				   TEXT("Robot Mapping Context added")
			   );
			}
		}
	}

	// Bind the IA that you assigned in BP_PlayerController
	if (UEnhancedInputComponent* EnhancedInput =
		Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (RobotMoveXAction)
		{
			EnhancedInput->BindAction(
				RobotMoveXAction,
				ETriggerEvent::Triggered,
				this,
				&ACPP_PlayerController::HandleRobotMoveX
			);

			EnhancedInput->BindAction(
				RobotMoveXAction,
				ETriggerEvent::Completed,
				this,
				&ACPP_PlayerController::StopRobotMoveX
			);

			EnhancedInput->BindAction(
				RobotMoveXAction,
				ETriggerEvent::Canceled,
				this,
				&ACPP_PlayerController::StopRobotMoveX
			);
		}

		if (RobotMoveYAction)
		{
			EnhancedInput->BindAction(
				RobotMoveYAction,
				ETriggerEvent::Triggered,
				this,
				&ACPP_PlayerController::HandleRobotMoveY
			);

			EnhancedInput->BindAction(
				RobotMoveYAction,
				ETriggerEvent::Completed,
				this,
				&ACPP_PlayerController::StopRobotMoveY
			);

			EnhancedInput->BindAction(
				RobotMoveYAction,
				ETriggerEvent::Canceled,
				this,
				&ACPP_PlayerController::StopRobotMoveY
			);
		}

		// AI BRIDGE: optional VR controller buttons
		if (TalkAction)
		{
			EnhancedInput->BindAction(
				TalkAction,
				ETriggerEvent::Started,
				this,
				&ACPP_PlayerController::OnTalkPressed
			);

			EnhancedInput->BindAction(
				TalkAction,
				ETriggerEvent::Completed,
				this,
				&ACPP_PlayerController::OnTalkReleased
			);

			EnhancedInput->BindAction(
				TalkAction,
				ETriggerEvent::Canceled,
				this,
				&ACPP_PlayerController::OnTalkReleased
			);
		}

		if (AIStopAction)
		{
			EnhancedInput->BindAction(
				AIStopAction,
				ETriggerEvent::Started,
				this,
				&ACPP_PlayerController::OnAIStopPressed
			);
		}
	}

}

void ACPP_PlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(
	   LogTemp,
	   Warning,
	   TEXT("OnPossess called -> %s"),
	   InPawn ? *InPawn->GetName() : TEXT("NULL")
   );
}

void ACPP_PlayerController::HandleRobotMoveX(
	const FInputActionValue& Value)
{
	RobotMoveX = Value.Get<float>();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("Robot X: %.2f"),
		RobotMoveX
	);

	UpdateRobotMovement();
}

void ACPP_PlayerController::HandleRobotMoveY(
	const FInputActionValue& Value)
{
	RobotMoveY = Value.Get<float>();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("Robot Y: %.2f"),
		RobotMoveY
	);

	UpdateRobotMovement();
}

void ACPP_PlayerController::StopRobotMoveX(
	const FInputActionValue& Value)
{
	RobotMoveX = 0.0f;

	UpdateRobotMovement();
}

void ACPP_PlayerController::StopRobotMoveY(
	const FInputActionValue& Value)
{
	RobotMoveY = 0.0f;

	UpdateRobotMovement();
}

void ACPP_PlayerController::UpdateRobotMovement()
{
	if (!RobotPawn || !RobotPawn->RosBridge)
	{
		return;
	}

	// AI BRIDGE: manual driving always wins over an AI command
	if (RobotPawn->AICommand)
	{
		RobotPawn->AICommand->ManualOverride();
	}

	const float Strafe = RobotMoveX;
	const float Forward = RobotMoveY;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			1,
			0.1f,
			FColor::Green,
			FString::Printf(
				TEXT("Robot | X: %.2f | Y: %.2f"),
				Strafe,
				Forward
			)
		);
	}

	RobotPawn->RosBridge->SetRobotMovement(
		Forward,
		Strafe,
		0.0f
	);
}


void ACPP_PlayerController::DebugWPressed()
{
	UE_LOG(
		LogTemp,
		Error,
		TEXT("!!!!!!!! RAW W KEY RECEIVED BY PLAYER CONTROLLER !!!!!!!!")
	);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			3.0f,
			FColor::Red,
			TEXT("RAW W WORKING")
		);
	}
}

// ---------------------------------------------------------------------------
// AI BRIDGE
// ---------------------------------------------------------------------------

UCPP_AICommandComponent* ACPP_PlayerController::GetRobotAI() const
{
	return RobotPawn ? RobotPawn->AICommand : nullptr;
}

void ACPP_PlayerController::AskAI(const FString& Command)
{
	if (UCPP_AICommandComponent* RobotAI = GetRobotAI())
	{
		RobotAI->SendTextCommand(Command);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AskAI: no RosOrinPawn / AICommand component found"));
	}
}

void ACPP_PlayerController::OnTalkPressed()
{
	if (UCPP_AICommandComponent* RobotAI = GetRobotAI())
	{
		RobotAI->StartVoiceCapture();
	}
}

void ACPP_PlayerController::OnTalkReleased()
{
	if (UCPP_AICommandComponent* RobotAI = GetRobotAI())
	{
		RobotAI->StopVoiceCaptureAndSend();
	}
}

void ACPP_PlayerController::OnAIStopPressed()
{
	if (UCPP_AICommandComponent* RobotAI = GetRobotAI())
	{
		RobotAI->EmergencyStop();
	}
	else if (RobotPawn && RobotPawn->RosBridge)
	{
		RobotPawn->RosBridge->StopRobot();
	}
}
