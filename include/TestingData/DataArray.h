#ifndef DATA_ARRAY_H
#define DATA_ARRAY_H

#include "Data.h"
#include <string>

template <typename T>
using MetadataArray1D = std::tuple<T*, size_t>;

template <typename T>
class DataArray1D : public Data<MetadataArray1D<T>> {
public:
    DataArray1D(const std::string &filename) {
        this->_filename = filename;
    }

    DataArray1D(T* array, size_t size, const char* file_path = "") {
        _data.resize(size);
        for (size_t i = 0; i < size; i++) {
            _data[0] = array[0];
        }
        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".array";    
        } else {
            this->_filename = filename;
        }
        save(false, 0, 0);

        clear();
    }

    DataArray1D(size_t size, T min, T max, const char* file_path = "") {
        _data.resize(size);
        fillRandom(min, max);
        
        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".array";    
        } else {
            this->_filename = filename;
        }
        save(false, 0, 0);

        clear();
    }
    
    DataArray1D(size_t size, NumberFillType type, T start, T step, size_t stepInterval, const char* file_path = "") {
        _data.resize(size);
        if (type == NumberFillType::Ascending) {
            fillAscending(start, step, stepInterval);   
        } else if (type == NumberFillType::Descending) {
            fillDescending(start, step, stepInterval);
        } else {
            throw std::invalid_argument("Invalid fill type");
        }
        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".array";    
        } else {
            this->_filename = filename;
        }
        save(false, 0, 0);

        clear();
    }

    void read() override {
        if (!this->_filename.empty()) {
            load();
        }
    }

    void clear() override {
        _data.clear();
        _data.shrink_to_fit();
    }

    MetadataArray1D<T>& copy() override {
        clear_copy();
        
        T* copy = new T[_data.size()];
        std::copy(_data.begin(), _data.end(), copy);
        this->_copy = std::make_tuple(copy, _data.size());
        return this->_copy;
    }
    
    void clear_copy() override {
        try {
            auto data = std::get<0>(this->_copy);
            if (data) {
                delete[] static_cast<T*>(data);
            }
        } catch (const std::bad_variant_access& e) {
            return;
        }
    }

    void save_copy(int args_id, int thread_num) const override {
        try {
            auto data = std::get<0>(this->_copy); 
            if (data) {
                save(true, args_id, thread_num);
            }
        } catch (const std::bad_variant_access& e) {
            return;
        }
    }

    const std::string title() const override {
        std::string title = "Одномерный массив. Количество элементов=" + std::to_string(_data.size()); 
        return title;
    }

    
private:
    std::vector<T> _data;

    void fillRandom(T min, T max) {
        std::random_device rd;
        std::mt19937 gen(rd());

        if constexpr (std::is_integral_v<T>) {
            std::uniform_int_distribution<T> dis(min, max);
            std::generate(_data.begin(), _data.end(), [&]() { return dis(gen); });
        } else {
            std::uniform_real_distribution<T> dis(min, max);
            std::generate(_data.begin(), _data.end(), [&]() { return dis(gen); });
        }
    }

    void fillAscending(T start, T step, size_t stepInterval) {
        T current = start;
        for (size_t i = 0; i < _data.size(); ++i) {
            _data[i] = current;
            if ((i + 1) % stepInterval == 0) {
                current += step;
            }
        }
    }

    void fillDescending(T start, T step, size_t stepInterval) {
        T current = start;
        for (size_t i = 0; i < _data.size(); ++i) {
            _data[i] = current;
            if ((i + 1) % stepInterval == 0) {
                current -= step;
            }
        }
    }

    void save(bool saveCopy, int args_id, int thread_num) const override {
        const T* data;
        std::string filename;
        if (saveCopy) {
            data = static_cast<T*>(std::get<0>(this->_copy));
            filename += "proc" + this->proc_data_str(args_id, thread_num) + "_" + this->_filename; 
        } else {
            data = _data.data();
            filename = this->_filename;
        }
        std::ofstream file(filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file");

        const uint64_t type_size = sizeof(T);
        const uint64_t size = _data.size();
        file.write(reinterpret_cast<const char*>(&type_size), sizeof(type_size));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(data), size * sizeof(T));
    }

    void load() override {
        std::ifstream file(this->_filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file");

        uint64_t type_size, size;
        file.read(reinterpret_cast<char*>(&type_size),sizeof(type_size));
        file.read(reinterpret_cast<char*>(&size),sizeof(size));
        _data.resize(size);
        file.read(reinterpret_cast<char*>(_data.data()),size * sizeof(T));
    }
};

// template <typename T>
// class DataArrayND : public Data {
// public:
//     DataArrayND(const std::string &filename) {
//         load(filename);
//         _filename = filename;
//     }

//     DataArrayND(size_t size, T min, T max) {
//         initialize();
//         fillRandom();
//         _filename = getCurrentDateTime() + ".array";
//         save();
//     }
    
//     DataArrayND(size_t size, NumberFillType type, T start, T step, size_t stepInterval) {
//         initialize();
//         if (type == NumberFillType::Ascending) {
//             fillAscending(start, step, stepInterval);
//         } else if (type == NumberFillType::Descending) {
//             fillDescending(start, step, stepInterval);
//         } else {
//             throw std::invalid_argument("Invalid fill type");
//         }
//         _filename = getCurrentDateTime() + ".array";
//         save();
//     }

//     void* copy() override {
//         clear_copy();
        
//         T* copy = new T[_data.size()];
//         std::copy(_data.begin(), _data.end(), copy);
//         _copy = static_cast<void*>(copy);
//         return _copy;
//     }
    
//     void clear_copy() override {
//         if (_copy) {
//             delete[] static_cast<T*>(_copy);
//         }
//     }

//     void save_copy() const override {
//         save(true);
//     }

    
// private:
//     std::vector<T> _data;
//     std::vector<size_t> dimensions;
//     std::vector<size_t> strides;

//     void initialize() {
//         size_t total_size = 1;
//         for (size_t dim: dimensions) {
//             if (dim == 0) throw std::invalid_argument("Zero dimension");
//             if (dim > SIZE_MAX / total_size) {
//                 throw std::overflow_error("Total size overflow");
//             }
//             total_size *= dim;
//         }
//         _data.resize(total_size);
//         computeStrides();
//     }

//     void computeStrides() {
//         strides.resize(dimensions.size());
//         if (dimensions.empty()) return;

//         strides.back() = 1;
//         for (int i = dimensions.size() - 2; i >= 0; --i) {
//             if (dimensions[i + 1] > SIZE_MAX / strides[i + 1]) {
//                 throw std::overflow_error("Stride computation overflow");
//             }
//             strides[i] = strides[i + 1] * dimensions[i + 1];
//         }
//     }

//     void fillRandom(T min, T max) {
//         std::random_device rd;
//         std::mt19937 gen(rd());

//         if constexpr (std::is_integral_v<T>) {
//             std::uniform_int_distribution<T> dis(min, max);
//             std::generate(_data.begin(), _data.end(), [&]() { return dis(gen); });
//         } else {
//             std::uniform_real_distribution<T> dis(min, max);
//             std::generate(_data.begin(), _data.end(), [&]() { return dis(gen); });
//         }
//     }

//     void fillAscending(T start, T step, size_t stepInterval) {
//         T current = start;
//         for (size_t i = 0; i < _data.size(); ++i) {
//             _data[i] = current;
//             if ((i + 1) % stepInterval == 0) {
//                 current += step;
//             }
//         }
//     }

//     void fillDescending(T start, T step, size_t stepInterval) {
//         T current = start;
//         for (size_t i = 0; i < _data.size(); ++i) {
//             _data[i] = current;
//             if ((i + 1) % stepInterval == 0) {
//                 current -= step;
//             }
//         }
//     }

//     void save(bool saveCopy) const override {
//         std::ofstream file(_filename, std::ios::binary);
//         if (!file) throw std::runtime_error("Cannot open file");

//         const uint64_t size = _data.size();
//         T* data;
//         if (saveCopy) {
//             data = static_cast<T*>(_copy);
//         } else {
//             data = _data.data();
//         }
//         file.write(reinterpret_cast<const char*>(&size), sizeof(size));
//         file.write(reinterpret_cast<const char*>(data), size * sizeof(T));
//     }

//     void load() override {
//         std::ifstream file(_filename, std::ios::binary);
//         if (!file) throw std::runtime_error("Cannot open file");

//         uint64_t size;
//         file.read(reinterpret_cast<char*>(&size),sizeof(size));
//         _data.resize(size);
//         file.read(reinterpret_cast<char*>(_data.data()),size * sizeof(T));
//     }
// };

#endif