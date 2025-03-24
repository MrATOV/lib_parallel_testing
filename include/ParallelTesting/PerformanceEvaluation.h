#ifndef PERFORMANCE_EVALUATION_H
#define PERFORMANCE_EVALUATION_H

#include <map>

using ThreadTime = std::pair<int, double>;

class PerformanceEvaluation {
public:
    void addTime(int thread_num, double time) {
        times[thread_num] = time;
    }

    double getAcceleration(size_t thread) {
        auto linearTime = times.find(1);
        if (linearTime != times.end()) {
            return linearTime->second / times[thread];
        }
        return -1.0;
    }
    
    double getEfficiency(size_t thread) {
        auto linearTime = times.find(1);
        if (linearTime != times.end()) {
            return linearTime->second / (thread * times[thread]);
        }
        return -1.0;
    }
    
    double getCost(size_t thread) {
        return thread * times[thread];
    }

    const auto& GetTimes() const {
        return times;
    }

private:
    std::map<int, double> times;
};

#endif