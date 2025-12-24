cmake_minimum_required(VERSION 3.16)

project(websocket_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(SERVICE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(websocket-service
    ${SERVICE_DIR}/src/main.cpp
    ${SERVICE_DIR}/src/websocket_server.cpp
    ${SERVICE_DIR}/src/notification_manager.cpp
    ${SERVICE_DIR}/src/message_queue.cpp
)

target_include_directories(websocket-service PRIVATE
    ${SERVICE_DIR}/include
    ${SERVICE_DIR}/../common/include
)

find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_path(RABBITMQ_INCLUDE_DIR amqp.h)
find_library(RABBITMQ_LIBRARY NAMES rabbitmq librabbitmq)

if(NOT RABBITMQ_INCLUDE_DIR OR NOT RABBITMQ_LIBRARY)
    message(FATAL_ERROR "rabbitmq-c not found (amqp.h / librabbitmq)")
endif()

target_include_directories(websocket-service PRIVATE ${RABBITMQ_INCLUDE_DIR})

target_link_libraries(websocket-service PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ${RABBITMQ_LIBRARY}
    nlohmann_json::nlohmann_json
)
