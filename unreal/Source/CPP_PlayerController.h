// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "GameFramework/PlayerController.h"
#include "CPP_PlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class ACPP_RosOrinPawn;
class UCPP_AICommandComponent;


/**
 * 
 */
UCLASS()
class ACPP_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACPP_PlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

public:

	
	// /** Input Mapping Contexts */
	// UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	// TArray<UInputMappingContext*> RobotMappingContext;
	//
	// /** Input Mapping Contexts */
	// UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	// TArray<UInputAction*> RobotMoveAction;
	//
	//
	// UPROPERTY(BlueprintReadOnly, Category="Robot")
	// TArray<ACPP_RosOrinPawn*> RobotPawn;

	// Assigned from BP_PlayerController
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|Robot")
	UInputMappingContext* RobotMappingContext;

	// // Assigned from BP_PlayerController
	// UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|Robot")
	// UInputAction* RobotMoveAction;

	// We will give this reference from Blueprint/runtime
	UPROPERTY(BlueprintReadWrite, Category="Robot")
	ACPP_RosOrinPawn* RobotPawn;

	UPROPERTY(BlueprintReadWrite, Category="Robot")
	APawn* CameraPawn;


	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|Robot")
	UInputAction* RobotMoveXAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|Robot")
	UInputAction* RobotMoveYAction;


	// AI BRIDGE: hold to talk to the robot, release to send
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|AI")
	FKey PushToTalkKey = EKeys::T;

	// AI BRIDGE: stops the robot immediately, without waiting for the AI
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|AI")
	FKey AIStopKey = EKeys::X;

	// AI BRIDGE (optional): Enhanced Input action for a VR controller talk button (hold = talk).
	// Add it to RobotMappingContext too.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|AI")
	UInputAction* TalkAction = nullptr;

	// AI BRIDGE (optional): Enhanced Input action for a VR controller stop button
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input|AI")
	UInputAction* AIStopAction = nullptr;

	// AI BRIDGE: type in the console (~):  AskAI "move forward for 2 seconds"
	UFUNCTION(Exec, BlueprintCallable, Category="AI")
	void AskAI(const FString& Command);



private:
	float RobotMoveX = 0.0f;
	float RobotMoveY = 0.0f;

	void HandleRobotMoveX(const FInputActionValue& Value);
	void HandleRobotMoveY(const FInputActionValue& Value);

	void StopRobotMoveX(const FInputActionValue& Value);
	void StopRobotMoveY(const FInputActionValue& Value);

	void UpdateRobotMovement();
	

	void DebugWPressed();

	// AI BRIDGE
	void OnTalkPressed();
	void OnTalkReleased();
	void OnAIStopPressed();
	UCPP_AICommandComponent* GetRobotAI() const;

};
