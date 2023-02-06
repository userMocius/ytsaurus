#ifndef HAZARD_PTR_INL_H_
#error "Direct inclusion of this file is not allowed, include hazard_ptr.h"
// For the sake of sane code completion.
#include "hazard_ptr.h"
#endif
#undef HAZARD_PTR_INL_H_

#include <array>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TRefCountedBase;

namespace NDetail {

constexpr int MaxHazardPointersPerThread = 2;
using THazardPointerSet = std::array<std::atomic<void*>, MaxHazardPointersPerThread>;

extern thread_local THazardPointerSet HazardPointers;

struct THazardThreadState;
extern thread_local THazardThreadState* HazardThreadState;

void InitHazardThreadState();

template <class T, bool = std::is_base_of_v<TRefCountedBase, T>>
struct THazardPtrTraits
{
    Y_FORCE_INLINE static void* GetBasePtr(T* object)
    {
        return object;
    }
};

template <class T>
struct THazardPtrTraits<T, true>
{
    Y_FORCE_INLINE static void* GetBasePtr(TRefCountedBase* object)
    {
        return object;
    }
};

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

template <class T>
THazardPtr<T>::THazardPtr(THazardPtr&& other)
    : Ptr_(other.Ptr_)
    , HazardPtr_(other.HazardPtr_)
{
    other.Ptr_ = nullptr;
    other.HazardPtr_ = nullptr;
}

template <class T>
THazardPtr<T>& THazardPtr<T>::operator=(THazardPtr&& other)
{
    if (this != &other) {
        Reset();
        Ptr_ = other.Ptr_;
        HazardPtr_ = other.HazardPtr_;
        other.Ptr_ = nullptr;
        other.HazardPtr_ = nullptr;
    }
    return *this;
}

template <class T>
template <class TPtrLoader>
THazardPtr<T> THazardPtr<T>::Acquire(TPtrLoader&& ptrLoader, T* ptr)
{
    if (!ptr) {
        return {};
    }

    auto* hazardPtr = [] {
        for (auto it = NYT::NDetail::HazardPointers.begin(); it !=  NYT::NDetail::HazardPointers.end(); ++it) {
            auto& ptr = *it;
            if (!ptr.load(std::memory_order::relaxed)) {
                return &ptr;
            }
        }
        // Too many hazard pointers are being used in a single thread concurrently.
        // Try increasing MaxHazardPointersPerThread.
        YT_ABORT();
    }();

    if (Y_UNLIKELY(!NYT::NDetail::HazardThreadState)) {
        NYT::NDetail::InitHazardThreadState();
    }

    void* checkPtr;
    do {
        hazardPtr->store(NYT::NDetail::THazardPtrTraits<T>::GetBasePtr(ptr), std::memory_order::release);
        std::atomic_thread_fence(std::memory_order::seq_cst);
        checkPtr = ptr;
        ptr = ptrLoader();
    } while (ptr != checkPtr);

    return THazardPtr(ptr, hazardPtr);
}

template <class T>
template <class TPtrLoader>
THazardPtr<T> THazardPtr<T>::Acquire(TPtrLoader&& ptrLoader)
{
    return Acquire(std::forward<TPtrLoader>(ptrLoader), ptrLoader());
}

template <class T>
void THazardPtr<T>::Reset()
{
    if (Ptr_) {
#ifdef NDEBUG
        HazardPtr_->store(nullptr, std::memory_order::release);
#else
        YT_VERIFY(HazardPtr_->exchange(nullptr) == NYT::NDetail::THazardPtrTraits<T>::GetBasePtr(Ptr_));
#endif
        Ptr_ = nullptr;
        HazardPtr_ = nullptr;
    }
}

template <class T>
THazardPtr<T>::~THazardPtr()
{
    Reset();
}

template <class T>
T* THazardPtr<T>::Get() const
{
    return Ptr_;
}

template <class T>
T& THazardPtr<T>::operator*() const
{
    YT_ASSERT(Ptr_);
    return *Ptr_;
}

template <class T>
T* THazardPtr<T>::operator->() const
{
    YT_ASSERT(Ptr_);
    return Ptr_;
}

template <class T>
THazardPtr<T>::operator bool() const
{
    return Ptr_ != nullptr;
}

template <class T>
THazardPtr<T>::THazardPtr(T* ptr, std::atomic<void*>* hazardPtr)
    : Ptr_(ptr)
    , HazardPtr_(hazardPtr)
{ }

////////////////////////////////////////////////////////////////////////////////

template <class U>
bool operator==(const THazardPtr<U>& lhs, const U* rhs)
{
    return lhs.Get() == rhs;
}

template <class U>
bool operator!=(const THazardPtr<U>& lhs, const U* rhs)
{
    return lhs.Get() != rhs;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
