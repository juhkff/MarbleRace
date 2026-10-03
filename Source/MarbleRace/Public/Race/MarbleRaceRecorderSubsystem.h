#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Async/Future.h"
#include "MarbleRaceRecorderSubsystem.generated.h"

class FViewport;
struct FMarbleRecordingSession;
struct FMarbleRecordingAudioCapture;

/** Captures the game viewport and game audio; the desktop and microphone are never inputs. */
UCLASS()
class MARBLERACE_API UMarbleRaceRecorderSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Deinitialize() override;
	bool StartRecording();
	void StopRecording();
	/** Discard this race only, without waiting for frame writes on the game thread. */
	void DiscardRecording();
	bool IsRecording() const;
	/** Readiness only: never blocks the game thread while the encoder is running. */
	int32 GetPendingSaveCount() const;
	FString GetStatus() const { return Status; }
	static FString GetRecordingsDirectory();
	static FString GetEncoderPath();
private:
	friend class FMarbleRecordingEncodingTest;
	friend class FMarbleExitRecordingTest;
	friend class FMarbleRecordingDiscardTest;
	void CaptureFrame(FViewport* Viewport);
	TSharedPtr<FMarbleRecordingSession, ESPMode::ThreadSafe> Session;
	FDelegateHandle CaptureHandle;
	FString Status;
	TSharedPtr<FMarbleRecordingAudioCapture, ESPMode::ThreadSafe> AudioCapture;
	TArray<TFuture<void>> Finalizations;
};
