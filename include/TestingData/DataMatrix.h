#ifndef DATA_MATRIX_H
#define DATA_MATRIX_H

#include "Data.h"
#include <tuple>
#include <variant>

template <typename T>
using MetadataMatrix = std::tuple<T**, size_t, size_t>;

template <typename T>
class DataMatrix : public Data<MetadataMatrix<T>> {
public:
    DataMatrix(const std::string &filename) {
        this->_filename = filename;
    }

    DataMatrix(T** mat, size_t rows, size_t cols, const char* file_path = "") {
        _data.resize(rows);
        for(size_t i = 0; i < rows; i++) {
            _data[i].resize(cols);
            for(size_t j = 0; j < cols; j++) {
                _data[i][j] = mat[i][j];
            }
        }

        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".matrix";    
        } else {
            this->_filename = filename;
        }
        save(false, 0, 0);
        clear();
    }

    DataMatrix(size_t rows, size_t cols, T min, T max, const char* file_path = "") {
        _data.resize(rows);
        for (auto& row : _data) {
            row.resize(cols); 
        }
        fillRandom(min, max);
        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".matrix";    
        } else {
            this->_filename = filename;
        }
        save(false, 0, 0);
        clear();
    }
    
    DataMatrix(size_t rows, size_t cols, NumberFillType type, T start, T step, size_t stepInterval, const char* file_path = "") {
        _data.resize(rows);
        for (auto& row : _data) {
            row.resize(cols);
        }
        if (type == NumberFillType::Ascending) {
            fillAscending(start, step, stepInterval);
        } else if (type == NumberFillType::Descending) {
            fillDescending(start, step, stepInterval);
        } 
        else {
            throw std::invalid_argument("Invalid fill type");
        }
        std::string filename = std::string(file_path);
        if (filename.empty()) {
            this->_filename = this->getCurrentDateTime() + ".matrix";    
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
        for(auto & row: _data) {
            row.clear();
            row.shrink_to_fit();
        }
        _data.clear();
        _data.shrink_to_fit();
    }

    MetadataMatrix<T>& copy() override {
        clear_copy();

        T** copy = new T*[_data.size()];
        for (size_t i = 0; i < _data.size(); ++i) {
            copy[i] = new T[_data[i].size()];
            std::copy(_data[i].begin(), _data[i].end(), copy[i]);
        }

        this->_copy = std::make_tuple(copy, _data.size(), _data.back().size());
        return this->_copy;
    }

    void clear_copy() override {
        try {
            auto data = std::get<0>(this->_copy);
            if (data) {
                T** arr = reinterpret_cast<T**>(data);
                for (size_t i = 0; i < _data.size(); ++i) {
                    delete[] arr[i];
                }
                delete[] arr;
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
        std::string title = "Матрица. Размер=" + std::to_string(_data.size()) + " на " + std::to_string(_data.back().size()); 
        return title;
    }

private:
    std::vector<std::vector<T>> _data;

    void fillRandom(T min, T max) {
        std::random_device rd;
        std::mt19937 gen(rd());

        if constexpr (std::is_integral_v<T>) {
            std::uniform_int_distribution<T> dis(min, max);
            for(auto & row : _data) {
                std::generate(row.begin(), row.end(), [&]() { return dis(gen); });
            }
        } else {
            std::uniform_real_distribution<T> dis(min, max);
            for(auto & row : _data) {
                std::generate(row.begin(), row.end(), [&]() { return dis(gen); });
            }
        }
    }

    void fillAscending(T start, T step, size_t stepInterval) {
        T current = start;
        for(auto& row : _data) {
            for (size_t i = 0; i < row.size(); ++i) {
                row[i] = current;
                if ((i + 1) % stepInterval == 0) {
                    current += step;
                }
            }
        }
    }

    void fillDescending(T start, T step, size_t stepInterval) {
        T current = start;
        for (auto& row : _data) {
            for (size_t i = 0; i < row.size(); ++i) {
                row[i] = current;
                if ((i + 1) % stepInterval == 0) {
                    current -= step;
                }
            }
        }
    }

    void save(bool saveCopy, int args_id, int thread_num) const override {
        std::string filename;
        if (saveCopy) {
            filename += "proc" + this->proc_data_str(args_id, thread_num) + "_" + this->_filename;
        } else {
            filename = this->_filename;
        }
        std::ofstream file(filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file");

        const uint64_t type_size = sizeof(T);
        const uint64_t rows = _data.size(), cols = _data.back().size();
        file.write(reinterpret_cast<const char*>(&type_size), sizeof(type_size));
        file.write(reinterpret_cast<const char*>(&rows), sizeof(rows));
        file.write(reinterpret_cast<const char*>(&cols), sizeof(cols));
        if (saveCopy) {
            T** data = static_cast<T**>(std::get<0>(this->_copy));
            for(int i = 0; i < _data.size(); i++) {
                file.write(reinterpret_cast<const char*>(data[i]), cols * sizeof(T));
            }
        } else {
            for(const auto& row : _data) {
                file.write(reinterpret_cast<const char*>(row.data()), cols * sizeof(T));
            }
        }
        
    }
    void load() override {
        std::ifstream file(this->_filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file");

        uint64_t type_size, rows, cols;
        file.read(reinterpret_cast<char*>(&type_size), sizeof(type_size));
        file.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        file.read(reinterpret_cast<char*>(&cols), sizeof(cols));
        _data.resize(rows);
        for (auto& row : _data) {
            row.resize(cols);
            file.read(reinterpret_cast<char*>(row.data()), cols * sizeof(T));
        }
    }
};

#endif