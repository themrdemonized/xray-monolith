#include "stdafx.h"
#pragma hdrstop

#include "xrstring.h"
#include <sstream>

#include "FS_impl.h"

XRCORE_API intrusive_ptr<str_container> g_pStringContainer = nullptr;

str_container::str_container() {}
str_container::str_container(str_container_constructor_key) 
{
	auto p = xr_malloc(block_size);
	if (!p)
		Debug.fatal(DEBUG_INFO, "str_container, failed to allocate block size %zu", block_size);
	storage.emplace_back((char*)p, block_size);
}

str_container* str_container::create()
{
	return xr_new<str_container>(str_container_constructor_key{});
}

char* str_container::alloc_in_pool(str_c s, u32 len)
{
	if (storage.back().used + len > storage.back().capacity)
	{
		u32 size = _max(block_size, len);
		auto p = xr_malloc(size);
		if (!p)
			Debug.fatal(DEBUG_INFO, "str_container, failed to allocate block size %zu", size);
		storage.emplace_back((char*)p, size);
	}

	pool_block& b = storage.back();
	char* dest = b.base + b.used;

	// Copy the raw characters
	std::memcpy(dest, s, len);
	b.used += len;

	return dest;
}

str_value* str_container::dock(str_c value)
{
	if (!value) return nullptr;

	size_t hash = xr_hash<std::string_view>()(value);
	u32 len = xr_strlen(value);
	u32 slot = u32(hash % buffer_size);

	// Most of the time, the string already exists. Use shared lock
	{
		xrSRWLockGuard guard(&rwlock, true);
		for (auto& item : buffer[slot])
		{
			if (hash == item.hash && len == item.length && std::memcmp(value, item.value, len) == 0)
				return &item;
		}
	}

	// String not found, upgrade to exclusive lock
	{
		xrSRWLockGuard guard(&rwlock, false);

		// Double-check: Did another thread create this while we were swapping locks?
		for (auto& item : buffer[slot])
		{
			if (hash == item.hash && len == item.length && std::memcmp(value, item.value, len) == 0)
				return &item;
		}

		char* pooled_ptr = alloc_in_pool(value, len + 1);
		buffer[slot].emplace_front(pooled_ptr, hash, len);
		return &buffer[slot].front();
	}
}

void str_container::erase(str_c value)
{
	if (!value) return;

	size_t hash = xr_hash<std::string_view>()(value);
	u32 len = xr_strlen(value);
	u32 slot = u32(hash % buffer_size);

	xrSRWLockGuard guard(&rwlock, false);
	auto before = buffer[slot].before_begin();
	for (auto it = buffer[slot].begin(); it != buffer[slot].end(); )
	{
		if (hash == it->hash && len == it->length && std::memcmp(value, it->value, len) == 0)
			it = buffer[slot].erase_after(before);
		else
		{
			before = it;
			it++;
		}
	}
}

void str_container::clean()
{
	xrSRWLockGuard guard(&rwlock, false);
	for (auto& list : buffer)
		list.clear();

	for (auto& block : storage)
	{
		if (block.base)
			xr_free(block.base);
	}
	storage.clear();
}

void str_container::verify()
{
	Msg("strings verify started");
	// do something
	Msg("strings verify completed");
}

void str_container::dump()
{
	FILE* F = fopen("d:\\$str_dump$.txt", "w");
	xrSRWLockGuard guard(&rwlock, true);
	for (const auto& list : buffer)
	{
		for (const auto& s: list)
			fprintf(F, "ref[%d]-len[%d] : %s\n", s.dwReference.load(), xr_strlen(s.value), s.value);
	}
	fclose(F);
}

void str_container::dump(IWriter* W)
{
	xrSRWLockGuard guard(&rwlock, true);
	for (const auto& list : buffer)
	{
		for (const auto& s : list)
		{
			string4096 temp;
			xr_sprintf(temp, sizeof(temp), "ref[%d]-len[%d] : %s\n", s.dwReference.load(), xr_strlen(s.value), s.value);
			W->w_string(temp);
		}
	}
}

void str_container::dump_console()
{
	xr_set<xr_string> set;
	u32 total_strings = 0;
	float load_factor = 0.0f;
	u32 max_collisions = 0;

	xrSRWLockGuard guard(&rwlock, true);
	for (const auto& list : buffer)
	{
		u32 count = 0;
		for (const auto& s : list)
		{
			Msg("ref[%d]-len[%d] : %s\n", s.dwReference.load(), xr_strlen(s.value), s.value);
			count++;
			set.emplace(s.value);
		}
		if (count > max_collisions)
			max_collisions = count;
		total_strings += count;
			
	}
	load_factor = (float)total_strings / (float)buffer_size;

	Msg("* [x-ray]: shared strings: pool blocks[%lu], count[%lu], unique[%lu], load factor[%.2f], max_collisions[%lu], shared_str objects[%lu]",
		storage.size(),
		total_strings,
		set.size(),
		load_factor,
		max_collisions,
		intrusive_ref_count() - 1
	);
	if (total_strings != set.size())
		Msg("! [x-ray]: shared strings, count != unique");
}

u32 str_container::stat_economy(u32& count, u32& unique)
{
	float load_factor = 0.0f;
	u32 max_collisions = 0;
	count = 0;

	xrSRWLockGuard guard(&rwlock, true);
	u32 size = sizeof(*this) + sizeof(shared_str) * intrusive_ref_count() - 1;
	size += buffer_size * sizeof(xr_forward_list<str_value>);
	for (const auto& block : storage) {
		size += block.capacity;
	}

	xr_set<xr_string> strings;
	for (const auto& list : buffer)
	{
		u32 c = 0;
		for (const auto& s : list)
		{
			c++;
			size += sizeof(str_value) + sizeof(void*);
			strings.emplace(s.value);
		}
		if (c > max_collisions)
			max_collisions = c;
		count += c;
	}
	unique = strings.size();
	load_factor = (float)count / (float)buffer_size;
	Msg("* [x-ray]: shared strings: pool blocks[%lu], count[%lu], unique[%lu], load factor[%.2f], max_collisions[%lu], shared_str objects[%lu]",
		storage.size(),
		count,
		unique,
		load_factor,
		max_collisions,
		intrusive_ref_count() - 1
	);
	return size;
}

str_container::~str_container()
{
	for (auto& list : buffer)
		list.clear();

	for (auto& block : storage)
	{
		if (block.base)
			xr_free(block.base);
	}
	storage.clear();
}

//xr_string class
xr_vector<xr_string> xr_string::Split(char splitCh) const
{
	xr_vector<xr_string> Result;

	u32 SubStrBeginCursor = 0;
	u32 Len = 0;

	u32 StrCursor = 0;
	for (; StrCursor < size(); ++StrCursor)
	{
		if (at(StrCursor) == splitCh)
		{
			if ((StrCursor - SubStrBeginCursor) > 0)
			{
				Len = StrCursor - SubStrBeginCursor;
				Result.emplace_back(xr_string(&at(SubStrBeginCursor), Len));
				SubStrBeginCursor = StrCursor + 1;
			}
			else
			{
				Result.emplace_back("");
				SubStrBeginCursor = StrCursor + 1;
			}
		}
	}

	if (StrCursor > SubStrBeginCursor)
	{
		Len = StrCursor - SubStrBeginCursor;
		Result.emplace_back(xr_string(&at(SubStrBeginCursor), Len));
	}
	return Result;
}

xr_string::xr_string(LPCSTR Str, u32 Size)
	: Super(Str, Size)
{
}

xr_string::xr_string(Super&& other)
	: Super(other)
{
}

xr_string::xr_string(LPCSTR Str)
	: Super(Str)
{
}

xr_string& xr_string::operator=(LPCSTR Str)
{
	Super::operator=(Str);
	return *this;
}

xr_string& xr_string::operator=(const Super& other)
{
	Super::operator=(other);
	return *this;
}

xr_vector<xr_string> xr_string::Split(u32 NumberOfSplits, ...) const
{
	xr_vector<xr_string> intermediateTokens;
	xr_vector<xr_string> Result;

	va_list args;
	va_start(args, NumberOfSplits);

	for (u32 i = 0; i < NumberOfSplits; ++i)
	{
		char splitCh = va_arg(args, char);

		//special case for first try
		if (i == 0)
		{
			Result = Split(splitCh);
		}

		for (xr_string& str : Result)
		{
			xr_vector<xr_string> TokenStrResult = str.Split(splitCh);
			intermediateTokens.insert(intermediateTokens.end(), TokenStrResult.begin(), TokenStrResult.end());
		}

		if (!intermediateTokens.empty())
		{
			Result.clear();
			Result.insert(Result.begin(), intermediateTokens.begin(), intermediateTokens.end());
			intermediateTokens.clear();
		}
	}

	va_end(args);

	return Result;
}


xr_string xr_string::RemoveWhitespaces() const
{
	size_t Size = size();
	if (Size == 0) return xr_string();

	xr_string Result;
	Result.reserve(Size);

	const char* OrigStr = data();

	for (size_t i = 0; i < Size; ++i)
	{
		if (*OrigStr != ' ')
		{
			Result.push_back(OrigStr[i]);
		}
	}

	return Result;
}

bool xr_string::StartWith(const xr_string& Other) const
{
	return StartWith(Other.data(), Other.size());
}


bool xr_string::StartWith(LPCSTR Str) const
{
	u32 StrLen = xr_strlen(Str);
	return StartWith(Str, (int)StrLen);
}

bool xr_string::StartWith(LPCSTR Str, size_t Size) const
{
	size_t OurSize = size();

	//String is greater then our, we can't success
	if (OurSize < Size) return false;

	const char* OurStr = data();

	for (int i = 0; i < Size; ++i)
	{
		if (OurStr[i] != Str[i])
		{
			return false;
		}
	}

	return true;
}

bool xr_string::Contains(const xr_string& SubStr) const
{
	return find(SubStr) != npos;
}

xr_string xr_string::ToString(int Value)
{
	string64 buf = { 0 };
	itoa(Value, &buf[0], 10);

	return xr_string(buf);
}

xr_string xr_string::ToString(unsigned int Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%u", Value);

	return xr_string(buf);
}

xr_string xr_string::ToString(float Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%f", Value);

	return xr_string(buf);
}

xr_string xr_string::ToString(double Value)
{
	string64 buf = { 0 };
	sprintf(buf, "%f", Value);

	return xr_string(buf);
}

xr_string xr_string::Join(xrStringVector::iterator beginIter, xrStringVector::iterator endIter, const char delimeter /*= '\0'*/)
{
	xr_string Result;
	xrStringVector::iterator cursorIter = beginIter;

	while (cursorIter != endIter)
	{
		Result.append(*cursorIter);
		if (delimeter != '\0')
		{
			Result.push_back(delimeter);
		}
		cursorIter++;
	}

	if (delimeter != '\0')
	{
		Result.erase(Result.end() - 1);
	}

	return Result;
}

// String utils
SStringVec xr_string::SplitStringMulti(xr_string separator, bool includeSeparators, bool trimStrings) const {
	std::stringstream stringStream(std::string(this->c_str()));
	xr_string line;
	SStringVec wordVector;

	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != xr_string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			if (includeSeparators)
				wordVector.push_back(line.substr(pos, 1));

			prev = pos + 1;
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, xr_string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			s = s.Trim();
		}
	}
	return wordVector;
}

SStringVec xr_string::SplitStringLimit(xr_string separator, int limit, bool trimStrings) const
{
	std::stringstream stringStream(std::string(this->c_str()));
	xr_string line;
	SStringVec wordVector;

	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != xr_string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			prev = pos + 1;
			if (limit > 0) {
				if (wordVector.size() >= limit) {
					wordVector.push_back(line.substr(prev, xr_string::npos));
					return wordVector;
				}
			}
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, xr_string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			s = s.Trim();
		}
	}
	return wordVector;
}

xr_string xr_string::Trim(const char* t) const
{
	xr_string result = *this;
	result.erase(result.find_last_not_of(t) + 1);
	result.erase(0, result.find_first_not_of(t));
	return result;
};

void xr_string::TrimInPlace(const char* t)
{
	erase(find_last_not_of(t) + 1);
	erase(0, find_first_not_of(t));
};


xr_string xr_string::ToLowerCase() const
{
	xr_string result = *this;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c)
	{
		return std::tolower(c);
	});
	return result;
}

xr_string xr_string::ReplaceAll(const xr_string& from, const xr_string& to) const
{
	xr_string result = *this;
	if (from.empty())
		return result;

	size_t start_pos = 0;
	while ((start_pos = result.find(from, start_pos)) != xr_string::npos)
	{
		result.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}

	return result;
}
