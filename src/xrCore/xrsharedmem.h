#pragma once
#include "_thread_types.h"

#pragma pack(push,4)
//////////////////////////////////////////////////////////////////////////
#pragma warning(push)
#pragma warning(disable : 4200)
struct XRCORE_API smem_value
{
	xr_atomic_u32 dwReference;
	u32 dwCRC;
	u32 dwLength;
	u32 _align_16;
	u8 value[];
};

#pragma warning(pop)

//////////////////////////////////////////////////////////////////////////
class XRCORE_API smem_container
{
private:
	struct key
	{
		u32 crc, length;
		const u8* bytes;
		size_t content_hash;
		bool operator==(const key& other) const noexcept
		{
			return content_hash == other.content_hash && crc == other.crc && length == other.length &&
				memcmp(bytes, other.bytes, length) == 0;
		}
	};
	struct hash
	{
		size_t operator()(const key& value) const noexcept { return value.content_hash ^ value.crc; }
	};
#ifdef USE_ROBINHOOD
	typedef xr_unordered_flat_map<key, smem_value*, hash> cdb;
#else
	// Rebinding preserves the counter for every STL allocation, including buckets,
	// nodes and debug proxies. The underlying engine allocator remains unchanged.
	template <class T>
	struct index_allocator
	{
		using value_type = T;
		size_t* used = nullptr;

		index_allocator() noexcept = default;
		explicit index_allocator(size_t* counter) noexcept : used(counter) {}
		template <class U>
		index_allocator(const index_allocator<U>& other) noexcept : used(other.used) {}

		T* allocate(size_t count)
		{
			T* ptr = xalloc<T>().allocate(count);
			*used += count * sizeof(T);
			return ptr;
		}
		void deallocate(T* ptr, size_t count) noexcept
		{
			xalloc<T>().deallocate(ptr, count);
			*used -= count * sizeof(T);
		}
		template <class U>
		bool operator==(const index_allocator<U>& other) const noexcept { return used == other.used; }
		template <class U>
		bool operator!=(const index_allocator<U>& other) const noexcept { return !(*this == other); }
	};
	using cdb = xr_unordered_flat_map<key, smem_value*, hash, std::equal_to<key>,
		index_allocator<std::pair<const key, smem_value*>>>;
#endif
	xrCriticalSection cs;
#ifdef USE_ROBINHOOD
	cdb container;
#else
	// Declared before container so the counter outlives allocator deallocation.
	size_t index_bytes = 0;
	cdb container{cdb::allocator_type(&index_bytes)};
#endif
public:
	smem_value* dock(u32 dwCRC, u32 dwLength, void* ptr);
	void clean();
	void dump();
	// Signed net savings in bytes versus one payload per reference. Includes block
	// headers and all retained index allocations; excludes heap-private metadata.
	s64 stat_economy(size_t* memory_usage = nullptr);
#ifdef PROFILE_CRITICAL_SECTIONS
    smem_container ():cs(MUTEX_PROFILE_ID(smem_container)) {}
#endif // PROFILE_CRITICAL_SECTIONS
	~smem_container();
};

XRCORE_API extern smem_container* g_pSharedMemoryContainer;

//////////////////////////////////////////////////////////////////////////
template <class T>
class ref_smem
{
private:
	smem_value* p_;
protected:
	// ref-counting
	void _dec()
	{
		if (0 == p_) return;
		if (p_->dwReference.fetch_sub(1, std::memory_order_acq_rel) == 1) p_ = 0;
	}

public:
	void _set(ref_smem const& rhs)
	{
		smem_value* v = rhs.p_;
		if (0 != v) v->dwReference.fetch_add(1, std::memory_order_relaxed);
		_dec();
		p_ = v;
	}

	const smem_value* _get() const { return p_; }
public:
	// construction
	ref_smem() { p_ = 0; }

	ref_smem(ref_smem<T> const& rhs)
	{
		p_ = 0;
		_set(rhs);
	}

	~ref_smem() { _dec(); }

	void create(u32 dwCRC, u32 dwLength, T* ptr)
	{
		smem_value* v = g_pSharedMemoryContainer->dock(dwCRC, dwLength * sizeof(T), ptr);
		if (0 != v) v->dwReference.fetch_add(1, std::memory_order_relaxed);
		_dec();
		p_ = v;
	}

	// assignment & accessors
	ref_smem<T>& operator=(ref_smem<T> const& rhs)
	{
		_set(rhs);
		return (ref_smem<T>&)*this;
	}

	T* operator*() const { return p_ ? (T*)p_->value : 0; }
	bool operator!() const { return p_ == 0; }
	T& operator[](size_t id) { return ((T*)(p_->value))[id]; }
	const T& operator[](size_t id) const { return ((T*)(p_->value))[id]; }
	// misc func
	u32 size()
	{
		if (0 == p_) return 0;
		else return p_->dwLength / sizeof(T);
	}

	void swap(ref_smem<T>& rhs)
	{
		smem_value* tmp = p_;
		p_ = rhs.p_;
		rhs.p_ = tmp;
	}

	bool equal(ref_smem<T>& rhs) { return p_ == rhs.p_; }

	u32 ref_count()
	{
		if (0 == p_) return 0;
		else return p_->dwReference.load(std::memory_order_relaxed);
	}
};

// res_ptr == res_ptr
// res_ptr != res_ptr
// const res_ptr == ptr
// const res_ptr != ptr
// ptr == const res_ptr
// ptr != const res_ptr
// res_ptr < res_ptr
// res_ptr > res_ptr
template <class T>
IC bool operator ==(ref_smem<T> const& a, ref_smem<T> const& b) { return a._get() == b._get(); }

template <class T>
IC bool operator !=(ref_smem<T> const& a, ref_smem<T> const& b) { return a._get() != b._get(); }

template <class T>
IC bool operator <(ref_smem<T> const& a, ref_smem<T> const& b) { return a._get() < b._get(); }

template <class T>
IC bool operator >(ref_smem<T> const& a, ref_smem<T> const& b) { return a._get() > b._get(); }

// externally visible standart functionality
template <class T>
IC void swap(ref_smem<T>& lhs, ref_smem<T>& rhs) { lhs.swap(rhs); }

#pragma pack(pop)
