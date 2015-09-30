#ifndef LIBFILEZILLA_APPLY_HEADER
#define LIBFILEZILLA_APPLY_HEADER

#include <utility>
#include <tuple>
#include <type_traits>

// apply takes a function and a tuple as argument
// and calls the function with the tuple's elements as argument

namespace fz {

// Apply tuple to ordinary functor
template<typename F, typename T, size_t... I>
auto apply_(F&& f, T&& t, std::index_sequence<I...> const&) -> decltype(std::forward<F>(f)(std::get<I>(std::forward<T>(t))...))
{
	return std::forward<F>(f)(std::get<I>(std::forward<T>(t))...);
}

template<typename F, typename T, typename Seq = typename std::make_index_sequence<std::tuple_size<typename std::remove_reference<T>::type>::value>>
auto apply(F && f, T&& args) -> decltype(apply_(std::forward<F>(f), std::forward<T>(args), Seq()))
{
	return apply_(std::forward<F>(f), std::forward<T>(args), Seq());
}

// Apply tuple to pointer to member function
template<typename Obj, typename F, typename T, size_t... I>
auto apply_(Obj&& obj, F&& f, T&& t, std::index_sequence<I...> const&) -> decltype((std::forward<Obj>(obj)->*std::forward<F>(f))(std::get<I>(std::forward<T>(t))...))
{
	return (std::forward<Obj>(obj)->*std::forward<F>(f))(std::get<I>(std::forward<T>(t))...);
}

template<typename Obj, typename F, typename T, typename Seq = typename std::make_index_sequence<std::tuple_size<typename std::remove_reference<T>::type>::value>>
auto apply(Obj&& obj, F && f, T&& args) -> decltype(apply_(std::forward<Obj>(obj), std::forward<F>(f), std::forward<T>(args), Seq()))
{
	return apply_(std::forward<Obj>(obj), std::forward<F>(f), std::forward<T>(args), Seq());
}

}

#endif
