#include "Race/MarbleRaceRecorderSubsystem.h"
#include "Async/Async.h"
#include "AudioDeviceManager.h"
#include "AudioMixerDevice.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeCounter.h"
#include "ImageUtils.h"
#include "ImageCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopeLock.h"
#include "ISubmixBufferListener.h"
#include "SceneView.h"
#include "Serialization/BufferArchive.h"
#include "UnrealClient.h"

struct FMarbleRecordingSession
{
	FString Directory, Output;
	TArray<double> FrameTimes;
	FIntPoint OutputSize = FIntPoint::ZeroValue;
	FThreadSafeCounter PendingFrames;
	TAtomic<bool> bFrameWriteFailed{false};
	TAtomic<bool> bDiscard{false};
	TAtomic<bool> bEncoding{false};
	double StartedAt = 0.0;
	double LastFrameAt = -1.0;
};

struct FMarbleRecordingAudioPacket
{
	double Time = 0.0;
	TArray<float> Samples;
};

/** The listener timestamps the actual buffers, rather than assuming an asynchronous
 * StartRecording command took effect at the instant the game thread sent it. */
struct FMarbleRecordingAudioCapture : ISubmixBufferListener
{
	explicit FMarbleRecordingAudioCapture(double InStartedAt) : StartedAt(InStartedAt) {}
	FCriticalSection Mutex;
	TArray<FMarbleRecordingAudioPacket> Packets;
	const double StartedAt;
	int32 Channels = 0, Rate = 0;
	bool bActive = true;
	virtual bool IsRenderingAudio() const override { return true; }
	virtual const FString& GetListenerName() const override
	{
		static const FString Name(TEXT("MarbleRaceRecording"));
		return Name;
	}
	virtual void OnNewSubmixBuffer(const USoundSubmix*, float* Data, int32 NumSamples,
		int32 NumChannels, const int32 SampleRate, double AudioClock) override
	{
		const double Time = FPlatformTime::Seconds() - StartedAt;
		FScopeLock Lock(&Mutex);
		if (!bActive || !Data || NumSamples <= 0 || NumChannels <= 0 || SampleRate <= 0) return;
		if (Packets.IsEmpty()) { Channels = NumChannels; Rate = SampleRate; }
		if (Channels != NumChannels || Rate != SampleRate) return;
		auto& Packet = Packets.AddDefaulted_GetRef();
		Packet.Time = Time;
		Packet.Samples.Append(Data, NumSamples);
	}
	TArray<FMarbleRecordingAudioPacket> Stop()
	{
		FScopeLock Lock(&Mutex);
		bActive = false;
		return MoveTemp(Packets);
	}
};

namespace
{
	void CleanupRecording(const TSharedRef<FMarbleRecordingSession, ESPMode::ThreadSafe>& Capture, bool bRemoveOutput)
	{
		// Every path is owned by this unique session; never recursively delete the
		// recordings folder or touch earlier completed recordings.
		for (int32 Index = 0; Index < Capture->FrameTimes.Num(); ++Index)
			IFileManager::Get().Delete(*(Capture->Directory / FString::Printf(TEXT("frame%06d.jpg"), Index)));
		for (const TCHAR* Name : {TEXT("audio.wav"), TEXT("frames.txt"), TEXT("encoder.log")})
			IFileManager::Get().Delete(*(Capture->Directory / Name));
		IFileManager::Get().DeleteDirectory(*Capture->Directory, false, false);
		if (bRemoveOutput) IFileManager::Get().Delete(*Capture->Output);
	}

	FIntRect RecordingRect(const FIntPoint& ViewportSize, const FIntRect& CameraRect)
	{
		FIntRect Rect = CameraRect;
		Rect.Clip(FIntRect(FIntPoint::ZeroValue, ViewportSize));
		return Rect.Width() >= 2 && Rect.Height() >= 2 ? Rect : FIntRect(FIntPoint::ZeroValue, ViewportSize);
	}

	FIntPoint RecordingSize(const FIntPoint& Size)
	{
		const double Scale = FMath::Min(1.0, FMath::Min(1920.0 / Size.X, 1080.0 / Size.Y));
		return FIntPoint(FMath::Max(2, FMath::FloorToInt(Size.X * Scale) / 2 * 2),
			FMath::Max(2, FMath::FloorToInt(Size.Y * Scale) / 2 * 2));
	}

	bool IsRecordingViewReady(const FIntRect& Rect, double CameraAspectRatio)
	{
		return Rect.Height() > 0 && CameraAspectRatio > 0. &&
			FMath::Abs(Rect.Width() / static_cast<double>(Rect.Height()) - CameraAspectRatio) < .005;
	}

	TArray<float> BuildAudioTimeline(const TArray<FMarbleRecordingAudioPacket>& Packets,
		int32 Channels, int32 Rate, double EndTime)
	{
		TArray<float> Audio;
		if (Packets.IsEmpty() || Channels <= 0 || Rate <= 0) return Audio;
		const int64 TotalFrames = FMath::CeilToInt64(EndTime * Rate);
		Audio.SetNumZeroed(TotalFrames * Channels);
		int64 Cursor = FMath::RoundToInt64(Packets[0].Time * Rate);
		for (const auto& Packet : Packets)
		{
			const int64 Target = FMath::RoundToInt64(Packet.Time * Rate);
			// Buffer scheduling jitters and sometimes delivers several buffers in a
			// burst. Keep those contiguous; real stalls/gaps must retain their place
			// on the same monotonic clock as the video, rather than shifting all BGM.
			const int64 Tolerance = FMath::Max<int64>(Rate / 10, 3 * Packet.Samples.Num() / Channels);
			if (FMath::Abs(Target - Cursor) > Tolerance) Cursor = Target;
			const int64 PacketFrames = Packet.Samples.Num() / Channels;
			const int64 Begin = FMath::Max<int64>(0, Cursor), End = FMath::Min(TotalFrames, Cursor + PacketFrames);
			if (End > Begin)
				FMemory::Memcpy(Audio.GetData() + Begin * Channels,
					Packet.Samples.GetData() + (Begin - Cursor) * Channels, (End - Begin) * Channels * sizeof(float));
			Cursor += PacketFrames;
		}
		return Audio;
	}

	bool WriteWave(const FString& Path, const TArray<float>& Samples, int32 Channels, int32 Rate)
	{
		if (Samples.IsEmpty() || Channels <= 0 || Rate <= 0) return false;
		FBufferArchive Bytes;
		uint32 DataSize = Samples.Num() * sizeof(int16), RiffSize = 36 + DataSize, FormatSize = 16;
		uint16 Format = 1, ChannelCount = Channels, Bits = 16, BlockAlign = Channels * sizeof(int16);
		uint32 SampleRate = Rate, ByteRate = Rate * BlockAlign;
		Bytes.Serialize((void*)"RIFF", 4); Bytes << RiffSize;
		Bytes.Serialize((void*)"WAVEfmt ", 8); Bytes << FormatSize << Format << ChannelCount << SampleRate << ByteRate << BlockAlign << Bits;
		Bytes.Serialize((void*)"data", 4); Bytes << DataSize;
		for (float Sample : Samples)
		{
			int16 Value = FMath::Clamp(FMath::RoundToInt(Sample * 32767.f), -32768, 32767);
			Bytes << Value;
		}
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}

	bool EncodeRecording(const TSharedRef<FMarbleRecordingSession, ESPMode::ThreadSafe>& Capture,
		const double EndTime, const TArray<float>& Audio, int32 Channels, int32 Rate, FString& Error)
	{
		while (Capture->PendingFrames.GetValue() > 0) FPlatformProcess::Sleep(.01f);
		if (Capture->bDiscard.Load()) return false;
		if (Capture->FrameTimes.IsEmpty() || Capture->bFrameWriteFailed.Load())
		{
			Error = TEXT("没有可用的游戏画面，或录制临时文件写入失败");
			return false;
		}
		const double FirstTime = Capture->FrameTimes[0];
		const double Duration = FMath::Max(1.0 / 30.0, EndTime - FirstTime);
		FString Concat;
		for (int32 Index = 0; Index < Capture->FrameTimes.Num(); ++Index)
		{
			const double Next = Index + 1 < Capture->FrameTimes.Num() ? Capture->FrameTimes[Index + 1] : EndTime;
			Concat += FString::Printf(TEXT("file 'frame%06d.jpg'\noption framerate 1000\nduration %.8f\n"), Index,
				FMath::Max(1.0 / 1000.0, Next - Capture->FrameTimes[Index]));
		}
		Concat += FString::Printf(TEXT("file 'frame%06d.jpg'\noption framerate 1000\n"), Capture->FrameTimes.Num() - 1);
		const FString ConcatPath = Capture->Directory / TEXT("frames.txt");
		if (!FFileHelper::SaveStringToFile(Concat, *ConcatPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Error = TEXT("无法保存录制时间信息");
			return false;
		}
		const FString WavePath = Capture->Directory / TEXT("audio.wav");
		const bool bHasAudio = WriteWave(WavePath, Audio, Channels, Rate);
		if (Capture->bDiscard.Load()) return false;
		if (!bHasAudio) UE_LOG(LogTemp, Warning, TEXT("录制中没有游戏音频，保存无声视频"));
		FString Args = FString::Printf(TEXT("-hide_banner -loglevel error -y -f concat -safe 0 -i \"%s\" "), *ConcatPath);
		if (bHasAudio) Args += FString::Printf(TEXT("-ss %.8f -i \"%s\" "), FirstTime, *WavePath);
		Args += FString::Printf(TEXT("-map 0:v:0 %s-vf \"scale=%d:%d,setsar=1,fps=30,tpad=stop_mode=clone:stop_duration=%.8f\" "
			"-c:v libx264 -preset veryfast -crf 20 -bf 0 -pix_fmt yuv420p -r 30 -fps_mode cfr %s-t %.8f "
			"-f mp4 -movflags frag_keyframe+empty_moov+default_base_moof pipe:1"),
			bHasAudio ? TEXT("-map 1:a:0 ") : TEXT(""), Capture->OutputSize.X, Capture->OutputSize.Y,
			Duration, bHasAudio ? TEXT("-c:a aac -b:a 192k -af apad ") : TEXT(""), Duration);
		int32 ExitCode = -1;
		FString Err;
		// Unreal owns the output file; the encoder only emits binary media to stdout.
		// This also avoids external encoder path/permission differences on Windows.
		void *ReadPipe = nullptr, *WritePipe = nullptr, *ErrorRead = nullptr, *ErrorWrite = nullptr;
		FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
		FPlatformProcess::CreatePipe(ErrorRead, ErrorWrite);
		FProcHandle Process = FPlatformProcess::CreateProc(*UMarbleRaceRecorderSubsystem::GetEncoderPath(), *Args,
			false, true, true, nullptr, 0, nullptr, WritePipe, nullptr, ErrorWrite);
		TUniquePtr<FArchive> Output(IFileManager::Get().CreateFileWriter(*Capture->Output));
		if (Process.IsValid())
		{
			bool bRunning = true;
			while (bRunning)
			{
				if (Capture->bDiscard.Load())
				{
					FPlatformProcess::TerminateProc(Process, true);
					FPlatformProcess::WaitForProc(Process);
					break;
				}
				TArray<uint8> Bytes;
				FPlatformProcess::ReadPipeToArray(ReadPipe, Bytes);
				if (Output && !Bytes.IsEmpty()) Output->Serialize(Bytes.GetData(), Bytes.Num());
				Err += FPlatformProcess::ReadPipe(ErrorRead);
				bRunning = FPlatformProcess::IsProcRunning(Process);
				if (bRunning) FPlatformProcess::Sleep(.005f);
			}
			TArray<uint8> Bytes;
			while (FPlatformProcess::ReadPipeToArray(ReadPipe, Bytes))
			{
				if (Output && !Bytes.IsEmpty()) Output->Serialize(Bytes.GetData(), Bytes.Num());
				Bytes.Reset();
			}
			Err += FPlatformProcess::ReadPipe(ErrorRead);
			FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
			FPlatformProcess::CloseProc(Process);
		}
		const bool bWritten = Output && !Output->IsError();
		Output.Reset();
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		FPlatformProcess::ClosePipe(ErrorRead, ErrorWrite);
		FFileHelper::SaveStringToFile(Err, *(Capture->Directory / TEXT("encoder.log")));
		if (!bWritten || ExitCode != 0 || IFileManager::Get().FileSize(*Capture->Output) <= 0)
		{
			Error = TEXT("MP4 生成失败，临时画面和声音已保留：") + Capture->Directory;
			return false;
		}
		CleanupRecording(Capture, false);
		return true;
	}
}

FString UMarbleRaceRecorderSubsystem::GetRecordingsDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Recordings"));
}

FString UMarbleRaceRecorderSubsystem::GetEncoderPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/FFmpeg/ffmpeg.exe"));
}

bool UMarbleRaceRecorderSubsystem::IsRecording() const { return CaptureHandle.IsValid(); }

int32 UMarbleRaceRecorderSubsystem::GetPendingSaveCount() const
{
	int32 Pending = 0;
	for (const TFuture<void>& Future : Finalizations)
	{
		if (Future.IsValid() && !Future.IsReady()) ++Pending;
	}
	return Pending;
}

bool UMarbleRaceRecorderSubsystem::StartRecording()
{
	if (IsRecording()) return true;
	if (!FPaths::FileExists(GetEncoderPath()))
	{
		Status = TEXT("缺少录制编码工具，未开始录制");
		UE_LOG(LogTemp, Error, TEXT("%s：%s"), *Status, *GetEncoderPath());
		return false;
	}
	Session = MakeShared<FMarbleRecordingSession, ESPMode::ThreadSafe>();
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));
	Finalizations.RemoveAll([](const TFuture<void>& Future) { return Future.IsReady(); });
	const FString Name = FDateTime::Now().ToString(TEXT("MarbleRace-%Y%m%d-%H%M%S-")) + FGuid::NewGuid().ToString().Left(8);
	Session->Output = GetRecordingsDirectory() / (Name + TEXT(".mp4"));
	Session->Directory = GetRecordingsDirectory() / (TEXT(".capture-") + Name);
	IFileManager::Get().MakeDirectory(*Session->Directory, true);
	Session->StartedAt = FPlatformTime::Seconds();
	if (Audio::FMixerDevice* Mixer = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(this))
	{
		AudioCapture = MakeShared<FMarbleRecordingAudioCapture, ESPMode::ThreadSafe>(Session->StartedAt);
		Mixer->RegisterSubmixBufferListener(AudioCapture.ToSharedRef(), Mixer->GetMainSubmixObject());
	}
	CaptureHandle = UGameViewportClient::OnViewportRendered().AddUObject(this, &UMarbleRaceRecorderSubsystem::CaptureFrame);
	Status = TEXT("正在录制游戏");
	UE_LOG(LogTemp, Log, TEXT("开始游戏录制：%s"), *Session->Output);
	return true;
}

void UMarbleRaceRecorderSubsystem::CaptureFrame(FViewport* Viewport)
{
	UGameViewportClient* Client = GetGameInstance()->GetGameViewportClient();
	if (!Session || !Client || Client->Viewport != Viewport) return;
	const double Now = FPlatformTime::Seconds() - Session->StartedAt;
	if (Now - Session->LastFrameAt < 1.0 / 30.0 || Session->PendingFrames.GetValue() >= 3) return;
	const FIntPoint ViewportSize = Viewport->GetSizeXY();
	FIntRect CameraRect(FIntPoint::ZeroValue, ViewportSize);
	if (const ULocalPlayer* Player = GetGameInstance()->GetFirstGamePlayer())
	{
		FSceneViewProjectionData Projection;
		if (Player->GetProjectionData(Viewport, Projection)) CameraRect = Projection.GetConstrainedViewRect();
		if (const AActor* ViewTarget = Player->PlayerController ? Player->PlayerController->GetViewTarget() : nullptr)
		{
			if (const auto* Camera = ViewTarget->FindComponentByClass<UCameraComponent>(); Camera && Camera->bConstrainAspectRatio)
			{
				// The initial loading frame can still use the default camera cache.
				// Do not lock this session's output to that temporary landscape view.
				if (!IsRecordingViewReady(CameraRect, Camera->AspectRatio)) return;
			}
		}
	}
	const FIntRect Rect = RecordingRect(ViewportSize, CameraRect);
	const FIntPoint Size = Rect.Size();
	if (Size.X < 2 || Size.Y < 2) return;
	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels, FReadSurfaceDataFlags(RCM_UNorm), Rect) || Pixels.Num() != Size.X * Size.Y) return;
	if (Session->OutputSize == FIntPoint::ZeroValue)
	{
		Session->OutputSize = RecordingSize(Size);
		UE_LOG(LogTemp, Log, TEXT("录制画面区域：%d,%d %dx%d；输出：%dx%d"), Rect.Min.X, Rect.Min.Y,
			Size.X, Size.Y, Session->OutputSize.X, Session->OutputSize.Y);
	}
	const int32 Index = Session->FrameTimes.Add(Now);
	Session->LastFrameAt = Now;
	Session->PendingFrames.Increment();
	const auto Capture = Session.ToSharedRef();
	Async(EAsyncExecution::ThreadPool, [Capture, Size, Index, Pixels = MoveTemp(Pixels)]()
	{
		TArray64<uint8> Jpeg;
		if (!FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Pixels.GetData(), Size.X, Size.Y), 90) ||
			!FFileHelper::SaveArrayToFile(Jpeg, *(Capture->Directory / FString::Printf(TEXT("frame%06d.jpg"), Index))))
			Capture->bFrameWriteFailed.Store(true);
		Capture->PendingFrames.Decrement();
	});
}

void UMarbleRaceRecorderSubsystem::StopRecording()
{
	if (!IsRecording() || !Session) return;
	UGameViewportClient::OnViewportRendered().Remove(CaptureHandle);
	CaptureHandle.Reset();
	const double EndTime = FPlatformTime::Seconds() - Session->StartedAt;
	TArray<FMarbleRecordingAudioPacket> AudioPackets;
	int32 Channels = 0, Rate = 0;
	if (AudioCapture)
	{
		AudioPackets = AudioCapture->Stop();
		Channels = AudioCapture->Channels;
		Rate = AudioCapture->Rate;
		if (Audio::FMixerDevice* Mixer = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(this))
		{
			Mixer->UnregisterSubmixBufferListener(AudioCapture.ToSharedRef(), Mixer->GetMainSubmixObject());
		}
		AudioCapture.Reset();
	}
	Status = TEXT("正在生成 MP4");
	const auto Capture = Session.ToSharedRef();
	Capture->bEncoding.Store(true);
	const TWeakObjectPtr<UMarbleRaceRecorderSubsystem> WeakThis(this);
	Finalizations.Add(Async(EAsyncExecution::Thread, [Capture, EndTime, Channels, Rate, AudioPackets = MoveTemp(AudioPackets), WeakThis]() mutable
	{
		const TArray<float> Audio = Capture->bDiscard.Load() ? TArray<float>() : BuildAudioTimeline(AudioPackets, Channels, Rate, EndTime);
		AudioPackets.Empty();
		FString Error;
		const bool bSuccess = EncodeRecording(Capture, EndTime, Audio, Channels, Rate, Error);
		Capture->bEncoding.Store(false);
		if (!Capture->bDiscard.Load()) UE_LOG(LogTemp, Log, TEXT("游戏录制完成：%s"), bSuccess ? *Capture->Output : *Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bSuccess, Error, Capture]()
		{
			if (WeakThis.IsValid() && WeakThis->Session == Capture && !Capture->bDiscard.Load())
				WeakThis->Status = bSuccess ? TEXT("已保存：") + FPaths::GetCleanFilename(Capture->Output) : Error;
		});
	}));
}

void UMarbleRaceRecorderSubsystem::DiscardRecording()
{
	if (!Session || Session->bDiscard.Exchange(true)) return;
	UGameViewportClient::OnViewportRendered().Remove(CaptureHandle);
	CaptureHandle.Reset();
	if (AudioCapture)
	{
		AudioCapture->Stop();
		if (auto* Mixer = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(this))
			Mixer->UnregisterSubmixBufferListener(AudioCapture.ToSharedRef(), Mixer->GetMainSubmixObject());
		AudioCapture.Reset();
	}
	const auto Capture = Session.ToSharedRef();
	Finalizations.Add(Async(EAsyncExecution::Thread, [Capture]()
	{
		while (Capture->PendingFrames.GetValue() > 0 || Capture->bEncoding.Load()) FPlatformProcess::Sleep(.01f);
		CleanupRecording(Capture, true);
	}));
	Status = TEXT("本场录制已丢弃");
	UE_LOG(LogTemp, Log, TEXT("中断比赛，丢弃本场录制：%s"), *Capture->Output);
}

void UMarbleRaceRecorderSubsystem::Deinitialize()
{
	StopRecording();
	for (const TFuture<void>& Future : Finalizations) Future.Wait();
	Finalizations.Reset();
	Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRecordingDiscardTest, "MarbleRace.Recording.DiscardCurrentRace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleRecordingDiscardTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* Recorder = Instance->GetSubsystem<UMarbleRaceRecorderSubsystem>();
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
		(TEXT("Automation/Discard-") + FGuid::NewGuid().ToString()));
	const FString PreviousOutput = Folder / TEXT("previous.mp4");
	IFileManager::Get().MakeDirectory(*Folder, true);
	FFileHelper::SaveStringToFile(TEXT("previous recording"), *PreviousOutput);
	const auto Capture = MakeShared<FMarbleRecordingSession, ESPMode::ThreadSafe>();
	Capture->Directory = Folder / TEXT(".capture-current");
	Capture->Output = Folder / TEXT("current.mp4");
	Capture->FrameTimes = {0.};
	IFileManager::Get().MakeDirectory(*Capture->Directory, true);
	FFileHelper::SaveStringToFile(TEXT("partial encoding"), *Capture->Output);
	FFileHelper::SaveStringToFile(TEXT("temporary audio"), *(Capture->Directory / TEXT("audio.wav")));
	Recorder->Session = Capture;
	Recorder->CaptureHandle = UGameViewportClient::OnViewportRendered().AddLambda([](FViewport*) {});
	Capture->PendingFrames.Increment();
	Capture->bEncoding.Store(true);
	TPromise<void> AllowWriter;
	TFuture<void> Writer = Async(EAsyncExecution::Thread, [Capture, Gate = AllowWriter.GetFuture()]() mutable
	{
		Gate.Wait();
		FFileHelper::SaveStringToFile(TEXT("last pending frame"), *(Capture->Directory / TEXT("frame000000.jpg")));
		Capture->PendingFrames.Decrement();
	});
	Recorder->DiscardRecording();
	TestFalse(TEXT("ESC stops viewport capture immediately"), Recorder->IsRecording());
	TestTrue(TEXT("Encoder is instructed to discard the current session"), Capture->bDiscard.Load());
	TestEqual(TEXT("Cleanup is asynchronous while pending writes are held"), Recorder->GetPendingSaveCount(), 1);
	Recorder->DiscardRecording();
	Recorder->StopRecording();
	TestEqual(TEXT("EndPlay and repeated ESC cannot start an MP4 save or duplicate cleanup"), Recorder->Finalizations.Num(), 1);
	TestTrue(TEXT("An in-flight output is not deleted until its encoder closes"), IFileManager::Get().FileExists(*Capture->Output));
	AllowWriter.SetValue();
	Writer.Wait();
	Capture->bEncoding.Store(false);
	for (const auto& Future : Recorder->Finalizations) Future.Wait();
	TestFalse(TEXT("Canceled current MP4 is removed"), IFileManager::Get().FileExists(*Capture->Output));
	TestFalse(TEXT("All late frame and audio writes are cleaned up"), IFileManager::Get().DirectoryExists(*Capture->Directory));
	FString PreviousContents;
	FFileHelper::LoadFileToString(PreviousContents, *PreviousOutput);
	TestEqual(TEXT("Previously saved recordings are unchanged"), PreviousContents, FString(TEXT("previous recording")));
	IFileManager::Get().Delete(*PreviousOutput);
	IFileManager::Get().DeleteDirectory(*Folder, false, false);
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRecordingEncodingTest, "MarbleRace.Recording.RealMP4WithAudio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleRecordingEncodingTest::RunTest(const FString& Parameters)
{
	if (!FPaths::FileExists(UMarbleRaceRecorderSubsystem::GetEncoderPath()) ||
		!FPaths::FileExists(UMarbleRaceRecorderSubsystem::GetEncoderPath().Replace(TEXT("ffmpeg.exe"), TEXT("ffprobe.exe"))))
	{
		AddInfo(TEXT("Optional FFmpeg tools are not installed; skipping MP4 encoding integration test."));
		return true;
	}
	const auto Capture = MakeShared<FMarbleRecordingSession, ESPMode::ThreadSafe>();
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/RecordingPreview"));
	Capture->Directory = Folder / (TEXT(".frames-") + FGuid::NewGuid().ToString());
	Capture->Output = Folder / TEXT("EncodingTest.mp4");
	Capture->OutputSize = FIntPoint(64, 64);
	Capture->FrameTimes = {.1, .45, .95};
	IFileManager::Get().MakeDirectory(*Capture->Directory, true);
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Index == 0 ? FColor::Red : Index == 1 ? FColor::Green : FColor::Blue, 64 * 64);
		TArray64<uint8> Compressed;
		TestTrue(TEXT("Capture frame compresses"), FImageUtils::CompressImage(Compressed, TEXT("jpg"), FImageView(Pixels.GetData(), 64, 64), 90));
		TestTrue(TEXT("Capture frame saves"), FFileHelper::SaveArrayToFile(Compressed,
			*(Capture->Directory / FString::Printf(TEXT("frame%06d.jpg"), Index))));
	}
	TArray<float> Audio;
	for (int32 Index = 0; Index < 48000 * 2; ++Index)
	{
		const float Sample = .2f * FMath::Sin(2.0 * PI * 440.0 * Index / 48000.0);
		Audio.Add(Sample); Audio.Add(Sample);
	}
	FString Error;
	TestTrue(TEXT("Real encoder exports timed frames and game audio into MP4"), EncodeRecording(Capture, 1.6, Audio, 2, 48000, Error));
	if (!Error.IsEmpty()) AddError(Error);
	TestTrue(TEXT("MP4 exists and contains encoded data"), IFileManager::Get().FileSize(*Capture->Output) > 1000);
	TestFalse(TEXT("Successful export cleans up only its temporary session files"), IFileManager::Get().DirectoryExists(*Capture->Directory));
	FString Metadata, ProbeError;
	int32 ProbeExit = -1;
	const FString Probe = UMarbleRaceRecorderSubsystem::GetEncoderPath().Replace(TEXT("ffmpeg.exe"), TEXT("ffprobe.exe"));
	FPlatformProcess::ExecProcess(*Probe, *FString::Printf(TEXT("-v error -show_entries stream=codec_name,codec_type,duration,r_frame_rate -of json \"%s\""),
		*Capture->Output), &ProbeExit, &Metadata, &ProbeError);
	TSharedPtr<FJsonObject> Document;
	if (TestEqual(TEXT("MP4 media inspection succeeds"), ProbeExit, 0) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Metadata), Document))
	{
		const auto& Streams = Document->GetArrayField(TEXT("streams"));
		TestEqual(TEXT("MP4 contains both video and audio"), Streams.Num(), 2);
		for (const auto& Stream : Streams)
		{
			const auto Object = Stream->AsObject();
			const bool bVideo = Object->GetStringField(TEXT("codec_type")) == TEXT("video");
			TestEqual(TEXT("Media uses compatible H.264 and AAC codecs"), Object->GetStringField(TEXT("codec_name")), FString(bVideo ? TEXT("h264") : TEXT("aac")));
			TestTrue(TEXT("Sparse captured frames retain the real duration for both audio and video"),
				FMath::IsNearlyEqual(FCString::Atod(*Object->GetStringField(TEXT("duration"))), 1.5, .075));
			if (bVideo) TestEqual(TEXT("Video is encoded at 30 FPS"), Object->GetStringField(TEXT("r_frame_rate")), FString(TEXT("30/1")));
		}
	}
	AddInfo(TEXT("Recording preview: ") + Capture->Output);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRecordingSyncTest, "MarbleRace.Recording.CropAndAVSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleRecordingSyncTest::RunTest(const FString& Parameters)
{
	const FIntRect Portrait(620, 0, 980, 900);
	TestEqual(TEXT("Portrait recording excludes both black side bars"),
		RecordingRect(FIntPoint(1600, 900), Portrait), Portrait);
	TestEqual(TEXT("Portrait output retains the camera's dimensions"), RecordingSize(Portrait.Size()), FIntPoint(360, 900));
	TestFalse(TEXT("Startup's uncached landscape camera cannot set portrait output dimensions"),
		IsRecordingViewReady(FIntRect(0, 0, 888, 500), .4));
	TestTrue(TEXT("Camera output is ready after portrait aspect constraint is applied"), IsRecordingViewReady(Portrait, .4));
	TestTrue(TEXT("An actual portrait-shaped window remains valid without black bars"),
		IsRecordingViewReady(FIntRect(0, 0, 360, 900), .4));
	TestEqual(TEXT("Horizontal letterbox bars are also excluded"),
		RecordingRect(FIntPoint(900, 900), FIntRect(0, 197, 900, 703)).Size(), FIntPoint(900, 506));
	TestEqual(TEXT("Missing projection safely records the viewport"),
		RecordingRect(FIntPoint(640, 480), FIntRect()).Size(), FIntPoint(640, 480));
	TestEqual(TEXT("Oversized views are bounded by the actual render target"),
		RecordingRect(FIntPoint(640, 480), FIntRect(-10, -10, 900, 900)).Size(), FIntPoint(640, 480));
	{
		TArray<FMarbleRecordingAudioPacket> LongSession;
		auto& First = LongSession.AddDefaulted_GetRef();
		First.Time = .2;
		First.Samples.Init(0.f, 10);
		auto& LateCue = LongSession.AddDefaulted_GetRef();
		LateCue.Time = 600.25;
		LateCue.Samples.Init(.5f, 10);
		const TArray<float> LongAudio = BuildAudioTimeline(LongSession, 1, 1000, 601.);
		TestEqual(TEXT("A cue ten minutes into a session stays on the real timeline"), LongAudio[600250], .5f);
		TestEqual(TEXT("Late audio is never compressed into the beginning of the recording"), LongAudio[210], 0.f);
	}

	// The audio listener starts late, then stalls for four seconds. Two visual
	// flashes and matching tones must still align in the actual exported MP4.
	if (!FPaths::FileExists(UMarbleRaceRecorderSubsystem::GetEncoderPath()))
	{
		AddInfo(TEXT("Optional encoder is not installed; crop and audio timeline checks completed without MP4 export."));
		return true;
	}
	constexpr int32 Rate = 48000, Channels = 2, FramesPerPacket = 480;
	constexpr double EndTime = 6.6;
	TArray<FMarbleRecordingAudioPacket> Packets;
	for (int32 Block = 20; Block < 660; ++Block)
	{
		if (Block >= 120 && Block < 520) continue;
		auto& Packet = Packets.AddDefaulted_GetRef();
		Packet.Time = Block * .01 + .002 * FMath::Sin((Block - 20) * .7);
		Packet.Samples.SetNumZeroed(FramesPerPacket * Channels);
		for (int32 Frame = 0; Frame < FramesPerPacket; ++Frame)
		{
			const double Time = Block * .01 + Frame / static_cast<double>(Rate);
			if ((Time >= .45 && Time < .55) || (Time >= 5.45 && Time < 5.55))
			{
				const float Sample = .6 * FMath::Sin(2. * PI * 1000. * Time);
				Packet.Samples[Frame * Channels] = Packet.Samples[Frame * Channels + 1] = Sample;
			}
		}
	}
	const TArray<float> Audio = BuildAudioTimeline(Packets, Channels, Rate, EndTime);
	TestEqual(TEXT("Audio covers real recording time including capture gaps"), Audio.Num(), 660 * FramesPerPacket * Channels);
	TestEqual(TEXT("Delayed registration produces leading silence"), Audio[Rate / 10 * Channels], 0.f);
	TestEqual(TEXT("Missing capture buffers preserve time instead of moving later music earlier"), Audio[Rate * 3 * Channels], 0.f);
	TestTrue(TEXT("Tone after the gap remains at its original time"), FMath::Abs(Audio[FMath::RoundToInt(5.451 * Rate) * Channels + 2]) > .01f);

	const auto Capture = MakeShared<FMarbleRecordingSession, ESPMode::ThreadSafe>();
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/RecordingPreview"));
	Capture->Directory = Folder / (TEXT(".sync-") + FGuid::NewGuid().ToString());
	Capture->Output = Folder / TEXT("PortraitSyncTest.mp4");
	Capture->OutputSize = RecordingSize(Portrait.Size());
	Capture->FrameTimes = {.1, .425, .45, .55, .95, 4.3, 5.45, 5.55, 6.0};
	IFileManager::Get().MakeDirectory(*Capture->Directory, true);
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("ImageWrapper"));
	for (int32 Index = 0; Index < Capture->FrameTimes.Num(); ++Index)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Index == 2 ? FColor::Green : Index == 6 ? FColor::Blue : FColor::Red, 360 * 900);
		TArray64<uint8> Compressed;
		TestTrue(TEXT("Test flash compresses"), FImageUtils::CompressImage(Compressed, TEXT("jpg"), FImageView(Pixels.GetData(), 360, 900), 90));
		TestTrue(TEXT("Test flash saves"), FFileHelper::SaveArrayToFile(Compressed,
			*(Capture->Directory / FString::Printf(TEXT("frame%06d.jpg"), Index))));
	}
	FString Error;
	if (!TestTrue(TEXT("Real encoder exports portrait flashes and synchronized tones"), EncodeRecording(Capture, EndTime, Audio, Channels, Rate, Error)))
	{
		AddError(Error);
		return false;
	}
	TArray<uint8> VideoBytes, AudioBytes;
	const auto Decode = [this, Capture](const FString& Options, TArray<uint8>& Bytes)
	{
		void *Read = nullptr, *Write = nullptr, *ErrorRead = nullptr, *ErrorWrite = nullptr;
		FPlatformProcess::CreatePipe(Read, Write);
		FPlatformProcess::CreatePipe(ErrorRead, ErrorWrite);
		const FString Args = FString::Printf(TEXT("-v error -i \"%s\" %s pipe:1"), *Capture->Output, *Options);
		FProcHandle Process = FPlatformProcess::CreateProc(*UMarbleRaceRecorderSubsystem::GetEncoderPath(), *Args,
			false, true, true, nullptr, 0, nullptr, Write, nullptr, ErrorWrite);
		int32 ExitCode = -1;
		FString StdErr;
		if (Process.IsValid())
		{
			bool bRunning;
			do
			{
				TArray<uint8> Chunk;
				FPlatformProcess::ReadPipeToArray(Read, Chunk);
				Bytes.Append(Chunk);
				StdErr += FPlatformProcess::ReadPipe(ErrorRead);
				bRunning = FPlatformProcess::IsProcRunning(Process);
				if (bRunning) FPlatformProcess::Sleep(.005f);
			} while (bRunning);
			TArray<uint8> Chunk;
			while (FPlatformProcess::ReadPipeToArray(Read, Chunk)) { Bytes.Append(Chunk); Chunk.Reset(); }
			StdErr += FPlatformProcess::ReadPipe(ErrorRead);
			FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
			FPlatformProcess::CloseProc(Process);
		}
		FPlatformProcess::ClosePipe(Read, Write);
		FPlatformProcess::ClosePipe(ErrorRead, ErrorWrite);
		if (ExitCode != 0) AddError(StdErr);
		return ExitCode == 0 && !Bytes.IsEmpty();
	};
	TestTrue(TEXT("Actual encoded video decodes"), Decode(TEXT("-an -vf scale=1:1 -pix_fmt rgb24 -f rawvideo"), VideoBytes));
	TestTrue(TEXT("Actual AAC audio decodes"), Decode(TEXT("-vn -ac 1 -ar 48000 -f f32le"), AudioBytes));
	const float* DecodedSamples = reinterpret_cast<const float*>(AudioBytes.GetData());
	for (int32 Marker = 0; Marker < 2; ++Marker)
	{
		const double Expected = (Marker == 0 ? .45 : 5.45) - Capture->FrameTimes[0];
		double VideoAt = -1., AudioAt = -1.;
		for (int32 Frame = 0; Frame * 3 + 2 < VideoBytes.Num(); ++Frame)
		{
			if (VideoBytes[Frame * 3 + (Marker == 0 ? 1 : 2)] > 180 && VideoBytes[Frame * 3] < 80)
			{ VideoAt = Frame / 30.; break; }
		}
		for (int32 Sample = FMath::Max(0, FMath::FloorToInt((Expected - .15) * Rate));
			Sample < AudioBytes.Num() / sizeof(float) && Sample < (Expected + .15) * Rate; ++Sample)
		{
			if (FMath::Abs(DecodedSamples[Sample]) > .15f) { AudioAt = Sample / static_cast<double>(Rate); break; }
		}
		TestTrue(TEXT("Visual flash occurs at the captured time despite sparse/dropped frames"), VideoAt >= 0. && FMath::Abs(VideoAt - Expected) < .075);
		TestTrue(TEXT("Tone occurs at the captured time despite delayed start and gaps"), AudioAt >= 0. && FMath::Abs(AudioAt - Expected) < .075);
		TestTrue(TEXT("Encoded flash and tone align within two video frames"), VideoAt >= 0. && AudioAt >= 0. && FMath::Abs(VideoAt - AudioAt) < 2. / 30.);
		AddInfo(FString::Printf(TEXT("Marker %d: video %.4fs, audio %.4fs"), Marker, VideoAt, AudioAt));
	}
	AddInfo(TEXT("Portrait synchronization preview: ") + Capture->Output);
	return true;
}
#endif
