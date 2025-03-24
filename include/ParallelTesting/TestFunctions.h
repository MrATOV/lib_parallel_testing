#ifndef TEST_FUNCTIONS_H
#define TEST_FUNCTIONS_H

#include "TestingData/DataArray.h"
#include "TestingData/DataImage.h"
#include "TestingData/DataMatrix.h"
#include "TestingData/DataText.h"
#include "TestOptions.h"
#include "utils.h"
#include "PerformanceEvaluation.h"
#include "TestingData/Data.h"
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
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
        for(const auto& data : data_set) {
            data->read();
            std::cout << data->title() << std::endl;
            json data_json;
            data_json["title"] = data->title();
            data_json["data"] = json::array();
            
            for (int args_id = 0; args_id < function_args.size(); args_id++) {
                const auto& args = function_args[args_id];
                std::string argsString = tupleToString(args);
                std::cout << "Тестовый набор " << argsString << std::endl;
                PerformanceEvaluation pe;
                json performance_result;

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
                    if (saveOption == SaveOption::saveAll) {
                        data->save_copy(args_id + 1, thread);
                        performance_result["processing_image"] = true;
                    }
                }
                auto times = pe.GetTimes();

                for(const auto& [th, tm] : times) {
                    std::cout << th << "->" << tm << "\t" << pe.getAcceleration(th) << "\t" << pe.getEfficiency(th) << "\t" << pe.getCost(th) << std::endl;
                    performance_result["thread"] = th;
                    performance_result["time"] = tm;
                    performance_result["acceleration"] = pe.getAcceleration(th);
                    performance_result["efficiency"] = pe.getEfficiency(th);
                    performance_result["cost"] = pe.getCost(th);
                }
                data_json["data"].push_back({
                    {"args", argsString},
                    {"performance", performance_result}
                });
                if (saveOption == SaveOption::saveArgs) {
                    data->save_copy(args_id + 1);
                    data_json["processing_image"] = true;
                }
            }
            result.push_back(data_json);
            data->clear_copy();
            data->clear();
        }
        if (_options.NeedResultFile()) {
            std::ofstream json_file("result.json");
            json_file << result.dump(4) << std::endl;
            json_file.close();
        }
    }

private:
    TestOptions& _options;
    DataManager<DataType>& _data;
    FunctionManager<Func, Args...> _function;
};


#endif