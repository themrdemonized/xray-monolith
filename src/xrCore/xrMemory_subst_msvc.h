#pragma once
#include <memory>

#ifdef DEBUG_MEMORY_NAME
// new(0)
template <class T>
IC T* xr_new()
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T();
}
// new(1)
template <class T, class P1>
IC T* xr_new(const P1& p1)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1);
}
// new(2)
template <class T, class P1, class P2>
IC T* xr_new(const P1& p1, const P2& p2)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2);
}
// new(3)
template <class T, class P1, class P2, class P3>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3);
}
// new(4)
template <class T, class P1, class P2, class P3, class P4>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4);
}
// new(5)
template <class T, class P1, class P2, class P3, class P4, class P5>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4, p5);
}
// new(6)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4, p5, p6);
}
// new(7)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4, p5, p6, p7);
}
// new(8)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7, class P8>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4, p5, p6, p7, p8);
}
// new(9)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7, class P8, class P9>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P8& p9)
{
    T* ptr = (T*)Memory.mem_alloc(sizeof(T), typeid(T).name());
    return new (ptr)T(p1, p2, p3, p4, p5, p6, p7, p8, p9);
}
#else // DEBUG_MEMORY_NAME
// new(0)
template <class T>
IC T* xr_new()
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T();
}

// new(1)
template <class T, class P1>
IC T* xr_new(const P1& p1)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1);
}

// new(2)
template <class T, class P1, class P2>
IC T* xr_new(const P1& p1, const P2& p2)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2);
}

// new(3)
template <class T, class P1, class P2, class P3>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3);
}

// new(4)
template <class T, class P1, class P2, class P3, class P4>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4);
}

// new(5)
template <class T, class P1, class P2, class P3, class P4, class P5>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4, p5);
}

// new(6)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4, p5, p6);
}

// new(7)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4, p5, p6, p7);
}

// new(8)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7, class P8>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7,
             const P8& p8)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4, p5, p6, p7, p8);
}

// new(9)
template <class T, class P1, class P2, class P3, class P4, class P5, class P6, class P7, class P8, class P9>
IC T* xr_new(const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7,
             const P8& p8, const P8& p9)
{
	T* ptr = (T*)Memory.mem_alloc(sizeof(T));
	return new(ptr)T(p1, p2, p3, p4, p5, p6, p7, p8, p9);
}
#endif // DEBUG_MEMORY_NAME

#include <fast_dynamic_cast/fast_dynamic_cast.hpp>

template <bool _is_pm, typename T>
struct xr_special_free
{
	IC void operator()(T*& ptr)
	{
		if (ptr == nullptr)
		{
			return;
		}

		if constexpr (_is_pm)
		{
			void* _real_ptr = fast_dynamic_cast<void*>(ptr);
			ptr->~T();
			Memory.mem_free(_real_ptr);
		}
		else
		{
			ptr->~T();
			Memory.mem_free(ptr);
		}
	}
};

template <typename T>
struct xr_special_free<false, T>
{
	IC void operator()(T*& ptr)
	{
		if (ptr == nullptr)
		{
			return;
		}
		
		ptr->~T();
		Memory.mem_free(ptr);
	}
};

template <class T>
IC void xr_delete(T*& ptr)
{
	if (ptr)
	{
		xr_special_free<std::is_polymorphic<T>::value, T>()(ptr);
		ptr = nullptr;
	}
}
template <class T>
IC void xr_delete(T* const& ptr)
{
	if (ptr)
	{
		xr_special_free<std::is_polymorphic<T>::value, T>()(const_cast<T*&>(ptr));
		const_cast<T*&>(ptr) = nullptr;
	}
}

#ifdef DEBUG_MEMORY_MANAGER
void XRCORE_API mem_alloc_gather_stats(const bool& value);
void XRCORE_API mem_alloc_gather_stats_frequency(const float& value);
void XRCORE_API mem_alloc_show_stats();
void XRCORE_API mem_alloc_clear_stats();
#endif // DEBUG_MEMORY_MANAGER

template <typename T>
struct xr_allocator_shared_helper {
    using value_type = T;
    xr_allocator_shared_helper() = default;
    template <class U> xr_allocator_shared_helper(const xr_allocator_shared_helper<U>&) {}
    T* allocate(std::size_t n) {
        return static_cast<T*>(Memory.mem_alloc(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) noexcept {
        Memory.mem_free(p);
    }
};

template<typename T>
using xr_weak_ptr = std::weak_ptr<T>;

template<typename T>
using xr_shared_ptr = std::shared_ptr<T>;

template<typename T>
using xr_unique_ptr = std::unique_ptr<T, xr_special_free<false, T>>;

template <class T, class... Args>
xr_shared_ptr<T> xr_make_shared(Args&&... args)
{
	xr_allocator_shared_helper<T> alloc;
	return std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
}

template <typename T, typename... ARGS>
xr_unique_ptr<T> xr_make_unique(ARGS&&... args)
{
	void* TypeMem = Memory.mem_alloc(sizeof(T));
	new (TypeMem)T(std::forward<ARGS>(args)...);
	return xr_unique_ptr<T>(static_cast<T*>(TypeMem), xr_special_free<false, T>{});
}
