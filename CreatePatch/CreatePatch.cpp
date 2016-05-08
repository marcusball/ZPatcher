//////////////////////////////////////////////////////////////////////////
//
// ZPatcher - Patcher system - Part of the ZUpdater suite
// Felipe "Zoc" Silveira - (c) 2016
//
//////////////////////////////////////////////////////////////////////////
//
// CreatePatch.cpp
// Functions to create the Patch data
//
//////////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include "CreatePatch.h"
#include "FileUtils.h"
#include "Lzma2Encoder.h"
#include <algorithm>
#include <assert.h>
#include "LogSystem.h"

ZPatcher::PatchFileList_t* ZPatcher::GetDifferences(std::string& oldVersion, std::string& newVersion)
{
	PatchFileList_t* patchFileList = new PatchFileList_t();

	std::vector<std::string> oldVersionFileList;
	GetFilesInDirectory(oldVersionFileList, oldVersion);

	std::vector<std::string> newVersionFileList;
	GetFilesInDirectory(newVersionFileList, newVersion);

	// Sort them now to avoid worries later. (Easier to find added/deleted files)
	std::sort(oldVersionFileList.begin(), oldVersionFileList.end());
	std::sort(newVersionFileList.begin(), newVersionFileList.end());

	unsigned int oldFileIndex = 0;
	unsigned int newFileIndex = 0;

	fprintf(stdout, "Detecting file differences between folders...\n");

	while (oldFileIndex < oldVersionFileList.size() && newFileIndex < newVersionFileList.size())
	{
		float progress = (oldFileIndex + newFileIndex) * 100.0f / (oldVersionFileList.size() + newVersionFileList.size());
		PrintCreatePatchProgressBar(progress, oldFileIndex + newFileIndex, oldVersionFileList.size() + newVersionFileList.size());

		const std::string& oldFileName = oldVersionFileList[oldFileIndex];
		const std::string& newFileName = newVersionFileList[newFileIndex];

		if (oldFileName == newFileName)
		{
			// Check if we are dealing with directories.
			size_t fileNameLength = oldFileName.length();
			if (fileNameLength > 0 && oldFileName[fileNameLength - 1] != '/')
			{
				// Check if the files have the same contents
				bool identical;
				bool success = AreFilesIdentical(oldVersion + "/" + oldFileName, newVersion + "/" + newFileName, identical);

				assert(success == true); // TODO: Handle this.
				fprintf(stdout, "\n\nError comparing files! Patch file might be inconsistent! Check the logs for details.\n\n");

				if (success && !identical)
				{
					patchFileList->ModifiedFileList.push_back(oldFileName);
				}
			}

			oldFileIndex++;
			newFileIndex++;

		}
		else if (oldFileName < newFileName)
		{
			// The new version doesn't contain this file.
			patchFileList->RemovedFileList.push_back(oldFileName);
			oldFileIndex++;
		}
		else
		{
			// The old version doesn't contain this file.
			patchFileList->AddedFileList.push_back(newFileName);
			newFileIndex++;
		}
	}

	// Okay, one of the lists is smaller than the other. Add or Remove all missing files, depending on the file list.
	while (oldFileIndex < oldVersionFileList.size())
	{
		float progress = (oldFileIndex + newFileIndex) * 100.0f / (oldVersionFileList.size() + newVersionFileList.size());
		PrintCreatePatchProgressBar(progress, oldFileIndex + newFileIndex, oldVersionFileList.size() + newVersionFileList.size());

		patchFileList->RemovedFileList.push_back(oldVersionFileList[oldFileIndex]);
		oldFileIndex++;
	}

	while (newFileIndex < newVersionFileList.size())
	{
		float progress = (newFileIndex + newFileIndex) * 100.0f / (newVersionFileList.size() + newVersionFileList.size());
		PrintCreatePatchProgressBar(progress, oldFileIndex + newFileIndex, oldVersionFileList.size() + newVersionFileList.size());

		patchFileList->RemovedFileList.push_back(newVersionFileList[newFileIndex]);
		newFileIndex++;
	}

	PrintCreatePatchProgressBar(100.0f, oldFileIndex + newFileIndex, oldVersionFileList.size() + newVersionFileList.size());
	fprintf(stdout, "\n\n");

	return patchFileList;
}

void ZPatcher::PrintCreatePatchProgressBar(const float& Percentage, const size_t& leftAmount, const size_t& rightAmount)
{
	int barWidth = 80;

	fprintf(stdout, "\xd[");

	int pos = (int)(barWidth * Percentage / 100.0f);
	for (int i = 0; i < barWidth; ++i)
	{
		if (i < pos) fprintf(stdout, "=");
		else if (i == pos) fprintf(stdout, ">");
		else fprintf(stdout, " ");
	}
	fprintf(stdout, "] %0.2f%% %llu/%llu", Percentage, leftAmount, rightAmount);
	fflush(stdout);
}

bool ZPatcher::CreatePatchFile(FILE* patchFile, std::string& newVersionPath, PatchFileList_t* patchFileList)
{
	// Initialize our custom LZMA2 Encoder
	CLzma2EncHandle hLzma2Enc = InitLzma2Encoder();

	fprintf(stdout, "Writing patch data...\n");

	Log(LOG, "Writing patch data");

	// Write the file header, including our custom LZMA2 props
	Byte props = Lzma2Enc_WriteProperties(hLzma2Enc);
	WritePatchFileHeader(patchFile, props);

	size_t totalFiles = patchFileList->RemovedFileList.size() + patchFileList->AddedFileList.size() + patchFileList->ModifiedFileList.size();

	unsigned int i = 0;
	// Process the removed file list in reverse order - Directories should be the last thing being deleted.
	for (std::vector<std::string>::reverse_iterator ritr = patchFileList->RemovedFileList.rbegin(); ritr != patchFileList->RemovedFileList.rend(); ++ritr)
	{
		PrintCreatePatchProgressBar(((float)++i / (float)totalFiles) * 100.0f, i, totalFiles);

		Log(LOG, "[del] %s", ritr->c_str());

		WriteFileInfo(patchFile, Patch_File_Delete, ritr->c_str());
	}

	for (std::vector<std::string>::iterator itr = patchFileList->AddedFileList.begin(); itr < patchFileList->AddedFileList.end(); ++itr)
	{
		PrintCreatePatchProgressBar(((float)++i / (float)totalFiles) * 100.0f, i, totalFiles);

		Log(LOG, "[add] %s", itr->c_str());

		size_t fileNameLength = itr->length();
		if (fileNameLength > 0 && (*itr)[fileNameLength - 1] != '/')
		{
			WriteFileInfo(patchFile, Patch_File_Add, itr->c_str());
			std::string localPath = newVersionPath + "/" + *itr;
			WriteCompressedFile(hLzma2Enc, localPath, patchFile);
		}
		else
		{
			WriteFileInfo(patchFile, Patch_Dir_Add, itr->c_str());
		}
	}

	// Right now, we replace both the modified files and the added files
	for (std::vector<std::string>::iterator itr = patchFileList->ModifiedFileList.begin(); itr < patchFileList->ModifiedFileList.end(); ++itr)
	{
		PrintCreatePatchProgressBar(((float)++i / (float)totalFiles) * 100.0f, i, totalFiles);

		Log(LOG, "[mod] %s", itr->c_str());

		WriteFileInfo(patchFile, Patch_File_Replace, itr->c_str());
		std::string localPath = newVersionPath + "/" + *itr;
		WriteCompressedFile(hLzma2Enc, localPath, patchFile);
	}

	PrintCreatePatchProgressBar(((float)++i / (float)totalFiles) * 100.0f, i, totalFiles);
	fprintf(stdout, "\n");

	Log(LOG, "Patch data writing process completed");

	DestroyLzma2EncHandle(hLzma2Enc);

	return true;
}

bool ZPatcher::CreatePatchFile(std::string& patchFileName, std::string& newVersionPath, PatchFileList_t* patchFileList)
{
	FILE* patchFile;
	errno_t err = 0;

	err = fopen_s(&patchFile, patchFileName.c_str(), "wb");
	if (err != 0)
	{
		const size_t buffer_size = 1024;
		char buffer[buffer_size];
		strerror_s(buffer, buffer_size, err);
		Log(LOG_FATAL, "Error opening file \"%s\" to write patch data: %s", patchFileName.c_str(), buffer);
		return false;
	}

	bool result = CreatePatchFile(patchFile, newVersionPath, patchFileList);

	fclose(patchFile);

	return result;
}
