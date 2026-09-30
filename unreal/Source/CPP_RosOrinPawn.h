// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CPP_RosOrinPawn.generated.h"
class UCPP_RosBridgeComponent;
class UCPP_AICommandComponent;
UCLASS()

class ACPP_RosOrinPawn : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ACPP_RosOrinPawn();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;


public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Robot")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Robot")
	UStaticMeshComponent* RobotMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Robot")
	UCPP_RosBridgeComponent* RosBridge;

	// AI BRIDGE: talk to the robot (voice / text -> DGX Spark -> RosBridge)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Robot")
	UCPP_AICommandComponent* AICommand;

};
