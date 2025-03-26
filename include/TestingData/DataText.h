#ifndef DATA_TEXT_H
#define DATA_TEXT_H

#include "Data.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>

using MetadataText = std::tuple<char*,  size_t>;

class DataText : public Data<MetadataText> {
public:
    DataText(const std::string &filename) {
        _filename = filename;
    }

    DataText(const std::string& data, const char* file_path) {
        _data = data;
        std::string filename(file_path);
        if (filename.empty()) {
            _filename = getCurrentDateTime() + ".txt";
        } else {
            _filename = filename;
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

    MetadataText& copy() override {
        clear_copy();

        char* copy = new char[_data.size() + 1];
        std::copy(_data.begin(), _data.end(), copy);
        copy[_data.size()] = '\0';

        _copy = std::make_tuple(copy, _data.length());
        return _copy;
    }

    void clear_copy() override {
        try {
            auto data = std::get<0>(_copy);
            if (data) {
                delete[] data;
            }
        } catch (const std::bad_variant_access& e) {
            return;
        }
    }

    void save_copy(int args_id, int thread_num) const override {
        try {
            auto data = std::get<0>(_copy);
            if (data) {
                save(true, args_id, thread_num);
            } else {
                throw std::runtime_error("Copy data not found");
            }
        } catch (const std::bad_variant_access& e) {
            return;
        }
    }

    const std::string title() const override {
        std::string title = "Строка. Количество символов=" + std::to_string(_data.length()); 
        return title;
    }
    
private:
    std::string _data;
    
    void save(bool saveCopy, int args_id, int thread_num) const override {
        std::string data;
        std::string filename;
        if (saveCopy) {
            data = std::string(std::get<0>(_copy));
            filename = "proc" + proc_data_str(args_id, thread_num) + " " + _filename;
        } else {
            data = _data;
            filename = _filename;
        }
        
        std::ofstream file(filename);
        if (file.is_open()) {
            file << data;
        }
        else throw std::runtime_error("Cannot open file");
        file.close();
    }

    void load() override {
        std::ifstream file(_filename);
        if (file.is_open()) {
            std::string newStr;
            while (std::getline(file, newStr)) {
                _data += newStr + '\n';
            }
        }
        else throw std::runtime_error("Cannot open file");
        file.close();
    }
};

#endif