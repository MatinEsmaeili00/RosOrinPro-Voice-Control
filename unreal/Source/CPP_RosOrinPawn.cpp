// Fill out your copyright notice in the Description page of Project Settings.


#include "CPP_RosOrinPawn.h"
#include "CPP_RosBridgeComponent.h"
#include "CPP_AICommandComponent.h"

// Sets default values
ACPP_RosOrinPawn::ACPP_RosOrinPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	RobotMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RobotMesh"));
	RobotMesh->SetupAttachment(Root);

	RosBridge = CreateDefaultSubobject<UCPP_RosBridgeComponent>(TEXT("RosBridge"));

	// AI BRIDGE
	AICommand = CreateDefaultSubobject<UCPP_AICommandComponent>(TEXT("AICommand"));

}

// Called when the game starts or when spawned
void ACPP_RosOrinPawn::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ACPP_RosOrinPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ACPP_RosOrinPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

