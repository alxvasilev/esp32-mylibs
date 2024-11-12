#include <type_traits> // for std::enable_if

template <typename T>
class HasParentheses
{
    typedef char one;
    typedef long two;

    template <typename C> static one test(decltype(&C::operator()));
    template <typename C> static two test(...);
public:
    enum { value = sizeof(test<T>(0)) == sizeof(char) };
};

template<class T, typename dummy=void>
struct FuncTraits {};

template <typename R, typename C, typename... Args>
struct TraitDefs {
    typedef R RetType;
    typedef C Class;
    enum { Nargs = sizeof...(Args) };
};
template<typename R, typename C, typename... Args>
struct FuncTraits<R(C::*)(Args...) const>: public TraitDefs<R, C, Args...>{};

template<typename R, typename C, typename... Args>
struct FuncTraits<R(C::*)(Args...)>: public TraitDefs<R, C, Args...>{};

template<typename R, typename... Args>
struct FuncTraits<R(&)(Args...)>: public TraitDefs<R, void, Args...>{};

template<typename R, typename... Args>
struct FuncTraits<R(*)(Args...)>: public TraitDefs<R, void, Args...>{};

template<typename C>
struct FuncTraits<C, std::enable_if_t<HasParentheses<C>::value, void>>
: public FuncTraits<decltype(&C::operator())>{};

template<class Cb>
using FuncRet_t = typename FuncTraits<Cb>::RetType;
