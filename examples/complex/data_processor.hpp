#pragma once

#include <vector>
#include <string>

namespace data_processor {

std::vector<int> sort_data(std::vector<int> data);
std::vector<int> filter_even(const std::vector<int>& data);
int aggregate_sum(const std::vector<int>& data);
std::string transform_to_string(const std::vector<int>& data);

} // namespace data_processor
