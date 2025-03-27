#ifndef UTILS_H
#define UTILS_H


#include <iomanip>
#include <string>
#include <sstream>
#include <chrono>

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

inline std::string getCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()) % 1000000000;

    std::tm *parts = std::localtime(&now_c);
    std::ostringstream oss;
    oss << std::put_time(parts, "%Y_%m_%d_%H_%M_%S") << "_" << nanoseconds.count();
    return oss.str();
}

#endif