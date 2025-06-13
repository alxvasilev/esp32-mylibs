#ifndef CSTRINGTUPLE_HPP
#define CSTRINGTUPLE_HPP
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <memory>

template <size_t N, class D=void>
class CStringTuple {
protected:
    typedef CStringTuple Base;
    typedef std::conditional_t<std::is_same_v<D, void>, CStringTuple<N, void>, D> Derived;
    // Protected constructor
    CStringTuple() = default;
    ~CStringTuple() = default;
    struct MallocFreeDeleter {
        void operator()(void* ptr) const {
            static_cast<Derived*>(ptr)->~Derived();
            ::free((void*)ptr);
        }
    };
public:
    const char* mStrings[N];
    typedef std::unique_ptr<Derived, MallocFreeDeleter> unique_ptr;
    CStringTuple(const CStringTuple&) = delete;
    CStringTuple& operator=(const CStringTuple&) = delete;
    // Accessor
    const char* get(size_t i) const
    {
        if (i >= N) throw std::out_of_range("Index out of range");
        return mStrings[i];
    }
    // Number of strings
    size_t size() const { return N; }
    // Create instance with malloc, store object + copied strings in one block
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
    template<typename... Strings>
    static Derived* create(Strings... strs)
    {
        static_assert(sizeof...(Strings) == N, "Expected exactly N strings");
        size_t totalSize = sizeof(Derived);
        size_t lens[N];
        size_t i = 0;
        size_t len;
        (
            (strs ? (
                len = std::strlen(strs) + 1,
                totalSize += len,
                lens[i++] = len
            ) : (
                lens[i++] = 0
            )), ...
        );
        void* mem = std::malloc(totalSize);
        if (!mem) return nullptr;

        // Construct the object in-place
        auto* obj = new (mem) Derived();
        char* dst = (char*)obj + sizeof(Derived);
        i = 0;
        // Copy strings
        (
            (strs ? (
                obj->mStrings[i] = dst,
                len = lens[i],
                std::memcpy(dst, strs, len),
                dst += len,
                i++
            ): (
                obj->mStrings[i] = nullptr,
                i++
            )), ...
        );
        return obj;
    }
#pragma GCC diagnostic pop
};
#endif // CSTRINGTUPLE_HPP
