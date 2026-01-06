#include "data_processor.hpp"
#include <ytrace/ytrace.hpp>
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>

namespace data_processor {

std::vector<int> sort_data(std::vector<int> data) {
    YTRACE("sorting %zu elements", data.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    
    YTRACE("starting bubble sort");
    for (size_t i = 0; i < data.size(); ++i) {
        for (size_t j = 0; j < data.size() - i - 1; ++j) {
            if (data[j] > data[j + 1]) {
                std::swap(data[j], data[j + 1]);
                YTRACE("swapped positions %zu and %zu", j, j + 1);
            }
        }
        YTRACE("completed pass %zu", i + 1);
    }
    
    YTRACE("sort complete");
    return data;
}

std::vector<int> filter_even(const std::vector<int>& data) {
    YTRACE("filtering even numbers from %zu elements", data.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    std::vector<int> result;
    for (int val : data) {
        YTRACE("checking value %d", val);
        if (val % 2 == 0) {
            result.push_back(val);
            YTRACE("kept even value %d", val);
        }
    }
    
    YTRACE("filter complete: %zu even numbers found", result.size());
    return result;
}

int aggregate_sum(const std::vector<int>& data) {
    YTRACE("aggregating sum of %zu elements", data.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    
    int sum = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        sum += data[i];
        YTRACE("running sum after element %zu: %d", i, sum);
    }
    
    YTRACE("aggregate result: sum = %d", sum);
    return sum;
}

std::string transform_to_string(const std::vector<int>& data) {
    YTRACE("transforming %zu elements to string", data.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << data[i];
        YTRACE("added element %zu: %d", i, data[i]);
    }
    oss << "]";
    
    YTRACE("transform complete: %s", oss.str().c_str());
    return oss.str();
}

} // namespace data_processor
