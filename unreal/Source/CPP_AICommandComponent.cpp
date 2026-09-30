// Fill out your copyright notice in the Description page of Project Settings.


#include "CPP_AICommandComponent.h"
#include "CPP_RosBridgeComponent.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	// "Listening..." / "Thinking..." / answer all replace each other on screen
	constexpr int32 AIStatusMessageKey = 4242;

	void AppendUtf8(TArray<uint8>& Out, const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
}


UCPP_AICommandComponent::UCPP_AICommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}


void UCPP_AICommandComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetRosBridge())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("AICommand: no CPP_RosBridgeComponent on %s, AI cannot move the robot"),
			*GetNameSafe(GetOwner())
		);
	}

	// Quick check so a wrong IP / server not running shows up right away
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
		FHttpModule::Get().CreateRequest();

	Request->SetURL(GetServerBaseURL() + TEXT("/health"));
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(5.0f);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&UCPP_AICommandComponent::HandleHealthResponse
	);
	Request->ProcessRequest();
}


void UCPP_AICommandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Close the mic stream (and its audio thread) BEFORE destroying the synth,
	// the same way UAudioCaptureComponent does it
	if (CaptureSynth.IsValid() && CaptureSynth->IsStreamOpen())
	{
		CaptureSynth->AbortCapturing();
	}

	bIsRecording = false;
	CaptureSynth.Reset();

	// Answers still on their way must not move the robot after this
	LastAppliedRequestId = NextRequestId;

	// Never leave the real robot driving when the game stops
	StopRobotNow();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	Super::EndPlay(EndPlayReason);
}


UCPP_RosBridgeComponent* UCPP_AICommandComponent::GetRosBridge() const
{
	return GetOwner()
		? GetOwner()->FindComponentByClass<UCPP_RosBridgeComponent>()
		: nullptr;
}


FString UCPP_AICommandComponent::GetServerBaseURL() const
{
	FString BaseURL = AIServerURL.TrimStartAndEnd();
	BaseURL.RemoveFromEnd(TEXT("/"));
	return BaseURL;
}


// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void UCPP_AICommandComponent::SendTextCommand(const FString& Text)
{
	const FString CleanText = Text.TrimStartAndEnd();

	if (CleanText.IsEmpty())
	{
		return;
	}

	const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("text"), CleanText);

	FString JsonString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObject, Writer);

	TArray<uint8> Body;
	AppendUtf8(Body, JsonString);

	UE_LOG(LogTemp, Warning, TEXT("AI TEXT COMMAND: %s"), *CleanText);

	// Before posting: a request that fails instantly must not be hidden behind "Thinking..."
	ShowMessage(TEXT("Thinking..."), FColor::Cyan, RequestTimeout, AIStatusMessageKey);

	PostToServer(TEXT("/command_text"), TEXT("application/json"), Body);
}


// ---------------------------------------------------------------------------
// Voice (push-to-talk)
// ---------------------------------------------------------------------------

void UCPP_AICommandComponent::StartVoiceCapture()
{
	if (bIsRecording || !GetWorld())
	{
		return;
	}

	if (!CaptureSynth.IsValid())
	{
		CaptureSynth = MakeUnique<Audio::FAudioCaptureSynth>();
	}

	// The stream is opened on every press and closed on release (same pattern as
	// UAudioCaptureComponent), so switching the Windows default mic just works.
	if (!CaptureSynth->IsStreamOpen())
	{
		Audio::FCaptureDeviceInfo DeviceInfo;

		// Without the real channel count / sample rate the WAV would be garbled
		if (!CaptureSynth->GetDefaultCaptureDeviceInfo(DeviceInfo)
			|| DeviceInfo.InputChannels <= 0
			|| DeviceInfo.PreferredSampleRate <= 0)
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("AI MIC: no default recording device. Enable the Audio Capture plugin and check the Windows default recording device.")
			);

			ShowMessage(TEXT("MICROPHONE NOT AVAILABLE"), FColor::Red, 5.0f, AIStatusMessageKey);
			return;
		}

		CaptureChannels = DeviceInfo.InputChannels;
		CaptureSampleRate = DeviceInfo.PreferredSampleRate;

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("AI MIC: %s | %d channels | %d Hz"),
			*DeviceInfo.DeviceName,
			CaptureChannels,
			CaptureSampleRate
		);

		if (!CaptureSynth->OpenDefaultStream())
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("AI MIC: could not open the microphone. Enable the Audio Capture plugin and check the Windows default recording device.")
			);

			ShowMessage(TEXT("MICROPHONE NOT AVAILABLE"), FColor::Red, 5.0f, AIStatusMessageKey);
			return;
		}
	}

	RecordedSamples.Reset();

	if (!CaptureSynth->StartCapturing())
	{
		UE_LOG(LogTemp, Error, TEXT("AI MIC: could not start capturing"));
		CaptureSynth->AbortCapturing();
		return;
	}

	bIsRecording = true;
	RecordStartTime = FPlatformTime::Seconds();

	FTimerManager& Timers = GetWorld()->GetTimerManager();

	Timers.SetTimer(
		MicPollTimer,
		this,
		&UCPP_AICommandComponent::PollMicrophone,
		0.05f,
		true
	);

	if (MaxRecordSeconds > 0.0f)
	{
		Timers.SetTimer(
			MaxRecordTimer,
			this,
			&UCPP_AICommandComponent::StopVoiceCaptureAndSend,
			MaxRecordSeconds,
			false
		);
	}

	ShowMessage(
		TEXT("Listening..."),
		FColor::Cyan,
		MaxRecordSeconds > 0.0f ? MaxRecordSeconds : 60.0f,
		AIStatusMessageKey
	);
}


void UCPP_AICommandComponent::PollMicrophone()
{
	if (!CaptureSynth.IsValid())
	{
		return;
	}

	TArray<float> NewSamples;

	if (CaptureSynth->GetAudioData(NewSamples))
	{
		RecordedSamples.Append(NewSamples);
	}
}


void UCPP_AICommandComponent::StopVoiceCaptureAndSend()
{
	if (!bIsRecording)
	{
		return;
	}

	bIsRecording = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MicPollTimer);
		World->GetTimerManager().ClearTimer(MaxRecordTimer);
	}

	// Grab whatever arrived since the last poll, then close the mic
	PollMicrophone();

	if (CaptureSynth.IsValid() && CaptureSynth->IsStreamOpen())
	{
		CaptureSynth->AbortCapturing();
	}

	const double RecordedSeconds = FPlatformTime::Seconds() - RecordStartTime;

	if (RecordedSeconds < MinRecordSeconds || RecordedSamples.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("AI MIC: clip too short (%.2fs), hold the talk key while speaking"), RecordedSeconds);
		ShowMessage(TEXT("Hold the talk key while you speak"), FColor::Yellow, 3.0f, AIStatusMessageKey);
		RecordedSamples.Reset();
		return;
	}

	const TArray<uint8> WavFile = BuildWavFile(RecordedSamples, CaptureChannels, CaptureSampleRate);
	RecordedSamples.Reset();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("AI MIC: sending %.2fs of audio (%d bytes)"),
		RecordedSeconds,
		WavFile.Num()
	);

	// multipart/form-data with one field called "file", like the syngenta bridge
	const FString Boundary = TEXT("RosOrinAI") + FGuid::NewGuid().ToString(EGuidFormats::Digits);

	TArray<uint8> Body;
	AppendUtf8(
		Body,
		FString::Printf(
			TEXT("--%s\r\n")
			TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"command.wav\"\r\n")
			TEXT("Content-Type: audio/wav\r\n\r\n"),
			*Boundary
		)
	);
	Body.Append(WavFile);
	AppendUtf8(Body, FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

	ShowMessage(TEXT("Thinking..."), FColor::Cyan, RequestTimeout, AIStatusMessageKey);

	PostToServer(
		TEXT("/command_voice"),
		FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary),
		Body
	);
}


TArray<uint8> UCPP_AICommandComponent::BuildWavFile(
	const TArray<float>& Samples,
	int32 NumChannels,
	int32 SampleRate) const
{
	NumChannels = FMath::Max(1, NumChannels);
	const int32 NumFrames = Samples.Num() / NumChannels;

	// Mix down to mono (all Whisper needs)
	TArray<float> Mono;
	Mono.SetNumUninitialized(NumFrames);

	float Peak = 0.0f;

	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float Sum = 0.0f;

		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Sum += Samples[Frame * NumChannels + Channel];
		}

		Mono[Frame] = Sum / static_cast<float>(NumChannels);
		Peak = FMath::Max(Peak, FMath::Abs(Mono[Frame]));
	}

	// Headset mics are often very quiet: boost up to 8x
	const float Gain = (Peak > 0.001f && Peak < 0.5f)
		? FMath::Min(0.9f / Peak, 8.0f)
		: 1.0f;

	// 16-bit PCM WAV
	const uint32 DataBytes = static_cast<uint32>(NumFrames) * 2u;

	TArray<uint8> Wav;
	Wav.Reserve(44 + DataBytes);

	auto WriteTag = [&Wav](const char* Tag)
	{
		Wav.Append(reinterpret_cast<const uint8*>(Tag), 4);
	};

	auto Write32 = [&Wav](uint32 Value)
	{
		Wav.Append(reinterpret_cast<const uint8*>(&Value), 4);
	};

	auto Write16 = [&Wav](uint16 Value)
	{
		Wav.Append(reinterpret_cast<const uint8*>(&Value), 2);
	};

	WriteTag("RIFF");
	Write32(36 + DataBytes);
	WriteTag("WAVE");

	WriteTag("fmt ");
	Write32(16);              // fmt chunk size
	Write16(1);               // PCM
	Write16(1);               // mono
	Write32(static_cast<uint32>(SampleRate));
	Write32(static_cast<uint32>(SampleRate) * 2u);  // bytes per second
	Write16(2);               // bytes per frame
	Write16(16);              // bits per sample

	WriteTag("data");
	Write32(DataBytes);

	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		const float Value = FMath::Clamp(Mono[Frame] * Gain, -1.0f, 1.0f);
		Write16(static_cast<uint16>(static_cast<int16>(Value * 32767.0f)));
	}

	return Wav;
}


// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void UCPP_AICommandComponent::PostToServer(
	const FString& Path,
	const FString& ContentType,
	const TArray<uint8>& Body)
{
	const int32 RequestId = ++NextRequestId;

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
		FHttpModule::Get().CreateRequest();

	Request->SetURL(GetServerBaseURL() + Path);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), ContentType);
	Request->SetContent(Body);
	Request->SetTimeout(RequestTimeout);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&UCPP_AICommandComponent::HandleServerResponse,
		RequestId
	);
	Request->ProcessRequest();
}


void UCPP_AICommandComponent::HandleHealthResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully)
{
	if (bConnectedSuccessfully && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		UE_LOG(LogTemp, Warning, TEXT("AI BRIDGE READY at %s"), *GetServerBaseURL());
		return;
	}

	UE_LOG(
		LogTemp,
		Error,
		TEXT("AI BRIDGE NOT REACHABLE at %s (is ./run.sh running on the DGX Spark?)"),
		*GetServerBaseURL()
	);

	ShowMessage(TEXT("AI BRIDGE NOT REACHABLE"), FColor::Red, 10.0f);
}


void UCPP_AICommandComponent::HandleServerResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	int32 RequestId)
{
	if (!HasBegunPlay())
	{
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AI BRIDGE: request failed, is the server running at %s ?"), *GetServerBaseURL());
		ShowMessage(TEXT("AI server not reachable"), FColor::Red, 5.0f, AIStatusMessageKey);
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("AI BRIDGE: HTTP %d: %s"),
			Response->GetResponseCode(),
			*Response->GetContentAsString()
		);
		ShowMessage(TEXT("AI server error"), FColor::Red, 5.0f, AIStatusMessageKey);
		return;
	}

	TSharedPtr<FJsonObject> Result;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Response->GetContentAsString());

	if (!FJsonSerializer::Deserialize(Reader, Result) || !Result.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AI BRIDGE: bad JSON: %s"), *Response->GetContentAsString());
		return;
	}

	// A newer command (or the emergency stop) already won
	if (RequestId <= LastAppliedRequestId)
	{
		UE_LOG(LogTemp, Warning, TEXT("AI BRIDGE: ignoring old answer #%d"), RequestId);
		return;
	}

	LastAppliedRequestId = RequestId;

	ApplyAIResult(Result);
}


void UCPP_AICommandComponent::ApplyAIResult(const TSharedPtr<FJsonObject>& Result)
{
	FString Transcript;
	FString Action;
	FString Command;
	FString SpokenResponse;

	Result->TryGetStringField(TEXT("transcript"), Transcript);
	Result->TryGetStringField(TEXT("action"), Action);
	Result->TryGetStringField(TEXT("command"), Command);
	Result->TryGetStringField(TEXT("spoken_response"), SpokenResponse);

	double Forward = 0.0;
	double Strafe = 0.0;
	double Turn = 0.0;
	double DurationSeconds = 0.0;
	double DistanceMeters = 0.0;
	double AngleDegrees = 0.0;

	Result->TryGetNumberField(TEXT("forward"), Forward);
	Result->TryGetNumberField(TEXT("strafe"), Strafe);
	Result->TryGetNumberField(TEXT("turn"), Turn);
	Result->TryGetNumberField(TEXT("duration_s"), DurationSeconds);
	Result->TryGetNumberField(TEXT("distance_m"), DistanceMeters);
	Result->TryGetNumberField(TEXT("angle_deg"), AngleDegrees);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("AI | heard: \"%s\" | %s %s | F: %.2f S: %.2f T: %.2f | %.1fs %.2fm %.0fdeg | \"%s\""),
		*Transcript,
		*Action,
		*Command,
		Forward,
		Strafe,
		Turn,
		DurationSeconds,
		DistanceMeters,
		AngleDegrees,
		*SpokenResponse
	);

	if (!Transcript.IsEmpty())
	{
		ShowMessage(FString::Printf(TEXT("You: %s"), *Transcript), FColor::White);
	}

	ShowMessage(
		FString::Printf(TEXT("AI: %s"), *SpokenResponse),
		Action == TEXT("unknown") ? FColor::Yellow : FColor::Green,
		5.0f,
		AIStatusMessageKey
	);

	if (Action == TEXT("stop"))
	{
		StopRobotNow();
	}
	else if (Action == TEXT("move"))
	{
		UCPP_RosBridgeComponent* RosBridge = GetRosBridge();

		if (bInvertStrafe)
		{
			Strafe = -Strafe;
		}

		if (bInvertTurn)
		{
			Turn = -Turn;
		}

		// Same clamp SetRobotMovement applies, so the time math below matches what the robot does
		Forward = FMath::Clamp(Forward, -1.0, 1.0);
		Strafe = FMath::Clamp(Strafe, -1.0, 1.0);
		Turn = FMath::Clamp(Turn, -1.0, 1.0);

		const double Strongest = FMath::Max3(FMath::Abs(Forward), FMath::Abs(Strafe), FMath::Abs(Turn));

		if (RosBridge && Strongest < RosBridge->DeadZone)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("AI: move is below the RosBridge DeadZone (%.2f < %.2f), robot would not move -> stopping"),
				Strongest,
				RosBridge->DeadZone
			);

			StopRobotNow();
			OnAIResponse.Broadcast(Transcript, Action, SpokenResponse);
			return;
		}

		// Distances and angles are turned into time using the speeds set on
		// the RosBridge component, so the speed limits stay in Unreal.
		double Seconds = DurationSeconds;

		const double LinearAmount = FMath::Max(FMath::Abs(Forward), FMath::Abs(Strafe));

		if (RosBridge && DistanceMeters > 0.0 && LinearAmount > 0.0 && RosBridge->LinearSpeed > 0.0f)
		{
			Seconds = DistanceMeters / (RosBridge->LinearSpeed * LinearAmount);
		}

		if (RosBridge && AngleDegrees > 0.0 && FMath::Abs(Turn) > 0.0 && RosBridge->TurnSpeed > 0.0f)
		{
			Seconds = FMath::DegreesToRadians(AngleDegrees) / (RosBridge->TurnSpeed * FMath::Abs(Turn));
		}

		if (MaxMoveSeconds > 0.0f && (Seconds <= 0.0 || Seconds > MaxMoveSeconds))
		{
			if (Seconds > MaxMoveSeconds)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("AI: move needs %.1fs, capped to MaxMoveSeconds (%.1fs)"),
					Seconds,
					MaxMoveSeconds
				);
			}

			Seconds = MaxMoveSeconds;
		}

		StartAIMotion(
			static_cast<float>(Forward),
			static_cast<float>(Strafe),
			static_cast<float>(Turn),
			static_cast<float>(Seconds)
		);
	}
	else if (bStopOnUnknownCommand && bAIMotionActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("AI: did not understand while moving -> stopping to be safe"));
		StopRobotNow();
	}

	OnAIResponse.Broadcast(Transcript, Action, SpokenResponse);
}


// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------

void UCPP_AICommandComponent::StartAIMotion(
	float Forward,
	float Strafe,
	float Turn,
	float DurationSeconds)
{
	UCPP_RosBridgeComponent* RosBridge = GetRosBridge();

	if (!RosBridge || !GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("AI: cannot move, no RosBridge component"));
		return;
	}

	CancelAIMotion();

	AIForward = Forward;
	AIStrafe = Strafe;
	AITurn = Turn;
	bAIMotionActive = true;

	RosBridge->SetRobotMovement(AIForward, AIStrafe, AITurn);

	FTimerManager& Timers = GetWorld()->GetTimerManager();

	if (RepublishRateHz > 0.0f)
	{
		Timers.SetTimer(
			RepublishTimer,
			this,
			&UCPP_AICommandComponent::RepublishAIMotion,
			1.0f / RepublishRateHz,
			true
		);
	}

	if (DurationSeconds > 0.0f)
	{
		Timers.SetTimer(
			MotionEndTimer,
			this,
			&UCPP_AICommandComponent::FinishAIMotion,
			DurationSeconds,
			false
		);
	}
}


void UCPP_AICommandComponent::RepublishAIMotion()
{
	if (!bAIMotionActive)
	{
		return;
	}

	if (UCPP_RosBridgeComponent* RosBridge = GetRosBridge())
	{
		RosBridge->SetRobotMovement(AIForward, AIStrafe, AITurn);
	}
}


void UCPP_AICommandComponent::FinishAIMotion()
{
	StopRobotNow();

	UE_LOG(LogTemp, Warning, TEXT("AI: move finished"));
}


void UCPP_AICommandComponent::StopRobotNow()
{
	CancelAIMotion();

	if (UCPP_RosBridgeComponent* RosBridge = GetRosBridge())
	{
		RosBridge->StopRobot();
	}
}


void UCPP_AICommandComponent::CancelAIMotion()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RepublishTimer);
		World->GetTimerManager().ClearTimer(MotionEndTimer);
	}

	bAIMotionActive = false;
	AIForward = 0.0f;
	AIStrafe = 0.0f;
	AITurn = 0.0f;
}


void UCPP_AICommandComponent::ManualOverride()
{
	LastAppliedRequestId = NextRequestId;

	CancelAIMotion();
}


void UCPP_AICommandComponent::EmergencyStop()
{
	// Anything still on its way from the server is now out of date
	LastAppliedRequestId = NextRequestId;

	StopRobotNow();

	UE_LOG(LogTemp, Warning, TEXT("AI: EMERGENCY STOP"));
	ShowMessage(TEXT("STOP"), FColor::Red, 3.0f, AIStatusMessageKey);
}


void UCPP_AICommandComponent::ShowMessage(
	const FString& Text,
	const FColor& Color,
	float Seconds,
	int32 Key) const
{
	if (bShowOnScreenMessages && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Seconds, Color, Text);
	}
}
