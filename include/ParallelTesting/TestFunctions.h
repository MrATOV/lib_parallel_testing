#ifndef TEST_FUNCTIONS_H
#define TEST_FUNCTIONS_H

#include "TestingData/DataArray.h"
#include "TestingData/DataImage.h"
#include "TestingData/DataMatrix.h"
#include "TestingData/DataText.h"
#include "TestingData/DataAudio.h"
#include "TestingData/DataVideo.h"
#include "TestOptions.h"
#include "utils.h"
#include "PerformanceEvaluation.h"
#include "TestingData/Data.h"
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <filesystem>
#include <omp.h>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>


template<typename Func, typename DataType,  typename... Args>
class TestFunctions {
public:
    TestFunctions(TestOptions& options, DataManager<DataType>& data, FunctionManager<Func, Args...>& function) : _options(options), _data(data), _function(function) {}

    using json = nlohmann::json;

    void run() {
        double time, time_start, time_end;
        auto& interval = _options.GetInterval();
        const auto &saveOption = _options.GetSaveOption();
        auto call_function = _function.Function();
        auto function_args = _function.Arguments();
        auto data_set = _data.DataSet();
        json result;
        result = json::array();
        
        std::string dirname = getCurrentDateTime();

        try {
            if (!std::filesystem::create_directory(dirname)) {
                std::cerr << "Не удалось создать директорию. Тестирование отменено" << std::endl;
                return;
            } 
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Ошибка " << e.what() << std::endl;
            return;
        }

        for(const auto& data : data_set) {
            data->read();
            std::cout << "==============================================" << std::endl;
            std::cout << "Обработка данных: " << data->title() << std::endl;
            std::cout << "==============================================" << std::endl;
            json data_json;
            data_json["title"] = data->title();
            data_json["type"] = data->type();
            data_json["data"] = json::array();
            
            for (int args_id = 0; args_id < function_args.size(); args_id++) {
                const auto& args = function_args[args_id];
                std::string argsString = tupleToString(args);
                std::cout << "\nТестовый набор параметров: " << argsString << std::endl;
                std::cout << "----------------------------------------------" << std::endl;
                PerformanceEvaluation pe;
                json performance_result = json::array();
    
                for(const auto& thread : _options.GetThreads()) {
                    omp_set_num_threads(thread);
                    for(size_t i = 0; i < interval.getSize(); ++i) {
                        auto full_args = std::tuple_cat(data->copy(), args);
                        time_start = omp_get_wtime();
                        std::apply(call_function, full_args);
                        time_end = omp_get_wtime();
                        
                        interval.setValue(i, time_end - time_start);
                    }
                    time = interval.calculateInterval();
                    pe.addTime(thread, time);
                    
                    json thread_result;
                    if (saveOption == SaveOption::saveAll) {
                        thread_result["processing_data"] = data->save_copy(dirname, args_id + 1, thread);
                    }
    
                    thread_result["thread"] = thread;
                    thread_result["time"] = time;
                    thread_result["acceleration"] = pe.getAcceleration(thread);
                    thread_result["efficiency"] = pe.getEfficiency(thread);
                    thread_result["cost"] = pe.getCost(thread);
                    
                    performance_result.push_back(thread_result);
    
                    std::cout << "Количество потоков: " << std::setw(3) << thread 
                              << " | Время: " << std::fixed << std::setprecision(6) << time << " с"
                              << " | Ускорение: " << std::setw(8) << std::setprecision(3) << pe.getAcceleration(thread)
                              << " | Эффективность: " << std::setw(6) << std::setprecision(3) << pe.getEfficiency(thread)
                              << " | Стоимость: " << std::setw(10) << std::setprecision(3) << pe.getCost(thread) 
                              << std::endl;
                }
    
                data_json["data"].push_back({
                    {"args", argsString},
                    {"performance", performance_result}
                });
                
                if (saveOption == SaveOption::saveArgs) {
                    data_json["processing_data"] = data->save_copy(dirname, args_id + 1);
                }
            }
            result.push_back(data_json);
            data->clear_copy();
            data->clear();
            std::cout << "==============================================\n" << std::endl;
        }
        
        if (_options.NeedResultFile()) {
            std::filesystem::path result_path = std::filesystem::path(dirname) / "result.json";
            std::ofstream json_file(result_path);
            if (json_file.is_open()){
                json_file << result.dump(4) << std::endl;
                json_file.close();
            }
        }
    }

    

private:
    TestOptions& _options;
    DataManager<DataType>& _data;
    FunctionManager<Func, Args...> _function;
};

#endif