#include "stdafx.h"
#pragma hdrstop

#include "fs_internal.h"

#include <functional>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <sstream>
#include "mezz_stringbuffer.h"

XRCORE_API CInifile const* pSettings = NULL;
XRCORE_API CInifile const* pSettingsAuth = NULL;

//#define INICACHE_PRINT_DEBUG

CInifile* CInifile::Create(const char* szFileName, BOOL ReadOnly)
{
	return xr_new<CInifile>(szFileName, ReadOnly);
}

void CInifile::Destroy(CInifile* ini)
{
	xr_delete(ini);
}

bool sect_pred(const CInifile::Sect* x, LPCSTR val)
{
	return xr_strcmp(*x->Name, val) < 0;
};

bool item_pred(const CInifile::Item& x, LPCSTR val)
{
	if ((!x.first) || (!val)) return x.first < val;
	else return xr_strcmp(*x.first, val) < 0;
}

//------------------------------------------------------------------------------
//Тело функций Inifile
//------------------------------------------------------------------------------
XRCORE_API BOOL _parse(LPSTR dest, LPCSTR src)
{
	BOOL bInsideSTR = false;
	if (src)
	{
		while (*src)
		{
			if (isspace((u8)*src))
			{
				if (bInsideSTR)
				{
					*dest++ = *src++;
					continue;
				}
				while (*src && isspace(*src))
				{
					++src;
				}
				continue;
			}
			else if (*src == '"')
			{
				bInsideSTR = !bInsideSTR;
			}
			*dest++ = *src++;
		}
	}
	*dest = 0;
	return bInsideSTR;
}

XRCORE_API void _decorate(LPSTR dest, LPCSTR src)
{
	if (src)
	{
		BOOL bInsideSTR = false;
		while (*src)
		{
			if (*src == ',')
			{
				if (bInsideSTR) { *dest++ = *src++; }
				else
				{
					*dest++ = *src++;
					*dest++ = ' ';
				}
				continue;
			}
			else if (*src == '"')
			{
				bInsideSTR = !bInsideSTR;
			}
			*dest++ = *src++;
		}
	}
	*dest = 0;
}

//------------------------------------------------------------------------------

BOOL CInifile::Sect::line_exist(LPCSTR L, LPCSTR* val)
{
	SectCIt A = std::lower_bound(Data.begin(), Data.end(), L, item_pred);
	if (A != Data.end() && xr_strcmp(*A->first, L) == 0)
	{
		if (val) *val = *A->second;
		return TRUE;
	}
	return FALSE;
}

//------------------------------------------------------------------------------

CInifile::CInifile(IReader* F, LPCSTR path
#ifndef _EDITOR
                   , allow_include_func_t allow_include_func
#endif
)
{
	m_file_name[0] = 0;
	m_flags.zero();
	m_flags.set(eSaveAtEnd, FALSE);
	m_flags.set(eReadOnly, TRUE);
	m_flags.set(eOverrideNames, FALSE);
	Load(F, path
#ifndef _EDITOR
	     , allow_include_func
#endif
	);
}

CInifile::CInifile(LPCSTR szFileName,
                   BOOL ReadOnly,
                   BOOL bLoad,
                   BOOL SaveAtEnd,
                   u32 sect_count
#ifndef _EDITOR
                   , allow_include_func_t allow_include_func
#endif
)

{
	if (szFileName && strstr(szFileName, "system"))
		Msg("-----loading %s", szFileName);

	m_file_name[0] = 0;
	m_flags.zero();
	if (szFileName)
		xr_strcpy(m_file_name, sizeof(m_file_name), szFileName);

	m_flags.set(eSaveAtEnd, SaveAtEnd);
	m_flags.set(eReadOnly, ReadOnly);

	if (bLoad)
	{
		string_path path, folder;
		_splitpath(m_file_name, path, folder, 0, 0);
		xr_strcat(path, sizeof(path), folder);
		IReader* R = FS.r_open(szFileName);
		if (R)
		{
			if (sect_count)
				DATA.reserve(sect_count);
			Load(R, path
#ifndef _EDITOR
			     , allow_include_func
#endif
			);
			FS.r_close(R);
		}
	}
}

CInifile::~CInifile()
{
	if (!m_flags.test(eReadOnly) && m_flags.test(eSaveAtEnd))
	{
		if (!save_as())
			Log("!Can't save inifile:", m_file_name);
	}

	RootIt I = DATA.begin();
	RootIt E = DATA.end();
	for (; I != E; ++I)
		xr_delete(*I);
}

std::unordered_map<std::string, std::vector<CInifile::Item>> OverrideModifyListData;

static void insert_item(CInifile::Sect* tgt, const CInifile::Item& I)
{
	// demonized
	// DLTX: add or remove item from the section parameter if it has a structure of "name = item1, item2, item3, ..."
	// >name = item will add item to the list
	// <name = item will remove item from the list
	if (*I.first && (I.first.c_str()[0] == '<' || I.first.c_str()[0] == '>')) {
		OverrideModifyListData[std::string(tgt->Name.c_str())].push_back(I);
		return;
	}

	CInifile::SectIt_ sect_it = std::lower_bound(tgt->Data.begin(), tgt->Data.end(), *I.first, item_pred);
	if (sect_it != tgt->Data.end() && sect_it->first.equal(I.first))
	{
		sect_it->second = I.second;
		sect_it->filename = I.filename;
		//#ifdef DEBUG
		// sect_it->comment= I.comment;
		//#endif
	}
	else
	{
		tgt->Data.insert(sect_it, I);
	}
}

IC BOOL is_empty_line_now(IReader* F)
{
	char* a0 = (char*)F->pointer() - 4;
	char* a1 = (char*)(F->pointer()) - 3;
	char* a2 = (char*)F->pointer() - 2;
	char* a3 = (char*)(F->pointer()) - 1;

	return (*a0 == 13) && (*a1 == 10) && (*a2 == 13) && (*a3 == 10);
};

void CInifile::Load(IReader* F, LPCSTR path
#ifndef _EDITOR
                    , allow_include_func_t allow_include_func
#endif
)
{
	R_ASSERT(F);

	std::string DLTX_DELETE = "DLTX_DELETE";

	std::function<void(std::vector<std::string>*, std::vector<std::string>*, bool)> MergeParentSet = [](std::vector<std::string>* ParentsBase, std::vector<std::string>* ParentsOverride, bool bIncludeRemovers)
	{
		for (std::string CurrentParent : *ParentsOverride)
		{
			bool bIsParentRemoval = CurrentParent[0] == '!';

			std::string StaleParentString = (!bIsParentRemoval ? "!" : "") + CurrentParent.substr(1);

			for (auto It = ParentsBase->rbegin(); It != ParentsBase->rend(); It++)
			{
				if (*It == StaleParentString)
				{
					ParentsBase->erase(std::next(It).base());
				}
			}

			if (bIncludeRemovers || !bIsParentRemoval)
			{
				ParentsBase->insert(ParentsBase->end(), CurrentParent);
			}
		}
	};

	string_path currentFileName;
	std::unordered_map<std::string, std::unordered_map<std::string, bool>> OverrideToFilename;
	std::unordered_map<std::string, std::string> SectionToFilename;
	std::unordered_set<std::string> SectionsToDelete;

	std::function<void
		(
		IReader*,
		LPCSTR,
		std::unordered_map<std::string, Sect>*,
		std::unordered_map<std::string, std::vector<std::string>>*,
		BOOL,
		BOOL
		)
	> LTXLoad = [&]
		(
		IReader* F,
		LPCSTR path,
		std::unordered_map<std::string, Sect>* OutputData,
		std::unordered_map<std::string, std::vector<std::string>>* ParentDataMap,
		BOOL bOverridesOnly,
		BOOL bIsRootFile
		)
	{
		Sect* Current = 0;
		MezzStringBuffer str;
		MezzStringBuffer str2;

		BOOL bInsideSTR = FALSE;

		BOOL bIsCurrentSectionOverride = FALSE;
		BOOL bHasLoadedModFiles = FALSE;

		std::function<std::vector<std::string>*(std::string)> GetParentStrings = [&](std::string SectionName)
		{
			auto It = ParentDataMap->find(SectionName);

			if (It == ParentDataMap->end())
			{
				ParentDataMap->insert(std::pair<std::string, std::vector<std::string>>(SectionName, std::vector<std::string>()));

				It = ParentDataMap->find(SectionName);
			}

			return &It->second;
		};

		auto GetParentsSetFromString = [&](const char* ParentString)
		{
			std::vector<std::string> ParentSet = std::vector<std::string>();

			u32 ItemCount = _GetItemCount(ParentString);

			for (u32 i = 0; i < ItemCount; i++)
			{
				_GetItem(ParentString, i, str2, str2.GetSize());

				ParentSet.insert(ParentSet.end(), str2.GetBuffer());
			}

			return ParentSet;
		};

		auto GetRegexMatch = [](std::string InputString, std::string PatternString)
		{
			std::regex Pattern = std::regex(PatternString);
			std::smatch MatchResult;

			std::regex_search(InputString, MatchResult, Pattern);

			if (MatchResult.begin() == MatchResult.end())
			{
				return std::string();
			}

			return MatchResult.begin()->str();
		};

		auto IsFullRegexMatch = [](std::string InputString, std::string PatternString)
		{
			return std::regex_match(InputString, std::regex(PatternString));
		};

		const auto loadFile = [&, LTXLoad](const string_path _fn, const string_path inc_path, const string_path name)
		{
			if (!allow_include_func || allow_include_func(_fn))
			{
				IReader* I = FS.r_open(_fn);
				R_ASSERT3(I, "Can't find include file:", name);

				strcpy(currentFileName, name);

				LTXLoad(I, inc_path, OutputData, ParentDataMap, bOverridesOnly, false);

				FS.r_close(I);
			}
		};

		auto StashCurrentSection = [&]()
		{
			if (Current && bIsCurrentSectionOverride == bOverridesOnly)
			{
				//store previous section
				auto SectIt = OutputData->find(std::string(Current->Name.c_str()));
				if (SectIt != OutputData->end())
				{
					if (!bIsCurrentSectionOverride)
					{

						Debug.fatal(DEBUG_INFO, "Duplicate section '%s' wasn't marked as an override.\n\nOverride section by prefixing it with '!' (![%s]) or give it a unique name.\n\nCheck this file and its DLTX mods:\n\"%s\",\nfile with section \"%s\",\nfile with duplicate \"%s\"", *Current->Name, *Current->Name, m_file_name, SectionToFilename[std::string(Current->Name.c_str())].c_str(), currentFileName);
					}

					//Overwrite existing override data
					for (Item CurrentItem : Current->Data)
					{
						insert_item(&SectIt->second, CurrentItem);
					}

					OverrideToFilename[SectIt->first][currentFileName] = true;
				}
				else
				{
					OutputData->emplace(std::pair<std::string, Sect>(std::string(Current->Name.c_str()), *Current));
					OverrideToFilename[std::string(Current->Name.c_str())][currentFileName] = true;
					SectionToFilename[std::string(Current->Name.c_str())] = currentFileName;
				}
			}

			Current = NULL;
		};

		std::unordered_set<std::string> sectionsMarkedForCreate;

		while (!F->eof() || (bIsRootFile && !bHasLoadedModFiles))
		{
			if (!F->eof())
			{
				F->r_string(str, str.GetSize());
				_Trim(str);
			}
			else if (!bHasLoadedModFiles && bIsRootFile)
			{
				StashCurrentSection();
				bHasLoadedModFiles = TRUE;

				if (!m_file_name[0])
				{
					continue;
				}

				//Assemble paths and filename
				MezzStringBuffer split_drive;
				MezzStringBuffer split_dir;
				MezzStringBuffer split_name;

				_splitpath_s(m_file_name, split_drive, split_drive.GetSize(), split_dir, split_dir.GetSize(), split_name, split_name.GetSize(), NULL, 0);

				std::string FilePath = std::string(split_drive) + std::string(split_dir);
				std::string FileName = split_name;

				//Collect all files that could potentially be confused as a root file by our mod files
				FS_FileSet AmbiguousFiles;
				FS.file_list(AmbiguousFiles, FilePath.c_str(), FS_ListFiles, (FileName + "_*.ltx").c_str());

				//Collect all matching mod files
				FS_FileSet ModFiles;
				FS.file_list(ModFiles, FilePath.c_str(), FS_ListFiles, ("mod_" + FileName + "_*.ltx").c_str());

				for (auto It = ModFiles.begin(); It != ModFiles.end(); ++It)
				{
					std::string ModFileName = It->name.c_str();

					//Determine if we should load this mod file, or if it's meant for a different root file
					BOOL bIsModfileMeantForMe = [&]()
					{
						for (auto It2 = AmbiguousFiles.begin(); It2 != AmbiguousFiles.end(); ++It2)
						{
							std::string AmbiguousFileName = GetRegexMatch(It2->name.c_str(), "^.+(?=.ltx$)");
							std::string AmbiguousFileMatchPattern = std::string("mod_") + AmbiguousFileName + std::string("_.+.ltx");

							if (IsFullRegexMatch(ModFileName, AmbiguousFileMatchPattern))
							{
								return false;
							}
						}

						return true;
					}();

					if (!bIsModfileMeantForMe)
					{
						continue;
					}

					loadFile((FilePath + ModFileName).c_str(), FilePath.c_str(), ModFileName.c_str());
				}

				continue;
			}

			LPSTR comm = strchr(str, ';');
			LPSTR comm_1 = strchr(str, '/');

			if (comm_1 && (*(comm_1 + 1) == '/') && ((!comm) || (comm && (comm_1 < comm))))
			{
				comm = comm_1;
			}

#ifdef DEBUG
			LPSTR comment = 0;
#endif
			if (comm)
			{
				//."bla-bla-bla;nah-nah-nah"
				char quot = '"';
				bool in_quot = false;

				LPCSTR q1 = strchr(str, quot);
				if (q1 && q1 < comm)
				{
					LPCSTR q2 = strchr(++q1, quot);
					if (q2 && q2 > comm)
						in_quot = true;
				}

				if (!in_quot)
				{
					*comm = 0;
#ifdef DEBUG
					comment = comm + 1;
#endif
				}
			}

			_Trim(str);

			auto isOverrideSection = [](char* str) {
				return strstr(str, "![") == &str[0];
			};

			auto isSafeOverrideSection = [](char* str) {
				return strstr(str, "@[") == &str[0];
			};

			auto isModSection = [isOverrideSection, isSafeOverrideSection](char* str) {
				return isOverrideSection(str) || isSafeOverrideSection(str);
			};

			if (str[0] && (str[0] == '#') && strstr(str, "#include")) //handle includes
			{
				string_path inc_name;
				R_ASSERT(path && path[0]);
				if (_GetItem(str, 1, inc_name, '"'))
				{
					string_path fn, inc_path, folder;
					strconcat(sizeof(fn), fn, path, inc_name);
					_splitpath(fn, inc_path, folder, 0, 0);
					xr_strcat(inc_path, sizeof(inc_path), folder);


					if (strstr(inc_name, "*.ltx"))
					{
						FS_FileSet fset;
						FS.file_list(fset, inc_path, FS_ListFiles, inc_name);

						for (FS_FileSet::iterator it = fset.begin(); it != fset.end(); it++)
						{
							LPCSTR _name = it->name.c_str();
							string_path _fn;
							strconcat(sizeof(_fn), _fn, inc_path, _name);
							loadFile(_fn, inc_path, _name);
						}
					}
					else
						loadFile(fn, inc_path, inc_name);
				}

				continue;
			}
			else if (str[0] && strstr(str, "!![") == &str[0])	//Section delete
			{
				StashCurrentSection();

				if (!bOverridesOnly)
				{
					continue;
				}

				u32 SectionNameStartPos = 3;
				std::string SecName = std::string(str).substr(SectionNameStartPos, strchr(str, ']') - str - SectionNameStartPos).c_str();
				for (auto i = SecName.begin(); i != SecName.end(); ++i)
				{
					*i = tolower(*i);
				}
				Msg("[DLTX] [%s] Encountered %s, mark section to delete", m_file_name, str.GetBuffer());
				SectionsToDelete.insert(SecName);

				continue;
			}
			else if ((str[0] && (str[0] == '[')) || isModSection(str)) //new section ?
			{
				// insert previous filled section
				StashCurrentSection();

				u32 SectionNameStartPos = (isModSection(str) ? 2 : 1);
				std::string SecName = std::string(str).substr(SectionNameStartPos, strchr(str, ']') - str - SectionNameStartPos).c_str();
				for (auto i = SecName.begin(); i != SecName.end(); ++i)
				{
					*i = tolower(*i);
				}
				
				if (isOverrideSection(str)) { //Used to detect bad or unintended overrides
					bIsCurrentSectionOverride = true;
				} else if (isSafeOverrideSection(str)) { // Create section if it doesnt exist, override if it does
					bIsCurrentSectionOverride = true;
					if (bOverridesOnly) {
						// Msg("using @[, override existing section %s", SecName.c_str());
					} else {
						auto SectIt = OutputData->find(SecName);
						if (SectIt != OutputData->end()) {
							// Msg("using @[, override existing section %s", SecName.c_str());
						} else {
							// Msg("using @[, create new section %s", SecName.c_str());
							sectionsMarkedForCreate.insert(SecName);
						}
					}
				} else {
					bIsCurrentSectionOverride = false;
				}

				Current = xr_new<Sect>();
				Current->Name = SecName.c_str();

				// start new section
				R_ASSERT3(strchr(str, ']'), "Bad ini section found: ", str);

				if (bIsCurrentSectionOverride == bOverridesOnly)
				{
					LPCSTR inherited_names = strstr(str, "]:");
					if (0 != inherited_names)
					{
						VERIFY2(m_flags.test(eReadOnly), "Allow for readonly mode only.");
						inherited_names += 2;

						std::vector<std::string> CurrentParents = GetParentsSetFromString(inherited_names);
						std::vector<std::string>* SectionParents = GetParentStrings(Current->Name.c_str());

						MergeParentSet(SectionParents, &CurrentParents, true);
					}
				}

				continue;
			}
			else // name = value
			{
				if (Current && bIsCurrentSectionOverride == bOverridesOnly)
				{
					bool bIsDelete = str[0] == '!';

					MezzStringBuffer value_raw;
					char* name = (char*) (str + (bIsDelete ? 1 : 0));
					char* t = strchr(name, '=');
					if (t)
					{
						*t = 0;
						_Trim(name);
						++t;
						xr_strcpy(value_raw, value_raw.GetSize(), t);
						bInsideSTR = _parse(str2, value_raw);
						if (bInsideSTR) //multiline str value
						{
							while (bInsideSTR)
							{
								xr_strcat(value_raw, value_raw.GetSize(), "\r\n");
								MezzStringBuffer str_add_raw;
								F->r_string(str_add_raw, str_add_raw.GetSize());
								R_ASSERT2(
									xr_strlen(value_raw) + xr_strlen(str_add_raw) < value_raw.GetSize(),
									make_string(
										"Incorrect inifile format: section[%s], variable[%s]. Odd number of quotes (\") found, but should be even."
										,
										Current->Name.c_str(),
										name
									)
								);
								xr_strcat(value_raw, value_raw.GetSize(), str_add_raw);
								bInsideSTR = _parse(str2, value_raw);
								if (bInsideSTR)
								{
									if (is_empty_line_now(F))
										xr_strcat(value_raw, value_raw.GetSize(), "\r\n");
								}
							}
						}
					}
					else
					{
						_Trim(name);
						str2[0] = 0;
					}

					Item I;
					I.first = (name[0] ? name : NULL);
					I.second = bIsDelete ? DLTX_DELETE.c_str() : (str2[0] ? str2.GetBuffer() : NULL);

					auto fname = toLowerCaseCopy(trimCopy(getFilename(std::string(currentFileName))));
					// Remove .ltx part, unused for now
					/*fname.pop_back();
					fname.pop_back();
					fname.pop_back();
					fname.pop_back();*/
					I.filename = fname.c_str();

					if (*I.first || *I.second)
					{
						insert_item(Current, I);
					}
				}

				continue;
			}
		}

		StashCurrentSection();

		// Create empty sections that were marked with @[ and weren't defined normally
		if (!bOverridesOnly) {
			for (auto& SecName : sectionsMarkedForCreate) {
				auto SectIt = OutputData->find(SecName);
				if (SectIt == OutputData->end()) {
					// Msg("section %s does not exist but was marked as @[, creating", SecName.c_str());
					Current = xr_new<Sect>();
					Current->Name = SecName.c_str();
					OutputData->emplace(std::pair<std::string, Sect>(std::string(Current->Name.c_str()), *Current));
					OverrideToFilename[std::string(Current->Name.c_str())][currentFileName] = true;
					SectionToFilename[std::string(Current->Name.c_str())] = currentFileName;
					Current = NULL;
				}
			}
		}
	};

	std::unordered_map<std::string, std::vector<std::string>> BaseParentDataMap;
	std::unordered_map<std::string, Sect> BaseData;

	std::unordered_map<std::string, std::vector<std::string>> OverrideParentDataMap;
	std::unordered_map<std::string, Sect> OverrideData;

	std::unordered_map<std::string, Sect> FinalData;

	std::unordered_set<std::string> FinalizedSections;

	enum InsertType
	{
		Override,
		Base,
		Parent
	};

	std::function<void(std::string, std::vector<std::string>*)> EvaluateSection = [&](std::string SectionName, std::vector<std::string>* PreviousEvaluations)
	{
		if (FinalizedSections.find(SectionName) != FinalizedSections.end())
		{
			return;
		}

		PreviousEvaluations->insert(PreviousEvaluations->end(), SectionName);

		std::vector<std::string>* BaseParents = &BaseParentDataMap.find(SectionName)->second;
		std::vector<std::string>* OverrideParents = &OverrideParentDataMap.find(SectionName)->second;

		BOOL bDeleteSectionIfEmpty = FALSE;

		MergeParentSet(BaseParents, OverrideParents, false);

		std::pair<std::string, Sect> CurrentSecPair = std::pair<std::string, Sect>(SectionName, Sect());
		Sect* CurrentSect = &CurrentSecPair.second;
		CurrentSect->Name = SectionName.c_str();

		auto IsStringDLTXDelete = [&](shared_str str)
		{
			const char* RawString = str.c_str();

			return RawString && std::string(RawString) == DLTX_DELETE;
		};

		auto InsertItemWithDelete = [&](Item CurrentItem, InsertType Type)
		{
			if (IsStringDLTXDelete(CurrentItem.first))
			{
				//Delete section
				bDeleteSectionIfEmpty = TRUE;
			}
			else
			{
				//Insert item if variable isn't already set
				CInifile::SectIt_ sect_it = std::lower_bound(CurrentSect->Data.begin(), CurrentSect->Data.end(), *CurrentItem.first, item_pred);
				if (sect_it != CurrentSect->Data.end() && sect_it->first.equal(CurrentItem.first))
				{
					bool bShouldInsert = [&]()
					{
						switch (Type)
						{
						case InsertType::Override:		return true;
						case InsertType::Base:			return false;
						case InsertType::Parent:		return IsStringDLTXDelete(sect_it->second);
						}
					}();

					if (bShouldInsert)
					{
						sect_it->second = CurrentItem.second;
					}
				}
				else
				{
					CurrentSect->Data.insert(sect_it, CurrentItem);
				}
			}
		};

		//Insert variables of own data
		auto InsertData = [&](std::unordered_map<std::string, Sect>* Data, BOOL bIsBase)
		{
			auto It = Data->find(SectionName);

			if (It != Data->end())
			{
				Sect* DataSection = &It->second;
				for (Item CurrentItem : DataSection->Data)
				{
					InsertItemWithDelete(CurrentItem, bIsBase ? Base : Override);
				}

				if (!bIsBase)
				{
					Data->erase(It);
				}
			}
		};

		InsertData(&OverrideData, false);
		InsertData(&BaseData, true);

		//Insert variables from parents
		for (auto It = BaseParents->rbegin(); It != BaseParents->rend(); ++It)
		{
			std::string ParentSectionName = *(It.base() - 1);

			for (auto It = PreviousEvaluations->begin(); It != PreviousEvaluations->end(); ++It)
			{
				if (ParentSectionName == *It)
				{
					Debug.fatal(DEBUG_INFO, "Section '%s' has cyclical dependencies. Ensure that sections with parents don't inherit in a loop. Check this file and its DLTX mods: %s, mod file %s", ParentSectionName.c_str(), m_file_name, currentFileName);
				}
			}

			EvaluateSection(ParentSectionName, PreviousEvaluations);

			auto ParentIt = FinalData.find(ParentSectionName);

			if (ParentIt == FinalData.end())
			{
				Debug.fatal(DEBUG_INFO, "Section '%s' inherits from non-existent section '%s'. Check this file and its DLTX mods: %s, mod file %s", SectionName.c_str(), ParentSectionName.c_str(), m_file_name, currentFileName);
			}

			Sect* ParentSec = &ParentIt->second;

			for (Item CurrentItem : ParentSec->Data)
			{
				InsertItemWithDelete(CurrentItem, Parent);
			}
		}

		//Delete entries that are still marked DLTX_DELETE
		for (auto It = CurrentSect->Data.rbegin(); It != CurrentSect->Data.rend(); ++It)
		{
			if (IsStringDLTXDelete(It->second))
			{
				CurrentSect->Data.erase(It.base() - 1);
			}
		}

		// If there is data to modify parameters lists
		if (OverrideModifyListData.find(std::string(CurrentSect->Name.c_str())) != OverrideModifyListData.end()) {
			for (auto It = OverrideModifyListData[std::string(CurrentSect->Name.c_str())].begin(); It != OverrideModifyListData[std::string(CurrentSect->Name.c_str())].end(); ++It) {
				CInifile::Item &I = *It;

				// If section exists with item list, split list and perform operation
				char dltx_listmode = I.first[0];
				I.first = I.first.c_str() + 1;

				CInifile::SectIt_ sect_it = std::lower_bound(CurrentSect->Data.begin(), CurrentSect->Data.end(), *I.first, item_pred);
				if (sect_it != CurrentSect->Data.end() && sect_it->first.equal(I.first)) {

					//Msg("%s has dltx_listmode %s", I.first.c_str(), std::string(1, dltx_listmode).c_str());

					if (dltx_listmode && sect_it->second != NULL) {
						// Split list 
						auto split_list = [](const std::string items, const std::string delimiter = ",") {
							std::string i = items;
							std::vector<std::string> vec;
							size_t pos = 0;
							std::string token;
							while ((pos = i.find(delimiter)) != std::string::npos) {
								token = i.substr(0, pos);
								vec.push_back(token);
								i.erase(0, pos + delimiter.length());
							}
							vec.push_back(i);

							for (auto &item : vec) {
								trim(item);
							}
							return vec;
						};
						std::vector<std::string> sect_it_items_vec = split_list(sect_it->second.c_str());
						std::vector<std::string> I_items_vec = split_list(I.second.c_str());

						// Add or remove to the list
						auto find_and_store_index = [](const std::vector<std::string> &items_vec, const std::string item, int &vec_index) {
							auto it = std::find(items_vec.begin(), items_vec.end(), item);
							if (it != items_vec.end()) {
								vec_index = it - items_vec.begin();
								return true;
							}
							else {
								vec_index = -1;
								return false;
							}
						};
						int vec_index = -1;
						for (const auto &item : I_items_vec) {
							if (dltx_listmode == '>') {
								sect_it_items_vec.push_back(item);
							}
							else if (dltx_listmode == '<') {
								while (find_and_store_index(sect_it_items_vec, item, vec_index)) {
									sect_it_items_vec.erase(sect_it_items_vec.begin() + vec_index);
								}
							}
						}

						// Store result back
						auto join_list = [](const std::vector<std::string> &items_vec, const std::string delimiter = ",") {
							std::string ret;
							for (const auto &i : items_vec) {
								if (!ret.empty()) {
									ret += delimiter;
								}
								ret += i;
							}
							return ret;
						};

						/*std::string c(1, dltx_listmode);
						Msg("%s has dltx_listmode %s, %s items", I.first.c_str(), c.c_str(), dltx_listmode == '>' ? "adding" : "removing");
						Msg("old %s", sect_it->second.c_str());
						Msg("new %s", join_list(sect_it_items_vec).c_str());*/

						sect_it->second = join_list(sect_it_items_vec, ",").c_str();
					}
				}
			}
		}
		

		//Pop from stack
		auto LastElement = PreviousEvaluations->end();
		LastElement--;

		PreviousEvaluations->erase(LastElement);

		//Finalize
		if (!bDeleteSectionIfEmpty || CurrentSecPair.second.Data.size())
		{
			FinalData.emplace(CurrentSecPair);
		}

		FinalizedSections.insert(SectionName);
	};

	//Read contents of root file
	LTXLoad(F, path, &OverrideData, &OverrideParentDataMap, true, true);
	F->seek(0);
	LTXLoad(F, path, &BaseData, &BaseParentDataMap, false, true);

	//Merge base and override data together
	std::vector<std::string> PreviousEvaluations = std::vector<std::string>();

	for (std::pair<std::string, Sect> SectPair : BaseData)
	{
		EvaluateSection(SectPair.first, &PreviousEvaluations);
	}

	// demonized: check for marked for delete sections and return
	for (auto &s: SectionsToDelete)
	{
		Msg("[DLTX] [%s] Found section %s to delete", m_file_name, s.c_str());
		if (FinalData.find(s) != FinalData.end()) {
			Msg("[DLTX] [%s] Deleting section %s", m_file_name, s.c_str());
			FinalData.erase(s);
			if (OverrideData.find(s) != OverrideData.end()) {
				Msg("[DLTX] [%s] Deleting overrides for section %s", m_file_name, s.c_str());
				OverrideData.erase(s);
			}
		}
	}

	//Insert all finalized sections into final container
	for (std::pair<std::string, Sect> SectPair : FinalData)
	{
		Sect* NewSect = xr_new<Sect>();
		*NewSect = SectPair.second;

		RootIt I = std::lower_bound(DATA.begin(), DATA.end(), SectPair.first.c_str(), sect_pred);
		DATA.insert(I, NewSect);
	}

	// Clean modifiers of parameters' lists
	OverrideModifyListData.clear();

	//throw errors if there are overrides that never got used
	if (OverrideData.size())
	{
		//Debug.fatal(DEBUG_INFO, "Attemped to override section '%s', which doesn't exist. Ensure that a base section with the same name is loaded first. Check this file and its DLTX mods: %s", OverrideData.begin()->first.c_str(), m_file_name);
		for (auto i = OverrideData.begin(); i != OverrideData.end(); i++) {
			auto override_filenames = OverrideToFilename.find(i->first);
			if (override_filenames != OverrideToFilename.end()) {
				for (auto &override_filename : override_filenames->second) {
					Msg("!!!DLTX ERROR Attemped to override section '%s', which doesn't exist. Ensure that a base section with the same name is loaded first. Check this file and its DLTX mods: %s, mod file %s", i->first.c_str(), m_file_name, override_filename.first.c_str());
				}
			}
		}
	}
}

// demonized: print DLTX override info
void CInifile::DLTX_print(LPCSTR sec, LPCSTR line)
{
	Msg("%s", m_file_name);
	if (!sec) {
		for (const auto& d : DATA) {
			Msg("[%s]", d->Name.c_str());
			for (const auto& s : d->Data) {
				printIniItemLine(s);
			}
		}
		return;
	}

	if (!section_exist(sec)) {
		Msg("![DLTX_print] no section exists by name %s", sec);
		return;
	}

	Sect& I = r_section(sec);

	if (!line) {
		Msg("[%s]", I.Name.c_str());
		for (const auto& s : I.Data) {
			printIniItemLine(s);
		}
		return;
	}

	if (!line_exist(sec, line)) {
		Msg("![DLTX_print] no line %s exists in section %s", line, sec);
		return;
	}

	SectCIt A = std::lower_bound(I.Data.begin(), I.Data.end(), line, item_pred);
	Msg("[%s]", I.Name.c_str());
	printIniItemLine(*A);
}
LPCSTR CInifile::DLTX_getFilenameOfLine(LPCSTR sec, LPCSTR line)
{
	if (!sec) {
		Msg("![DLTX_getFilenameOfLine] no section provided");
		return nullptr;
	}

	if (!line) {
		Msg("![DLTX_getFilenameOfLine] no line provided for section %s", sec);
		return nullptr;
	}

	if (!section_exist(sec)) {
		Msg("![DLTX_getFilenameOfLine] no section exists by name %s", sec);
		return nullptr;
	}

	if (!line_exist(sec, line)) {
		Msg("![DLTX_getFilenameOfLine] no line %s exists in section %s", line, sec);
		return nullptr;
	}

	Sect& I = r_section(sec);
	SectCIt A = std::lower_bound(I.Data.begin(), I.Data.end(), line, item_pred);
	auto fname = A->filename.c_str();
	return fname;
}
bool CInifile::DLTX_isOverride(LPCSTR sec, LPCSTR line)
{
	auto fname = DLTX_getFilenameOfLine(sec, line);
	if (!fname) {
		return false;
	}
	return std::string(fname).find("mod_") == 0;
}

void CInifile::save_as(IWriter& writer, bool bcheck) const
{
	string4096 temp, val;
	for (RootCIt r_it = DATA.begin(); r_it != DATA.end(); ++r_it)
	{
		xr_sprintf(temp, sizeof(temp), "[%s]", (*r_it)->Name.c_str());
		writer.w_string(temp);
		if (bcheck)
		{
			xr_sprintf(temp, sizeof(temp), "; %d %d %d", (*r_it)->Name._get()->dwCRC,
			           (*r_it)->Name._get()->dwReference,
			           (*r_it)->Name._get()->dwLength);
			writer.w_string(temp);
		}

		for (SectCIt s_it = (*r_it)->Data.begin(); s_it != (*r_it)->Data.end(); ++s_it)
		{
			const Item& I = *s_it;
			if (*I.first)
			{
				if (*I.second)
				{
					_decorate(val, *I.second);
					// only name and value
					xr_sprintf(temp, sizeof(temp), "%8s%-32s = %-32s", " ", I.first.c_str(), val);
				}
				else
				{
					// only name
					xr_sprintf(temp, sizeof(temp), "%8s%-32s = ", " ", I.first.c_str());
				}
			}
			else
			{
				// no name, so no value
				temp[0] = 0;
			}
			_TrimRight(temp);
			if (temp[0]) writer.w_string(temp);
		}
		writer.w_string(" ");
	}
}

bool CInifile::save_as(LPCSTR new_fname)
{
	// save if needed
	if (new_fname && new_fname[0])
		xr_strcpy(m_file_name, sizeof(m_file_name), new_fname);

	R_ASSERT(m_file_name&&m_file_name[0]);
	IWriter* F = FS.w_open_ex(m_file_name);
	if (!F)
		return (false);

	save_as(*F);
	FS.w_close(F);
	return (true);
}

BOOL CInifile::section_exist(LPCSTR S) const
{
	if (S && m_cache.find(S) != m_cache.end()) {
#ifdef INICACHE_PRINT_DEBUG
		Msg("[%s] section_exist: found section %s in cache", m_file_name, S);
#endif // INICACHE_PRINT_DEBUG
		return TRUE;
	}
	RootCIt I = std::lower_bound(DATA.begin(), DATA.end(), S, sect_pred);
	return (I != DATA.end() && xr_strcmp(*(*I)->Name, S) == 0);
}

BOOL CInifile::line_exist(LPCSTR S, LPCSTR L) const
{
	if (!section_exist(S)) return FALSE;

	if (S && L) {
		auto cacheSec = m_cache.find(S);
		if (cacheSec != m_cache.end() && cacheSec->second.find(L) != cacheSec->second.end()) {

#ifdef INICACHE_PRINT_DEBUG
			Msg("[%s] line_exist: found section %s line %s in cache", m_file_name, S, L);
#endif // INICACHE_PRINT_DEBUG

			return TRUE;
		}
	}

	Sect& I = r_section(S);
	SectCIt A = std::lower_bound(I.Data.begin(), I.Data.end(), L, item_pred);
	return (A != I.Data.end() && xr_strcmp(*A->first, L) == 0);
}

u32 CInifile::line_count(LPCSTR Sname) const
{
	Sect& S = r_section(Sname);
	SectCIt I = S.Data.begin();
	u32 C = 0;
	for (; I != S.Data.end(); I++) if (*I->first) C++;
	return C;
}

u32 CInifile::section_count() const
{
	return DATA.size();
}


//--------------------------------------------------------------------------------------
CInifile::Sect& CInifile::r_section(const shared_str& S) const { return r_section(*S); }
BOOL CInifile::line_exist(const shared_str& S, const shared_str& L) const { return line_exist(*S, *L); }
u32 CInifile::line_count(const shared_str& S) const { return line_count(*S); }
BOOL CInifile::section_exist(const shared_str& S) const { return section_exist(*S); }

//--------------------------------------------------------------------------------------
// Read functions
//--------------------------------------------------------------------------------------
CInifile::Sect& CInifile::r_section(LPCSTR S) const
{
	R_ASSERT(S && strlen(S),
	         "Empty section (null\\'') passed into CInifile::r_section(). See info above ^, check your configs and 'call stack'.")
	; //--#SM+#--

	char section[256];
	xr_strcpy(section, sizeof(section), S);
	strlwr(section);
	RootCIt I = std::lower_bound(DATA.begin(), DATA.end(), section, sect_pred);
	if (!(I != DATA.end() && xr_strcmp(*(*I)->Name, section) == 0))
	{
		//g_pStringContainer->verify();

		//string_path ini_dump_fn, path;
		//strconcat (sizeof(ini_dump_fn), ini_dump_fn, Core.ApplicationName, "_", Core.UserName, ".ini_log");
		//
		//FS.update_path (path, "$logs$", ini_dump_fn);
		//IWriter* F = FS.w_open_ex(path);
		//save_as (*F);
		//F->w_string ("shared strings:");
		//g_pStringContainer->dump(F);
		//FS.w_close (F);

		Debug.fatal(DEBUG_INFO, "Can't open section '%s'. Please attach [*.ini_log] file to your bug report", S);
	}
	return **I;
}

void CInifile::cacheValue(LPCSTR S, LPCSTR L, shared_str& V) {
	if (S && L) {

#ifdef INICACHE_PRINT_DEBUG
		Msg("[%s] cacheValue: writing [%s] %s = %s in cache", m_file_name, S, L, V.c_str());
#endif // INICACHE_PRINT_DEBUG

		std::string s = S;
		std::string l = L;
		m_cache[s][l] = V;
	}
}

LPCSTR CInifile::r_string(LPCSTR S, LPCSTR L) const
{
	if (!S || !L || !strlen(S) || !strlen(L)) //--#SM+#-- [fix for one of "xrDebug - Invalid handler" error log]
	{
		Msg("!![ERROR] CInifile::r_string: S = [%s], L = [%s]", S, L);
	}
	
	if (S && L) {
		std::string s = S;
		std::string l = L;
		auto sectKey = m_cache.find(s);
		if (sectKey != m_cache.end()) {
			auto lineKey = sectKey->second.find(l);
			if (lineKey != sectKey->second.end()) {
				auto& res = lineKey->second;

#ifdef INICACHE_PRINT_DEBUG
				Msg("[%s] r_string: getting [%s] %s = %s in cache", m_file_name, S, L, res.c_str());
#endif // INICACHE_PRINT_DEBUG

				return *res;
			}
		}
	}

	Sect const& I = r_section(S);
	SectCIt A = std::lower_bound(I.Data.begin(), I.Data.end(), L, item_pred);
	if (A != I.Data.end() && xr_strcmp(*A->first, L) == 0) {
		shared_str V = A->second;
		LPCSTR res = *V;
		const_cast<CInifile*>(this)->cacheValue(S, L, V);
		return res;
	}
	else
		Debug.fatal(DEBUG_INFO, "Can't find variable %s in [%s]", L, S);
	return 0;
}

shared_str CInifile::r_string_wb(LPCSTR S, LPCSTR L) const
{
	LPCSTR _base = r_string(S, L);

	if (0 == _base) return shared_str(0);

	string4096 _original;
	xr_strcpy(_original, sizeof(_original), _base);
	u32 _len = xr_strlen(_original);
	if (0 == _len) return shared_str("");
	if ('"' == _original[_len - 1]) _original[_len - 1] = 0; // skip end
	if ('"' == _original[0]) return shared_str(&_original[0] + 1); // skip begin
	return shared_str(_original);
}

u8 CInifile::r_u8(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return u8(atoi(C));
}

u16 CInifile::r_u16(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return u16(atoi(C));
}

u32 CInifile::r_u32(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return u32(atoi(C));
}

u64 CInifile::r_u64(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
#ifndef _EDITOR
	return _strtoui64(C, NULL, 10);
#else
    return (u64)_atoi64(C);
#endif
}

s64 CInifile::r_s64(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return _atoi64(C);
}

s8 CInifile::r_s8(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return s8(atoi(C));
}

s16 CInifile::r_s16(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return s16(atoi(C));
}

s32 CInifile::r_s32(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return s32(atoi(C));
}

float CInifile::r_float(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return float(atof(C));
}

Fcolor CInifile::r_fcolor(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Fcolor V = {0, 0, 0, 0};
	sscanf(C, "%f,%f,%f,%f", &V.r, &V.g, &V.b, &V.a);
	return V;
}

u32 CInifile::r_color(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	u32 r = 0, g = 0, b = 0, a = 255;
	sscanf(C, "%d,%d,%d,%d", &r, &g, &b, &a);
	return color_rgba(r, g, b, a);
}

Ivector2 CInifile::r_ivector2(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Ivector2 V = {0, 0};
	sscanf(C, "%d,%d", &V.x, &V.y);
	return V;
}

Ivector3 CInifile::r_ivector3(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Ivector V = {0, 0, 0};
	sscanf(C, "%d,%d,%d", &V.x, &V.y, &V.z);
	return V;
}

Ivector4 CInifile::r_ivector4(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Ivector4 V = {0, 0, 0, 0};
	sscanf(C, "%d,%d,%d,%d", &V.x, &V.y, &V.z, &V.w);
	return V;
}

Fvector2 CInifile::r_fvector2(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Fvector2 V = {0.f, 0.f};
	sscanf(C, "%f,%f", &V.x, &V.y);
	return V;
}

Fvector3 CInifile::r_fvector3(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Fvector3 V = {0.f, 0.f, 0.f};
	sscanf(C, "%f,%f,%f", &V.x, &V.y, &V.z);
	return V;
}

Fvector4 CInifile::r_fvector4(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	Fvector4 V = {0.f, 0.f, 0.f, 0.f};
	sscanf(C, "%f,%f,%f,%f", &V.x, &V.y, &V.z, &V.w);
	return V;
}

BOOL CInifile::r_bool(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	VERIFY2(
		xr_strlen(C) <= 5,
		make_string(
			"\"%s\" is not a valid bool value, section[%s], line[%s]",
			C,
			S,
			L
		)
	);
	char B[8];
	strncpy_s(B, sizeof(B), C, 7);
	B[7] = 0;
	strlwr(B);
	return IsBOOL(B);
}

CLASS_ID CInifile::r_clsid(LPCSTR S, LPCSTR L) const
{
	LPCSTR C = r_string(S, L);
	return TEXT2CLSID(C);
}

int CInifile::r_token(LPCSTR S, LPCSTR L, const xr_token* token_list) const
{
	LPCSTR C = r_string(S, L);
	for (int i = 0; token_list[i].name; i++)
		if (!stricmp(C, token_list[i].name))
			return token_list[i].id;
	return 0;
}

BOOL CInifile::r_line(LPCSTR S, int L, const char** N, const char** V) const
{
	Sect& SS = r_section(S);
	if (L >= (int)SS.Data.size() || L < 0) return FALSE;
	for (SectCIt I = SS.Data.begin(); I != SS.Data.end(); I++)
		if (!(L--))
		{
			*N = *I->first;
			*V = *I->second;
			return TRUE;
		}
	return FALSE;
}

BOOL CInifile::r_line(const shared_str& S, int L, const char** N, const char** V) const
{
	return r_line(*S, L, N, V);
}

//--------------------------------------------------------------------------------------------------------
// Write functions
//--------------------------------------------------------------------------------------
void CInifile::w_string(LPCSTR S, LPCSTR L, LPCSTR V, LPCSTR comment)
{
	R_ASSERT(!m_flags.test(eReadOnly));

	// section
	string256 sect;
	_parse(sect, S);
	_strlwr(sect);

	if (!section_exist(sect))
	{
		// create _new_ section
		Sect* NEW = xr_new<Sect>();
		NEW->Name = sect;
		RootIt I = std::lower_bound(DATA.begin(), DATA.end(), sect, sect_pred);
		DATA.insert(I, NEW);
	}

	// parse line/value
	string4096 line;
	_parse(line, L);
	string4096 value;
	_parse(value, V);

	// duplicate & insert
	Item I;
	Sect& data = r_section(sect);
	I.first = (line[0] ? line : 0);
	I.second = (value[0] ? value : 0);

	//#ifdef DEBUG
	// I.comment = (comment?comment:0);
	//#endif
	SectIt_ it = std::lower_bound(data.Data.begin(), data.Data.end(), *I.first, item_pred);

	if (it != data.Data.end())
	{
		// Check for "first" matching
		if (0 == xr_strcmp(*it->first, *I.first))
		{
			BOOL b = m_flags.test(eOverrideNames);
			R_ASSERT2(b, make_string("name[%s] already exist in section[%s]", line, sect).c_str());
			*it = I;
		}
		else
		{
			data.Data.insert(it, I);
		}
	}
	else
	{
		data.Data.insert(it, I);
	}

	cacheValue(sect, I.first.c_str(), I.second);
	
}

void CInifile::w_u8(LPCSTR S, LPCSTR L, u8 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_u16(LPCSTR S, LPCSTR L, u16 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_u32(LPCSTR S, LPCSTR L, u32 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_u64(LPCSTR S, LPCSTR L, u64 V, LPCSTR comment)
{
	string128 temp;
#ifndef _EDITOR
	_ui64toa_s(V, temp, sizeof(temp), 10);
#else
    _ui64toa(V, temp, 10);
#endif
	w_string(S, L, temp, comment);
}

void CInifile::w_s64(LPCSTR S, LPCSTR L, s64 V, LPCSTR comment)
{
	string128 temp;
#ifndef _EDITOR
	_i64toa_s(V, temp, sizeof(temp), 10);
#else
    _i64toa(V, temp, 10);
#endif
	w_string(S, L, temp, comment);
}

void CInifile::w_s8(LPCSTR S, LPCSTR L, s8 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_s16(LPCSTR S, LPCSTR L, s16 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_s32(LPCSTR S, LPCSTR L, s32 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_float(LPCSTR S, LPCSTR L, float V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%f", V);
	w_string(S, L, temp, comment);
}

void CInifile::w_fcolor(LPCSTR S, LPCSTR L, const Fcolor& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%f,%f,%f,%f", V.r, V.g, V.b, V.a);
	w_string(S, L, temp, comment);
}

void CInifile::w_color(LPCSTR S, LPCSTR L, u32 V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d,%d,%d,%d", color_get_R(V), color_get_G(V), color_get_B(V), color_get_A(V));
	w_string(S, L, temp, comment);
}

void CInifile::w_ivector2(LPCSTR S, LPCSTR L, const Ivector2& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d,%d", V.x, V.y);
	w_string(S, L, temp, comment);
}

void CInifile::w_ivector3(LPCSTR S, LPCSTR L, const Ivector3& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d,%d,%d", V.x, V.y, V.z);
	w_string(S, L, temp, comment);
}

void CInifile::w_ivector4(LPCSTR S, LPCSTR L, const Ivector4& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%d,%d,%d,%d", V.x, V.y, V.z, V.w);
	w_string(S, L, temp, comment);
}

void CInifile::w_fvector2(LPCSTR S, LPCSTR L, const Fvector2& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%f,%f", V.x, V.y);
	w_string(S, L, temp, comment);
}

void CInifile::w_fvector3(LPCSTR S, LPCSTR L, const Fvector3& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%f,%f,%f", V.x, V.y, V.z);
	w_string(S, L, temp, comment);
}

void CInifile::w_fvector4(LPCSTR S, LPCSTR L, const Fvector4& V, LPCSTR comment)
{
	string128 temp;
	xr_sprintf(temp, sizeof(temp), "%f,%f,%f,%f", V.x, V.y, V.z, V.w);
	w_string(S, L, temp, comment);
}

void CInifile::w_bool(LPCSTR S, LPCSTR L, BOOL V, LPCSTR comment)
{
	w_string(S, L, V ? "on" : "off", comment);
}

void CInifile::remove_line(LPCSTR S, LPCSTR L)
{
	R_ASSERT(!m_flags.test(eReadOnly));

	if (line_exist(S, L))
	{
		Sect& data = r_section(S);
		SectIt_ A = std::lower_bound(data.Data.begin(), data.Data.end(), L, item_pred);
		R_ASSERT(A != data.Data.end() && xr_strcmp(*A->first, L) == 0);
		data.Data.erase(A);

#ifdef INICACHE_PRINT_DEBUG
		Msg("[%s] remove_line: removing [%s] %s from cache", m_file_name, S, L);
#endif // INICACHE_PRINT_DEBUG

		std::string s = S;
		std::string l = L;
		m_cache[s].erase(l);
	}
}
