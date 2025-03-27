#ifndef TEST_OPTIONS_H
#define TEST_OPTIONS_H

#include <set>
#include <vector>
#include "TestingData/Data.h"
#include "ConfidenceInterval.h"
#include "TestingData/DataArray.h"
#include "TestingData/DataImage.h"
#include "TestingData/DataMatrix.h"
#include "TestingData/DataText.h"

enum class SaveOption {
    saveAll,
    saveArgs,
    notSave
};

class TestOptions {
public:
    TestOptions() 
        : TestOptions({1, 2}, 2, Alpha::percent90, IntervalType::CD, CalcValue::Mean, SaveOption::notSave, false) {}
    
    TestOptions(SaveOption saveOption) 
        : TestOptions({1, 2}, 2, Alpha::percent90, IntervalType::CD, CalcValue::Mean, saveOption, false) {}
    
    TestOptions(SaveOption saveOption, bool generateResultFile) 
        : TestOptions({1, 2}, 2, Alpha::percent90, IntervalType::CD, CalcValue::Mean, saveOption, generateResultFile) {}

    TestOptions(const std::set<unsigned int>& threads) 
        : TestOptions(threads, 2, Alpha::percent90, IntervalType::CD, CalcValue::Mean, SaveOption::notSave, false) {}

    TestOptions(bool generateResultFile)
        : TestOptions({1, 2}, 2, Alpha::percent90, IntervalType::CD, CalcValue::Mean, SaveOption::notSave, generateResultFile) {}

    TestOptions(size_t CI_iterationSize, Alpha CI_alpha, IntervalType CI_intervalType, CalcValue CI_calcValue) 
        : TestOptions({1, 2}, CI_iterationSize, CI_alpha, CI_intervalType, CI_calcValue, SaveOption::notSave, false) {}

    TestOptions(const std::set<unsigned int>& threads, size_t CI_iterationSize, Alpha CI_alpha, IntervalType CI_intervalType, CalcValue CI_calcValue, SaveOption saveOption, bool generateResultFile) 
        : _threads(threads), _interval(CI_iterationSize, CI_alpha, CI_intervalType, CI_calcValue), _saveOption(saveOption), _resultFile(generateResultFile) {}

    const std::set<unsigned int>& GetThreads() const {
        return _threads;
    }

    ConfidenceInterval& GetInterval() {
        return _interval;
    }

    SaveOption GetSaveOption() const {
        return _saveOption;
    }

    bool NeedResultFile() const {
        return _resultFile;
    }

private:
    std::set<unsigned int> _threads;
    ConfidenceInterval _interval;
    SaveOption _saveOption;
    bool _resultFile;
};

template<typename Func, typename... Args>
class FunctionManager {
    public:
    FunctionManager(Func f, Args... args) : _func(std::move(f)) {
        _arguments_list.emplace_back(std::make_tuple(std::move(args)...));
    }
    
    void add_arguments(Args... args) {
        _arguments_list.emplace_back(std::make_tuple(std::move(args)...));
    }
    
    void add_arguments_set(std::initializer_list<std::tuple<Args...>> new_arguments) {
        _arguments_list.insert(_arguments_list.end(), new_arguments.begin(), new_arguments.end());
    }
    
    const Func& Function() const {
        return _func;
    }

    const auto& Arguments() const {
        return _arguments_list;
    }

private:
    Func _func;
    std::vector<std::tuple<Args...>> _arguments_list;
};



template<typename DataType>
struct MetadataTraits;

template<typename T>
struct MetadataTraits<DataArray1D<T>> {
    using MetadataType = MetadataArray1D<T>;
};
template<typename T>
struct MetadataTraits<DataMatrix<T>> {
    using MetadataType = MetadataMatrix<T>;
};
template<>
struct MetadataTraits<DataText> {
    using MetadataType = MetadataText;
};
template<>
struct MetadataTraits<DataImage> {
    using MetadataType = MetadataImage;
};

template <typename T>
class DataManager {
public:
    using MetadataType = typename MetadataTraits<T>::MetadataType;

    DataManager(T&& data) {
        add(std::move(data));
    }

    DataManager(std::initializer_list<T> data) {
        add(data);
    }

    void add(T&& data) {
        _data.push_back(std::make_shared<T>(std::move(data)));
    }

    void add(std::initializer_list<T> data) {
        for (auto&& value: data) {
            _data.push_back(std::make_shared<T>(std::move(value)));
        }
    }

    const auto& DataSet() const {
        return _data;
    }

private:
    std::vector<std::shared_ptr<Data<MetadataType>>> _data;
};

#endif