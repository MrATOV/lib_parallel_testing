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
                        thread_result["processing_image"] = data->save_copy(dirname, args_id + 1, thread);
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
                    data_json["processing_image"] = data->save_copy(dirname, args_id + 1);
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


// template<typename Func, typename... Args>
// class TestFunctions<Func, DataVideo, Args...> {
// public:
//     TestFunctions(TestOptions& options, DataManager<DataVideo>& data, FunctionManager<Func, Args...>& function) 
//         : _options(options), _data(data), _function(function) {}

//     using json = nlohmann::json;

//     void run() {
//         double time, time_start, time_end;
//         auto& interval = _options.GetInterval();
//         const auto &saveOption = _options.GetSaveOption();
//         auto call_function = _function.Function();
//         auto function_args = _function.Arguments();
//         auto data_set = _data.DataSet();
//         json result;
//         result = json::array();
        
//         std::string dirname = getCurrentDateTime();

//         try {
//             if (!std::filesystem::create_directory(dirname)) {
//                 std::cerr << "Не удалось создать директорию. Тестирование отменено" << std::endl;
//                 return;
//             } 
//         } catch (const std::filesystem::filesystem_error& e) {
//             std::cerr << "Ошибка " << e.what() << std::endl;
//             return;
//         }

//         for(const auto& data : data_set) {
//             data->read();
//             std::cout << "==============================================" << std::endl;
//             std::cout << "Обработка видео: " << data->title() << std::endl;
//             std::cout << "==============================================" << std::endl;
            
//             json data_json;
//             data_json["title"] = data->title();
//             // data_json["width"] = data->width();
//             // data_json["height"] = data->height();
//             // data_json["frames"] = data->frameCount();
//             // data_json["audio_samples"] = data->audioSampleCount();
//             data_json["data"] = json::array();
            
//             auto video_metadata = data->copy();
//             auto frame_loader = std::get<0>(video_metadata);
//             auto frame_pts = std::get<1>(video_metadata);
//             size_t frame_count = std::get<2>(video_metadata);
//             size_t width = std::get<3>(video_metadata);
//             size_t height = std::get<4>(video_metadata);
//             auto audio_loader = std::get<5>(video_metadata);
            
//             // Оптимальный размер блока кадров для обработки
//             const size_t chunk_size = std::min<size_t>(100, frame_count/omp_get_max_threads() + 1);

//             for (int args_id = 0; args_id < function_args.size(); args_id++) {
//                 const auto& args = function_args[args_id];
//                 std::string argsString = tupleToString(args);
//                 std::cout << "\nТестовый набор параметров: " << argsString << std::endl;
//                 std::cout << "----------------------------------------------" << std::endl;
                
//                 PerformanceEvaluation pe;
//                 json performance_result = json::array();

//                 for(const auto& thread : _options.GetThreads()) {
//                     omp_set_num_threads(thread);
                    
//                     for(size_t i = 0; i < interval.getSize(); ++i) {
//                         time_start = omp_get_wtime();
                        
//                         // Обработка видео по блокам кадров
//                         #pragma omp parallel for schedule(dynamic)
//                         for (size_t chunk_start = 0; chunk_start < frame_count; chunk_start += chunk_size) {
//                             size_t chunk_end = std::min(chunk_start + chunk_size, frame_count);
                            
//                             for (size_t frame_idx = chunk_start; frame_idx < chunk_end; frame_idx++) {
//                                 // Загрузка текущего кадра
//                                 RGBImage** frame_data = frame_loader(frame_idx);
//                                 if (!frame_data) continue;
                                
//                                 // Подготовка аргументов для функции обработки
//                                 auto full_args = std::tuple_cat(
//                                     std::make_tuple(
//                                         frame_data, 
//                                         frame_pts + frame_idx,
//                                         width,
//                                         height
//                                     ), 
//                                     args
//                                 );
                                
//                                 // Обработка кадра
//                                 std::apply(call_function, full_args);
                                
//                                 // Освобождение памяти кадра
//                                 for (size_t y = 0; y < height; ++y) {
//                                     delete[] frame_data[y];
//                                 }
//                                 delete[] frame_data;
//                             }
//                         }
                        
//                         time_end = omp_get_wtime();
//                         interval.setValue(i, time_end - time_start);
//                     }
                    
//                     time = interval.calculateInterval();
//                     pe.addTime(thread, time);
                    
//                     json thread_result;
//                     if (saveOption == SaveOption::saveAll) {
//                         thread_result["processing_video"] = data->save_copy(dirname, args_id + 1, thread);
//                     }
    
//                     thread_result["thread"] = thread;
//                     thread_result["time"] = time;
//                     thread_result["acceleration"] = pe.getAcceleration(thread);
//                     thread_result["efficiency"] = pe.getEfficiency(thread);
//                     thread_result["cost"] = pe.getCost(thread);
//                     thread_result["chunk_size"] = chunk_size;
                    
//                     performance_result.push_back(thread_result);
    
//                     std::cout << "Потоки: " << std::setw(2) << thread 
//                               << " | Время: " << std::fixed << std::setprecision(4) << time << " с"
//                               << " | Ускорение: " << std::setw(6) << std::setprecision(3) << pe.getAcceleration(thread)
//                               << " | Эффективность: " << std::setw(5) << std::setprecision(3) << pe.getEfficiency(thread)
//                               << " | Блок: " << chunk_size << " кадров"
//                               << std::endl;
//                 }
    
//                 data_json["data"].push_back({
//                     {"args", argsString},
//                     {"performance", performance_result}
//                 });
                
//                 if (saveOption == SaveOption::saveArgs) {
//                     data_json["processing_video"] = data->save_copy(dirname, args_id + 1);
//                 }
//             }
            
//             result.push_back(data_json);
//             data->clear_copy();
//             data->clear();
//             std::cout << "==============================================\n" << std::endl;
//         }
        
//         if (_options.NeedResultFile()) {
//             std::filesystem::path result_path = std::filesystem::path(dirname) / "result.json";
//             std::ofstream json_file(result_path);
//             if (json_file.is_open()){
//                 json_file << result.dump(4) << std::endl;
//                 json_file.close();
//             }
//         }
//     }

// private:
//     TestOptions& _options;
//     DataManager<DataVideo>& _data;
//     FunctionManager<Func, Args...>& _function;
// };


#endif