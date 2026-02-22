#ifndef xrstringH
#define xrstringH
#pragma once

#pragma pack(push,4)
//////////////////////////////////////////////////////////////////////////
using str_c = const char*;

#include <string>
#include <vector>
#include "_thread_types.h"
#include "_stl_extensions.h"
#include "intrusive_ptr.h"

class xr_string;

class XRCORE_API xr_string : public std::basic_string<char, std::char_traits<char>, xalloc<char>>
{
public:
	typedef std::basic_string<char, std::char_traits<char>, xalloc<char>> Super;

	xr_string() = default;
	xr_string(const xr_string& other) = default;
	xr_string(xr_string&& other) noexcept = default;

	xr_string(LPCSTR Str);
	xr_string(LPCSTR Str, u32 Size);
	xr_string(Super&& other);

	xr_string& operator=(LPCSTR Str);
	xr_string& operator=(const Super& other);

	xr_string& operator=(const xr_string& other) = default;
	xr_string& operator=(xr_string&& other) = default;

	template <size_t ArrayLenght>
	xr_string(char* (&InArray)[ArrayLenght]);

	xr_vector<xr_string> Split(char splitCh) const;
	xr_vector<xr_string> Split(u32 NumberOfSplits, ...) const;

	bool StartWith(const xr_string& Other) const;
	bool StartWith(LPCSTR Str) const;
	bool StartWith(LPCSTR Str, size_t Size) const;

	bool Contains(const xr_string& SubStr) const;

	xr_string RemoveWhitespaces() const;

	static xr_string ToString(int Value);
	static xr_string ToString(unsigned int Value);
	static xr_string ToString(float Value);
	static xr_string ToString(double Value);
	static xr_string ToString(const Fvector& Value);
	static xr_string ToString(const Dvector& Value);

	using xrStringVector = xr_vector<xr_string>;

	xrStringVector SplitStringMulti(xr_string separator = " ", bool includeSeparators = false, bool trimStrings = false) const;
	xrStringVector SplitStringLimit(xr_string separator = " ", int limit = 0, bool trimStrings = false) const;
	xr_string Trim(const char* t = " \t\n\r\f\v") const;
	void TrimInPlace(const char* t = " \t\n\r\f\v");
	xr_string ToLowerCase() const;
	xr_string ReplaceAll(const xr_string& from, const xr_string& to) const;

	static xr_string Join(xrStringVector::iterator beginIter, xrStringVector::iterator endIter, const char delimeter = '\0');
};

using SStringVec = xr_vector<xr_string>;
using SStringVecIt = SStringVec::iterator;

// warning
// this function can be used for debug purposes only
//template <typename... Args>
//const char* make_string(const char* format, const Args&... args) {
//	static constexpr size_t bufferSize = 4096;
//	static char temp[bufferSize];
//	snprintf(temp, bufferSize, format, args...);
//	return temp;
//}

namespace std
{
	template<>
	class hash<xr_string>
	{
		public:
			size_t operator()(const xr_string& s) const
			{
				return xr_hash<std::string_view>()(std::string_view(s));
			}
	};
}

struct XRCORE_API str_value
{
	char* value;
	size_t hash;
	mutable xr_atomic_u32 dwReference;
	u32 length;

	str_value() : value(nullptr), hash(0), dwReference(0), length(0) {}
	str_value(char* s) : value(s), hash(xr_hash<std::string_view>()(s)), dwReference(0), length(xr_strlen(s)) {};
	str_value(char* s, size_t hash, u32 length) : value(s), hash(hash), dwReference(0), length(length)  {};
	
	// Explicit Move Semantics
	str_value(str_value&& other) noexcept : 
		value(other.value),
		hash(other.hash),
		length(other.length)
	{
		dwReference.store(other.dwReference.exchange(0));
		other.value = nullptr;
		other.hash = 0;
		other.length = 0;
	}

	// Move Assignment
	str_value& operator=(str_value&& other) noexcept 
	{
		if (this == &other) return *this;

		value = std::exchange(other.value, nullptr);
		hash = std::exchange(other.hash, 0);
		length = std::exchange(other.length, 0);
		dwReference.store(other.dwReference.exchange(0));

		return *this;
	}

	// Disable Copying (Standard for interned strings to prevent accidents)
	str_value(const str_value&) = delete;
	str_value& operator=(const str_value&) = delete;

	bool operator<(const str_value& other) const
	{
		return value < other.value;
	}

	bool operator==(const str_value& other) const
	{
		return hash == other.hash && value == other.value;
	}
};

struct XRCORE_API str_value_hash
{
	size_t operator()(const str_value& s) const noexcept
	{
		return xr_hash<xr_string>()(s.value);
	}
};

class IWriter;

class XRCORE_API str_container : public intrusive_base
{
private:
	struct pool_block {
		char* base;     // Start of the block
		u32 used;     // How much used in block
		const u32 capacity; // Total memory used by block
		pool_block(char* base, u32 capacity) : base(base), used(0), capacity(capacity) {}
	};
	xr_vector<pool_block> storage;
	char* alloc_in_pool(str_c value, u32 len);
	static constexpr const u32 block_size = 4 * 1024 * 1024; // 4MB
	static constexpr const u32 buffer_size = 1 << 19; // 524288 slots
	xr_array<xr_forward_list<str_value>, buffer_size> buffer;
	xrSRWLock rwlock;

	// Force create only on heap
private:
	struct str_container_constructor_key { explicit str_container_constructor_key() = default; };
	str_container();
	
public:
	str_container(str_container_constructor_key);
	static str_container* create();
	~str_container();

	str_value* dock(str_c value);
	void erase(str_c value);
	void clean();
	void dump();
	void dump(IWriter* W);
	void dump_console();
	void verify();
	u32 stat_economy(u32& count, u32& unique);
};

XRCORE_API extern intrusive_ptr<str_container> g_pStringContainer;

class shared_str
{
private:
	str_value* p_ = nullptr;
	intrusive_ptr<str_container> container_ptr = nullptr;
protected:
	// ref-counting
	void _dec()
	{
		if (0 == p_) return;
		if (0 == --p_->dwReference)
		{
			//g_pStringContainer->erase(p_->value.c_str()); // erasing causes crashes due to invalid pointers, not implemented yet
			p_ = 0;
		}
	}

public:
	void _set(str_c rhs)
	{
		auto gc = g_pStringContainer;
		if (gc)
		{
			str_value* v = gc->dock(rhs);
			if (0 != v) v->dwReference++;
			_dec();

			container_ptr = gc;	

			p_ = v;
		}
		else
		{
			// no container available
			_dec();
			container_ptr = nullptr;
			p_ = 0;
		}
	}

	void _set(shared_str const& rhs)
	{
		if (this == &rhs)
			return;

		str_value* v = rhs.p_;
		if (0 != v) v->dwReference++;
		_dec();

		container_ptr = rhs.container_ptr;

		p_ = v;
	}

	const str_value* _get() const { return p_; }

	// construction
	shared_str() {}

	shared_str(str_c rhs)
	{
		_set(rhs);
	}

	shared_str(shared_str const& rhs)
	{
		_set(rhs);
	}

	~shared_str()
	{
		_dec();
	}

	// assignment & accessors
	shared_str& operator=(str_c rhs)
	{
		_set(rhs);
		return (shared_str&)*this;
	}

	shared_str& operator=(shared_str const& rhs)
	{
		if (this == &rhs)
			return (shared_str&)*this;

		_set(rhs);
		return (shared_str&)*this;
	}

	str_c operator*() const { return p_ ? p_->value : 0; }
	bool operator!() const { return p_ == 0; }
	char operator[](size_t id) { return p_->value[id]; }
	str_c c_str() const { return p_ ? p_->value : 0; }

	// misc func
	u32 size() const
	{
		if (0 == p_) return 0;
		else return p_->length;
	}

	void swap(shared_str& rhs)
	{
		std::swap(p_, rhs.p_);
		std::swap(container_ptr, rhs.container_ptr);
	}

	bool equal(const shared_str& rhs) const { return (p_ == rhs.p_); }

	shared_str& __cdecl printf(const char* format, ...)
	{
		string4096 buf;
		va_list p;
		va_start(p, format);
		int vs_sz = _vsnprintf(buf, sizeof(buf) - 1, format, p);
		buf[sizeof(buf) - 1] = 0;
		va_end(p);
		if (vs_sz) _set(buf);
		return (shared_str&)*this;
	}
};

namespace std
{
	template<>
	class hash<shared_str>
	{
	public:
		size_t operator()(const shared_str& s) const
		{
			return xr_hash<str_value*>()(const_cast<str_value*>(s._get()));
		}
	};
}

// res_ptr == res_ptr
// res_ptr != res_ptr
// const res_ptr == ptr
// const res_ptr != ptr
// ptr == const res_ptr
// ptr != const res_ptr
// res_ptr < res_ptr
// res_ptr > res_ptr
IC bool operator ==(shared_str const& a, shared_str const& b) { return a._get() == b._get(); }
IC bool operator !=(shared_str const& a, shared_str const& b) { return a._get() != b._get(); }
IC bool operator <(shared_str const& a, shared_str const& b) { return a._get() < b._get(); }
IC bool operator >(shared_str const& a, shared_str const& b) { return a._get() > b._get(); }

// externally visible standart functionality
IC void swap(shared_str& lhs, shared_str& rhs) { lhs.swap(rhs); }
IC u32 xr_strlen(shared_str& a) { return a.size(); }
IC int xr_strcmp(const shared_str& a, const char* b) { return xr_strcmp(*a, b); }
IC int xr_strcmp(const char* a, const shared_str& b) { return xr_strcmp(a, *b); }
IC int xr_strcmp(const shared_str& a, const shared_str& b)
{
	if (a.equal(b)) return 0;
	else return xr_strcmp(*a, *b);
}

IC void xr_strlwr(xr_string& src)
{
	for (xr_string::iterator it = src.begin(); it != src.end(); ++it) *it = xr_string::value_type(tolower(*it));
}

IC void xr_strlwr(shared_str& src)
{
	if (*src)
	{
		LPSTR lp = xr_strdup(*src);
		xr_strlwr(lp);
		src = lp;
		xr_free(lp);
	}
}

IC bool IsUTF8(const char* string)
{
	if (!string)
		return true;

	const unsigned char* bytes = (const unsigned char*)string;
	int num;
	while (*bytes != 0x00)
	{
		if ((*bytes & 0x80) == 0x00)
		{
			// U+0000 to U+007F
			num = 1;
		}
		else if ((*bytes & 0xE0) == 0xC0)
		{
			// U+0080 to U+07FF
			num = 2;
		}
		else if ((*bytes & 0xF0) == 0xE0)
		{
			// U+0800 to U+FFFF
			num = 3;
		}
		else if ((*bytes & 0xF8) == 0xF0)
		{
			// U+10000 to U+10FFFF
			num = 4;
		}
		else
			return false;
		bytes += 1;
		for (int i = 1; i < num; ++i)
		{
			if ((*bytes & 0xC0) != 0x80)
				return false;
			bytes += 1;
		}
	}
	return true;
}

IC xr_string UTF8_to_CP1251(xr_string const& utf8)
{
	if (!utf8.empty() && IsUTF8(utf8.data()))
	{
		int wchlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), nullptr, 0);
		if (wchlen > 0 && wchlen != 0xFFFD)
		{
			xr_vector<wchar_t> wbuf(wchlen);
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), &wbuf[0], wchlen);
			xr_vector<char> buf(wchlen);
			WideCharToMultiByte(1251, 0, &wbuf[0], wchlen, &buf[0], wchlen, 0, 0);

			return xr_string(&buf[0], wchlen);
		}
	}

	return utf8;
}

#pragma pack(pop)

#endif
