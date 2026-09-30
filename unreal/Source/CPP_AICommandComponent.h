// Fill out your copyright notice in the Description page of Project Settings.
// AI voice / text control for the robot.
// Sends what you say (or type) to RosOrinPro_ai_bridge on the DGX Spark,
// gets back a command (move / stop) and drives the robot through the
// RosBridge component on the same actor.
//
// SendTextCommand()        -> POST /command_text
// StartVoiceCapture()      -> mic on (push-to-talk pressed)
// StopVoiceCaptureAndSend()-> mic off, POST /command_voice
// EmergencyStop()          -> stop now, ignore AI answers still on the way

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "AudioCaptureCore.h"
#include "CPP_AICommandComponent.generated.h"

class FJsonObject;
class UCPP_RosBridgeComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnRobotAIResponse,
	const FString&, Transcript,
	const FString&, Action,
	const FString&, SpokenResponse
);


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UCPP_AICommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCPP_AICommandComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	// RosOrinPro_ai_bridge server running on the DGX Spark
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
	FString AIServerURL = TEXT("http://130.39.94.33:8002");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
	float RequestTimeout = 30.0f;

	// Print what you said / what the AI answered on screen (desktop window, not the headset)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
	bool bShowOnScreenMessages = true;

	// A move without a time or distance ("move forward") runs until you say stop,
	// but never longer than this. 0 = no limit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Safety")
	float MaxMoveSeconds = 20.0f;

	// If the AI did not understand you while the robot is moving, stop the robot.
	// (A shouted "stop" that Whisper mishears still stops it.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Safety")
	bool bStopOnUnknownCommand = true;

	// The AI velocity is re-sent to /cmd_vel this often while moving,
	// so robots with a cmd_vel timeout keep moving.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Safety")
	float RepublishRateHz = 10.0f;

	// The server uses the ROS convention (+strafe = left, +turn = left / counter-clockwise).
	// Tick these if the real robot slides or turns the wrong way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Safety")
	bool bInvertStrafe = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Safety")
	bool bInvertTurn = false;

	// Recording stops and is sent automatically after this long
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Voice")
	float MaxRecordSeconds = 8.0f;

	// Shorter clips are ignored (accidental taps on the talk button)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI|Voice")
	float MinRecordSeconds = 0.3f;

	// Fires for every AI answer. Hook a widget or text-to-speech to this in Blueprint.
	UPROPERTY(BlueprintAssignable, Category="AI")
	FOnRobotAIResponse OnAIResponse;


	UFUNCTION(BlueprintCallable, Category="AI")
	void SendTextCommand(const FString& Text);

	UFUNCTION(BlueprintCallable, Category="AI|Voice")
	void StartVoiceCapture();

	UFUNCTION(BlueprintCallable, Category="AI|Voice")
	void StopVoiceCaptureAndSend();

	UFUNCTION(BlueprintPure, Category="AI|Voice")
	bool IsRecording() const { return bIsRecording; }

	// Stop the robot right now and ignore any AI answers still on the way.
	UFUNCTION(BlueprintCallable, Category="AI")
	void EmergencyStop();

	// Stop re-sending the AI velocity WITHOUT sending a stop.
	// Used when manual (keyboard) driving takes over.
	UFUNCTION(BlueprintCallable, Category="AI")
	void CancelAIMotion();

	// Manual driving took over: cancel the AI move AND drop AI answers still on
	// the way, so a slow "move" can't start fighting the keyboard.
	UFUNCTION(BlueprintCallable, Category="AI")
	void ManualOverride();

	UFUNCTION(BlueprintPure, Category="AI")
	bool IsExecutingAIMotion() const { return bAIMotionActive; }


private:

	UCPP_RosBridgeComponent* GetRosBridge() const;

	// HTTP
	void PostToServer(const FString& Path, const FString& ContentType, const TArray<uint8>& Body);

	void HandleHealthResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	void HandleServerResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 RequestId);

	void ApplyAIResult(const TSharedPtr<FJsonObject>& Result);

	FString GetServerBaseURL() const;

	// Every request gets a number. An answer older than the last one we acted on
	// is dropped, so a slow "move" can never override a newer "stop".
	int32 NextRequestId = 0;
	int32 LastAppliedRequestId = 0;

	// Motion
	void StartAIMotion(float Forward, float Strafe, float Turn, float DurationSeconds);
	void RepublishAIMotion();
	void FinishAIMotion();
	void StopRobotNow();

	bool bAIMotionActive = false;
	float AIForward = 0.0f;
	float AIStrafe = 0.0f;
	float AITurn = 0.0f;

	FTimerHandle RepublishTimer;
	FTimerHandle MotionEndTimer;

	// Voice
	void PollMicrophone();
	TArray<uint8> BuildWavFile(const TArray<float>& Samples, int32 NumChannels, int32 SampleRate) const;

	TUniquePtr<Audio::FAudioCaptureSynth> CaptureSynth;
	TArray<float> RecordedSamples;
	int32 CaptureChannels = 1;
	int32 CaptureSampleRate = 48000;
	bool bIsRecording = false;
	double RecordStartTime = 0.0;

	FTimerHandle MicPollTimer;
	FTimerHandle MaxRecordTimer;

	void ShowMessage(const FString& Text, const FColor& Color, float Seconds = 5.0f, int32 Key = -1) const;
};
