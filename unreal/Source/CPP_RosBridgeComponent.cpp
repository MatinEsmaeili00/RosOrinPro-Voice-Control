// Fill out your copyright notice in the Description page of Project Settings.


#include "CPP_RosOrinPawn.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Async/Async.h"

#include "CPP_RosBridgeComponent.h"

// Sets default values for this component's properties
UCPP_RosBridgeComponent::UCPP_RosBridgeComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void UCPP_RosBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	ConnectToRobot();
	
	// ...
	
}


// Called every frame
void UCPP_RosBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}


void UCPP_RosBridgeComponent::ConnectToRobot()
{
	const FString URL =
		FString::Printf(TEXT("ws://%s:9090/?x=1"), *RobotIP);

	UE_LOG(LogTemp, Warning, TEXT("Connecting to: %s"), *URL);

	Socket =
		FWebSocketsModule::Get().CreateWebSocket(URL);

	// Socket->OnConnected().AddLambda([]()
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("CONNECTED TO ROBOT ROSBRIDGE"));
	// });

	Socket->OnConnected().AddLambda([this]()
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("CONNECTED TO ROBOT ROSBRIDGE")
	);

	SubscribeToOdom();
});
	

	Socket->OnConnectionError().AddLambda(
		[](const FString& Error)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("ROSBRIDGE CONNECTION ERROR: %s"),
				*Error
			);
		}
	);


	Socket->OnMessage().AddLambda(
	[this](const FString& Message)
	{
		HandleRosMessage(Message);
	}
);
	Socket->Connect();
}


void UCPP_RosBridgeComponent::MoveForward()
{
	SendVelocity(0.05f, 0.0f, 0.0f);
}

void UCPP_RosBridgeComponent::MoveBackward()
{
	SendVelocity(-0.05f, 0.0f, 0.0f);
}

void UCPP_RosBridgeComponent::StopRobot()
{
	SendVelocity(0.0f, 0.0f, 0.0f);
}

void UCPP_RosBridgeComponent::SendVelocity(
	float X,
	float Y,
	float Rotation)
{
	if (!Socket.IsValid() || !Socket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("Robot WebSocket is NOT connected!"));
		return;
	}

	const FString Message = FString::Printf(
		TEXT(
			"{"
				"\"op\":\"publish\","
				"\"topic\":\"/cmd_vel\","
				"\"msg\":{"
					"\"linear\":{"
						"\"x\":%.3f,"
						"\"y\":%.3f,"
						"\"z\":0.0"
					"},"
					"\"angular\":{"
						"\"x\":0.0,"
						"\"y\":0.0,"
						"\"z\":%.3f"
					"}"
				"}"
			"}"
		),
		X,
		Y,
		Rotation
	);

	Socket->Send(Message);

	UE_LOG(LogTemp, Warning, TEXT("Sent ROS command: %s"), *Message);
}


void UCPP_RosBridgeComponent::SetRobotMovement(
	float Forward,
	float Strafe,
	float Turn)
{
	Forward = FMath::Clamp(Forward, -1.0f, 1.0f);
	Strafe  = FMath::Clamp(Strafe,  -1.0f, 1.0f);
	Turn    = FMath::Clamp(Turn,    -1.0f, 1.0f);

	if (FMath::Abs(Forward) < DeadZone)
		Forward = 0.0f;

	if (FMath::Abs(Strafe) < DeadZone)
		Strafe = 0.0f;

	if (FMath::Abs(Turn) < DeadZone)
		Turn = 0.0f;

	SendVelocity(
		Forward * LinearSpeed,
		Strafe  * LinearSpeed,
		Turn    * TurnSpeed
	);
}

// implement SubscribeToOdom()
void UCPP_RosBridgeComponent::SubscribeToOdom()
{
	if (!Socket.IsValid() || !Socket->IsConnected())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Cannot subscribe to /odom: WebSocket not connected")
		);

		return;
	}

	const FString Message =
		TEXT(
			"{"
				"\"op\":\"subscribe\","
				"\"topic\":\"/odom\","
				"\"type\":\"nav_msgs/msg/Odometry\""
			"}"
		);

	Socket->Send(Message);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("Subscribed to /odom")
	);
}

void UCPP_RosBridgeComponent::HandleRosMessage(
	const FString& Message)
{
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Message);

	if (!FJsonSerializer::Deserialize(Reader, RootObject)
		|| !RootObject.IsValid())
	{
		return;
	}

	FString Topic;

	if (!RootObject->TryGetStringField(TEXT("topic"), Topic))
	{
		return;
	}

	// Ignore every ROS message except /odom.
	if (Topic != TEXT("/odom"))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* MsgObject;

	if (!RootObject->TryGetObjectField(TEXT("msg"), MsgObject))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PoseContainer;

	if (!(*MsgObject)->TryGetObjectField(
			TEXT("pose"),
			PoseContainer))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PoseObject;

	if (!(*PoseContainer)->TryGetObjectField(
			TEXT("pose"),
			PoseObject))
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PositionObject;

	if (!(*PoseObject)->TryGetObjectField(
			TEXT("position"),
			PositionObject))
	{
		return;
	}

	const double RosX =
		(*PositionObject)->GetNumberField(TEXT("x"));

	const double RosY =
		(*PositionObject)->GetNumberField(TEXT("y"));

	const double RosZ =
		(*PositionObject)->GetNumberField(TEXT("z"));

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("ODOM -> X: %.3f Y: %.3f Z: %.3f"),
		RosX,
		RosY,
		RosZ
	);


	ACPP_RosOrinPawn* RobotPawn =
	Cast<ACPP_RosOrinPawn>(GetOwner());

	if (!RobotPawn)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("RosBridge owner is NOT RosOrinPawn")
		);

		return;
	}

	if (!bHasOdomOrigin)
	{
		InitialRosX = RosX;
		InitialRosY = RosY;
		InitialRosZ = RosZ;

		InitialUnrealLocation =
			RobotPawn->GetActorLocation();

		bHasOdomOrigin = true;

		UE_LOG(
			LogTemp,
			Warning,
			TEXT(
				"ODOM ORIGIN SAVED | ROS: %.3f %.3f %.3f | Unreal: %s"
			),
			InitialRosX,
			InitialRosY,
			InitialRosZ,
			*InitialUnrealLocation.ToString()
		);

		return;
	}

	const double DeltaRosX =
	RosX - InitialRosX;

	const double DeltaRosY =
		RosY - InitialRosY;

	const FVector NewLocation =
	InitialUnrealLocation +
	FVector(
		DeltaRosX * 100.0,
		-DeltaRosY * 100.0,
		0.0
	);

	AsyncTask(
	ENamedThreads::GameThread,
	[RobotPawn, NewLocation]()
	{
		if (IsValid(RobotPawn))
		{
			RobotPawn->SetActorLocation(
				NewLocation
			);
		}
	}
);
}

