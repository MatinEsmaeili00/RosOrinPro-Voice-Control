// Fill out your copyright notice in the Description page of Project Settings.
// will cold
// WebSocket
// ConnectToRobot()
// SendVelocity()
// SubscribeToOdom()
// OnMessage()

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Runtime/Online/WebSockets/Public/WebSocketsModule.h"
#include "Runtime/Online/WebSockets/Public/IWebSocket.h"
#include "CPP_RosBridgeComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UCPP_RosBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UCPP_RosBridgeComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;



public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ROS")
	FString RobotIP = TEXT("130.39.95.23");

	UFUNCTION(BlueprintCallable, Category="ROS")
	void ConnectToRobot();

	UFUNCTION(BlueprintCallable, Category="ROS")
	void MoveForward();

	UFUNCTION(BlueprintCallable, Category="ROS")
	void MoveBackward();

	UFUNCTION(BlueprintCallable, Category="ROS")
	void StopRobot();

	UFUNCTION(BlueprintCallable, Category="ROS")
	void SetRobotMovement(float Forward, float Strafe, float Turn);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ROS|Movement")
	float LinearSpeed = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ROS|Movement")
	float TurnSpeed = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ROS|Movement")
	float DeadZone = 0.10f;


	UFUNCTION(BlueprintCallable, Category="ROS|Odom")
	void SubscribeToOdom();


	

private:

	TSharedPtr<IWebSocket> Socket;

	void SendVelocity(float X, float Y, float Rotation);

	void HandleRosMessage(const FString& Message);

	bool bHasOdomOrigin = false;

	double InitialRosX = 0.0;
	double InitialRosY = 0.0;
	double InitialRosZ = 0.0;

	FVector InitialUnrealLocation = FVector::ZeroVector;

	
};
