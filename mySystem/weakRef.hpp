#ifndef TRACKDELETE_H
#define TRACKDELETE_H
#include <atomic>
/** @brief Used to keep track of deletion of a lambda-captured object
  * pointer/reference - the instance may get deleted before the lambda is called
  */
template <class T>
class WeakReferenceable
{
protected:
public:
    struct WeakRefSharedData
    {
        T* mPtr;
        std::atomic<int> mRefCount;
        WeakRefSharedData(T* aPtr): mPtr(aPtr), mRefCount(0) {}
    };

    class WeakRefHandle
    {
    protected:
        WeakRefSharedData* mData;
        WeakRefHandle(WeakRefSharedData* data)
        : mData(data)
        {
            mData->mRefCount++;
        }
    public:
        friend class WeakReferenceable;
        WeakRefHandle(): mData(nullptr) {}
        WeakRefHandle(const WeakRefHandle& other): WeakRefHandle(other.mData){}
        ~WeakRefHandle()
        {
            if (--(mData->mRefCount) <= 0) {
                delete mData;
            }
        }
        bool isValid() const { return mData->mPtr != nullptr; }
        T* weakPtr()
        {
            return mData->mPtr;
        }
        const T* weakPtr() const
        {
            return mData->mPtr;
        }
        T* operator->() { return weakPtr(); }
        const T* operator->() const { return weakPtr(); }
#ifdef __EXCEPTIONS__
        void throwIfInvalid() const
        {
            if (!isValid())
                throw std::runtime_error("WeakRefHandle::isValid: Handle is invalid or referenced object has been deleted");
        }
        T& operator*()
        {
            throwIfInvalid();
            return mData->mPtr;
        }
        const T& operator*() const
        {
            throwIfInvalid();
            return mData->mPtr;
        }
#endif
    };
protected:
    WeakRefHandle mWeakRefHandle;
public:
    WeakReferenceable(T* target): mWeakRefHandle(new WeakRefSharedData(target)){}
    ~WeakReferenceable()
    {
        assert(mWeakRefHandle.isValid());
        mWeakRefHandle.mData->mPtr = nullptr;
    }
    WeakRefHandle weakHandle() const { return mWeakRefHandle; }
};

#endif
