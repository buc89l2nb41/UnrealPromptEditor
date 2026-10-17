#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "PromptEditorChats.h"
#include "HttpFwd.h"

class FJsonObject;
class SMultiLineEditableTextBox;
class SScrollBox;
class SWrapBox;

struct FPromptEditorAttachItem
{
	FString Path;
	TSharedPtr<FSlateDynamicImageBrush> Brush;
};

struct FPromptEditorModelParam
{
	FString Id;
	FString Value;
};

struct FPromptEditorModelVariant
{
	FString DisplayName;
	bool bDefault = false;
	TArray<FPromptEditorModelParam> Params;
};

struct FPromptEditorModelInfo
{
	FString Id;
	FString DisplayName;
	TArray<FPromptEditorModelVariant> Variants;
};

class SPromptEditorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPromptEditorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	void OnSendClicked();
	FReply OnSendButton();
	FReply OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& Event);
	void AppendLog(const FString& Role, const FString& Text);
	void SetBusy(bool bInBusy);
	void PostPrompt(const FString& Message);
	FText StatusLine() const;
	FText SelectedModelLabel() const;
	FText SelectedVariantLabel() const;
	TSharedRef<SWidget> MakeModelRow(TSharedPtr<FString> Item);
	TSharedRef<SWidget> MakeChatRow(TSharedPtr<FString> Item);
	void OnModelChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type SelectInfo);
	void OnVariantChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type SelectInfo);
	void EnsureFallbackModel();
	void LoadSavedModel();
	void SaveSelectedModel() const;
	void ApplyCatalog(const TArray<FPromptEditorModelInfo>& InCatalog);
	void RefreshVariantCombo();
	const FPromptEditorModelInfo* FindModel(const FString& Id) const;
	void RequestModels();
	void EnsureDefaultChat();
	void LoadSavedChat();
	void SaveSelectedChat() const;
	void RefreshChatCombo();
	void RestoreChatLog();
	FText SelectedChatLabel() const;
	void OnChatChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type SelectInfo);
	FReply OnNewAgentClicked();
	FReply OnDeleteAgentClicked();
	FReply OnStopClicked();
	void FinishPrompt();
	void PlaySendFinishedSound();
	void PostCancel();
	void StartStatusPolling();
	void StopStatusPolling();
	void PollStatus();
	EActiveTimerReturnType HandleStatusTimer(double CurrentTime, float DeltaTime);
	void ApplyStatusJson(const TSharedPtr<FJsonObject>& Root);
	FString CurrentChatId() const;
	void RefreshAttachChips();
	void AddFeedText(const FString& Role, const FString& Text);
	void AddFeedImage(const FString& Path);
	void ApplyStatusImages(const TSharedPtr<FJsonObject>& Root);
	void OpenDrawnImage(const FString& Path);
	void ScrollFeedToEnd();
	static FText ImageLinkCaption(const FString& Path);
	bool TryAddImageFile(const FString& Path);
	void AddPendingImage(const FString& Path);
	FReply OnAttachClicked();
	bool TryPasteClipboardImages();

	TSharedPtr<SMultiLineEditableTextBox> InputBox;
	TSharedPtr<SScrollBox> FeedScroll;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> VariantCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ChatCombo;
	TArray<TSharedPtr<FString>> ModelOptions;
	TArray<TSharedPtr<FString>> VariantOptions;
	TArray<TSharedPtr<FString>> ChatOptions;
	TArray<FPromptEditorChatInfo> Chats;
	TArray<FPromptEditorModelInfo> Catalog;
	TArray<FPromptEditorModelParam> SelectedParams;
	FString SelectedModelId;
	FString SelectedVariantName;
	FString SelectedChatId;
	FString LogText;
	TSharedPtr<SWrapBox> AttachWrap;
	TArray<FPromptEditorAttachItem> PendingImages;
	TArray<FString> FeedImagePaths;
	TArray<TSharedPtr<FSlateDynamicImageBrush>> OpenImageBrushes;
	FHttpRequestPtr InFlightPrompt;
	TSharedPtr<FActiveTimerHandle> StatusTimer;
	int32 StatusAfter = 0;
	int32 StatusFailCount = 0;
	bool bBusy = false;
	bool bStopRequested = false;
	bool bStatusInFlight = false;
	bool bSyncingCombos = false;
	bool bSyncingChats = false;
};
