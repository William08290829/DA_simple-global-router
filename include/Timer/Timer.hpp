#pragma once
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

class Timer {
    using clock = std::chrono::steady_clock;

    struct TimeData
    {
        clock::time_point startTime;
        clock::time_point endTime;
        std::chrono::duration<double> duration{0.0}; // 單位：秒
        bool running = false;
    };

    // std::chrono::seconds timeLimit;
    std::unordered_map<std::string, TimeData> timers;
    std::vector<std::string> timerOrder;
    TimeData totalTimer;
    std::size_t maxTimerNameLength = 0;

public:
    // 設定時限（秒
    // 預設不自動啟動 total 計時
    Timer(){}

    void startTotalTimer() {
        if (!totalTimer.running) {
            totalTimer.startTime = clock::now();
            totalTimer.running = true;
        }
    }

    void stopTotalTimer() {
        if (totalTimer.running) {
            totalTimer.endTime = clock::now();
            totalTimer.duration = totalTimer.endTime - totalTimer.startTime;
            totalTimer.running = false;
        }
    }

    // bool isOverTime() {
    //     if (totalTimer.running) {
    //         auto now = clock::now();
    //         auto total = now - totalTimer.startTime;
    //         return total >= timeLimit;
    //     }
    //     return false;
    // }
    
    double totalElapsedSec() const {
        if (totalTimer.running) {
            return std::chrono::duration<double>(clock::now() - totalTimer.startTime).count();
        }
        return totalTimer.duration.count();
    }
    void printTotal() const {
        std::cout << "[Timer] " << std::fixed << std::setprecision(6)
                  << "Total time: " << totalElapsedSec() << "s\n";
    }   

    // 區段計時器（用 tag 管理）
    void startTimer(const std::string& timerName) {
        auto it = timers.find(timerName);
        if (it == timers.end()) {
            timerOrder.push_back(timerName);
            it = timers.emplace(timerName, TimeData{}).first;
        }

        auto& timedata = it->second;
        timedata.startTime = clock::now();
        timedata.running = true;

        if (timerName.length() > maxTimerNameLength) {
            maxTimerNameLength = timerName.length();
        }
    }

    void stopTimer(const std::string& timerName) {
        auto it = timers.find(timerName);

        if (it == timers.end()) {
            std::cerr << "[Timer Error] Cannot stop timer. Timer \"" << timerName << "\" does not exist.\n";
            return;
        }

        // iter.second 是 pair 裡的 second，也就是 TimeData
        auto& timedata = it->second;
        if (timedata.running) {
            timedata.endTime = clock::now();
            timedata.duration = timedata.endTime - timedata.startTime;
            timedata.running = false;
        } else {
            std::cerr << "[Timer Error] Cannot stop timer. Timer \"" << timerName << "\" is not running.\n";
        }
    }

    // 若想在未 stop 前也能查時間
    double elapsedSec(const std::string& name) const {
        auto it = timers.find(name);
        if (it == timers.end()) return 0.0;
        const auto& timedata = it->second;
        if (timedata.running) {
            return std::chrono::duration<double>(clock::now() - timedata.startTime).count();
        }
        return timedata.duration.count();
    }

    void printTimer(const std::string& timerName) {
        auto it = timers.find(timerName);
        if (it == timers.end()) {
            std::cerr << "[Timer Error] Cannot print timer. Timer \"" << timerName << "\" does not exist.\n";
            return;
        }

        auto& timedata = it->second;
        if (!timedata.running) {
            std::cout << std::left << std::setw(static_cast<int>(maxTimerNameLength) + 2)
                << (timerName + ":")
                << std::fixed << std::setprecision(6)
                << timedata.duration.count() << "s\n";
        } else {
            std::cerr << "[Timer Error] Cannot print timer. Timer \"" << timerName << "\" is still running.\n";
        }
    }

    void printAllTimers() const {
        const std::size_t headerNameLength = std::string("Timer Name").length();
        const std::size_t displayNameLength =
            maxTimerNameLength > headerNameLength ? maxTimerNameLength : headerNameLength;
        const int nameColumnWidth = static_cast<int>(displayNameLength) + 4;

        std::cout << "\n========= Timer Report =========\n";
        std::cout << std::left << std::setw(nameColumnWidth)
                  << "Timer Name"
                  << "Duration (seconds)\n";
        std::cout << std::string(nameColumnWidth + 18, '-') << '\n';

        for (const auto& name : timerOrder) {
            const auto& td = timers.at(name);
            if (!td.running) {
                std::cout << std::left << std::setw(nameColumnWidth)
                          << name << std::fixed << std::setprecision(6)
                          << td.duration.count() << '\n';
            } else {
                std::cout << std::left << std::setw(nameColumnWidth)
                          << name << "[Running]\n";
            }
        }
        if (!totalTimer.running) {
            std::cout << std::left << std::setw(nameColumnWidth)
                      << "Total" << std::fixed << std::setprecision(6)
                      << totalTimer.duration.count() << '\n';
        } else {
            std::cout << std::left << std::setw(nameColumnWidth)
                      << "Total" << "[Running]\n";
        }
        std::cout << "================================\n";
    }

};
