#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <random>
#include <ctime>

namespace utils {

inline std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) ss << "-";
        if (i == 12) ss << 4;
        else if (i == 16) ss << dis2(gen);
        else ss << dis(gen);
    }
    return ss.str();
}

inline std::string time_to_string(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

inline std::chrono::system_clock::time_point string_to_time(const std::string& str) {
    std::tm tm = {};
    std::stringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

}

#endif
