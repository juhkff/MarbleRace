#include "UI/MarbleRaceHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Race/MarbleRaceDirector.h"
#include "Roster/RaceCharacterProfile.h"
#include "Race/RaceParticipantComponent.h"
#include "UObject/Class.h"

namespace
{
	struct FFinishCallout
	{
		FString Text;
		FVector WorldLocation = FVector::ZeroVector;
		float BornTime = 0.0f;
	};

	/** 只用于显示，所以不放进界面类的内存布局里。 */
	TArray<FFinishCallout> FinishCallouts;
	int32 SeenResultCount = 0;
	bool bContinueVisible = false;
	float ContinueMinX = 0.0f;
	float ContinueMinY = 0.0f;
	float ContinueMaxX = 0.0f;
	float ContinueMaxY = 0.0f;

	void ResetPresentationalState()
	{
		FinishCallouts.Reset();
		SeenResultCount = 0;
		bContinueVisible = false;
	}
}

AMarbleRaceHUD::AMarbleRaceHUD()
{
	PrimaryActorTick.bCanEverTick = false;
	// 调试读数用来核对领跑和音乐。正常比赛把它藏起来，
	// 画面才接近参考播出。
	bShowDebugReadout = false;
}

void AMarbleRaceHUD::BeginPlay()
{
	Super::BeginPlay();

	if (AMarbleRaceDirector* Director = ResolveDirector())
	{
		EnsureVisualOverride(*Director);
	}
}

void AMarbleRaceHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetPresentationalState();
	if (AMarbleRaceDirector* Director = CachedDirector.Get())
	{
		Director->SetVisualOverrideEnabled(false);
	}
	Super::EndPlay(EndPlayReason);
}

AMarbleRaceDirector* AMarbleRaceHUD::ResolveDirector()
{
	if (CachedDirector.IsValid())
	{
		return CachedDirector.Get();
	}

	if (UWorld* World = GetWorld())
	{
		if (AMarbleRaceDirector* Found = Cast<AMarbleRaceDirector>(
			UGameplayStatics::GetActorOfClass(World, AMarbleRaceDirector::StaticClass())))
		{
			CachedDirector = Found;
			return Found;
		}
	}
	return nullptr;
}

void AMarbleRaceHUD::DrawHUD()
{
	Super::DrawHUD();

	AMarbleRaceDirector* Director = ResolveDirector();
	if (!Director || !Canvas)
	{
		return;
	}

	if (bAllowPauseAndRestartKeys)
	{
		HandleShortcutKeys(*Director);
	}
	bContinueVisible = false;

	EnsureVisualOverride(*Director);

	// 先画尾迹，头像和姓名才压在上面。
	DrawTrails(*Director);
	DrawRacerLayer(*Director);
	DrawCountdown(*Director);
	DrawFinishCallouts(*Director);

	if (bShowDebugReadout)
	{
		DrawReadout(*Director);
	}

	if (Director->GetRaceState() == ERaceState::Results)
	{
		DrawResults(*Director);
	}
}

void AMarbleRaceHUD::EnsureVisualOverride(AMarbleRaceDirector& Director)
{
	if (bAppliedVisualOverride)
	{
		return;
	}
	bAppliedVisualOverride = true;
	Director.SetVisualOverrideEnabled(bHideRacerMeshes);
}

void AMarbleRaceHUD::HandleShortcutKeys(AMarbleRaceDirector& Director)
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::P))
	{
		Director.SetRacePaused(!Director.IsRacePaused());
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::R) || ConsumeContinueClick())
	{
		Director.RestartRace();
	}
}

void AMarbleRaceHUD::DrawTrails(AMarbleRaceDirector& Director)
{
	if (!bShowTrails || !Canvas)
	{
		return;
	}

	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const float Lifetime = FMath::Max(0.05f, TrailLifetimeSeconds);

	// 重开比赛会把时钟清零；过期尾迹一起丢掉。
	const float RaceClock = Director.GetRaceClock();
	if (RaceClock + 0.5f < LastTrailRaceClock)
	{
		RacerTrails.Reset();
	}
	LastTrailRaceClock = RaceClock;

	const float SampleDistanceSquared = FMath::Square(FMath::Max(1.0f, TrailSampleDistance));
	const int32 MaxSamples = FMath::Max(2, TrailMaxSamples);

	TSet<int32> LiveRacerIds;
	for (const FRacerRuntime& Racer : Director.GetRacers())
	{
		URaceParticipantComponent* Participant = Racer.Participant;
		if (!Participant)
		{
			continue;
		}
		AActor* RacerActor = Participant->GetOwner();
		if (!RacerActor)
		{
			continue;
		}

		const int32 RacerId = Participant->GetRacerId();
		LiveRacerIds.Add(RacerId);

		const float WorldRadius = FMath::Max(1.0f, Participant->GetWorldRadius());
		const float MaxTrailLength = WorldRadius * FMath::Max(0.5f, TrailLengthInRadii);

		TArray<FRacerTrailSample>& Samples = RacerTrails.FindOrAdd(RacerId);
		const FVector Location = RacerActor->GetActorLocation();
		if (Samples.Num() == 0 || FVector::DistSquared(Location, Samples.Last().Location) >= SampleDistanceSquared)
		{
			FRacerTrailSample Sample;
			Sample.Location = Location;
			Sample.Time = Now;
			Samples.Add(Sample);
		}

		// 先按寿命裁，再按长度，再按点数。长度预算让尾巴
		// 不论小球多快都保持得短；寿命预算在小球
		// 停住时把尾巴清掉，而不是在屏幕上留一条冻住的带子。
		while (Samples.Num() > 0 && Now - Samples[0].Time > Lifetime)
		{
			Samples.RemoveAt(0, EAllowShrinking::No);
		}
		while (Samples.Num() > 1 && FVector::Dist(Samples.Last().Location, Samples[0].Location) > MaxTrailLength)
		{
			Samples.RemoveAt(0, EAllowShrinking::No);
		}
		while (Samples.Num() > MaxSamples)
		{
			Samples.RemoveAt(0, EAllowShrinking::No);
		}

		if (Samples.Num() < 2)
		{
			continue;
		}

		// 宽度跟着画出来的头像大小，带子才和小球对齐。
		const FVector ProjectedCenter = Project(Location);
		const FVector ProjectedEdge = Project(Location + FVector(0.0f, 0.0f, WorldRadius));
		const float ScreenRadius = FMath::Clamp(FMath::Abs(ProjectedCenter.Y - ProjectedEdge.Y), 6.0f, 64.0f);

		DrawTrailRibbon(Samples, ScreenRadius, Participant->GetDisplayColor(), Now, Lifetime);
	}

	// 丢掉已经不在场上的选手的历史点。
	for (auto It = RacerTrails.CreateIterator(); It; ++It)
	{
		if (!LiveRacerIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

void AMarbleRaceHUD::DrawTrailRibbon(const TArray<FRacerTrailSample>& Samples, float ScreenRadius,
	const FLinearColor& Color, float Now, float Lifetime)
{
	if (!Canvas || !Canvas->DefaultTexture || Samples.Num() < 2)
	{
		return;
	}

	const int32 Count = Samples.Num();

	// 从最新的点开始投影，尾巴上的点会一起离开画面，
	// 而不是经过摄像机后方时拉成一条。
	TArray<FVector2D> Points;
	Points.SetNumZeroed(Count);
	int32 FirstVisible = Count;
	for (int32 Index = Count - 1; Index >= 0; --Index)
	{
		const FVector Projected = Project(Samples[Index].Location);
		if (Projected.Z <= 0.0f)
		{
			break;
		}
		Points[Index] = FVector2D(Projected.X, Projected.Y);
		FirstVisible = Index;
	}

	const int32 Span = Count - 1 - FirstVisible;
	if (Span < 1)
	{
		return;
	}

	// 在每个点上把相邻方向取平均，带子在转弯处
	// 才能连上，而不是段与段之间裂开。
	TArray<FVector2D> Normals;
	Normals.SetNumZeroed(Count);
	for (int32 Index = FirstVisible; Index < Count; ++Index)
	{
		FVector2D Tangent = FVector2D::ZeroVector;
		if (Index > FirstVisible)
		{
			Tangent += (Points[Index] - Points[Index - 1]).GetSafeNormal();
		}
		if (Index < Count - 1)
		{
			Tangent += (Points[Index + 1] - Points[Index]).GetSafeNormal();
		}
		if (Tangent.IsNearlyZero())
		{
			Tangent = FVector2D(0.0f, -1.0f);
		}
		Tangent = Tangent.GetSafeNormal();
		Normals[Index] = FVector2D(-Tangent.Y, Tangent.X);
	}

	const float HeadHalfWidth = FMath::Max(0.5f, ScreenRadius * TrailWidthScale);
	const float TaperPower = FMath::Max(0.2f, TrailTaperPower);
	const float AlphaFadePower = 0.8f;

	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Span * 2);
	for (int32 Index = FirstVisible + 1; Index < Count; ++Index)
	{
		const int32 Prev = Index - 1;
		// 小球处为 0，最旧的可见点为 1。
		const float PrevS = static_cast<float>(Count - 1 - Prev) / static_cast<float>(Span);
		const float CurS = static_cast<float>(Count - 1 - Index) / static_cast<float>(Span);

		const float PrevHalfWidth = HeadHalfWidth * FMath::Pow(1.0f - PrevS, TaperPower);
		const float CurHalfWidth = HeadHalfWidth * FMath::Pow(1.0f - CurS, TaperPower);

		const float PrevAge = FMath::Clamp(1.0f - (Now - Samples[Prev].Time) / Lifetime, 0.0f, 1.0f);
		const float CurAge = FMath::Clamp(1.0f - (Now - Samples[Index].Time) / Lifetime, 0.0f, 1.0f);

		FLinearColor PrevColor = Color;
		PrevColor.A = TrailMaxAlpha * PrevAge * FMath::Pow(1.0f - PrevS, AlphaFadePower);
		FLinearColor CurColor = Color;
		CurColor.A = TrailMaxAlpha * CurAge * FMath::Pow(1.0f - CurS, AlphaFadePower);

		const FVector2D EdgeA = Points[Prev] + Normals[Prev] * PrevHalfWidth;
		const FVector2D EdgeB = Points[Prev] - Normals[Prev] * PrevHalfWidth;
		const FVector2D EdgeC = Points[Index] - Normals[Index] * CurHalfWidth;
		const FVector2D EdgeD = Points[Index] + Normals[Index] * CurHalfWidth;

		FCanvasUVTri QuadA;
		QuadA.V0_Pos = EdgeA;
		QuadA.V1_Pos = EdgeB;
		QuadA.V2_Pos = EdgeC;
		QuadA.V0_Color = PrevColor;
		QuadA.V1_Color = PrevColor;
		QuadA.V2_Color = CurColor;
		Triangles.Add(QuadA);

		FCanvasUVTri QuadB;
		QuadB.V0_Pos = EdgeA;
		QuadB.V1_Pos = EdgeC;
		QuadB.V2_Pos = EdgeD;
		QuadB.V0_Color = PrevColor;
		QuadB.V1_Color = CurColor;
		QuadB.V2_Color = CurColor;
		Triangles.Add(QuadB);
	}

	if (Triangles.Num() == 0)
	{
		return;
	}

	// 在画布白纹理上按顶点透明度：靠近小球半透明，
	// 尖端全透明，不需要额外材质或粒子资源。
	FCanvasTriangleItem Ribbon(Triangles, Canvas->DefaultTexture->GetResource());
	Ribbon.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Ribbon);
}

void AMarbleRaceHUD::DrawRacerLayer(AMarbleRaceDirector& Director)
{
	const int32 LeaderId = Director.GetLeaderRacerId();
	const int32 MusicTargetId = Director.GetMusicTargetRacerId();
	const int32 ChampionId = Director.GetChampionRacerId();

	for (const FRacerRuntime& Racer : Director.GetRacers())
	{
		URaceParticipantComponent* Participant = Racer.Participant;
		if (!Participant)
		{
			continue;
		}
		AActor* RacerActor = Participant->GetOwner();
		if (!RacerActor)
		{
			continue;
		}

		const FVector WorldLocation = RacerActor->GetActorLocation();
		const FVector Projected = Project(WorldLocation);
		if (Projected.ContainsNaN() || Projected.Z <= 0.0f
			|| Projected.X < 0.0f || Projected.X >= Canvas->SizeX
			|| Projected.Y < 0.0f || Projected.Y >= Canvas->SizeY)
		{
			// 小球中心离开画面后，整层身份信息一起藏起来。
			continue;
		}

		// 屏幕半径取真实物理包围盒，画出的圆盘才和球体对齐。
		const FVector ProjectedEdge = Project(WorldLocation + FVector(0.0f, 0.0f, FMath::Max(1.0f, Participant->GetWorldRadius())));
		const float ScreenRadius = FMath::Clamp(FMath::Abs(Projected.Y - ProjectedEdge.Y), 6.0f, 64.0f);
		const float CenterX = Projected.X;
		const float CenterY = Projected.Y;

		const FLinearColor Color = Participant->GetDisplayColor();
		const bool bIsLeader = Participant->GetRacerId() == LeaderId;
		const bool bIsMusicTarget = Participant->GetRacerId() == MusicTargetId;
		const bool bIsChampion = Participant->GetRacerId() == ChampionId;

		UTexture2D* Portrait = Participant->GetPortrait();
		if (Portrait && Portrait->GetResource())
		{
			// 只在圆盘内部出几何：边框裁不掉一张方形贴图。
			// 屏幕 XY 和 UV 用同一套轴，头像保持正立。
			constexpr int32 PortraitSegments = 64;
			const FVector2D PortraitCenter(CenterX, CenterY);
			const FVector2D UVCenter(0.5f, 0.5f);
			TArray<FCanvasUVTri> PortraitTriangles;
			PortraitTriangles.Reserve(PortraitSegments);
			for (int32 Index = 0; Index < PortraitSegments; ++Index)
			{
				const float AngleA = 2.0f * UE_PI * Index / PortraitSegments;
				const float AngleB = 2.0f * UE_PI * (Index + 1) / PortraitSegments;
				const FVector2D DirectionA(FMath::Cos(AngleA), FMath::Sin(AngleA));
				const FVector2D DirectionB(FMath::Cos(AngleB), FMath::Sin(AngleB));
				FCanvasUVTri Fan;
				Fan.V0_Pos = PortraitCenter;
				Fan.V1_Pos = PortraitCenter + DirectionA * ScreenRadius;
				Fan.V2_Pos = PortraitCenter + DirectionB * ScreenRadius;
				Fan.V0_UV = UVCenter;
				Fan.V1_UV = UVCenter + DirectionA * 0.5f;
				Fan.V2_UV = UVCenter + DirectionB * 0.5f;
				Fan.V0_Color = FLinearColor::White;
				Fan.V1_Color = FLinearColor::White;
				Fan.V2_Color = FLinearColor::White;
				PortraitTriangles.Add(Fan);
			}
			FCanvasTriangleItem PortraitItem(PortraitTriangles, Portrait->GetResource());
			PortraitItem.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(PortraitItem);
			DrawCircleBand(CenterX, CenterY, ScreenRadius - 1.0f, ScreenRadius + 1.0f, Color);
		}
		else
		{
			DrawCircleBand(CenterX, CenterY, 0.0f, ScreenRadius, Color);
			const int32 InitialSize = FMath::Clamp(FMath::RoundToInt(ScreenRadius * 1.2f), 12, 24);
			DrawSharpCenteredText(Participant->GetInitialText(), CenterX, CenterY,
				InitialSize, FLinearColor(0.02f, 0.06f, 0.1f), false);
		}

		if (bShowStatusRings)
		{
			if (bIsChampion)
			{
				DrawCircleBand(CenterX, CenterY, ScreenRadius + 1.5f, ScreenRadius + 4.5f,
					FLinearColor(1.0f, 0.85f, 0.15f));
			}
			else if (bIsLeader)
			{
				DrawCircleBand(CenterX, CenterY, ScreenRadius + 1.5f, ScreenRadius + 4.5f, FLinearColor::White);
			}
			if (bIsMusicTarget)
			{
				DrawCircleBand(CenterX, CenterY, ScreenRadius + 6.0f, ScreenRadius + 8.0f,
					FLinearColor(0.20f, 0.95f, 0.60f));
			}
		}

		const FString Name = Participant->GetDisplayName().ToString();
		DrawSharpCenteredText(Name, CenterX, CenterY - ScreenRadius - 11.0f, 14,
			FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), true);
	}
}

void AMarbleRaceHUD::DrawSharpText(const FString& Text, float X, float Y, int32 FontSize,
	const FLinearColor& Color, bool bOutline, bool bCenterOnPosition)
{
	if (!Canvas || !GEngine || !GEngine->GetMediumFont() || Text.IsEmpty())
	{
		return;
	}

	// 默认界面字体大约只有 10 像素。用绘制文字的缩放去放大，
	// 会把字形图集放大，中文尤其容易糊。
	// 改为向运行时字体缓存要目标字号的字形。
	FSlateFontInfo FontInfo = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
	FontInfo.Size = FMath::Max(6, FontSize);
	FCanvasTextItem TextItem(FVector2D::ZeroVector, FText::FromString(Text), FontInfo, Color);
	TextItem.Scale = FVector2D(1.0f, 1.0f);
	if (bOutline)
	{
		TextItem.bOutlined = true;
		TextItem.OutlineColor = FLinearColor(0.015f, 0.04f, 0.07f, 0.9f);
	}
	// 居中时由画布自己量目标字号的字形；再取整到
	// 整像素，文字跟着小球移动时才不会闪。
	TextItem.bCentreX = bCenterOnPosition;
	TextItem.bCentreY = bCenterOnPosition;
	TextItem.Position = FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y));
	Canvas->DrawItem(TextItem);
}

void AMarbleRaceHUD::DrawSharpCenteredText(const FString& Text, float CenterX, float CenterY, int32 FontSize,
	const FLinearColor& Color, bool bOutline)
{
	DrawSharpText(Text, CenterX, CenterY, FontSize, Color, bOutline, true);
}

void AMarbleRaceHUD::DrawCircleBand(float CenterX, float CenterY, float InnerRadius, float OuterRadius,
	const FLinearColor& Color)
{
	if (!Canvas || !Canvas->DefaultTexture || OuterRadius <= 0.0f)
	{
		return;
	}

	// 用三角扇（实心圆）或三角带（圆环），不用一叠矩形：旧做法
	// 会在圆盘上留下色带，圆环看起来像一串点。
	const float Inner = FMath::Clamp(InnerRadius, 0.0f, OuterRadius);
	const bool bSolid = Inner <= 0.01f;
	constexpr int32 Segments = 64;
	const FVector2D Center(CenterX, CenterY);

	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Segments * (bSolid ? 1 : 2));
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float AngleA = (2.0f * UE_PI * static_cast<float>(Index)) / static_cast<float>(Segments);
		const float AngleB = (2.0f * UE_PI * static_cast<float>(Index + 1)) / static_cast<float>(Segments);
		const FVector2D DirectionA(FMath::Cos(AngleA), FMath::Sin(AngleA));
		const FVector2D DirectionB(FMath::Cos(AngleB), FMath::Sin(AngleB));
		const FVector2D OuterA = Center + DirectionA * OuterRadius;
		const FVector2D OuterB = Center + DirectionB * OuterRadius;

		if (bSolid)
		{
			FCanvasUVTri Fan;
			Fan.V0_Pos = Center;
			Fan.V1_Pos = OuterA;
			Fan.V2_Pos = OuterB;
			Fan.V0_Color = Color;
			Fan.V1_Color = Color;
			Fan.V2_Color = Color;
			Triangles.Add(Fan);
			continue;
		}

		const FVector2D InnerA = Center + DirectionA * Inner;
		const FVector2D InnerB = Center + DirectionB * Inner;

		FCanvasUVTri QuadA;
		QuadA.V0_Pos = InnerA;
		QuadA.V1_Pos = OuterA;
		QuadA.V2_Pos = OuterB;
		QuadA.V0_Color = Color;
		QuadA.V1_Color = Color;
		QuadA.V2_Color = Color;
		Triangles.Add(QuadA);

		FCanvasUVTri QuadB;
		QuadB.V0_Pos = InnerA;
		QuadB.V1_Pos = OuterB;
		QuadB.V2_Pos = InnerB;
		QuadB.V0_Color = Color;
		QuadB.V1_Color = Color;
		QuadB.V2_Color = Color;
		Triangles.Add(QuadB);
	}

	FCanvasTriangleItem Item(Triangles, Canvas->DefaultTexture->GetResource());
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AMarbleRaceHUD::DrawCountdown(AMarbleRaceDirector& Director)
{
	if (!Canvas)
	{
		return;
	}

	// 现有比赛时钟每次开赛清零，暂停时停住。
	if (Director.GetRaceState() == ERaceState::Racing && Director.GetRaceClock() < 0.8f)
	{
		DrawSharpCenteredText(TEXT("GO!"), Canvas->SizeX * 0.5f, Canvas->SizeY * 0.42f,
			CountdownFontSize, FLinearColor::White, true);
		return;
	}
	if (Director.GetRaceState() != ERaceState::Countdown)
	{
		return;
	}

	const int32 Seconds = FMath::Max(1, FMath::CeilToInt(Director.GetCountdownRemaining()));
	const FString Text = FString::Printf(TEXT("%d"), Seconds);
	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.42f;
	// 目标字号字形：旧路径把大约 10 像素的默认字体放大 6 倍，字会糊。
	DrawSharpCenteredText(Text, CenterX, CenterY, CountdownFontSize, FLinearColor::White, true);
}

void AMarbleRaceHUD::DrawReadout(AMarbleRaceDirector& Director)
{
	if (!Canvas)
	{
		return;
	}

	float LineY = 12.0f;
	const float LineHeight = 18.0f;
	auto Line = [this, &LineY, LineHeight](const FString& Text, const FLinearColor& Color)
	{
		DrawSharpText(Text, 12.0f, LineY, 14, Color, false, false);
		LineY += LineHeight;
	};

	int32 PortraitCount = 0;
	int32 MusicCount = 0;
	int32 RacerCount = 0;
	for (const FRacerRuntime& Racer : Director.GetRacers())
	{
		if (!Racer.Participant)
		{
			continue;
		}
		++RacerCount;
		if (Racer.Participant->GetPortrait())
		{
			++PortraitCount;
		}
		const URaceCharacterProfile* Profile = Racer.Participant->GetCharacterProfile();
		if (Profile && Profile->ThemeMusic)
		{
			++MusicCount;
		}
	}

	Line(FString::Printf(TEXT("状态：%s   参赛：%d   比赛时间：%.2f 秒"),
		*UEnum::GetDisplayValueAsText(Director.GetRaceState()).ToString(), RacerCount, Director.GetRaceClock()),
		FLinearColor::White);

	const URaceParticipantComponent* Leader = Director.FindRacerComponent(Director.GetLeaderRacerId());
	Line(FString::Printf(TEXT("领跑：%s"), Leader ? *Leader->GetDisplayName().ToString() : TEXT("<无>")),
		FLinearColor(1.0f, 1.0f, 0.85f));
	Line(FString::Printf(TEXT("音乐目标：%s"), *Director.GetMusicTargetDisplayName().ToString()),
		FLinearColor(0.55f, 1.0f, 0.75f));

	if (const URaceParticipantComponent* Champion = Director.FindRacerComponent(Director.GetChampionRacerId()))
	{
		Line(FString::Printf(TEXT("冠军：%s%s"), *Champion->GetDisplayName().ToString(),
			Director.IsChampionCelebrating() ? TEXT("（庆祝中）") : TEXT("")),
			FLinearColor(1.0f, 0.85f, 0.35f));
	}

	Line(FString::Printf(TEXT("素材：头像 %d/%d，主题曲 %d/%d"),
		PortraitCount, RacerCount, MusicCount, RacerCount),
		(PortraitCount == 0 || MusicCount == 0) ? FLinearColor(1.0f, 0.6f, 0.3f) : FLinearColor(0.7f, 0.9f, 0.7f));

	Line(Director.GetRouteStatusText().ToString(),
		Director.IsUsingVerticalFallback() ? FLinearColor(1.0f, 0.7f, 0.35f) : FLinearColor(0.7f, 0.85f, 1.0f));

	if (Director.IsRacePaused())
	{
		Line(TEXT("已暂停：按 P 继续"), FLinearColor(1.0f, 0.4f, 0.4f));
	}
	else
	{
		Line(TEXT("P 暂停／继续    R 重新开始"), FLinearColor(0.6f, 0.6f, 0.6f));
	}
}

FString AMarbleRaceHUD::FormatRaceTime(float Seconds)
{
	const int32 TotalCentiseconds = FMath::Max(0, FMath::RoundToInt(Seconds * 100.0f));
	const int32 Minutes = TotalCentiseconds / 6000;
	const int32 WholeSeconds = (TotalCentiseconds / 100) % 60;
	const int32 Centiseconds = TotalCentiseconds % 100;
	return FString::Printf(TEXT("%02d:%02d.%02d"), Minutes, WholeSeconds, Centiseconds);
}

FString AMarbleRaceHUD::FormatOrdinal(int32 ZeroBasedRank)
{
	const int32 Place = ZeroBasedRank + 1;
	const int32 Tens = Place % 100;
	if (Tens >= 11 && Tens <= 13)
	{
		return FString::Printf(TEXT("%dth"), Place);
	}
	switch (Place % 10)
	{
	case 1: return FString::Printf(TEXT("%dst"), Place);
	case 2: return FString::Printf(TEXT("%dnd"), Place);
	case 3: return FString::Printf(TEXT("%drd"), Place);
	default: return FString::Printf(TEXT("%dth"), Place);
	}
}

bool AMarbleRaceHUD::ConsumeContinueClick()
{
	if (!bContinueVisible)
	{
		return false;
	}
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController || !PlayerController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		return false;
	}
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!PlayerController->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	return MouseX >= ContinueMinX && MouseX <= ContinueMaxX
		&& MouseY >= ContinueMinY && MouseY <= ContinueMaxY;
}

void AMarbleRaceHUD::DrawFinishCallouts(AMarbleRaceDirector& Director)
{
	if (!Canvas)
	{
		return;
	}

	const TArray<FRaceResultEntry>& Results = Director.GetResults();
	if (Results.Num() < SeenResultCount)
	{
		SeenResultCount = 0;
		FinishCallouts.Reset();
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	while (SeenResultCount < Results.Num())
	{
		const FRaceResultEntry& Entry = Results[SeenResultCount];
		++SeenResultCount;
		FFinishCallout Callout;
		Callout.Text = FString::Printf(TEXT("%s: %s"), *FormatOrdinal(Entry.Rank), *Entry.DisplayName.ToString());
		Callout.BornTime = Now;
		Callout.WorldLocation = FVector::ZeroVector;
		if (const URaceParticipantComponent* Participant = Director.FindRacerComponent(Entry.RacerId))
		{
			if (const AActor* RacerActor = Participant->GetOwner())
			{
				Callout.WorldLocation = RacerActor->GetActorLocation();
			}
		}
		FinishCallouts.Add(Callout);
	}

	constexpr float LifeSeconds = 2.4f;
	for (int32 Index = FinishCallouts.Num() - 1; Index >= 0; --Index)
	{
		const FFinishCallout& Callout = FinishCallouts[Index];
		const float Age = Now - Callout.BornTime;
		if (Age > LifeSeconds)
		{
			FinishCallouts.RemoveAt(Index);
			continue;
		}

		FVector2D Screen(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.38f);
		if (!Callout.WorldLocation.IsNearlyZero())
		{
			const FVector Projected = Project(Callout.WorldLocation);
			if (Projected.Z > 0.0f)
			{
				Screen = FVector2D(Projected.X, Projected.Y);
			}
		}
		Screen.Y -= 36.0f + Age * 28.0f;

		FLinearColor Ink = FLinearColor::White;
		Ink.A = FMath::Clamp(1.0f - (Age / LifeSeconds), 0.0f, 1.0f);
		DrawSharpCenteredText(Callout.Text, Screen.X, Screen.Y, 28, Ink, true);
	}
}

void AMarbleRaceHUD::DrawResults(AMarbleRaceDirector& Director)
{
	if (!Canvas)
	{
		return;
	}

	const TArray<FRaceResultEntry>& Results = Director.GetResults();
	const int32 MaxRows = 12;
	const int32 Rows = FMath::Min(Results.Num(), MaxRows);
	const bool bHasOmittedResults = Results.Num() > MaxRows;
	const float PanelWidth = 420.0f;
	const float RowHeight = 22.0f;
	const float HeaderHeight = 58.0f;
	const float ButtonHeight = 36.0f;
	const float PanelHeight = HeaderHeight + (Rows + (bHasOmittedResults ? 1 : 0)) * RowHeight + 16.0f + ButtonHeight;
	const float PanelX = Canvas->SizeX * 0.5f - PanelWidth * 0.5f;
	const float PanelY = Canvas->SizeY * 0.18f;

	DrawRect(FLinearColor(0.05f, 0.12f, 0.18f, 0.82f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawSharpText(TEXT("比赛结果"), PanelX + 16.0f, PanelY + 12.0f, 22, FLinearColor::White, false, false);
	DrawSharpText(TEXT("名次"), PanelX + 16.0f, PanelY + 36.0f, 13, FLinearColor(0.75f, 0.85f, 0.95f), false, false);
	DrawSharpText(TEXT("选手"), PanelX + 92.0f, PanelY + 36.0f, 13, FLinearColor(0.75f, 0.85f, 0.95f), false, false);
	DrawSharpText(TEXT("时间"), PanelX + PanelWidth - 92.0f, PanelY + 36.0f, 13, FLinearColor(0.75f, 0.85f, 0.95f), false, false);

	for (int32 Index = 0; Index < Rows; ++Index)
	{
		const FRaceResultEntry& Entry = Results[Index];
		const float RowY = PanelY + HeaderHeight + Index * RowHeight;
		DrawSharpText(FormatOrdinal(Entry.Rank), PanelX + 16.0f, RowY, 16, Entry.Color, false, false);
		DrawSharpText(Entry.DisplayName.ToString(), PanelX + 92.0f, RowY, 16, FLinearColor::White, false, false);
		DrawSharpText(FormatRaceTime(Entry.FinishTime), PanelX + PanelWidth - 92.0f, RowY, 16,
			FLinearColor(0.9f, 0.95f, 1.0f), false, false);
	}

	if (bHasOmittedResults)
	{
		DrawSharpText(FString::Printf(TEXT("…… 共 %d 名"), Results.Num()), PanelX + 16.0f,
			PanelY + HeaderHeight + Rows * RowHeight, 16, FLinearColor::White, false, false);
	}

	const float ButtonWidth = 120.0f;
	ContinueMinX = PanelX + PanelWidth * 0.5f - ButtonWidth * 0.5f;
	ContinueMaxX = ContinueMinX + ButtonWidth;
	ContinueMaxY = PanelY + PanelHeight - 10.0f;
	ContinueMinY = ContinueMaxY - ButtonHeight;
	bContinueVisible = true;
	DrawRect(FLinearColor(0.12f, 0.28f, 0.85f, 1.0f), ContinueMinX, ContinueMinY, ButtonWidth, ButtonHeight);
	DrawSharpCenteredText(TEXT("继续"), (ContinueMinX + ContinueMaxX) * 0.5f,
		(ContinueMinY + ContinueMaxY) * 0.5f, 18, FLinearColor::White, false);
}
