#pragma once
#include <iostream>
#include <memory>
#include <Windows.h>

#define ALIGN_UP(value, alignment) ((value + alignment - 1) & ~(alignment - 1))

namespace RyujinUtils {

	inline std::pair<BOOL, uintptr_t> MapPortableExecutableFileIntoMemory(const std::string& m_strInputFilePath, std::shared_ptr<unsigned char>& mappedPE) {

		auto hFile = ::CreateFileA(

			_In_ m_strInputFilePath.c_str(),
			_In_ GENERIC_READ,
			_In_ FILE_SHARE_READ | FILE_SHARE_DELETE,
			_In_opt_ nullptr,
			_In_ OPEN_EXISTING,
			_In_ FILE_ATTRIBUTE_NORMAL,
			_In_opt_ nullptr

		);

		if (hFile == INVALID_HANDLE_VALUE) {

			OutputDebugStringA(
				
				_In_opt_ "RyujinUtils::MapExecutableFileIntoMemory: failed because cannot open a handle for input PE FILE\n"
			
			);

			return std::make_pair(FALSE, 0);
		}

		LARGE_INTEGER szFile;
		if (!::GetFileSizeEx(
			_In_ hFile,
			_Out_ &szFile
		)) {

			::OutputDebugStringA(

				_In_opt_ "RyujinUtils::MapExecutableFileIntoMemory: was not possible to get filesizeru for the PE FILE\n"

			);

			::CloseHandle(

				_In_ hFile

			);

			return std::make_pair(FALSE, 0);
		}

		auto hMap = ::CreateFileMappingA(

			_In_ hFile,
			_In_opt_ nullptr,
			_In_ PAGE_READONLY | SEC_IMAGE,
			_In_ NULL,
			_In_ NULL,
			_In_opt_ nullptr

		);

		if (!hMap) {

			::OutputDebugStringA(

				_In_opt_ "RyujinUtils::MapExecutableFileIntoMemory: was not possible to create a mapping to the PE FILE.\n"

			);

			::CloseHandle(

				_In_ hMap

			);

			return std::make_pair(FALSE, 0);
		}

		mappedPE = std::shared_ptr<unsigned char>(

			reinterpret_cast<unsigned char*>(

				::MapViewOfFile(
					
					_In_ hMap,
					_In_ FILE_MAP_READ,
					_In_ NULL,
					_In_ NULL,
					_In_ NULL
				
				)

			),
			[](unsigned char* p) {

				if (p) ::UnmapViewOfFile(
					
					_In_ p
				
				);

			}

		);

		::CloseHandle(

			_In_ hMap

		);

		::CloseHandle(
		
			_In_ hFile

		);

		return std::make_pair(TRUE, szFile.QuadPart);
	}

	inline void randomizeSectionName(char* chName) {

		const char charset[] { "abcdefghijklmnopqrstuvwxyz" };
		for (size_t i = 0; i < 8 - 1; ++i) chName[i] = charset[std::rand() % (sizeof(charset) - 1)];
		chName[8 - 1] = '\0';

	}

	inline BOOL SaveBuffer(const std::string& strPath, unsigned char* ucBuffer, uintptr_t szBuffer) {

		auto hFile = ::CreateFileA(

			_In_ strPath.c_str(),
			_In_ GENERIC_WRITE,
			_In_ 0,
			_In_opt_ nullptr,
			_In_ CREATE_ALWAYS,
			_In_ FILE_ATTRIBUTE_NORMAL,
			_In_opt_ nullptr

		);

		if (hFile == INVALID_HANDLE_VALUE) return FALSE;

		DWORD szWritten{ 0 };
		::WriteFile(

			_In_ hFile,
			_In_ ucBuffer,
			_In_ szBuffer,
			_Out_ &szWritten,
			_Inout_opt_ nullptr

		);

		::CloseHandle(

			_In_ hFile

		);

		return szWritten == szBuffer;
	}

	inline std::pair<unsigned char*, SIZE_T> MapDiskPE(const std::string& strPath) {

		auto hFile = ::CreateFileA(

			_In_ strPath.c_str(),
			_In_ GENERIC_READ,
			_In_ FILE_SHARE_READ,
			_In_opt_ nullptr,
			_In_ OPEN_EXISTING,
			_In_ FILE_ATTRIBUTE_NORMAL,
			_In_opt_ nullptr

		);

		if (hFile == INVALID_HANDLE_VALUE) return { nullptr, 0 };

		LARGE_INTEGER fileSize;
		if (!::GetFileSizeEx(
			
			_In_ hFile,
			_Out_ &fileSize
		
		) || fileSize.QuadPart == 0) {

			::CloseHandle(
				
				_In_ hFile
			
			);

			return { nullptr, 0 };
		}

		SIZE_T outSize = static_cast<SIZE_T>(fileSize.QuadPart);

		HANDLE hMapping = ::CreateFileMappingA(

			_In_ hFile,
			_In_opt_ nullptr,
			_In_ PAGE_READONLY,
			_In_ 0,
			_In_ 0,
			_In_opt_ nullptr

		);

		if (!hMapping) {

			::CloseHandle(
				
				_In_ hFile
			
			);

			return { nullptr, 0 };
		}

		auto outData = static_cast<unsigned char*>(::MapViewOfFile(

			_In_ hMapping,
			_In_ FILE_MAP_READ,
			_In_ 0,
			_In_ 0,
			_In_ 0

		));

		::CloseHandle(
			
			_In_ hMapping
		
		);

		::CloseHandle(
			
			_In_ hFile
		
		);

		if (!outData) return { nullptr, 0 };

		return { outData, outSize };
	}

};

