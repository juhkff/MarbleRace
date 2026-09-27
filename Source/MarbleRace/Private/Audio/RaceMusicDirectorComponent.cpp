#include "Audio/RaceMusicDirectorComponent.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h"

namespace
{
	float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	float PlaybackOffset(USoundBase* Sound, float Offset)
	{
		Offset = NonNegativeFinite(Offset);
		const float Duration = Sound ? Sound->GetDuration() : 0.0f;
		// 有限长度的资源放到头之后，不要反复再去播。
		return FMath::IsFinite(Duration) && Duration > 0.0f && Offset >= Duration ? 0.0f : Offset;
	}
}

URaceMusicDirectorComponent::URaceMusicDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 普通的世界暂停也会冻住音乐。显式暂停在下面单独控制。
	PrimaryComponentTick.bTickEvenWhenPaused = false;
}

void URaceMusicDirectorComponent::RequestTheme(USoundBase* Sound, float StartOffset, float Gain, bool bForce)
{
	if (bEndingPlay)
	{
		return;
	}

	PendingSound = IsValid(Sound) ? Sound : DefaultMusic.Get();
	if (!IsValid(PendingSound.Get()))
	{
		PendingSound = nullptr;
	}
	PendingOffset = PlaybackOffset(PendingSound.Get(), StartOffset);
	PendingGain = NonNegativeFinite(Gain);
	bPendingForce = bForce;
	bHasPending = true;

	if (HasBegunPlay() && !bMusicPaused)
	{
		ApplyPending();
	}
}

bool URaceMusicDirectorComponent::EnsureChannels()
{
	if (!GetOwner() || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!Channels[Index])
		{
			UAudioComponent* Audio = NewObject<UAudioComponent>(GetOwner());
			Channels[Index] = Audio;
			Audio->bAutoActivate = false;
			Audio->bAutoDestroy = false;
			Audio->bAllowSpatialization = false;
			Audio->bOverrideAttenuation = true;
			Audio->AttenuationOverrides.bAttenuate = false;
			Audio->AttenuationOverrides.bSpatialize = false;
			Audio->bCanPlayMultipleInstances = false;
			Audio->bIsUISound = false;
			Audio->bStopWhenOwnerDestroyed = true;
			Audio->SetVolumeMultiplier(0.0f);
			if (Index == 0)
			{
				Audio->OnAudioFinished.AddDynamic(this, &URaceMusicDirectorComponent::OnFirstChannelFinished);
			}
			else
			{
				Audio->OnAudioFinished.AddDynamic(this, &URaceMusicDirectorComponent::OnSecondChannelFinished);
			}
			Audio->RegisterComponentWithWorld(GetWorld());
		}
	}
	return true;
}

void URaceMusicDirectorComponent::ApplyPending()
{
	if (!bHasPending || bMusicPaused || bEndingPlay)
	{
		return;
	}

	UAudioComponent* Current = CurrentChannel != INDEX_NONE ? Channels[CurrentChannel].Get() : nullptr;
	if (Current && Current->Sound == PendingSound && PendingSound)
	{
		// 同一首曲会取消已经排队的旧请求，强制请求也一样。只更新
		// 下次重播的起点和音量，不跳转、也不重开这一次播放。
		Offsets[CurrentChannel] = PendingOffset;
		if (FadeTo[CurrentChannel] != PendingGain)
		{
			BeginFade(PendingGain);
		}
		bHasPending = false;
		PendingSound = nullptr;
		return;
	}

	if (Current && !bPendingForce && PlayedSeconds < NonNegativeFinite(MinimumPlaySeconds))
	{
		return;
	}

	if (!PendingSound)
	{
		// 没有已加载的资源，也没有默认曲：静音是合法的最新请求。
		StopChannel(0);
		StopChannel(1);
		CurrentChannel = INDEX_NONE;
		PlayedSeconds = 0.0f;
		bRestartCurrent = false;
		bFading = false;
		bHasPending = false;
		return;
	}
	if (!EnsureChannels())
	{
		return;
	}

	const int32 Next = CurrentChannel == 0 ? 1 : 0;
	UAudioComponent* Incoming = Channels[Next].Get();
	// A 切到 B 再切回 A 时，仍在播的 A 接着用，不重新开始。
	const bool bReuse = Incoming->Sound == PendingSound && Incoming->IsPlaying();
	if (!bReuse)
	{
		// 第三次快速请求时，先停掉正在淡出的旧声部再复用。
		// 不再分配第三个音频组件，也不让三次播放叠在一起。
		StopChannel(Next);
		Incoming->SetSound(PendingSound.Get());
		Incoming->SetVolumeMultiplier(0.0f);
		Incoming->Play(PendingOffset);
	}
	Offsets[Next] = PendingOffset;
	CurrentChannel = Next;
	PlayedSeconds = 0.0f;
	bRestartCurrent = false;
	BeginFade(PendingGain);
	bHasPending = false;
	PendingSound = nullptr;
}

void URaceMusicDirectorComponent::BeginFade(float TargetGain)
{
	FadeDuration = NonNegativeFinite(CrossfadeSeconds);
	FadeElapsed = 0.0f;
	bFading = true;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FadeFrom[Index] = Gains[Index];
		FadeTo[Index] = Index == CurrentChannel ? TargetGain : 0.0f;
	}
	AdvanceFade(0.0f);
}

void URaceMusicDirectorComponent::AdvanceFade(float DeltaTime)
{
	if (!bFading)
	{
		return;
	}
	FadeElapsed += DeltaTime;
	const float Alpha = FadeDuration > 0.0f ? FMath::Clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f) : 1.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Gains[Index] = FMath::Lerp(FadeFrom[Index], FadeTo[Index], Alpha);
		if (Channels[Index])
		{
			Channels[Index]->SetVolumeMultiplier(Gains[Index]);
		}
	}
	if (Alpha >= 1.0f)
	{
		bFading = false;
		StopChannel(CurrentChannel == 0 ? 1 : 0);
	}
}

void URaceMusicDirectorComponent::StopChannel(int32 Index)
{
	if (Channels[Index])
	{
		TGuardValue<bool> Guard(bStoppingChannel, true);
		Channels[Index]->Stop();
		Channels[Index]->SetSound(nullptr);
		Channels[Index]->SetVolumeMultiplier(0.0f);
	}
	Gains[Index] = 0.0f;
	Offsets[Index] = 0.0f;
}

void URaceMusicDirectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bMusicPaused || bEndingPlay)
	{
		return;
	}

	const float Step = NonNegativeFinite(DeltaTime);
	if (CurrentChannel != INDEX_NONE)
	{
		PlayedSeconds += Step;
	}
	AdvanceFade(Step);
	ApplyPending();

	// 重播推迟到播放结束回调之外，避免音频操作重入。
	// 引擎原生循环的资源不会触发结束，会一直播下去。
	if (bRestartCurrent && CurrentChannel != INDEX_NONE)
	{
		bRestartCurrent = false;
		UAudioComponent* Audio = Channels[CurrentChannel].Get();
		if (Audio && IsValid(Audio->Sound.Get()) && !Audio->IsPlaying())
		{
			Audio->SetVolumeMultiplier(Gains[CurrentChannel]);
			Audio->Play(Offsets[CurrentChannel]);
		}
	}
}

void URaceMusicDirectorComponent::OnChannelFinished(int32 Index)
{
	if (!bStoppingChannel && !bEndingPlay && Index == CurrentChannel)
	{
		bRestartCurrent = true;
	}
}

void URaceMusicDirectorComponent::OnFirstChannelFinished()
{
	OnChannelFinished(0);
}

void URaceMusicDirectorComponent::OnSecondChannelFinished()
{
	OnChannelFinished(1);
}

void URaceMusicDirectorComponent::SetMusicPaused(bool bPaused)
{
	bMusicPaused = bPaused;
	for (UAudioComponent* Audio : Channels)
	{
		if (Audio)
		{
			Audio->SetPaused(bPaused);
		}
	}
}

void URaceMusicDirectorComponent::ResetMusic()
{
	StopChannel(0);
	StopChannel(1);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (Channels[Index])
		{
			Channels[Index]->SetPaused(false);
		}
		FadeFrom[Index] = 0.0f;
		FadeTo[Index] = 0.0f;
	}
	PendingSound = nullptr;
	PendingOffset = 0.0f;
	PendingGain = 1.0f;
	bHasPending = false;
	bPendingForce = false;
	bMusicPaused = false;
	bRestartCurrent = false;
	CurrentChannel = INDEX_NONE;
	PlayedSeconds = 0.0f;
	FadeElapsed = 0.0f;
	FadeDuration = 0.0f;
	bFading = false;
}

void URaceMusicDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ResetMusic();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (Channels[Index])
		{
			Channels[Index]->OnAudioFinished.RemoveAll(this);
			Channels[Index]->DestroyComponent();
			Channels[Index] = nullptr;
		}
	}
	Super::EndPlay(EndPlayReason);
}
