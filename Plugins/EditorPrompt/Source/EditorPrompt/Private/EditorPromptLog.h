#pragma once

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEditorPrompt, Log, All);

namespace EditorPrompt
{
	constexpr uint32 McpPort = 18765;
	constexpr uint32 McpPortMax = 18779;
	constexpr uint32 SidecarPort = 18766;
	constexpr const TCHAR* DefaultModel = TEXT("composer-2.5");
	constexpr const TCHAR* DefaultChatId = TEXT("default");
}
