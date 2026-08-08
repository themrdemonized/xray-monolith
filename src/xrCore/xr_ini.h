#pragma once

#include <fastdelegate/fastdelegate.h>

// refs
class CInifile;
struct xr_token;

class XRCORE_API CInifile
{
public:
	struct XRCORE_API Item
	{
		shared_str first;
		mutable shared_str second;

		//demonized: add DLTX info
		mutable shared_str filename;

		// depth determines load order of DLTX overrides, lower depth is more important
		// depth order: DLTX mod_file -> its includes -> Base file -> its includes
		int depth;
		
		// Insertion index will determine what kv pair in overrides will win even if the depth is the same
		u32 insertionIndex;

		bool operator<(const Item& other) const noexcept
		{
			return xr_strcmp(*first, *other.first) < 0;
		}

		Item() : first(0), second(0), filename(0), depth(0), insertionIndex(0) {};
	};

	struct item_comparator
	{
		// Allows for searching by string-likes (string, char*,...) in set
		using is_transparent = void;

		bool operator() (const Item& x, const Item& y) const noexcept
		{
			return xr_strcmp(*x.first, *y.first) < 0;
		}

		template <typename T>
		bool operator() (const Item& x, const T& y) const noexcept
		{
			if constexpr (std::is_same_v<T, shared_str>)
				return xr_strcmp(*x.first, *y) < 0;
			else
				return xr_strcmp(*x.first, y) < 0;
		}

		template <typename T>
		bool operator() (const T& x, const Item& y) const noexcept
		{
			if constexpr (std::is_same_v<T, shared_str>)
				return xr_strcmp(*x, *y.first) < 0;
			else
				return xr_strcmp(x, *y.first) < 0;
		}
	};

	typedef xr_vector<Item> Items;
	typedef Items::const_iterator SectCIt;
	typedef Items::iterator SectIt_;

	struct XRCORE_API Sect
	{
		shared_str Name;
		Items Data;

		BOOL line_exist(LPCSTR L, LPCSTR* val = 0);
	};

	typedef xr_vector<Sect> Root;
	typedef Root::iterator RootIt;
	typedef Root::const_iterator RootCIt;

#ifndef _EDITOR
	typedef fastdelegate::FastDelegate1<LPCSTR, bool> allow_include_func_t;
#endif
	static CInifile* Create(LPCSTR szFileName, BOOL ReadOnly = TRUE);
	static void Destroy(CInifile*);

	static IC BOOL IsBOOL(LPCSTR B)
	{
		return (xr_strcmp(B, "on") == 0 || xr_strcmp(B, "yes") == 0 || xr_strcmp(B, "true") == 0 || xr_strcmp(B, "1") ==
			0);
	}

private:
	string_path m_file_name;
	Root DATA;
	void Load(IReader* F, LPCSTR path
#ifndef _EDITOR
	          , allow_include_func_t allow_include_func = NULL
#endif
	);
public:
	enum { eSaveAtEnd = (1 << 0), eReadOnly = (1 << 1), eOverrideNames = (1 << 2), };

	Flags8 m_flags;
	CInifile(IReader* F,
	         LPCSTR path = 0
#ifndef _EDITOR
	         , allow_include_func_t allow_include_func = NULL
#endif
	);

	CInifile(LPCSTR szFileName,
	         BOOL ReadOnly = TRUE,
	         BOOL bLoadAtStart = TRUE,
	         BOOL SaveAtEnd = TRUE,
	         u32 sect_count = 0
#ifndef _EDITOR
	         , allow_include_func_t allow_include_func = NULL
#endif
	);

	virtual ~CInifile();
	bool save_as(LPCSTR new_fname = 0);

	// DLTX
	void DLTX_print(LPCSTR sec, LPCSTR line);
	LPCSTR DLTX_getFilenameOfLine(LPCSTR sec, LPCSTR line);
	bool DLTX_isOverride(LPCSTR sec, LPCSTR line);
	
private:
	static xr_unordered_flat_map<xr_string, Root> CachedData;
	static xrCriticalSection CacheCS;

public:
	static void InvalidateCache(LPCSTR path = nullptr);
	static void CInifile::GetCacheStats(u64& files_cached, u64& total_bytes, u64& section_count)
	{
		total_bytes = 0;
		section_count = 0;
		files_cached = CachedData.size();
        xr_unordered_flat_set<shared_str> strings;

		for (const auto& file_pair : CachedData)
		{
			// Size of the file path string
			total_bytes += file_pair.first.capacity();

			// Inner map overhead
			for (const auto& sect_pair : file_pair.second)
			{
				section_count++;
				// Each section name
				// Plus the overhead of the xr_vector structure
                strings.insert(sect_pair.Name);
				total_bytes += sizeof(sect_pair.Name) + sizeof(sect_pair.Data);

                // Items
                for (const auto& d : sect_pair.Data)
                {
                    strings.insert(d.first);
                    strings.insert(d.second);
                    strings.insert(d.filename);
                    total_bytes += sizeof(d.depth) + sizeof(d.insertionIndex) + sizeof(d.first) + sizeof(d.second) + sizeof(d.filename);
                }
			}
		}
        for (const auto& s : strings)
            total_bytes += s.size();
	}

private:
	IC bool IsValidFileNameForCache() const
	{
		return m_file_name && m_file_name[0];
	}

	xr_unordered_flat_map<shared_str, xr_unordered_flat_set<shared_str>> OverrideToFilename;
	xr_unordered_flat_map<shared_str, shared_str> SectionToFilename;
	xr_unordered_flat_set<shared_str> SectionsToDelete;
	xr_unordered_flat_map<shared_str, RStringVec> BaseParentDataMap;
	xr_unordered_flat_map<shared_str, Sect> BaseData;
	xr_unordered_flat_map<shared_str, RStringVec> OverrideParentDataMap;
	xr_unordered_flat_map<shared_str, Sect> OverrideData;
	xr_unordered_flat_map<shared_str, Items> OverrideModifyListData;
	struct EvaluationsContext
	{
		xr_unordered_flat_map<shared_str, Items> ResolvedCache; // "Black" Set
		RStringVec RecursionStack;              // "Gray" Set

		// Helper to check if we are currently visiting a section
		bool IsInStack(const shared_str& section) const
		{
			return std::find(RecursionStack.begin(), RecursionStack.end(), section) != RecursionStack.end();
		}

		xr_string GetRecursionStackAsString() const
		{
			xr_string result;
			for (const auto& section : RecursionStack)
			{
				if (!result.empty())
					result += " -> ";
				result += section.c_str();
			}
			return result;
		}
	};
	void InsertIntoDATA(xr_unordered_flat_map<shared_str, Items>& FinalData);
	enum InsertType
	{
		Override,
		Base,
		Parent
	};
	enum ModifyListType : char
	{
		Insert = '>',
		Remove = '<'
	};
	void LTXLoad(
		IReader* F,
		LPCSTR path,
		BOOL bIsRootFile,
		string_path currentFileName,
		int depth
#ifndef _EDITOR
		, allow_include_func_t allow_include_func = NULL
#endif
	);
private:
	void loadFile(
		const string_path _fn,
		const string_path inc_path,
		const string_path name,
		string_path currentFileName,
		int depth
#ifndef _EDITOR
		, allow_include_func_t allow_include_func
#endif
	);
	void StashCurrentSection(
		Sect*& CurrentBase,
		Sect*& CurrentOverride,
		string_path currentFileName
	);
	Items EvaluateSection(
		shared_str SectionName,
		EvaluationsContext& Evaluations,
		string_path currentFileName
	);
	Items MergeSections(
		const Items& BaseItems,
		const Items& OverrideItems,
		xr_unordered_flat_set<shared_str>& DeletedItems,
		bool IsMergingBaseAndMod
	);
	void insert_item(CInifile::Sect* tgt, CInifile::Item& I);
	void SortAndFilterSection(Sect& Data);

public:
	void save_as(IWriter& writer, bool bcheck = false) const;
	void set_override_names(BOOL b) { m_flags.set(eOverrideNames, b); }
	void save_at_end(BOOL b) { m_flags.set(eSaveAtEnd, b); }
	LPCSTR fname() const { return m_file_name; };

	Sect& r_section(LPCSTR S) const;
	Sect& r_section(const shared_str& S) const;
	BOOL line_exist(LPCSTR S, LPCSTR L) const;
	BOOL line_exist(const shared_str& S, const shared_str& L) const;
	u32 line_count(LPCSTR S) const;
	u32 line_count(const shared_str& S) const;
	u32 section_count() const;
	BOOL section_exist(LPCSTR S) const;
	BOOL section_exist(const shared_str& S) const;
	Root& sections() { return DATA; }
	Root const& sections() const { return DATA; }

	CLASS_ID r_clsid(LPCSTR S, LPCSTR L) const;
	CLASS_ID r_clsid(const shared_str& S, LPCSTR L) const { return r_clsid(*S, L); }
	LPCSTR r_string(LPCSTR S, LPCSTR L) const; // оставляет кавычки
	LPCSTR r_string(const shared_str& S, LPCSTR L) const { return r_string(*S, L); } // оставляет кавычки
	shared_str r_string_wb(LPCSTR S, LPCSTR L) const; // убирает кавычки
	shared_str r_string_wb(const shared_str& S, LPCSTR L) const { return r_string_wb(*S, L); } // убирает кавычки
	u8 r_u8(LPCSTR S, LPCSTR L) const;
	u8 r_u8(const shared_str& S, LPCSTR L) const { return r_u8(*S, L); }
	u16 r_u16(LPCSTR S, LPCSTR L) const;
	u16 r_u16(const shared_str& S, LPCSTR L) const { return r_u16(*S, L); }
	u32 r_u32(LPCSTR S, LPCSTR L) const;
	u32 r_u32(const shared_str& S, LPCSTR L) const { return r_u32(*S, L); }
	u64 r_u64(LPCSTR S, LPCSTR L) const;
	s8 r_s8(LPCSTR S, LPCSTR L) const;
	s8 r_s8(const shared_str& S, LPCSTR L) const { return r_s8(*S, L); }
	s16 r_s16(LPCSTR S, LPCSTR L) const;
	s16 r_s16(const shared_str& S, LPCSTR L) const { return r_s16(*S, L); }
	s32 r_s32(LPCSTR S, LPCSTR L) const;
	s32 r_s32(const shared_str& S, LPCSTR L) const { return r_s32(*S, L); }
	s64 r_s64(LPCSTR S, LPCSTR L) const;
	float r_float(LPCSTR S, LPCSTR L) const;
	float r_float(const shared_str& S, LPCSTR L) const { return r_float(*S, L); }
	Fcolor r_fcolor(LPCSTR S, LPCSTR L) const;
	Fcolor r_fcolor(const shared_str& S, LPCSTR L) const { return r_fcolor(*S, L); }
	u32 r_color(LPCSTR S, LPCSTR L) const;
	u32 r_color(const shared_str& S, LPCSTR L) const { return r_color(*S, L); }
	Ivector2 r_ivector2(LPCSTR S, LPCSTR L) const;
	Ivector2 r_ivector2(const shared_str& S, LPCSTR L) const { return r_ivector2(*S, L); }
	Ivector3 r_ivector3(LPCSTR S, LPCSTR L) const;
	Ivector3 r_ivector3(const shared_str& S, LPCSTR L) const { return r_ivector3(*S, L); }
	Ivector4 r_ivector4(LPCSTR S, LPCSTR L) const;
	Ivector4 r_ivector4(const shared_str& S, LPCSTR L) const { return r_ivector4(*S, L); }
	Fvector2 r_fvector2(LPCSTR S, LPCSTR L) const;
	Fvector2 r_fvector2(const shared_str& S, LPCSTR L) const { return r_fvector2(*S, L); }
	Fvector3 r_fvector3(LPCSTR S, LPCSTR L) const;
	Fvector3 r_fvector3(const shared_str& S, LPCSTR L) const { return r_fvector3(*S, L); }
	Fvector4 r_fvector4(LPCSTR S, LPCSTR L) const;
	Fvector4 r_fvector4(const shared_str& S, LPCSTR L) const { return r_fvector4(*S, L); }
	BOOL r_bool(LPCSTR S, LPCSTR L) const;
	BOOL r_bool(const shared_str& S, LPCSTR L) const { return r_bool(*S, L); }
	int r_token(LPCSTR S, LPCSTR L, const xr_token* token_list) const;
	BOOL r_line(LPCSTR S, int L, LPCSTR* N, LPCSTR* V) const;
	BOOL r_line(const shared_str& S, int L, LPCSTR* N, LPCSTR* V) const;

	void w_string(LPCSTR S, LPCSTR L, LPCSTR V, LPCSTR comment = 0);
	void w_u8(LPCSTR S, LPCSTR L, u8 V, LPCSTR comment = 0);
	void w_u16(LPCSTR S, LPCSTR L, u16 V, LPCSTR comment = 0);
	void w_u32(LPCSTR S, LPCSTR L, u32 V, LPCSTR comment = 0);
	void w_u64(LPCSTR S, LPCSTR L, u64 V, LPCSTR comment = 0);
	void w_s64(LPCSTR S, LPCSTR L, s64 V, LPCSTR comment = 0);
	void w_s8(LPCSTR S, LPCSTR L, s8 V, LPCSTR comment = 0);
	void w_s16(LPCSTR S, LPCSTR L, s16 V, LPCSTR comment = 0);
	void w_s32(LPCSTR S, LPCSTR L, s32 V, LPCSTR comment = 0);
	void w_float(LPCSTR S, LPCSTR L, float V, LPCSTR comment = 0);
	void w_fcolor(LPCSTR S, LPCSTR L, const Fcolor& V, LPCSTR comment = 0);
	void w_color(LPCSTR S, LPCSTR L, u32 V, LPCSTR comment = 0);
	void w_ivector2(LPCSTR S, LPCSTR L, const Ivector2& V, LPCSTR comment = 0);
	void w_ivector3(LPCSTR S, LPCSTR L, const Ivector3& V, LPCSTR comment = 0);
	void w_ivector4(LPCSTR S, LPCSTR L, const Ivector4& V, LPCSTR comment = 0);
	void w_fvector2(LPCSTR S, LPCSTR L, const Fvector2& V, LPCSTR comment = 0);
	void w_fvector3(LPCSTR S, LPCSTR L, const Fvector3& V, LPCSTR comment = 0);
	void w_fvector4(LPCSTR S, LPCSTR L, const Fvector4& V, LPCSTR comment = 0);
	void w_bool(LPCSTR S, LPCSTR L, BOOL V, LPCSTR comment = 0);

	void remove_line(LPCSTR S, LPCSTR L);
};

// Main configuration file
extern XRCORE_API CInifile const* pSettings;
extern XRCORE_API CInifile const* pSettingsAuth;
