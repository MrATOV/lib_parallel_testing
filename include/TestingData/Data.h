#ifndef DATA_H
#define DATA_H

#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <stdexcept>

enum class NumberFillType {
    Ascending,
    Descending
};

enum class TextFillType {
    Text,
    File
};

template <typename Metadata>
class Data {
public:
    virtual void read() = 0;
    virtual void clear() = 0;
    virtual Metadata& copy() = 0;
    virtual void clear_copy() = 0;
    virtual void save_copy(int args_id, int thread_num = 0) const = 0;
    virtual const std::string title() const = 0;
    virtual ~Data() = default;
protected:
    std::string _filename;
    Metadata _copy;

    virtual void save(bool saveCopy = false, int args_id = 0, int thread_num = 0) const = 0;
    virtual void load() = 0;

    std::string getCurrentDateTime() const {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()) % 1000000000;
    
        std::tm *parts = std::localtime(&now_c);
        std::ostringstream oss;
        oss << std::put_time(parts, "%Y_%m_%d_%H_%M_%S") << "_" << nanoseconds.count();
        return oss.str();
    }

    std::string proc_data_str(int args_id, int thread_num) const {
        return (args_id != 0 ? "_args_" + std::to_string(args_id) : "") + (thread_num != 0 ? "_thread_" + std::to_string(thread_num) : "");
    }
};

#endif