cmake_minimum_required(VERSION 3.16)

project(common_lib LANGUAGES CXX)

add_library(common_lib
    src/database.cpp
)

target_compile_features(common_lib PUBLIC cxx_std_17)

target_include_directories(common_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(nlohmann_json CONFIG REQUIRED)

find_package(pqxx CONFIG QUIET)
if(pqxx_FOUND)
    set(_pqxx_target pqxx::pqxx)
else()
    find_package(libpqxx CONFIG REQUIRED)
    set(_pqxx_target libpqxx::pqxx)
endif()

target_link_libraries(common_lib
    PUBLIC
        ${_pqxx_target}
        nlohmann_json::nlohmann_json
)
