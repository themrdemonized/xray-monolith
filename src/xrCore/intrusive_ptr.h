////////////////////////////////////////////////////////////////////////////
// Module : intrusive_ptr.h
// Created : 30.07.2004
// Modified : 04.02.2026
// Author : Dmitriy Iassenev, demonized
// Description : Intrusive pointer template (C++17)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include <type_traits>
#include <utility> // for std::swap
#include <cstddef> // for std::nullptr_t
#include "_thread_types.h"

// Counter behaviors
enum class CounterPolicy
{
    Atomic,
    NonAtomic
};

template <CounterPolicy Counter = CounterPolicy::Atomic>
struct ref_count_storage
{
protected:
    std::conditional_t<Counter == CounterPolicy::Atomic, xr_atomic_u32, u32> __ref_count;

public:
    IC u32 intrusive_ref_count() const
    {
        if constexpr (Counter == CounterPolicy::Atomic)
            return __ref_count.load(std::memory_order_relaxed);
        else
            return __ref_count;
    }
    IC u32 intrusive_ref_add()
    {
        if constexpr (Counter == CounterPolicy::Atomic)
        {
            u32 t = __ref_count.fetch_add(1, std::memory_order_relaxed);
            return t + 1;
        }
        else
            return ++__ref_count;
    }
    IC u32 intrusive_ref_sub()
    {
        if constexpr (Counter == CounterPolicy::Atomic)
        {
            u32 t = __ref_count.fetch_sub(1, std::memory_order_acq_rel)
            return t - 1;
        }
        else
            return --__ref_count;
    }

protected:
    IC bool intrusive_ref_sub_and_check()
    {
        if constexpr (Counter == CounterPolicy::Atomic)
        {
            if (__ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                return true;
            }
            return false;
        }
        else
            return (--__ref_count == 0);
    }

public:
    IC ref_count_storage() : __ref_count(0) {}
};

// Possible deletetion behaviors
enum class DeletionPolicy
{
    Immediate,
    Deferred,
    Strict
};

// A simple, empty struct just for is_base_of checks
struct intrusive_base_marker {};

// Helpers for virtual or non-virtual destructor
struct destructor_virtual { virtual ~destructor_virtual() = default; };
struct destructor_non_virtual { ~destructor_non_virtual() = default; };

template <DeletionPolicy Policy = DeletionPolicy::Immediate, CounterPolicy Counter = CounterPolicy::Atomic, bool Virtual = true>
struct __declspec(novtable) intrusive_base_impl : public intrusive_base_marker, ref_count_storage<Counter>, std::conditional_t<Virtual, destructor_virtual, destructor_non_virtual>
{
    // This makes the policy visible to the smart pointer
    static constexpr DeletionPolicy deletion_policy = Policy;
    static constexpr bool virtual_destructor = Virtual;

    template <typename T>
    IC bool intrusive_release(T* object)
    {
        if (this->intrusive_ref_sub_and_check())
        {
            this->_release(object);
            return true;
        }
        return false;
    }

	IC intrusive_base_impl() {}

private:
	// Deferred will use callback to use own deletion logic, ie zombie state
	template <typename T>
	IC void _release(T* object)
	{
        if constexpr (Policy == DeletionPolicy::Immediate)
            xr_delete(object);
        else 
            object->on_deferred_release();
	}
};

// Forward declare the engine's internal deleter struct.
// This is what 'xr_delete' uses under the hood
template <bool _is_pm, typename T> struct xr_special_free;

// Strict policy - forbid calling xr_delete<ptr.get()>, must have protected destructor
template<CounterPolicy Counter>
struct __declspec(novtable) intrusive_base_impl<DeletionPolicy::Strict, Counter> : public intrusive_base_marker, ref_count_storage<Counter>
{
    // This makes the policy visible to the smart pointer
    static constexpr DeletionPolicy deletion_policy = DeletionPolicy::Strict;
    static constexpr bool virtual_destructor = true;

    template <typename T>
    IC bool intrusive_release(T* object)
    {
        if (this->intrusive_ref_sub_and_check())
        {
            this->_release(object);
            return true;
        }
        return false;
    }

    IC intrusive_base_impl() {}

    // Grant access to the engine's memory freer.
    // This allows 'xr_delete(base_ptr)' to work even though the destructor is protected.
    template <bool, typename> friend struct ::xr_special_free;

protected:
    // Force virtual destructor on children
    // Protected destructor prevents manual deletion by USERS,
    // but the friend declaration above allows deletion by the ENGINE.
    IC virtual ~intrusive_base_impl() {}

private:
    // Changed from template<T> to intrusive_base_impl*
    // This forces all derived objects to be deleted 
    // via their base pointer. This invokes the virtual destructor chain correctly
    // but ensures we only need to friend xr_special_free in THIS class, 
    // not in every derived class.
    IC void _release(intrusive_base_impl* object)
    {
        xr_delete(object);
    }
};

using intrusive_base = intrusive_base_impl<DeletionPolicy::Immediate, CounterPolicy::Atomic>;
using intrusive_base_nonatomic = intrusive_base_impl<DeletionPolicy::Immediate, CounterPolicy::NonAtomic>;
using intrusive_base_deferred = intrusive_base_impl<DeletionPolicy::Deferred, CounterPolicy::Atomic>;
using intrusive_base_deferred_nonatomic = intrusive_base_impl<DeletionPolicy::Deferred, CounterPolicy::NonAtomic>;
using intrusive_base_strict = intrusive_base_impl<DeletionPolicy::Strict, CounterPolicy::Atomic>;
using intrusive_base_strict_nonatomic = intrusive_base_impl<DeletionPolicy::Strict, CounterPolicy::NonAtomic>;

#define TEMPLATE_SPECIALIZATION template <typename object_type>
#define _intrusive_ptr intrusive_ptr<object_type>

TEMPLATE_SPECIALIZATION
class intrusive_ptr
{
public:
    typedef object_type object_type;
    typedef _intrusive_ptr self_type;

private:
    static constexpr DeletionPolicy deletion_policy = object_type::deletion_policy;
    static constexpr bool virtual_destructor = object_type::virtual_destructor;

    // Static check instead of the old enum hack
    static_assert(std::is_base_of_v<intrusive_base_marker, object_type>,
        "intrusive_ptr<T>: T must be derived from intrusive_base");

    // Safety Enforcement
    // This asserts that 'object_type' does NOT have a public destructor if policy is Strict (intrusive_base_strict).
    // If this triggers, it means you forgot to make your destructor protected.
    // Note: std::is_destructible_v is false if the destructor is protected/private.
    static_assert(!(deletion_policy == DeletionPolicy::Strict && std::is_destructible_v<object_type>),
        "intrusive_ptr<T>: T must have a protected destructor, Strict Policy");


    // Static check to see if class have virtual destructor if base is virtual
    static_assert(!virtual_destructor || std::is_polymorphic_v<object_type>,
        "intrusive_ptr<T>: T must be polymorphic because the base class is virtual");

    object_type* m_object;

protected:
    IC void dec();

public:
    // Allow intrusive_ptrs of different types to access private m_object for conversion
    template <typename U> friend class intrusive_ptr;

    IC intrusive_ptr() noexcept;
    IC intrusive_ptr(object_type* rhs);
    IC intrusive_ptr(self_type const& rhs);

    // Move Constructor
    IC intrusive_ptr(self_type&& rhs) noexcept;

    // Generalized Copy Constructor (Derived -> Base)
    template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int> = 0>
    IC intrusive_ptr(intrusive_ptr<other_type> const& rhs);

    IC ~intrusive_ptr();

    IC self_type& operator=(object_type* rhs);
    IC self_type& operator=(self_type const& rhs);

    // Move Assignment
    IC self_type& operator=(self_type&& rhs) noexcept;

    // Generalized Assignment
    template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int> = 0>
    IC self_type& operator=(intrusive_ptr<other_type> const& rhs);

    // Accessors
    IC object_type& operator*() const;
    IC object_type* operator->() const;

    // Boolean Conversion
    // Replaces the old "unspecified_bool_type" hack with modern standard
    explicit IC operator bool() const noexcept
    {
        return m_object != nullptr;
    }

    // Legacy support for !ptr checks
    IC bool operator!() const noexcept
    {
        return m_object == nullptr;
    }

    // Utilities
    IC u32 size();
    IC void swap(self_type& rhs) noexcept;
    IC bool equal(const self_type& rhs) const noexcept;

    IC void set(object_type* rhs);
    IC void set(self_type const& rhs);
    IC object_type* get() const noexcept;
};

TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator<(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

TEMPLATE_SPECIALIZATION
IC bool operator>(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept;

// Modern nullptr comparisons
TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, std::nullptr_t) noexcept { return !a; }

TEMPLATE_SPECIALIZATION
IC bool operator==(std::nullptr_t, _intrusive_ptr const& a) noexcept { return !a; }

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, std::nullptr_t) noexcept { return (bool)a; }

TEMPLATE_SPECIALIZATION
IC bool operator!=(std::nullptr_t, _intrusive_ptr const& a) noexcept { return (bool)a; }

TEMPLATE_SPECIALIZATION
IC void swap(_intrusive_ptr& lhs, _intrusive_ptr& rhs) noexcept;


// Implementation

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr() noexcept
{
    m_object = nullptr;
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(object_type* rhs)
{
    m_object = nullptr;
    set(rhs);
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(self_type const& rhs)
{
    m_object = nullptr;
    set(rhs);
}

// Move Constructor
TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::intrusive_ptr(self_type&& rhs) noexcept
    : m_object(rhs.m_object)
{
    rhs.m_object = nullptr;
}

// Generalized Constructor (Derived -> Base)
TEMPLATE_SPECIALIZATION
template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int>>
IC _intrusive_ptr::intrusive_ptr(intrusive_ptr<other_type> const& rhs)
{
    m_object = nullptr;
    set(rhs.get());
}

TEMPLATE_SPECIALIZATION
IC _intrusive_ptr::~intrusive_ptr()
{
    dec();
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::dec()
{
    if (m_object)
    {
        object_type* temp = m_object;
        m_object = nullptr;
        temp->intrusive_release(temp);
    }
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(object_type* rhs)
{
    set(rhs);
    return (*this);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(self_type const& rhs)
{
    set(rhs);
    return (*this);
}

// Move Assignment
TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(self_type&& rhs) noexcept
{
    if (this != &rhs)
    {
        dec();
        m_object = rhs.m_object;
        rhs.m_object = nullptr;
    }
    return *this;
}

// Generalized Assignment
TEMPLATE_SPECIALIZATION
template <typename other_type, std::enable_if_t<std::is_convertible_v<other_type*, object_type*>, int>>
IC typename _intrusive_ptr::self_type& _intrusive_ptr::operator=(intrusive_ptr<other_type> const& rhs)
{
    set(rhs.get());
    return *this;
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type& _intrusive_ptr::operator*() const
{
    VERIFY(m_object);
    return (*m_object);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type* _intrusive_ptr::operator->() const
{
    VERIFY(m_object);
    return (m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::swap(self_type& rhs) noexcept
{
    std::swap(m_object, rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC bool _intrusive_ptr::equal(const self_type& rhs) const noexcept
{
    return (m_object == rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(object_type* rhs)
{
    if (rhs)
		rhs->intrusive_ref_add();

    object_type* old = m_object;
    m_object = rhs;

    if (old)
        old->intrusive_release(old);
}

TEMPLATE_SPECIALIZATION
IC void _intrusive_ptr::set(self_type const& rhs)
{
    set(rhs.m_object);
}

TEMPLATE_SPECIALIZATION
IC typename _intrusive_ptr::object_type* _intrusive_ptr::get() const noexcept
{
    return (m_object);
}

TEMPLATE_SPECIALIZATION
IC u32 _intrusive_ptr::size()
{
	return m_object ? m_object->intrusive_ref_count() : 0;
}

// Operator Implementations

TEMPLATE_SPECIALIZATION
IC bool operator==(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() == b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator!=(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() != b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator<(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() < b.get());
}

TEMPLATE_SPECIALIZATION
IC bool operator>(_intrusive_ptr const& a, _intrusive_ptr const& b) noexcept
{
    return (a.get() > b.get());
}

TEMPLATE_SPECIALIZATION
IC void swap(_intrusive_ptr& lhs, _intrusive_ptr& rhs) noexcept
{
    lhs.swap(rhs);
}

template <typename T, typename... Args>
IC intrusive_ptr<T> make_intrusive(Args&&... args)
{
    return intrusive_ptr<T>(xr_new<T>(std::forward<Args>(args)...));
}

#undef TEMPLATE_SPECIALIZATION
#undef _intrusive_ptr
