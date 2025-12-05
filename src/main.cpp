
#include <cstdint>
#include <filesystem>
#include <string>
#include <fstream>
#include <mutex>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#include "lz4.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "DirectXTex.h"

#define CHECKSZ(t, s) static_assert(sizeof(t) == s)

using uint64 = uint64_t;
using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8 = uint8_t;

std::string ReadFile(const std::filesystem::path& p);
void Print(const std::string& str);

std::mutex gLogMutex;
std::vector<std::string> gVecErrorMessages;

static void LogError(const std::string& fname, const std::string& str) {
	std::string err = std::format("File: '{}': {}", fname, str);
	Print(err);
	std::scoped_lock l(gLogMutex);
	gVecErrorMessages.emplace_back(std::move(err));
}

template<typename T>
bool Read(char*& buf, char* end, T& res, bool advance = true) {
	if ( buf + sizeof(T) >= end ) {
		return false;
	}
	res = *(T*)buf;
	if ( advance )
		buf += sizeof(T);
	return true;
}


enum class TCOLayout : int {
	BC1, BC2, BC3, BC4, BC5,
	_Not_Used_,
	R11G11B10, RGBA8, RG16,
	R16, R32, R32G8, R24G8,
	R8
};
CHECKSZ(TCOLayout, 0x4);

static const char* ToString(TCOLayout layout) {
	switch(layout) {
		case TCOLayout::BC1:
			return "BC1";
		case TCOLayout::BC2:
			return "BC2";
		case TCOLayout::BC3:
			return "BC3";
		case TCOLayout::BC4:
			return "BC4";
		case TCOLayout::BC5:
			return "BC5";
		case TCOLayout::_Not_Used_:
			return "NOT USED";
		case TCOLayout::R11G11B10:
			return "R11G11B10";
		case TCOLayout::RGBA8:
			return "RGBA8";
		case TCOLayout::RG16:
			return "RG16";
		case TCOLayout::R16:
			return "R16";
		case TCOLayout::R32:
			return "R32";
		case TCOLayout::R32G8:
			return "R32G8";
		case TCOLayout::R24G8:
			return "R24G8";
		case TCOLayout::R8:
			return "R8";
		default:
			return "ERROR";
	}
}

struct BaseHeader {
	uint32 flag;
};

struct CompressedDataHeader : BaseHeader {
	char _unk[0x8];
	uint32 dataHeaderSize;
	uint32 compressedSize;
	uint32 decompressedSize;
};
CHECKSZ(CompressedDataHeader, 0x18);

struct TCOHeader : BaseHeader {
	uint32 width;
	uint32 height;
	TCOLayout layout;
	uint32 numMips;
	bool flipV;
	char _pad[0x3];
};
CHECKSZ(TCOHeader, 0x18);

void ProcessOneFile(const std::filesystem::path& path);
void DecompressBC(const std::string& fName, uint8* source, uint64 sourceSize, uint8*& dst, uint32& numChannels, const TCOHeader& header);

int main() {
	if ( !std::filesystem::exists("./Textures") ) {
		Print("./Textures directory did not exist. Make sure the program is running in Scrap Mechanic/Cache/ !");
		return 0;
	}

	if ( !std::filesystem::exists("./Textures_OUT") ) {
		try {
			std::filesystem::create_directory("./Textures_OUT");
		} catch(...) {
			Print("failed to create ./Textures_OUT directory! Make sure the directory has write permissions or create it yourself.");
			return 0;
		}
	}

	std::vector<std::filesystem::path> vecCachedFiles;
	for ( const auto& file : std::filesystem::directory_iterator("./Textures/") ) {
		if ( file.is_regular_file() && file.path().filename().string().ends_with(".tco") )
			vecCachedFiles.emplace_back(file.path());
	}

	Print(std::format("Found {} TCO files", vecCachedFiles.size()));

	if ( vecCachedFiles.empty() )
		return 0;

	uint32 numThreads = std::max(std::thread::hardware_concurrency(), 1u);

	Print(std::format("Using {} threads", numThreads));

	uint64 fileCount = vecCachedFiles.size();
	uint64 chunkSize = (fileCount + numThreads - 1) / numThreads;

	std::vector<std::thread> vecThreads;
	for ( uint32 i = 0; i < numThreads; ++i ) {
		uint64 startIndex = i * chunkSize;
		uint64 endIndex = std::min(startIndex + chunkSize, fileCount);

		vecThreads.emplace_back([&vecCachedFiles, startIndex, endIndex](){
			for ( uint64 i = startIndex; i < endIndex; ++i )
				ProcessOneFile(vecCachedFiles[i]);
		});
	}

	for ( std::thread& th : vecThreads )
		th.join();


	if ( !gVecErrorMessages.empty() ) {
		Print("\n\n-------------------------------------------------\n");
		Print("The following ERRORS were encountered:\n");
		for ( const std::string& err : gVecErrorMessages )
			Print(err);
	}

	Print("\n\n-------------------------------------------------\n");
	Print("CacheDumper Finished.");

	return 0;
}



void ProcessOneFile(const std::filesystem::path& path) {
	std::string fName = path.filename().string();

	Print(std::format("\nReading TCO file '{}'", fName));

	std::string data = ReadFile(path);
	if ( data.empty() )
		return;

	if ( data.size() < sizeof(TCOHeader) )
		return LogError(fName, "File is incomplete or malformed");

	char* pData = data.data();
	char* pDataEnd = pData + data.size();

	BaseHeader baseHeader;
	if ( !Read(pData, pDataEnd, baseHeader, false) )
		return LogError(fName, "Failed to read base header");

	if ( baseHeader.flag != 0x4 )
		return LogError(fName, std::format("ERROR: File has unsupported type flag: {}", baseHeader.flag));

	CompressedDataHeader compHeader;
	if ( !Read(pData, pDataEnd, compHeader) )
		return LogError(fName, "Failed to read compressed data header");

	Print(std::format(
		"File is COMPRESSED: compressedSize: {}, decompressedSize: {}, dataHeaderSize: {}",
		compHeader.compressedSize, compHeader.decompressedSize, compHeader.dataHeaderSize
	));

	if ( compHeader.dataHeaderSize != sizeof(TCOHeader) )
		return LogError(fName, "File dataHeaderSize did not match TCOHeader size");

	TCOHeader tcoHeader;
	if ( !Read(pData, pDataEnd, tcoHeader) )
		return LogError(fName, "Failed to read TCO header");

	Print(std::format(
		"TCO header: width: {}, height: {}, layout: {}, numMips: {}, flipV: {}\n",
		tcoHeader.width, tcoHeader.height, ToString(tcoHeader.layout), tcoHeader.numMips, tcoHeader.flipV
	));

	char* pDecData = new char[compHeader.decompressedSize];
	char* pDecDataEnd = pDecData + compHeader.decompressedSize;

	int res = LZ4_decompress_safe(pData, pDecData, compHeader.compressedSize, compHeader.decompressedSize);
	if ( res <= 0 ) {
		delete[] pDecData;
		return LogError(fName, "Failed to decompress file data");
	}

	uint8* p8BitData = nullptr;
	uint32 numChannels = 0;

	switch( tcoHeader.layout ) {
		case TCOLayout::BC1:
		case TCOLayout::BC2:
		case TCOLayout::BC3:
		case TCOLayout::BC4:
		case TCOLayout::BC5:
			DecompressBC(fName, (uint8*)pDecData, compHeader.decompressedSize, p8BitData, numChannels, tcoHeader);
			break;
		case TCOLayout::R11G11B10: {
			// It claims to be R11G11B10 but the actual data is just standard RGBA - wtf?
			numChannels = 4;
			p8BitData = (uint8*)pDecData;
			break;
		}
		case TCOLayout::RGBA8: {
			numChannels = 4;
			p8BitData = (uint8*)pDecData;
			break;
		}
		case TCOLayout::RG16: {
			numChannels = 2;
			p8BitData = new uint8[(tcoHeader.width * tcoHeader.height) * numChannels];

			uint32 i = 0;
			for ( uint16* pChannel = (uint16*)pDecData; pChannel != (uint16*)pDecDataEnd; pChannel += 2 ) {
				p8BitData[i * 2 + 0] = uint8((float(*(pChannel + 0)) / UINT16_MAX) * UINT8_MAX);
				p8BitData[i * 2 + 1] = uint8((float(*(pChannel + 1)) / UINT16_MAX) * UINT8_MAX);
				++i;
			}
			break;
		}
		case TCOLayout::R16: {
			numChannels = 1;
			p8BitData = new uint8[(tcoHeader.width * tcoHeader.height) * numChannels];

			uint32 i = 0;
			for ( uint16* pPix = (uint16*)pDecData; pPix != (uint16*)pDecDataEnd; pPix += 2 ) {
				p8BitData[i] = uint8((float(*pPix) / UINT16_MAX) * UINT8_MAX);
				++i;
			}
			break;
		}
		case TCOLayout::R32: {
			numChannels = 1;
			p8BitData = new uint8[(tcoHeader.width * tcoHeader.height) * numChannels];

			uint32 i = 0;
			for ( uint16* pPix = (uint16*)pDecData; pPix != (uint16*)pDecDataEnd; pPix += 2 ) {
				p8BitData[i] = uint8((float(*pPix) / UINT_MAX) * UINT8_MAX);
				++i;
			}
			break;
		}
		case TCOLayout::R32G8: {
			numChannels = 2;
			p8BitData = new uint8[(tcoHeader.width * tcoHeader.height) * numChannels];

			uint32 i = 0;
			uint8* pPixel = (uint8*)pDecData;
			while( pPixel != (uint8*)pDecDataEnd ) {
				uint16 red = *(uint16*)pPixel;
				uint8 green = *(uint8*)(pPixel + sizeof(uint16));
				pPixel += 0x3;
				p8BitData[i * 2 + 0] = uint8((float(red) / UINT16_MAX) * UINT8_MAX);
				p8BitData[i * 2 + 1] = green;
				++i;
			}

			break;
		}
		case TCOLayout::R24G8: {
			numChannels = 2;
			p8BitData = new uint8[(tcoHeader.width * tcoHeader.height) * numChannels];

			uint32 i = 0;
			uint32* pPixel = (uint32*)pDecData;
			while ( pPixel != (uint32*)pDecDataEnd ) {
				uint32 pix = *pPixel;
				uint32 red = pix & 0xFFFFFF00;
				uint8 green = (pix << 0x18) & 0xFF;

				pPixel += 0x3;
				p8BitData[i * 2 + 0] = uint8((float(red) / 0xFFFFFF00) * UINT8_MAX);
				p8BitData[i * 2 + 1] = green;
				++i;
			}

			break;
		}
		case TCOLayout::R8: {
			numChannels = 1;
			p8BitData = (uint8*)pDecData;
			break;
		}
		default:
			delete[] pDecData;
			return LogError(fName, std::format("TCO Layout ({}) is not currently supported", int(tcoHeader.layout)));
	}

	if ( p8BitData == nullptr ) {
		delete[] pDecData;
		return;
	}

	std::string outName = std::format("./Textures_OUT/{}.tga", fName);

	stbi_flip_vertically_on_write(!tcoHeader.flipV);
	res = stbi_write_tga(outName.c_str(), tcoHeader.width, tcoHeader.height, numChannels, p8BitData);
	if ( !res )
		LogError(fName, "Failed to write image to disk");
	else
		Print(std::format("Wrote output file '{}'", outName));

	if ( p8BitData != (uint8*)pDecData )
		delete[] pDecData;
	delete[] p8BitData;
}

void DecompressBC(const std::string& fName, uint8* source, uint64 sourceSize, uint8*& dst, uint32& numChannels, const TCOHeader& header) {
	dst = nullptr;
	numChannels = 4;
	if ( header.layout == TCOLayout::BC4 )
		numChannels = 1;
	else if ( header.layout == TCOLayout::BC5 )
		numChannels = 2;

	DXGI_FORMAT sourceFormat;
	switch(header.layout) {
		case TCOLayout::BC1:
			sourceFormat = DXGI_FORMAT_BC1_TYPELESS;
			break;
		case TCOLayout::BC2:
			sourceFormat = DXGI_FORMAT_BC2_TYPELESS;
			break;
		case TCOLayout::BC3:
			sourceFormat = DXGI_FORMAT_BC3_TYPELESS;
			break;
		case TCOLayout::BC4:
			sourceFormat = DXGI_FORMAT_BC4_TYPELESS;
			break;
		case TCOLayout::BC5:
			sourceFormat = DXGI_FORMAT_BC5_TYPELESS;
			break;
	}

	DXGI_FORMAT dstFormat;
	switch(header.layout) {
		case TCOLayout::BC1:
		case TCOLayout::BC2:
		case TCOLayout::BC3:
			dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case TCOLayout::BC4:
			dstFormat = DXGI_FORMAT_R8_UNORM;
			break;
		case TCOLayout::BC5:
			dstFormat = DXGI_FORMAT_R8G8_UNORM;
			break;
	}

	uint64 size = (header.width * header.height) * numChannels;
	uint8* p8BitData = new uint8[size];
	//memset(p8BitData, 0xFFFFFFFF, size);

	DirectX::TexMetadata meta;
	meta.width = header.width;
	meta.height = header.height;
	meta.depth = 1;
	meta.arraySize = 1;
	meta.mipLevels = header.numMips;
	meta.miscFlags = 0;
	meta.miscFlags2 = 0;
	meta.format = sourceFormat;
	meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

	DirectX::ScratchImage compImage;
	HRESULT hRes = compImage.Initialize(meta);
	if ( FAILED(hRes) ) {
		LogError(fName, "Failed to initialize DX ScratchImage");
		return;
	}

	memcpy(compImage.GetPixels(), source, sourceSize);
	DirectX::ScratchImage resImage;

	hRes = DirectX::Decompress(
		compImage.GetImages(), compImage.GetImageCount(), compImage.GetMetadata(), dstFormat, resImage
	);
	if ( FAILED(hRes) ) {
		LogError(fName, "Failed to decompress image data");
		return;
	}

	const DirectX::Image* pRes = resImage.GetImage(0, 0, 0);

	memcpy(p8BitData, pRes->pixels, std::min(pRes->slicePitch, size));

	compImage.Release();
	resImage.Release();

	dst = p8BitData;
}

std::string ReadFile(const std::filesystem::path& p) {
	try {
		if ( !std::filesystem::exists(p) )
			return {};

		uint64 size = std::filesystem::file_size(p);
		std::string data(size, '\0');
		std::ifstream file;
		file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		file.open(p, std::ios::binary);
		file.read(data.data(), size);
		file.close();
		return data;

	} catch ( const std::exception& e ) {
		std::string s = std::format("Failed to read file '{}': {}", p.string(), e.what());
		MessageBoxA(nullptr, s.data(), "File Read Error", MB_OK);
		return {};
	}
}

void Print(const std::string& str) {
	std::scoped_lock l(gLogMutex);
	std::printf("%s\n", str.c_str());
}
