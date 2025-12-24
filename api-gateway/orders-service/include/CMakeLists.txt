cmake_minimum_required(VERSION 3.16)

project(orders_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SERVICE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(orders-service
    ${SERVICE_DIR}/src/main.cpp
    ${SERVICE_DIR}/src/order_service.cpp
    ${SERVICE_DIR}/src/outbox_processor.cpp
    ${SERVICE_DIR}/src/message_gueue.cpp
    ${SERVICE_DIR}/src/database.cpp
)

target_include_directories(orders-service PRIVATE
    ${SERVICE_DIR}/include
    ${SERVICE_DIR}/../common/include
)

find_package(nlohmann_json CONFIG REQUIRED)

find_package(pqxx CONFIG QUIET)
if(pqxx_FOUND)
    set(_pqxx_target pqxx::pqxx)
else()
    find_package(libpqxx CONFIG REQUIRED)
    set(_pqxx_target libpqxx::pqxx)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_path(RABBITMQ_INCLUDE_DIR amqp.h)
find_library(RABBITMQ_LIBRARY NAMES rabbitmq librabbitmq)

if(NOT RABBITMQ_INCLUDE_DIR OR NOT RABBITMQ_LIBRARY)
    message(FATAL_ERROR "rabbitmq-c not found (amqp.h / librabbitmq)")
endif()

target_include_directories(orders-service PRIVATE ${RABBITMQ_INCLUDE_DIR})

target_link_libraries(orders-service PRIVATE
    ${_pqxx_target}
    nlohmann_json::nlohmann_json
    ${RABBITMQ_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
