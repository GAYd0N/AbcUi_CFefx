// framework::ImageLoader implementation - see ImageLoader.h.
#include "ImageLoader.h"
#include "ResourceFile.h"
#include "ResourceLimits.h"
#include <algorithm>
#include <memory>

#include <FreeImage.h>

#include <cstdio>
#include <cstring>

namespace framework {

namespace ImageLoader {

namespace {

using BitmapHandle = std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)>;

bool validDimensions(FIBITMAP* Bitmap) {
	if (!Bitmap)
		return false;
	const unsigned Width = FreeImage_GetWidth(Bitmap);
	const unsigned Height = FreeImage_GetHeight(Bitmap);
	if (Width > ResourceLimits::MaxDimension || Height > ResourceLimits::MaxDimension)
		return false;
	std::size_t Bytes;
	return ResourceLimits::imageBytes(static_cast<int>(Width), static_cast<int>(Height), Bytes);
}

// FreeImage reads through a FreeImageIO callback table; these adapters run
// that interface over a plain memory span so buffers decode like files.
struct MemoryStream {
	const unsigned char* Data = nullptr;
	std::size_t Size = 0;
	std::size_t Cursor = 0;
};

unsigned DLL_CALLCONV streamRead(void* buffer, unsigned size, unsigned count, fi_handle handle) {
	auto* stream = static_cast<MemoryStream*>(handle);
	if (size == 0)
		return 0;
	if (stream->Cursor > stream->Size || count > ResourceLimits::MaxFileBytes / size) return 0;
	const std::size_t requested = static_cast<std::size_t>(size) * count;
	const std::size_t remaining = stream->Size - stream->Cursor;
	const std::size_t copied = requested < remaining ? requested : remaining;
	memcpy(buffer, stream->Data + stream->Cursor, copied);
	stream->Cursor += copied;
	return static_cast<unsigned>(copied / size);
}

int DLL_CALLCONV streamSeek(fi_handle handle, long offset, int origin) {
	auto* stream = static_cast<MemoryStream*>(handle);
	std::size_t base = 0;
	if (origin == SEEK_CUR)
		base = stream->Cursor;
	else if (origin == SEEK_END)
		base = stream->Size;
	const long long target = static_cast<long long>(base) + offset;
	if (target < 0 || target > static_cast<long long>(stream->Size))
		return -1;
	stream->Cursor = static_cast<std::size_t>(target);
	return 0;
}

long DLL_CALLCONV streamTell(fi_handle handle) {
	auto* stream = static_cast<MemoryStream*>(handle);
	return static_cast<long>(stream->Cursor);
}

// Decodes one loaded bitmap into out (32bpp, top-down, RGBA byte order) and
// releases it. Takes ownership: the input is unloaded on every path.
bool decodeToRgba(FIBITMAP* bitmap, DecodedImage& out, const PaletteTransform& Transform = {}) {
	BitmapHandle Input(bitmap, FreeImage_Unload);
	if (!validDimensions(bitmap))
		return false;
	const unsigned BitsPerPixel = FreeImage_GetBPP(bitmap);
	if (Transform && FreeImage_GetImageType(bitmap) == FIT_BITMAP && BitsPerPixel > 0 && BitsPerPixel <= 8) {
		if (RGBQUAD* Palette = FreeImage_GetPalette(bitmap)) {
			const unsigned Count = (std::min)(FreeImage_GetColorsUsed(bitmap), 256u);
			for (unsigned I = 0; I < Count; ++I) {
				PaletteColor Color{Palette[I].rgbRed, Palette[I].rgbGreen, Palette[I].rgbBlue};
				Transform(I, Color);
				Palette[I].rgbRed = Color.Red;
				Palette[I].rgbGreen = Color.Green;
				Palette[I].rgbBlue = Color.Blue;
			}
		}
	}
	// ConvertTo32Bits always returns a fresh bitmap (it clones 32bpp input),
	// so the loaded original is released before the result takes over.
	FIBITMAP* rgba = FreeImage_ConvertTo32Bits(bitmap);
	Input.reset();
	if (!rgba)
		return false;

	const int width = FreeImage_GetWidth(rgba);
	const int height = FreeImage_GetHeight(rgba);
	std::size_t bytes;
	if (!ResourceLimits::imageBytes(width,height,bytes)) {
		FreeImage_Unload(rgba);
		return false;
	}

	std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> owned(rgba,FreeImage_Unload);
	// FreeImage scans bottom-up BGRA; the output is top-down RGBA.
	FreeImage_FlipVertical(rgba);
	const int stride = width * 4;
	out.Pixels.resize(static_cast<std::size_t>(stride) * height);
	for (int y = 0; y < height; y++) {
		const BYTE* src = FreeImage_GetScanLine(rgba, y);
		unsigned char* dst = out.Pixels.data() + static_cast<std::size_t>(y) * stride;
		for (int x = 0; x < width; x++) {
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}
	out.Width = width;
	out.Height = height;
	return true;
}

bool loadStream(FreeImageIO& io, fi_handle handle, DecodedImage& out, const PaletteTransform& Transform,
	FREE_IMAGE_FORMAT fallbackFormat = FIF_UNKNOWN) {
	FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromHandle(&io, handle, 0);
	if (format == FIF_UNKNOWN)
		format = fallbackFormat;
	if (format == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(format))
		return false;
	// The format sniff left the cursor mid-stream; plugin loaders read from
	// the start.
	if (FreeImage_FIFSupportsNoPixels(format)) {
		streamSeek(handle, 0, SEEK_SET);
		BitmapHandle Header(FreeImage_LoadFromHandle(format, &io, handle, FIF_LOAD_NOPIXELS), FreeImage_Unload);
		if (!validDimensions(Header.get()))
			return false;
	}
	streamSeek(handle, 0, SEEK_SET);
	return decodeToRgba(FreeImage_LoadFromHandle(format, &io, handle, 0), out, Transform);
}

// Builds a bottom-up 32-bit FIBITMAP from a top-down RGBA image. FreeImage's
// own scan order is bottom-up, so the rows are written in reverse and read
// back the same way; no other code sees the flip.
FIBITMAP* toBitmap(const DecodedImage& Image) {
	FIBITMAP* Bitmap = FreeImage_Allocate(Image.Width, Image.Height, 32,
		FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if (!Bitmap)
		return nullptr;
	const int Stride = Image.Width * 4;
	for (int Y = 0; Y < Image.Height; ++Y) {
		BYTE* Line = FreeImage_GetScanLine(Bitmap, Image.Height - 1 - Y);
		const unsigned char* Source = Image.Pixels.data() + static_cast<std::size_t>(Y) * Stride;
		for (int X = 0; X < Image.Width; ++X) {
			Line[X * 4 + FI_RGBA_RED] = Source[X * 4 + 0];
			Line[X * 4 + FI_RGBA_GREEN] = Source[X * 4 + 1];
			Line[X * 4 + FI_RGBA_BLUE] = Source[X * 4 + 2];
			Line[X * 4 + FI_RGBA_ALPHA] = Source[X * 4 + 3];
		}
	}
	return Bitmap;
}

// Reads a 32-bit FIBITMAP back into a top-down RGBA image (the inverse of
// toBitmap; truecolor input only).
bool fromBitmap(FIBITMAP* Bitmap, DecodedImage& Output) {
	Output = DecodedImage();
	if (!validDimensions(Bitmap))
		return false;
	BitmapHandle Owned(Bitmap, FreeImage_Unload);
	const int Width = static_cast<int>(FreeImage_GetWidth(Bitmap));
	const int Height = static_cast<int>(FreeImage_GetHeight(Bitmap));
	const int Stride = Width * 4;
	Output.Pixels.resize(static_cast<std::size_t>(Stride) * Height);
	for (int Y = 0; Y < Height; ++Y) {
		const BYTE* Line = FreeImage_GetScanLine(Bitmap, Height - 1 - Y);
		unsigned char* Target = Output.Pixels.data() + static_cast<std::size_t>(Y) * Stride;
		for (int X = 0; X < Width; ++X) {
			Target[X * 4 + 0] = Line[X * 4 + FI_RGBA_RED];
			Target[X * 4 + 1] = Line[X * 4 + FI_RGBA_GREEN];
			Target[X * 4 + 2] = Line[X * 4 + FI_RGBA_BLUE];
			Target[X * 4 + 3] = Line[X * 4 + FI_RGBA_ALPHA];
		}
	}
	Output.Width = Width;
	Output.Height = Height;
	return true;
}

} // namespace

bool loadFromFile(const char* path, DecodedImage& out) {
	out = DecodedImage();
	if (!path || !*path)
		return false;
	std::vector<unsigned char> bytes;
	if (!ResourceFile::read(path, bytes, ResourceLimits::MaxFileBytes))
		return false;

	// Engine filesystem first: the resource may live in a mounted
	// *_downloads/*_addon tree or inside a pack. Decode from the resident
	// bytes; the extension is only a hint for headerless formats (TGA).
	FreeImageIO io = {};
	io.read_proc = streamRead;
	io.seek_proc = streamSeek;
	io.tell_proc = streamTell;
	MemoryStream stream;
	stream.Data = bytes.data();
	stream.Size = bytes.size();
	return loadStream(io, &stream, out, {}, FreeImage_GetFIFFromFilename(path));
}

bool loadFromMemory(const unsigned char* Data, std::size_t Size, DecodedImage& Output,
	const PaletteTransform& Transform) {
	Output = DecodedImage();
	if (!Data || Size == 0 || Size > ResourceLimits::MaxFileBytes)
		return false;
	FreeImageIO io = {};
	io.read_proc = streamRead;
	io.seek_proc = streamSeek;
	io.tell_proc = streamTell;
	MemoryStream stream;
	stream.Data = Data;
	stream.Size = Size;
	return loadStream(io, &stream, Output, Transform);
}

bool rescale(const DecodedImage& Input, int Width, int Height, DecodedImage& Output) {
	Output = DecodedImage();
	if (!Input.valid() || Width <= 0 || Height <= 0)
		return false;
	std::size_t Bytes = 0;
	if (!ResourceLimits::imageBytes(Width, Height, Bytes))
		return false;
	BitmapHandle Source(toBitmap(Input), FreeImage_Unload);
	if (!Source)
		return false;
	return fromBitmap(FreeImage_Rescale(Source.get(), Width, Height, FILTER_BILINEAR), Output);
}

bool quantizeIndexed(const DecodedImage& Input, int MaxColors, IndexedImage& Output) {
	Output = IndexedImage();
	if (!Input.valid() || MaxColors < 1 || MaxColors > 256)
		return false;
	BitmapHandle Source(toBitmap(Input), FreeImage_Unload);
	if (!Source)
		return false;
	BitmapHandle Quantized(FreeImage_ColorQuantizeEx(Source.get(), FIQ_WUQUANT, MaxColors),
		FreeImage_Unload);
	if (!Quantized)
		return false;
	const int Width = static_cast<int>(FreeImage_GetWidth(Quantized.get()));
	const int Height = static_cast<int>(FreeImage_GetHeight(Quantized.get()));
	if (!validDimensions(Quantized.get()))
		return false;
	RGBQUAD* Palette = FreeImage_GetPalette(Quantized.get());
	if (!Palette)
		return false;
	const unsigned Count = (std::min)(FreeImage_GetColorsUsed(Quantized.get()),
		static_cast<unsigned>(MaxColors));
	if (Count == 0)
		return false;
	Output.Palette.resize(Count);
	for (unsigned I = 0; I < Count; ++I)
		Output.Palette[I] = {Palette[I].rgbRed, Palette[I].rgbGreen, Palette[I].rgbBlue};
	Output.Indices.resize(static_cast<std::size_t>(Width) * Height);
	for (int Y = 0; Y < Height; ++Y) {
		const BYTE* Line = FreeImage_GetScanLine(Quantized.get(), Height - 1 - Y);
		memcpy(Output.Indices.data() + static_cast<std::size_t>(Y) * Width, Line,
			static_cast<std::size_t>(Width));
	}
	Output.Width = Width;
	Output.Height = Height;
	return true;
}

}

}
