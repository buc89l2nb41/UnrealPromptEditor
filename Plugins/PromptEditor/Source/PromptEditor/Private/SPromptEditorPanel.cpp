#include "SPromptEditorPanel.h"
#include "PromptEditorAttach.h"
#include "PromptEditorChats.h"
#include "PromptEditorLog.h"
#include "PromptEditorSidecar.h"
#include "Editor.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Application/SlateApplication.h"
#include "HttpModule.h"
#include "HttpFwd.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/Char.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Templates/UnrealTemplate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"

#define LOCTEXT_NAMESPACE "SPromptEditorPanel"

void SPromptEditorPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(this, &SPromptEditorPanel::StatusLine)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 4, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Hint", "Enter 전송 · Shift+Enter 줄바꿈. 그림 링크를 누르면 크게 봅니다."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ChatCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ChatOptions)
					.OnGenerateWidget(this, &SPromptEditorPanel::MakeChatRow)
					.OnSelectionChanged(this, &SPromptEditorPanel::OnChatChanged)
					[
						SNew(STextBlock)
						.Text(this, &SPromptEditorPanel::SelectedChatLabel)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("NewAgent", "New Agent"))
					.OnClicked(this, &SPromptEditorPanel::OnNewAgentClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("DeleteAgent", "Delete"))
					.OnClicked(this, &SPromptEditorPanel::OnDeleteAgentClicked)
					.IsEnabled_Lambda([this]() { return !bBusy; })
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(6)
				[
					SAssignNew(FeedScroll, SScrollBox)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 0)
			[
				SAssignNew(AttachWrap, SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4.f, 4.f))
				.Visibility_Lambda([this]()
				{
					return PendingImages.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 4)
			[
				SNew(SBox)
				.MinDesiredHeight(72.f)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.AutoWrapText(true)
					.AllowMultiLine(true)
					.HintText(LOCTEXT("InputHint", "원점에 큐브 · 이미지 첨부 후 질문"))
					.OnKeyDownHandler(this, &SPromptEditorPanel::OnInputKeyDown)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(220.f)
					[
						SAssignNew(ModelCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&ModelOptions)
						.OnGenerateWidget(this, &SPromptEditorPanel::MakeModelRow)
						.OnSelectionChanged(this, &SPromptEditorPanel::OnModelChanged)
						[
							SNew(STextBlock)
							.Text(this, &SPromptEditorPanel::SelectedModelLabel)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.MinDesiredWidth(140.f)
					[
						SAssignNew(VariantCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&VariantOptions)
						.OnGenerateWidget(this, &SPromptEditorPanel::MakeModelRow)
						.OnSelectionChanged(this, &SPromptEditorPanel::OnVariantChanged)
						[
							SNew(STextBlock)
							.Text(this, &SPromptEditorPanel::SelectedVariantLabel)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Attach", "Attach"))
					.OnClicked(this, &SPromptEditorPanel::OnAttachClicked)
					.IsEnabled_Lambda([this]() { return !bBusy; })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Send", "Send"))
					.OnClicked(this, &SPromptEditorPanel::OnSendButton)
					.IsEnabled_Lambda([this]() { return !bBusy; })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Stop", "Stop"))
					.OnClicked(this, &SPromptEditorPanel::OnStopClicked)
					.IsEnabled_Lambda([this]() { return bBusy; })
				]
			]
		]
	];

	EnsureFallbackModel();
	LoadSavedModel();
	EnsureDefaultChat();
	LoadSavedChat();
	RefreshChatCombo();
	RestoreChatLog();

	FString StartError;
	FPromptEditorSidecar::EnsureStarted(StartError);
	RequestModels();

	TWeakPtr<SPromptEditorPanel> Weak = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[Weak](float)
			{
				if (TSharedPtr<SPromptEditorPanel> Panel = Weak.Pin())
				{
					Panel->RequestModels();
				}
				return false;
			}),
		0.6f);
}

FReply SPromptEditorPanel::OnSendButton()
{
	OnSendClicked();
	return FReply::Handled();
}

FReply SPromptEditorPanel::OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if ((Event.GetKey() == EKeys::V) && Event.IsControlDown() && !Event.IsAltDown() && !Event.IsShiftDown())
	{
		if (TryPasteClipboardImages())
		{
			return FReply::Handled();
		}
	}
	if (Event.GetKey() == EKeys::Enter)
	{
		if (Event.IsShiftDown())
		{
			return FReply::Unhandled();
		}
		OnSendClicked();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FString SPromptEditorPanel::CurrentChatId() const
{
	return SelectedChatId.IsEmpty() ? FString(PromptEditor::DefaultChatId) : SelectedChatId;
}

void SPromptEditorPanel::AddPendingImage(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return;
	}
	for (const FPromptEditorAttachItem& Item : PendingImages)
	{
		if (Item.Path == Path)
		{
			return;
		}
	}
	if (PendingImages.Num() >= PromptEditorAttach::MaxCount)
	{
		AppendLog(TEXT("error"), TEXT("이미지는 한 번에 8장까지입니다."));
		return;
	}
	FPromptEditorAttachItem Item;
	Item.Path = Path;
	Item.Brush = PromptEditorAttach::MakeThumbBrush(Path);
	PendingImages.Add(MoveTemp(Item));
	RefreshAttachChips();
}

bool SPromptEditorPanel::TryAddImageFile(const FString& Path)
{
	FString Copied;
	FString Error;
	if (!PromptEditorAttach::ImportFile(Path, CurrentChatId(), Copied, Error))
	{
		AppendLog(TEXT("error"), Error.IsEmpty() ? TEXT("이미지를 첨부하지 못했습니다.") : Error);
		return false;
	}
	AddPendingImage(Copied);
	return true;
}

void SPromptEditorPanel::RefreshAttachChips()
{
	if (!AttachWrap.IsValid())
	{
		return;
	}
	AttachWrap->ClearChildren();
	for (const FPromptEditorAttachItem& Item : PendingImages)
	{
		const FString Path = Item.Path;
		const FString Name = FPaths::GetCleanFilename(Path);
		const FSlateBrush* Thumb = Item.Brush.IsValid() ? static_cast<const FSlateBrush*>(Item.Brush.Get()) : nullptr;
		TSharedRef<SWidget> Preview = SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AttachNoThumb", "image"))
			];
		if (Thumb)
		{
			Preview = SNew(SImage).Image(Thumb);
		}
		AttachWrap->AddSlot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(4.f))
			.ToolTipText(FText::FromString(Name))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(80.f)
						.HeightOverride(80.f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							Preview
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 4, 0, 0)
					[
						SNew(SBox)
						.WidthOverride(80.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Name))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveAttach", "x"))
					.OnClicked_Lambda([this, Path]()
					{
						PendingImages.RemoveAll([&Path](const FPromptEditorAttachItem& Existing)
						{
							return Existing.Path == Path;
						});
						RefreshAttachChips();
						return FReply::Handled();
					})
				]
			]
		];
	}
}

FText SPromptEditorPanel::ImageLinkCaption(const FString& Path)
{
	const FString Stem = FPaths::GetBaseFilename(Path);
	if (Stem.EndsWith(TEXT("_original")) || Stem.EndsWith(TEXT("_ref")))
	{
		return LOCTEXT("PreviewOriginal", "원본");
	}
	if (Stem.EndsWith(TEXT("_compare")))
	{
		return LOCTEXT("PreviewCompare", "배경 제거 A/B");
	}
	if (Stem.EndsWith(TEXT("_tiled")))
	{
		return LOCTEXT("PreviewTiled", "타일 미리보기");
	}
	return LOCTEXT("PreviewSprite", "그림");
}

void SPromptEditorPanel::ScrollFeedToEnd()
{
	if (FeedScroll.IsValid())
	{
		FeedScroll->ScrollToEnd();
	}
}

void SPromptEditorPanel::AddFeedText(const FString& Role, const FString& Text)
{
	if (!FeedScroll.IsValid() || Text.IsEmpty())
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("[%s] %s"), *Role, *Text);
	FeedScroll->AddSlot()
	.Padding(0.f, 2.f)
	[
		SNew(SMultiLineEditableText)
		.Text(FText::FromString(Line))
		.IsReadOnly(true)
		.AutoWrapText(true)
	];
	ScrollFeedToEnd();
}

void SPromptEditorPanel::AddFeedImage(const FString& Path)
{
	const FString Clean = Path.TrimStartAndEnd();
	if (Clean.IsEmpty() || FeedImagePaths.Contains(Clean) || !FeedScroll.IsValid())
	{
		return;
	}
	if (!IFileManager::Get().FileExists(*Clean))
	{
		return;
	}
	FeedImagePaths.Add(Clean);
	const FText Caption = ImageLinkCaption(Clean);
	const FString Name = FPaths::GetCleanFilename(Clean);
	TSharedPtr<FSlateDynamicImageBrush> Thumb = PromptEditorAttach::MakeThumbBrush(Clean, 48, true);
	TSharedRef<SWidget> Icon = SNew(STextBlock).Text(LOCTEXT("DrawnNoThumb", "png"));
	if (Thumb.IsValid())
	{
		OpenImageBrushes.Add(Thumb);
		Icon = SNew(SImage).Image(Thumb.Get());
	}
	FeedScroll->AddSlot()
	.Padding(12.f, 2.f, 0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(48.f)
			.HeightOverride(48.f)
			[
				Icon
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHyperlink)
				.Text(Caption)
				.ToolTipText(FText::FromString(Clean))
				.OnNavigate(FSimpleDelegate::CreateLambda([this, Clean]()
				{
					OpenDrawnImage(Clean);
				}))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Name))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
		]
	];
	ScrollFeedToEnd();
}

void SPromptEditorPanel::OpenDrawnImage(const FString& Path)
{
	if (!IFileManager::Get().FileExists(*Path))
	{
		AppendLog(TEXT("error"), TEXT("이미지 파일을 찾을 수 없습니다."));
		return;
	}
	TSharedPtr<FSlateDynamicImageBrush> Brush = PromptEditorAttach::MakeThumbBrush(Path, 768, true);
	if (!Brush.IsValid())
	{
		FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
		return;
	}
	OpenImageBrushes.Add(Brush);
	const FVector2D ImageSize = Brush->ImageSize;
	const FVector2D Client(
		FMath::Clamp(ImageSize.X + 24.f, 280.f, 820.f),
		FMath::Clamp(ImageSize.Y + 56.f, 280.f, 820.f));
	const FString OpenPath = Path;
	TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(ImageLinkCaption(Path))
		.ClientSize(Client)
		.SupportsMaximize(true)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(8)
			[
				SNew(SImage).Image(Brush.Get())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 0, 8, 8)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenDrawnFile", "파일로 열기"))
				.OnClicked_Lambda([OpenPath]()
				{
					FPlatformProcess::LaunchFileInDefaultExternalApplication(*OpenPath);
					return FReply::Handled();
				})
			]
		];
	FSlateApplication::Get().AddWindow(Win);
}

void SPromptEditorPanel::ApplyStatusImages(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
	if (!Root->TryGetArrayField(TEXT("images"), Images) || !Images)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Images)
	{
		if (Value.IsValid())
		{
			AddFeedImage(Value->AsString());
		}
	}
}

bool SPromptEditorPanel::TryPasteClipboardImages()
{
	TArray<FString> Paths;
	FString Error;
	if (!PromptEditorAttach::ImportClipboard(CurrentChatId(), Paths, Error))
	{
		return false;
	}
	for (const FString& Path : Paths)
	{
		AddPendingImage(Path);
	}
	return Paths.Num() > 0;
}

FReply SPromptEditorPanel::OnAttachClicked()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return FReply::Handled();
	}
	void* Parent = nullptr;
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Parent = Window->GetNativeWindow()->GetOSWindowHandle();
	}
	TArray<FString> Files;
	const bool bOpened = Desktop->OpenFileDialog(
		Parent,
		TEXT("Attach images"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Images|*.png;*.jpg;*.jpeg;*.webp;*.gif;*.bmp;*.tif;*.tiff|All files|*.*"),
		EFileDialogFlags::Multiple,
		Files);
	if (bOpened)
	{
		for (const FString& File : Files)
		{
			TryAddImageFile(File);
		}
	}
	return FReply::Handled();
}

FReply SPromptEditorPanel::OnDragOver(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> External = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (External.IsValid() && External->HasFiles())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPromptEditorPanel::OnDrop(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> External = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (!External.IsValid() || !External->HasFiles())
	{
		return FReply::Unhandled();
	}
	bool bAny = false;
	for (const FString& File : External->GetFiles())
	{
		if (PromptEditorAttach::IsImageFile(File))
		{
			bAny = TryAddImageFile(File) || bAny;
		}
	}
	return bAny ? FReply::Handled() : FReply::Unhandled();
}

FText SPromptEditorPanel::StatusLine() const
{
	const FString Name = FApp::GetProjectName();
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s"), *Name, *FPromptEditorSidecar::McpUrl()));
}

void SPromptEditorPanel::SetBusy(bool bInBusy)
{
	bBusy = bInBusy;
}

void SPromptEditorPanel::FinishPrompt()
{
	StopStatusPolling();
	InFlightPrompt.Reset();
	bStopRequested = false;
	SetBusy(false);
}

void SPromptEditorPanel::PlaySendFinishedSound()
{
	if (!GEditor || !GEditor->CanPlayEditorSound())
	{
		return;
	}

	const int32 SampleRate = 22050;
	const int32 DingN = FMath::RoundToInt(SampleRate * 0.12f);
	const int32 DongAt = FMath::RoundToInt(SampleRate * 0.10f);
	const int32 DongN = FMath::RoundToInt(SampleRate * 0.20f);
	const int32 Total = DongAt + DongN;
	TArray<uint8> Pcm;
	Pcm.SetNumZeroed(Total * sizeof(int16));
	int16* Samples = reinterpret_cast<int16*>(Pcm.GetData());
	auto WriteBell = [Samples, SampleRate, Total](int32 Start, int32 Count, float Hz, float Amp)
	{
		const float Dur = Count / static_cast<float>(SampleRate);
		const float Attack = 0.006f;
		for (int32 I = 0; I < Count; ++I)
		{
			const int32 At = Start + I;
			if (At < 0 || At >= Total)
			{
				continue;
			}
			const float T = I / static_cast<float>(SampleRate);
			const float Env = (T < Attack)
				? (T / Attack)
				: FMath::Exp(-9.5f * (T - Attack) / FMath::Max(Dur - Attack, 0.04f));
			const float S =
				FMath::Sin(2.f * PI * Hz * T) +
				0.18f * FMath::Sin(2.f * PI * Hz * 2.01f * T);
			const int32 Mixed = Samples[At] + static_cast<int32>(FMath::Clamp(S * Amp * Env, -1.f, 1.f) * 32767.f);
			Samples[At] = static_cast<int16>(FMath::Clamp(Mixed, -32767, 32767));
		}
	};
	WriteBell(0, DingN, 784.f, 0.95f);
	WriteBell(DongAt, DongN, 523.25f, 1.0f);

	static TStrongObjectPtr<USoundWaveProcedural> Ding;
	if (!Ding.IsValid())
	{
		Ding.Reset(NewObject<USoundWaveProcedural>(GetTransientPackage(), NAME_None, RF_Transient));
		Ding->SetSampleRate(SampleRate);
		Ding->NumChannels = 1;
		Ding->SoundGroup = SOUNDGROUP_UI;
		Ding->bLooping = false;
		Ding->bProcedural = true;
		Ding->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		Ding->Volume = 2.5f;
	}
	Ding->ResetAudio();
	Ding->Duration = Total / static_cast<float>(SampleRate);
	Ding->QueueAudio(Pcm.GetData(), Pcm.Num());
	GEditor->PlayEditorSound(Ding.Get());
}

void SPromptEditorPanel::StopStatusPolling()
{
	if (StatusTimer.IsValid())
	{
		UnRegisterActiveTimer(StatusTimer.ToSharedRef());
		StatusTimer.Reset();
	}
	bStatusInFlight = false;
	StatusAfter = 0;
	StatusFailCount = 0;
}

void SPromptEditorPanel::StartStatusPolling()
{
	if (StatusTimer.IsValid())
	{
		UnRegisterActiveTimer(StatusTimer.ToSharedRef());
		StatusTimer.Reset();
	}
	StatusAfter = 0;
	StatusFailCount = 0;
	bStatusInFlight = false;
	StatusTimer = RegisterActiveTimer(
		0.45f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SPromptEditorPanel::HandleStatusTimer));
	PollStatus();
}

EActiveTimerReturnType SPromptEditorPanel::HandleStatusTimer(double, float)
{
	if (!bBusy)
	{
		StatusTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	if (!bStatusInFlight)
	{
		PollStatus();
	}
	return EActiveTimerReturnType::Continue;
}

void SPromptEditorPanel::ApplyStatusJson(const TSharedPtr<FJsonObject>& Root)
{
	if (!Root.IsValid())
	{
		return;
	}

	double AfterNum = StatusAfter;
	if (Root->TryGetNumberField(TEXT("after"), AfterNum))
	{
		StatusAfter = static_cast<int32>(AfterNum);
	}

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (Root->TryGetArrayField(TEXT("events"), Events) && Events)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Events)
		{
			if (!Value.IsValid())
			{
				continue;
			}
			const FString Line = Value->AsString().TrimStartAndEnd();
			if (Line.IsEmpty() || Line == TEXT("running"))
			{
				continue;
			}
			if (Line.StartsWith(TEXT("tool ")))
			{
				AppendLog(TEXT("run"), Line.Mid(5));
			}
			else
			{
				AppendLog(TEXT("run"), Line);
			}
			int32 PngAt = 0;
			while ((PngAt = Line.Find(TEXT("png="), ESearchCase::IgnoreCase, ESearchDir::FromStart, PngAt)) != INDEX_NONE)
			{
				int32 End = PngAt + 4;
				while (End < Line.Len() && !FChar::IsWhitespace(Line[End]))
				{
					++End;
				}
				AddFeedImage(Line.Mid(PngAt + 4, End - (PngAt + 4)));
				PngAt = End;
			}
		}
	}

	ApplyStatusImages(Root);

	bool bRunning = false;
	Root->TryGetBoolField(TEXT("running"), bRunning);
	if (bRunning)
	{
		return;
	}

	FString Reply;
	FString Error;
	Root->TryGetStringField(TEXT("reply"), Reply);
	Root->TryGetStringField(TEXT("error"), Error);

	const bool bStopped = bStopRequested;
	FinishPrompt();

	if (Error == TEXT("cancelled") || (bStopped && Reply.IsEmpty()))
	{
		AppendLog(TEXT("system"), TEXT("중지됨. 이미 실행된 큐브/파일은 그대로입니다."));
		return;
	}
	if (!Error.IsEmpty())
	{
		PlaySendFinishedSound();
		AppendLog(TEXT("error"), Error);
		if (!Reply.IsEmpty())
		{
			AppendLog(TEXT("model"), Reply);
		}
		return;
	}
	if (Reply.IsEmpty())
	{
		Reply = TEXT("(empty reply)");
	}
	PlaySendFinishedSound();
	AppendLog(TEXT("model"), Reply);
	RefreshChatCombo();
}

void SPromptEditorPanel::PollStatus()
{
	if (bStatusInFlight)
	{
		return;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Body->SetStringField(
		TEXT("chat_id"),
		SelectedChatId.IsEmpty() ? FString(PromptEditor::DefaultChatId) : SelectedChatId);
	Body->SetNumberField(TEXT("after"), StatusAfter);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Body, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(FPromptEditorSidecar::StatusUrl());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(8.f);
	Request->SetContentAsString(Json);

	bStatusInFlight = true;
	TWeakPtr<SPromptEditorPanel> Weak = SharedThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			TSharedPtr<SPromptEditorPanel> Panel = Weak.Pin();
			if (!Panel.IsValid())
			{
				return;
			}
			Panel->bStatusInFlight = false;
			if (!Panel->bBusy)
			{
				return;
			}

			if (!bSucceeded || !Response.IsValid())
			{
				++Panel->StatusFailCount;
				const bool bGiveUp = Panel->bStopRequested || Panel->StatusFailCount >= 12;
				const bool bStopped = Panel->bStopRequested;
				if (bGiveUp)
				{
					Panel->FinishPrompt();
					Panel->AppendLog(
						TEXT("error"),
						bStopped
							? TEXT("중지됨. 이미 실행된 큐브/파일은 그대로입니다.")
							: TEXT("사이드카 상태 확인에 실패했습니다. 사이드카를 다시 켜세요."));
					if (!bStopped)
					{
						Panel->PlaySendFinishedSound();
					}
				}
				return;
			}

			Panel->StatusFailCount = 0;
			const int32 Code = Response->GetResponseCode();
			const FString Content = Response->GetContentAsString();
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
			FJsonSerializer::Deserialize(Reader, Root);
			if (Code >= 400 || !Root.IsValid())
			{
				Panel->FinishPrompt();
				FString Error;
				if (Root.IsValid())
				{
					Root->TryGetStringField(TEXT("error"), Error);
				}
				Panel->AppendLog(TEXT("error"), Error.IsEmpty() ? Content.Left(400) : Error);
				Panel->PlaySendFinishedSound();
				return;
			}
			Panel->ApplyStatusJson(Root);
		});

	if (!Request->ProcessRequest())
	{
		bStatusInFlight = false;
		++StatusFailCount;
	}
}

void SPromptEditorPanel::PostCancel()
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Body->SetStringField(
		TEXT("chat_id"),
		SelectedChatId.IsEmpty() ? FString(PromptEditor::DefaultChatId) : SelectedChatId);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Body, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(FPromptEditorSidecar::CancelUrl());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(10.f);
	Request->SetContentAsString(Json);
	Request->ProcessRequest();
}

FReply SPromptEditorPanel::OnStopClicked()
{
	if (!bBusy)
	{
		return FReply::Handled();
	}
	bStopRequested = true;
	PostCancel();
	if (InFlightPrompt.IsValid())
	{
		InFlightPrompt->CancelRequest();
	}
	return FReply::Handled();
}

void SPromptEditorPanel::AppendLog(const FString& Role, const FString& Text)
{
	if (!LogText.IsEmpty())
	{
		LogText += TEXT("\n");
	}
	LogText += FString::Printf(TEXT("[%s] %s"), *Role, *Text);
	AddFeedText(Role, Text);
}

void SPromptEditorPanel::OnSendClicked()
{
	if (bBusy || !InputBox.IsValid())
	{
		return;
	}

	const FString Message = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Message.IsEmpty() && PendingImages.Num() == 0)
	{
		return;
	}

	InputBox->SetText(FText::GetEmpty());
	FString UserLine = Message;
	if (UserLine.IsEmpty())
	{
		UserLine = TEXT("(image)");
	}
	if (PendingImages.Num() > 0)
	{
		TArray<FString> Names;
		for (const FPromptEditorAttachItem& Item : PendingImages)
		{
			Names.Add(FPaths::GetCleanFilename(Item.Path));
		}
		FString Joined;
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			if (Index > 0)
			{
				Joined += TEXT(", ");
			}
			Joined += Names[Index];
		}
		UserLine += TEXT("  [") + Joined + TEXT("]");
	}
	AppendLog(TEXT("user"), UserLine);

	FString StartError;
	if (!FPromptEditorSidecar::EnsureStarted(StartError))
	{
		AppendLog(TEXT("error"), StartError);
		return;
	}

	PostPrompt(Message);
}

void SPromptEditorPanel::PostPrompt(const FString& Message)
{
	bStopRequested = false;
	SetBusy(true);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("message"), Message);
	Body->SetStringField(TEXT("mcp_url"), FPromptEditorSidecar::McpUrl());
	Body->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Body->SetStringField(TEXT("chat_id"), SelectedChatId.IsEmpty() ? FString(PromptEditor::DefaultChatId) : SelectedChatId);
	Body->SetStringField(TEXT("model"), SelectedModelId.IsEmpty() ? FString(PromptEditor::DefaultModel) : SelectedModelId);
	{
		TArray<TSharedPtr<FJsonValue>> ImageArray;
		for (const FPromptEditorAttachItem& Item : PendingImages)
		{
			ImageArray.Add(MakeShared<FJsonValueString>(Item.Path));
		}
		Body->SetArrayField(TEXT("images"), ImageArray);
	}
	{
		TArray<TSharedPtr<FJsonValue>> ParamArray;
		for (const FPromptEditorModelParam& Param : SelectedParams)
		{
			if (Param.Id.IsEmpty())
			{
				continue;
			}
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Param.Id);
			Obj->SetStringField(TEXT("value"), Param.Value);
			ParamArray.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Body->SetArrayField(TEXT("model_params"), ParamArray);
	}
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Body, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("POST"));
	Request->SetURL(FPromptEditorSidecar::PromptUrl());
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetTimeout(20.f);
	Request->SetContentAsString(Json);

	TWeakPtr<SPromptEditorPanel> Weak = SharedThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			TSharedPtr<SPromptEditorPanel> Panel = Weak.Pin();
			if (!Panel.IsValid())
			{
				return;
			}
			Panel->InFlightPrompt.Reset();

			const bool bStopped = Panel->bStopRequested;
			if (bStopped)
			{
				Panel->FinishPrompt();
				Panel->AppendLog(TEXT("system"), TEXT("중지됨. 이미 실행된 큐브/파일은 그대로입니다."));
				return;
			}

			if (!bSucceeded || !Response.IsValid())
			{
				Panel->FinishPrompt();
				Panel->AppendLog(
					TEXT("error"),
					TEXT("사이드카에 연결하지 못했습니다. 에디터를 켠 뒤 sidecar/.venv 가 있는지 확인하세요."));
				Panel->PlaySendFinishedSound();
				return;
			}

			const int32 Code = Response->GetResponseCode();
			const FString Content = Response->GetContentAsString();
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
			FJsonSerializer::Deserialize(Reader, Root);

			FString Reply;
			FString Error;
			bool bRunning = false;
			if (Root.IsValid())
			{
				Root->TryGetStringField(TEXT("reply"), Reply);
				Root->TryGetStringField(TEXT("error"), Error);
				Root->TryGetBoolField(TEXT("running"), bRunning);
			}

			if (Error == TEXT("cancelled"))
			{
				Panel->FinishPrompt();
				Panel->AppendLog(TEXT("system"), TEXT("중지됨. 이미 실행된 큐브/파일은 그대로입니다."));
				return;
			}

			if (Code >= 400 || !Error.IsEmpty())
			{
				Panel->FinishPrompt();
				Panel->AppendLog(TEXT("error"), Error.IsEmpty() ? Content.Left(400) : Error);
				if (!Reply.IsEmpty())
				{
					Panel->AppendLog(TEXT("model"), Reply);
				}
				Panel->PlaySendFinishedSound();
				return;
			}

			if (bRunning)
			{
				Panel->PendingImages.Reset();
				Panel->RefreshAttachChips();
				Panel->AppendLog(TEXT("system"), TEXT("작업 중…"));
				Panel->StartStatusPolling();
				return;
			}

			Panel->PendingImages.Reset();
			Panel->RefreshAttachChips();
			Panel->FinishPrompt();
			if (Reply.IsEmpty())
			{
				Reply = Content.Left(800);
			}
			Panel->PlaySendFinishedSound();
			Panel->AppendLog(TEXT("model"), Reply);
			Panel->RefreshChatCombo();
		});

	InFlightPrompt = Request;
	if (!Request->ProcessRequest())
	{
		FinishPrompt();
		AppendLog(TEXT("error"), TEXT("HTTP 요청을 시작하지 못했습니다."));
		PlaySendFinishedSound();
	}
}

FText SPromptEditorPanel::SelectedModelLabel() const
{
	return FText::FromString(SelectedModelId.IsEmpty() ? FString(PromptEditor::DefaultModel) : SelectedModelId);
}

FText SPromptEditorPanel::SelectedVariantLabel() const
{
	return FText::FromString(SelectedVariantName.IsEmpty() ? TEXT("Default") : SelectedVariantName);
}

TSharedRef<SWidget> SPromptEditorPanel::MakeModelRow(TSharedPtr<FString> Item)
{
	return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
}

void SPromptEditorPanel::OnModelChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type)
{
	if (bSyncingCombos || !NewItem.IsValid())
	{
		return;
	}
	if (SelectedModelId != *NewItem)
	{
		SelectedModelId = *NewItem;
		SelectedVariantName.Empty();
		SelectedParams.Reset();
	}
	RefreshVariantCombo();
	SaveSelectedModel();
}

void SPromptEditorPanel::OnVariantChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type)
{
	if (bSyncingCombos || !NewItem.IsValid())
	{
		return;
	}
	SelectedVariantName = *NewItem;
	if (const FPromptEditorModelInfo* Info = FindModel(SelectedModelId))
	{
		for (const FPromptEditorModelVariant& Variant : Info->Variants)
		{
			if (Variant.DisplayName == SelectedVariantName)
			{
				SelectedParams = Variant.Params;
				break;
			}
		}
	}
	SaveSelectedModel();
}

void SPromptEditorPanel::EnsureFallbackModel()
{
	if (Catalog.Num() == 0)
	{
		FPromptEditorModelInfo Fallback;
		Fallback.Id = PromptEditor::DefaultModel;
		Fallback.DisplayName = PromptEditor::DefaultModel;
		FPromptEditorModelVariant Variant;
		Variant.DisplayName = TEXT("Default");
		Variant.bDefault = true;
		Fallback.Variants.Add(Variant);
		Catalog.Add(Fallback);
	}
	if (ModelOptions.Num() == 0)
	{
		ModelOptions.Add(MakeShared<FString>(PromptEditor::DefaultModel));
	}
	if (SelectedModelId.IsEmpty())
	{
		SelectedModelId = PromptEditor::DefaultModel;
	}
	if (VariantOptions.Num() == 0)
	{
		VariantOptions.Add(MakeShared<FString>(TEXT("Default")));
	}
	if (SelectedVariantName.IsEmpty())
	{
		SelectedVariantName = TEXT("Default");
	}
}

void SPromptEditorPanel::LoadSavedModel()
{
	if (!GConfig)
	{
		return;
	}
	FString SavedModel;
	if (GConfig->GetString(TEXT("PromptEditor"), TEXT("SelectedModel"), SavedModel, GEditorPerProjectIni))
	{
		SavedModel.TrimStartAndEndInline();
		if (!SavedModel.IsEmpty())
		{
			SelectedModelId = SavedModel;
		}
	}
	FString SavedVariant;
	if (GConfig->GetString(TEXT("PromptEditor"), TEXT("SelectedVariant"), SavedVariant, GEditorPerProjectIni))
	{
		SavedVariant.TrimStartAndEndInline();
		if (!SavedVariant.IsEmpty())
		{
			SelectedVariantName = SavedVariant;
		}
	}
}

void SPromptEditorPanel::SaveSelectedModel() const
{
	if (!GConfig || SelectedModelId.IsEmpty())
	{
		return;
	}
	GConfig->SetString(TEXT("PromptEditor"), TEXT("SelectedModel"), *SelectedModelId, GEditorPerProjectIni);
	GConfig->SetString(TEXT("PromptEditor"), TEXT("SelectedVariant"), *SelectedVariantName, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

const FPromptEditorModelInfo* SPromptEditorPanel::FindModel(const FString& Id) const
{
	for (const FPromptEditorModelInfo& Info : Catalog)
	{
		if (Info.Id == Id)
		{
			return &Info;
		}
	}
	return nullptr;
}

void SPromptEditorPanel::RefreshVariantCombo()
{
	TArray<FString> Names;
	const FPromptEditorModelInfo* Info = FindModel(SelectedModelId);
	if (Info)
	{
		for (const FPromptEditorModelVariant& Variant : Info->Variants)
		{
			if (!Variant.DisplayName.IsEmpty() && !Names.Contains(Variant.DisplayName))
			{
				Names.Add(Variant.DisplayName);
			}
		}
	}
	if (Names.Num() == 0)
	{
		Names.Add(TEXT("Default"));
	}

	FString Pick = SelectedVariantName;
	if (Info)
	{
		bool bHasPick = Names.Contains(Pick);
		if (!bHasPick)
		{
			Pick.Empty();
			for (const FPromptEditorModelVariant& Variant : Info->Variants)
			{
				if (Variant.bDefault)
				{
					Pick = Variant.DisplayName;
					break;
				}
			}
			if (Pick.IsEmpty())
			{
				Pick = Names[0];
			}
		}
		SelectedParams.Reset();
		for (const FPromptEditorModelVariant& Variant : Info->Variants)
		{
			if (Variant.DisplayName == Pick)
			{
				SelectedParams = Variant.Params;
				break;
			}
		}
	}
	else
	{
		Pick = Names[0];
		SelectedParams.Reset();
	}
	SelectedVariantName = Pick;

	TGuardValue<bool> Guard(bSyncingCombos, true);
	VariantOptions.Reset();
	TSharedPtr<FString> Match;
	for (const FString& Name : Names)
	{
		TSharedPtr<FString> Opt = MakeShared<FString>(Name);
		if (Name == SelectedVariantName)
		{
			Match = Opt;
		}
		VariantOptions.Add(Opt);
	}
	if (VariantCombo.IsValid())
	{
		VariantCombo->RefreshOptions();
		VariantCombo->SetSelectedItem(Match);
	}
}

void SPromptEditorPanel::ApplyCatalog(const TArray<FPromptEditorModelInfo>& InCatalog)
{
	Catalog = InCatalog;
	if (Catalog.Num() == 0)
	{
		EnsureFallbackModel();
	}

	TArray<FString> Ids;
	for (const FPromptEditorModelInfo& Info : Catalog)
	{
		if (!Info.Id.IsEmpty() && !Ids.Contains(Info.Id))
		{
			Ids.Add(Info.Id);
		}
	}
	if (Ids.Num() == 0)
	{
		Ids.Add(PromptEditor::DefaultModel);
	}
	if (!SelectedModelId.IsEmpty() && !Ids.Contains(SelectedModelId))
	{
		Ids.Insert(SelectedModelId, 0);
		FPromptEditorModelInfo Extra;
		Extra.Id = SelectedModelId;
		Extra.DisplayName = SelectedModelId;
		FPromptEditorModelVariant Variant;
		Variant.DisplayName = SelectedVariantName.IsEmpty() ? TEXT("Default") : SelectedVariantName;
		Variant.bDefault = true;
		Extra.Variants.Add(Variant);
		Catalog.Insert(Extra, 0);
	}

	TGuardValue<bool> Guard(bSyncingCombos, true);
	ModelOptions.Reset();
	TSharedPtr<FString> Match;
	for (const FString& Id : Ids)
	{
		TSharedPtr<FString> Opt = MakeShared<FString>(Id);
		if (Id == SelectedModelId)
		{
			Match = Opt;
		}
		ModelOptions.Add(Opt);
	}
	if (!Match.IsValid() && ModelOptions.Num() > 0)
	{
		Match = ModelOptions[0];
		SelectedModelId = *Match;
	}
	if (ModelCombo.IsValid())
	{
		ModelCombo->RefreshOptions();
		ModelCombo->SetSelectedItem(Match);
	}
	RefreshVariantCombo();
}

void SPromptEditorPanel::RequestModels()
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(FPromptEditorSidecar::ModelsUrl());
	Request->SetTimeout(20.f);

	TWeakPtr<SPromptEditorPanel> Weak = SharedThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			TSharedPtr<SPromptEditorPanel> Panel = Weak.Pin();
			if (!Panel.IsValid())
			{
				return;
			}
			TArray<FPromptEditorModelInfo> Parsed;
			if (bSucceeded && Response.IsValid() && Response->GetResponseCode() < 400)
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* ModelsArr = nullptr;
					if (Root->TryGetArrayField(TEXT("models"), ModelsArr) && ModelsArr)
					{
						for (const TSharedPtr<FJsonValue>& ModelValue : *ModelsArr)
						{
							const TSharedPtr<FJsonObject> ModelObj = ModelValue.IsValid() ? ModelValue->AsObject() : nullptr;
							if (!ModelObj.IsValid())
							{
								continue;
							}
							FPromptEditorModelInfo Info;
							ModelObj->TryGetStringField(TEXT("id"), Info.Id);
							ModelObj->TryGetStringField(TEXT("display_name"), Info.DisplayName);
							Info.Id.TrimStartAndEndInline();
							if (Info.Id.IsEmpty())
							{
								continue;
							}
							if (Info.DisplayName.IsEmpty())
							{
								Info.DisplayName = Info.Id;
							}
							const TArray<TSharedPtr<FJsonValue>>* VariantsArr = nullptr;
							if (ModelObj->TryGetArrayField(TEXT("variants"), VariantsArr) && VariantsArr)
							{
								for (const TSharedPtr<FJsonValue>& VariantValue : *VariantsArr)
								{
									const TSharedPtr<FJsonObject> VariantObj = VariantValue.IsValid() ? VariantValue->AsObject() : nullptr;
									if (!VariantObj.IsValid())
									{
										continue;
									}
									FPromptEditorModelVariant Variant;
									VariantObj->TryGetStringField(TEXT("display_name"), Variant.DisplayName);
									VariantObj->TryGetBoolField(TEXT("is_default"), Variant.bDefault);
									Variant.DisplayName.TrimStartAndEndInline();
									if (Variant.DisplayName.IsEmpty())
									{
										Variant.DisplayName = TEXT("Default");
									}
									const TArray<TSharedPtr<FJsonValue>>* ParamsArr = nullptr;
									if (VariantObj->TryGetArrayField(TEXT("params"), ParamsArr) && ParamsArr)
									{
										for (const TSharedPtr<FJsonValue>& ParamValue : *ParamsArr)
										{
											const TSharedPtr<FJsonObject> ParamObj = ParamValue.IsValid() ? ParamValue->AsObject() : nullptr;
											if (!ParamObj.IsValid())
											{
												continue;
											}
											FPromptEditorModelParam Param;
											ParamObj->TryGetStringField(TEXT("id"), Param.Id);
											ParamObj->TryGetStringField(TEXT("value"), Param.Value);
											Param.Id.TrimStartAndEndInline();
											if (!Param.Id.IsEmpty())
											{
												Variant.Params.Add(Param);
											}
										}
									}
									Info.Variants.Add(Variant);
								}
							}
							if (Info.Variants.Num() == 0)
							{
								FPromptEditorModelVariant Variant;
								Variant.DisplayName = TEXT("Default");
								Variant.bDefault = true;
								Info.Variants.Add(Variant);
							}
							Parsed.Add(Info);
						}
					}
					if (Parsed.Num() == 0)
					{
						const TArray<TSharedPtr<FJsonValue>>* IdsArr = nullptr;
						if (Root->TryGetArrayField(TEXT("ids"), IdsArr) && IdsArr)
						{
							for (const TSharedPtr<FJsonValue>& Value : *IdsArr)
							{
								if (!Value.IsValid())
								{
									continue;
								}
								FPromptEditorModelInfo Info;
								Info.Id = Value->AsString().TrimStartAndEnd();
								if (Info.Id.IsEmpty())
								{
									continue;
								}
								Info.DisplayName = Info.Id;
								FPromptEditorModelVariant Variant;
								Variant.DisplayName = TEXT("Default");
								Variant.bDefault = true;
								Info.Variants.Add(Variant);
								Parsed.Add(Info);
							}
						}
					}
				}
			}
			Panel->ApplyCatalog(Parsed);
		});

	Request->ProcessRequest();
}

FText SPromptEditorPanel::SelectedChatLabel() const
{
	for (const FPromptEditorChatInfo& Chat : Chats)
	{
		if (Chat.Id == SelectedChatId)
		{
			return FText::FromString(Chat.Title);
		}
	}
	return FText::FromString(SelectedChatId.IsEmpty() ? FString(PromptEditor::DefaultChatId) : SelectedChatId);
}

TSharedRef<SWidget> SPromptEditorPanel::MakeChatRow(TSharedPtr<FString> Item)
{
	FString Label = Item.IsValid() ? *Item : FString();
	for (const FPromptEditorChatInfo& Chat : Chats)
	{
		if (Chat.Id == Label)
		{
			Label = Chat.Title;
			break;
		}
	}
	return SNew(STextBlock).Text(FText::FromString(Label));
}

void SPromptEditorPanel::EnsureDefaultChat()
{
	if (SelectedChatId.IsEmpty())
	{
		SelectedChatId = PromptEditor::DefaultChatId;
	}
	FPromptEditorChats::EnsureChat(SelectedChatId, TEXT("New Agent"));
}

void SPromptEditorPanel::LoadSavedChat()
{
	FString Saved;
	if (GConfig && GConfig->GetString(TEXT("PromptEditor"), TEXT("SelectedChatId"), Saved, GEditorPerProjectIni))
	{
		Saved = FPromptEditorChats::SanitizeId(Saved);
		if (!Saved.IsEmpty())
		{
			SelectedChatId = Saved;
		}
	}
	FPromptEditorChats::EnsureChat(SelectedChatId, TEXT("New Agent"));
}

void SPromptEditorPanel::SaveSelectedChat() const
{
	if (!GConfig || SelectedChatId.IsEmpty())
	{
		return;
	}
	GConfig->SetString(TEXT("PromptEditor"), TEXT("SelectedChatId"), *SelectedChatId, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SPromptEditorPanel::RefreshChatCombo()
{
	Chats = FPromptEditorChats::List();
	bool bHasSelected = false;
	for (const FPromptEditorChatInfo& Chat : Chats)
	{
		if (Chat.Id == SelectedChatId)
		{
			bHasSelected = true;
			break;
		}
	}
	if (!bHasSelected)
	{
		EnsureDefaultChat();
		Chats = FPromptEditorChats::List();
	}

	TGuardValue<bool> Guard(bSyncingChats, true);
	ChatOptions.Reset();
	TSharedPtr<FString> Match;
	for (const FPromptEditorChatInfo& Chat : Chats)
	{
		TSharedPtr<FString> Opt = MakeShared<FString>(Chat.Id);
		if (Chat.Id == SelectedChatId)
		{
			Match = Opt;
		}
		ChatOptions.Add(Opt);
	}
	if (!Match.IsValid() && ChatOptions.Num() > 0)
	{
		Match = ChatOptions[0];
		SelectedChatId = *Match;
	}
	if (ChatCombo.IsValid())
	{
		ChatCombo->RefreshOptions();
		ChatCombo->SetSelectedItem(Match);
	}
}

void SPromptEditorPanel::RestoreChatLog()
{
	LogText.Reset();
	FeedImagePaths.Reset();
	if (FeedScroll.IsValid())
	{
		FeedScroll->ClearChildren();
	}
	AppendLog(TEXT("system"), TEXT("Tools → PromptEditor. 메시지 드래그 후 Ctrl+C. 그림은 링크를 누르세요."));
	TArray<FPromptEditorTurn> Turns;
	FPromptEditorChats::ListTurns(SelectedChatId, Turns);
	for (const FPromptEditorTurn& Turn : Turns)
	{
		if (!Turn.User.IsEmpty())
		{
			AppendLog(TEXT("user"), Turn.User);
		}
		for (const FString& Path : Turn.UserImages)
		{
			AddFeedImage(Path);
		}
		for (const FString& Path : Turn.GenImages)
		{
			AddFeedImage(Path);
		}
		if (!Turn.Reply.IsEmpty())
		{
			AppendLog(TEXT("model"), Turn.Reply);
		}
	}
}

void SPromptEditorPanel::OnChatChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type)
{
	if (bSyncingChats || !NewItem.IsValid())
	{
		return;
	}
	if (SelectedChatId == *NewItem)
	{
		return;
	}
	SelectedChatId = *NewItem;
	SaveSelectedChat();
	RestoreChatLog();
}

FReply SPromptEditorPanel::OnNewAgentClicked()
{
	SelectedChatId = FPromptEditorChats::NewChatId();
	FPromptEditorChats::EnsureChat(SelectedChatId, TEXT("New Agent"));
	SaveSelectedChat();
	RefreshChatCombo();
	RestoreChatLog();
	return FReply::Handled();
}

FReply SPromptEditorPanel::OnDeleteAgentClicked()
{
	if (bBusy)
	{
		return FReply::Handled();
	}

	const FString Title = FPromptEditorChats::TitleOf(SelectedChatId);
	const FText Confirm = FText::Format(
		LOCTEXT("DeleteAgentConfirm", "Delete chat \"{0}\"? This removes Saved/PromptEditor/chats files for this thread."),
		FText::FromString(Title));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Confirm) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FString DeletedId = SelectedChatId;
	if (!FPromptEditorChats::DeleteChat(DeletedId))
	{
		AppendLog(TEXT("error"), TEXT("채팅 폴더를 지우지 못했습니다."));
		return FReply::Handled();
	}

	TArray<FPromptEditorChatInfo> Remaining = FPromptEditorChats::List();
	if (Remaining.Num() > 0)
	{
		SelectedChatId = Remaining[0].Id;
	}
	else
	{
		SelectedChatId = PromptEditor::DefaultChatId;
		FPromptEditorChats::EnsureChat(SelectedChatId, TEXT("New Agent"));
	}
	SaveSelectedChat();
	RefreshChatCombo();
	RestoreChatLog();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
