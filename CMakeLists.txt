cmake_minimum_required(VERSION 3.13)

project(ParallelTesting 
    LANGUAGES CXX
    VERSION 1.0
    DESCRIPTION "Parallel Testing Library"    
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET 
    libavcodec 
    libavformat 
    libavutil 
    libswscale
)

add_library(ParallelTesting INTERFACE)

target_sources(ParallelTesting INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/ParallelTesting/ConfidenceInterval.h
        include/ParallelTesting/PerformanceEvaluation.h
        include/ParallelTesting/TestFunctions.h
        include/ParallelTesting/TestOptions.h
        include/ParallelTesting/utils.h
        include/TestingData/Data.h
        include/TestingData/DataArray.h
        include/TestingData/DataMatrix.h
        include/TestingData/DataImage.h
        include/TestingData/DataText.h
)

target_link_libraries(ParallelTesting INTERFACE
    nlohmann_json::nlohmann_json
    OpenMP::OpenMP_CXX
    PkgConfig::FFMPEG
)

target_include_directories(ParallelTesting INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${FFMPEG_INCLUDE_DIRS}
)

install(TARGETS ParallelTesting
    EXPORT ParallelTestingTargets
    FILE_SET HEADERS
    DESTINATION include
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT ParallelTestingTargets
    FILE ParallelTestingConfig.cmake
    NAMESPACE ParallelTesting::
    DESTINATION lib/cmake/ParallelTesting
)