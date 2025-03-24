#ifndef UTILS_H
#define UTILS_H


#include <string>
#include <sstream>

template <typename T, typename = void>
struct is_convertible_to_string : std::false_type {};

template <typename T>
struct is_convertible_to_string<T, std::void_t<decltype(std::declval<std::ostringstream>() << std::declval<T>())>> : std::true_type {};

template <typename T>
std::string toString(const T& value) {
    if constexpr (is_convertible_to_string<T>::value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    } else {
        return "[Некоторый параметр]";
    }
}

template <typename Tuple, std::size_t... Is>
std::string tupleToStringImpl(const Tuple& t, std::index_sequence<Is...>) {
    std::string result;
    ((result += (Is == 0 ? "" : ", ") + toString(std::get<Is>(t))), ...);
    return result;
}

template <typename... Args>
std::string tupleToString(const std::tuple<Args...>& t) {
    return tupleToStringImpl(t, std::index_sequence_for<Args...>{});
}

#endif