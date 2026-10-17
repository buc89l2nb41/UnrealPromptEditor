#include "EditorPromptAttach.h"
#include "EditorPromptChats.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	FString UniqueDest(const FString& Dir, const FString& Stem, const FString& ExtWithDot)
	{
		FString Clean = Stem;
		Clean.ReplaceInline(TEXT("\\"), TEXT("_"));
		Clean.ReplaceInline(TEXT("/"), TEXT("_"));
		if (Clean.IsEmpty())
		{
			Clean = TEXT("image");
		}
		Clean = Clean.Left(40);
		const FString Tag = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
		return Dir / (Clean + TEXT("_") + Tag + ExtWithDot);
	}

	bool WriteBytes(const FString& Path, TArrayView<const uint8> Bytes, FString& OutError)
	{
		if (Bytes.Num() <= 0)
		{
			OutError = TEXT("empty image");
			return false;
		}
		if (static_cast<int64>(Bytes.Num()) > EditorPromptAttach::MaxBytes)
		{
			OutError = TEXT("image is larger than 12MB");
			return false;
		}
		TArray<uint8> Copy;
		Copy.Append(Bytes.GetData(), Bytes.Num());
		if (!FFileHelper::SaveArrayToFile(Copy, *Path))
		{
			OutError = TEXT("failed to write attachment");
			return false;
		}
		return true;
	}
}

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <shellapi.h>

namespace
{
	bool ClipboardPng(TArray<uint8>& OutBytes)
	{
		const UINT PngFormat = RegisterClipboardFormat(TEXT("PNG"));
		if (!PngFormat || !IsClipboardFormatAvailable(PngFormat))
		{
			return false;
		}
		HANDLE Handle = GetClipboardData(PngFormat);
		if (!Handle)
		{
			return false;
		}
		const SIZE_T Size = GlobalSize(Handle);
		const void* Data = GlobalLock(Handle);
		if (!Data || Size == 0)
		{
			if (Data)
			{
				GlobalUnlock(Handle);
			}
			return false;
		}
		OutBytes.SetNumUninitialized(static_cast<int32>(Size));
		FMemory::Memcpy(OutBytes.GetData(), Data, Size);
		GlobalUnlock(Handle);
		return OutBytes.Num() > 8;
	}

	bool ClipboardDibToPng(TArray<uint8>& OutPng)
	{
		if (!IsClipboardFormatAvailable(CF_DIB))
		{
			return false;
		}
		HANDLE Handle = GetClipboardData(CF_DIB);
		if (!Handle)
		{
			return false;
		}
		const SIZE_T DibBytes = GlobalSize(Handle);
		const uint8* Dib = static_cast<const uint8*>(GlobalLock(Handle));
		if (!Dib || DibBytes < sizeof(BITMAPINFOHEADER))
		{
			if (Dib)
			{
				GlobalUnlock(Handle);
			}
			return false;
		}

		const BITMAPINFOHEADER* Header = reinterpret_cast<const BITMAPINFOHEADER*>(Dib);
		const uint32 HeaderBytes = Header->biSize;
		if (HeaderBytes < sizeof(BITMAPINFOHEADER) || HeaderBytes > DibBytes)
		{
			GlobalUnlock(Handle);
			return false;
		}

		uint32 Extra = 0;
		if (HeaderBytes == sizeof(BITMAPINFOHEADER) && Header->biCompression == BI_BITFIELDS)
		{
			Extra = 12;
		}
		uint32 Palette = 0;
		if (Header->biBitCount <= 8)
		{
			const uint32 Used = Header->biClrUsed ? Header->biClrUsed : (1u << Header->biBitCount);
			Palette = Used * 4;
		}
		if (HeaderBytes + Extra + Palette > DibBytes)
		{
			GlobalUnlock(Handle);
			return false;
		}

		BITMAPFILEHEADER FileHeader;
		FMemory::Memzero(&FileHeader, sizeof(FileHeader));
		FileHeader.bfType = 0x4D42;
		FileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + HeaderBytes + Extra + Palette;
		FileHeader.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + DibBytes);

		TArray<uint8> Bmp;
		Bmp.Reserve(static_cast<int32>(sizeof(BITMAPFILEHEADER) + DibBytes));
		Bmp.Append(reinterpret_cast<const uint8*>(&FileHeader), sizeof(BITMAPFILEHEADER));
		Bmp.Append(Dib, static_cast<int32>(DibBytes));
		GlobalUnlock(Handle);

		FImage Image;
		if (!FImageUtils::DecompressImage(Bmp.GetData(), Bmp.Num(), Image))
		{
			return false;
		}
		TArray64<uint8> Png;
		if (!FImageUtils::CompressImage(Png, TEXT("png"), Image))
		{
			return false;
		}
		OutPng.SetNumUninitialized(static_cast<int32>(Png.Num()));
		FMemory::Memcpy(OutPng.GetData(), Png.GetData(), static_cast<SIZE_T>(Png.Num()));
		return OutPng.Num() > 0;
	}

	void ClipboardHdrop(TArray<FString>& OutFiles)
	{
		if (!IsClipboardFormatAvailable(CF_HDROP))
		{
			return;
		}
		HDROP Drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
		if (!Drop)
		{
			return;
		}
		const UINT Count = DragQueryFile(Drop, 0xFFFFFFFF, nullptr, 0);
		for (UINT Index = 0; Index < Count; ++Index)
		{
			const UINT Chars = DragQueryFile(Drop, Index, nullptr, 0);
			if (Chars == 0)
			{
				continue;
			}
			TArray<TCHAR> Buffer;
			Buffer.SetNumUninitialized(static_cast<int32>(Chars + 1));
			DragQueryFile(Drop, Index, Buffer.GetData(), Chars + 1);
			OutFiles.Add(FString(Buffer.GetData()));
		}
	}
}

#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool EditorPromptAttach::IsImageFile(const FString& Path)
{
	const FString Ext = FPaths::GetExtension(Path, false).ToLower();
	return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("webp")
		|| Ext == TEXT("gif") || Ext == TEXT("bmp") || Ext == TEXT("tif") || Ext == TEXT("tiff");
}

FString EditorPromptAttach::AttachDir(const FString& ChatId)
{
	const FString Dir = FEditorPromptChats::ChatDir(ChatId) / TEXT("attach");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return FPaths::ConvertRelativePathToFull(Dir);
}

bool EditorPromptAttach::ImportBytes(
	TArrayView<const uint8> Bytes,
	const FString& ChatId,
	const FString& Stem,
	const FString& ExtensionWithDot,
	FString& OutPath,
	FString& OutError)
{
	const FString Dir = AttachDir(ChatId);
	OutPath = UniqueDest(Dir, Stem, ExtensionWithDot);
	return WriteBytes(OutPath, Bytes, OutError);
}

bool EditorPromptAttach::ImportFile(const FString& Src, const FString& ChatId, FString& OutPath, FString& OutError)
{
	if (!FPaths::FileExists(Src) || !IsImageFile(Src))
	{
		OutError = TEXT("not an image file");
		return false;
	}
	const int64 Size = IFileManager::Get().FileSize(*Src);
	if (Size < 0 || Size > MaxBytes)
	{
		OutError = TEXT("image is larger than 12MB");
		return false;
	}

	const FString Dir = AttachDir(ChatId);
	const FString FullSrc = FPaths::ConvertRelativePathToFull(Src);
	FString FullDir = Dir;
	FPaths::NormalizeDirectoryName(FullDir);
	if (FullSrc.StartsWith(FullDir))
	{
		OutPath = FullSrc;
		return true;
	}

	OutPath = UniqueDest(Dir, FPaths::GetBaseFilename(Src), TEXT(".") + FPaths::GetExtension(Src, false).ToLower());
	if (IFileManager::Get().Copy(*OutPath, *FullSrc, true, true) != COPY_OK)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FullSrc) || !WriteBytes(OutPath, Bytes, OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("failed to copy attachment");
			}
			return false;
		}
	}
	return true;
}

bool EditorPromptAttach::ImportClipboard(const FString& ChatId, TArray<FString>& OutPaths, FString& OutError)
{
	OutPaths.Reset();
#if PLATFORM_WINDOWS
	if (!OpenClipboard(nullptr))
	{
		OutError = TEXT("clipboard is busy");
		return false;
	}

	TArray<uint8> Png;
	TArray<FString> Dropped;
	const bool bPng = ClipboardPng(Png);
	TArray<uint8> FromDib;
	const bool bDib = !bPng && ClipboardDibToPng(FromDib);
	if (!bPng && !bDib)
	{
		ClipboardHdrop(Dropped);
	}
	CloseClipboard();

	if (bPng)
	{
		FString Path;
		if (!ImportBytes(Png, ChatId, TEXT("clipboard"), TEXT(".png"), Path, OutError))
		{
			return false;
		}
		OutPaths.Add(Path);
		return true;
	}
	if (bDib)
	{
		FString Path;
		if (!ImportBytes(FromDib, ChatId, TEXT("clipboard"), TEXT(".png"), Path, OutError))
		{
			return false;
		}
		OutPaths.Add(Path);
		return true;
	}

	bool bAny = false;
	for (const FString& File : Dropped)
	{
		if (!IsImageFile(File))
		{
			continue;
		}
		FString Path;
		FString Error;
		if (ImportFile(File, ChatId, Path, Error))
		{
			OutPaths.Add(Path);
			bAny = true;
		}
		else if (OutError.IsEmpty())
		{
			OutError = Error;
		}
	}
	if (bAny)
	{
		OutError.Empty();
		return true;
	}
	if (OutError.IsEmpty())
	{
		OutError = TEXT("clipboard has no image");
	}
	return false;
#else
	OutError = TEXT("clipboard images are Windows-only");
	return false;
#endif
}

TSharedPtr<FSlateDynamicImageBrush> EditorPromptAttach::MakeThumbBrush(
	const FString& Path,
	int32 MaxEdge,
	bool bCheckerboard)
{
	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *Path) || FileBytes.Num() == 0)
	{
		return nullptr;
	}

	FImage Source;
	if (!FImageUtils::DecompressImage(FileBytes.GetData(), FileBytes.Num(), Source) || Source.SizeX < 1 || Source.SizeY < 1)
	{
		return nullptr;
	}

	if (bCheckerboard)
	{
		Source.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const int32 Width = Source.SizeX;
		const int32 Height = Source.SizeY;
		uint8* Pixels = Source.RawData.GetData();
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				uint8* Pixel = Pixels + (static_cast<int64>(Y) * Width + X) * 4;
				const float Alpha = Pixel[3] / 255.f;
				const bool bLight = (((X / 8) + (Y / 8)) & 1) == 0;
				const float Bg = bLight ? 210.f : 150.f;
				Pixel[0] = static_cast<uint8>(Pixel[0] * Alpha + Bg * (1.f - Alpha));
				Pixel[1] = static_cast<uint8>(Pixel[1] * Alpha + Bg * (1.f - Alpha));
				Pixel[2] = static_cast<uint8>(Pixel[2] * Alpha + Bg * (1.f - Alpha));
				Pixel[3] = 255;
			}
		}
	}

	const int32 ClampEdge = FMath::Max(1, MaxEdge);
	int32 DestX = Source.SizeX;
	int32 DestY = Source.SizeY;
	if (DestX > ClampEdge || DestY > ClampEdge)
	{
		if (DestX >= DestY)
		{
			DestY = FMath::Max(1, FMath::RoundToInt(float(DestY) * ClampEdge / float(DestX)));
			DestX = ClampEdge;
		}
		else
		{
			DestX = FMath::Max(1, FMath::RoundToInt(float(DestX) * ClampEdge / float(DestY)));
			DestY = ClampEdge;
		}
	}

	FImage Thumb;
	Source.ResizeTo(Thumb, DestX, DestY, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	TArray<uint8> Bgra;
	Bgra.Append(Thumb.RawData.GetData(), static_cast<int32>(Thumb.RawData.Num()));

	static int32 Serial = 0;
	const FName ResourceName(*FString::Printf(TEXT("EditorPromptThumb_%d"), ++Serial));
	return FSlateDynamicImageBrush::CreateWithImageData(
		ResourceName,
		FVector2D(static_cast<float>(DestX), static_cast<float>(DestY)),
		Bgra);
}
