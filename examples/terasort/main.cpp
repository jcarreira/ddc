#include "config.hpp"
#include "record.hpp"
#include "hash_input.hpp"
#include "hash_output.hpp"
#include "hash_lambda.hpp"
#include "sort_input.hpp"
#include "sort_lambda.hpp"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <thread>
#include <future>
#include <deque>
#include <stdexcept>
#include <algorithm>
#include <chrono>

#define HASH_BYTES 1
#define HASH_MODULUS 5
#define HASH_NODES 16
#define INPUT_CHUNKS 10
#define SORT_NODES 16

int main() {
	auto start_time = std::chrono::high_resolution_clock::now();
	std::cout << "Configuring..." << std::endl;
	cirrus_terasort::config conf(HASH_NODES, HASH_BYTES, HASH_MODULUS,
		cirrus_terasort::config::hash_mode::HASH_STREAMING,
		INPUT_CHUNKS, "hash_outputs", SORT_NODES, "sort_outputs");
	std::cout << "Starting Hashing..." << std::endl;
	auto hash_start = std::chrono::high_resolution_clock::now();
	std::shared_ptr<cirrus_terasort::config> u_conf = std::make_shared<cirrus_terasort::config>(conf);
	std::shared_ptr<cirrus_terasort::hash_input> input = std::make_shared<cirrus_terasort::hash_input>(u_conf, "terasort_data.txt");
	std::shared_ptr<cirrus_terasort::hash_output> output = std::make_shared<cirrus_terasort::hash_output>(u_conf);
	std::thread all[u_conf->hash_nodes()];
	for(uint32_t i = 0; i < u_conf->hash_nodes(); i++)
		all[i] = std::thread(cirrus_terasort::hash_lambda, input, output);
	for(uint32_t i = 0; i < u_conf->hash_nodes(); i++)
		all[i].join();
	auto flushing_start = std::chrono::high_resolution_clock::now();
	output.reset();
	auto flushing_end = std::chrono::high_resolution_clock::now();
	std::cout << "Flushing time: " << std::chrono::duration_cast<std::chrono::seconds>(flushing_end - flushing_start).count() << " seconds" << std::endl;
	auto hash_end = std::chrono::high_resolution_clock::now();
	std::cout << "Finished Hashing..." << std::endl;
	std::cout << "Hashing time: " << std::chrono::duration_cast<std::chrono::seconds>(hash_end - hash_start).count() << " seconds" << std::endl;
	std::cout << "Starting Sorting..." << std::endl;
	auto sort_start = std::chrono::high_resolution_clock::now();
	std::shared_ptr<cirrus_terasort::sort_input> si = std::make_shared<cirrus_terasort::sort_input>(u_conf);
	std::deque<std::future<std::vector<std::shared_ptr<cirrus_terasort::record>>>> sorted_futures;
	std::deque<std::vector<std::shared_ptr<cirrus_terasort::record>>> sorted;
	std::thread all2[u_conf->sort_nodes()];
	for(uint32_t i = 0; i < u_conf->sort_nodes(); i++) {
		std::promise<std::vector<std::shared_ptr<cirrus_terasort::record>>> p;
		sorted_futures.push_back(p.get_future());
		all2[i] = std::thread(cirrus_terasort::sort_lambda, si, std::move(p));
	}
	for(uint32_t i = 0; i < u_conf->sort_nodes(); i++) {
		all2[i].join();
		sorted.push_back(sorted_futures.front().get());
		sorted_futures.pop_front();
	}
	auto sort_end = std::chrono::high_resolution_clock::now();
	std::cout << "Finished Sorting..." << std::endl;
	std::cout << "Sorting time: " << std::chrono::duration_cast<std::chrono::seconds>(sort_end - sort_start).count() << " seconds" << std::endl;
	std::cout << "Starting Merging..." << std::endl;
	auto merge_start = std::chrono::high_resolution_clock::now();
	std::deque<std::thread> all3;
	std::deque<std::future<std::vector<std::shared_ptr<cirrus_terasort::record>>>> sorted_merge_futures;
	while(sorted.size() >= 2) {
		uint32_t k = 0;
		for(uint32_t i = 0; i / 2 < u_conf->sort_nodes() && i + 1 < sorted.size(); i += 2, k++) {
			std::vector<std::shared_ptr<cirrus_terasort::record>>& vec1 = sorted.at(i);
			std::vector<std::shared_ptr<cirrus_terasort::record>>& vec2 = sorted.at(i + 1);
			std::promise<std::vector<std::shared_ptr<cirrus_terasort::record>>> p;
			sorted_merge_futures.push_back(p.get_future());
			all3.push_back(std::thread(cirrus_terasort::merge_lambda, std::ref(vec1), std::ref(vec2), std::move(p)));
		}
		for(uint32_t j = 0; j < k; j++) {
			all3.front().join();
			sorted.pop_front();
			sorted.pop_front();
			sorted.push_back(sorted_merge_futures.front().get());
			sorted_merge_futures.pop_front();
			all3.pop_front();
		}
	}
	if(sorted.size() != 1)
		throw std::runtime_error("expected a sorted output.");
	std::vector<std::shared_ptr<cirrus_terasort::record>>& to_write = sorted.front();
	auto merge_end = std::chrono::high_resolution_clock::now();
	std::cout << "Finished Merging..." << std::endl;
	std::cout << "Merging time: " << std::chrono::duration_cast<std::chrono::seconds>(merge_end - merge_start).count() << " seconds" << std::endl;
	std::cout << "Starting Verification..." << std::endl;
	if(std::is_sorted(to_write.begin(), to_write.end(), [](const std::shared_ptr<cirrus_terasort::record>& lhs, const std::shared_ptr<cirrus_terasort::record>& rhs) {
		return lhs->raw_data() < rhs->raw_data();
	}))
		std::cout << "Is sorted" << std::endl;
	else
		std::cout << "Is not sorted" << std::endl;
	auto end_time = std::chrono::high_resolution_clock::now();
	std::cout << "Total time: " << std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count() << " seconds" << std::endl;
	std::cout << "Writing to file..." << std::endl;
	std::ofstream of(u_conf->sort_output_dir() + "/" + "terasorted.txt");
	std::for_each(to_write.begin(), to_write.end(), [&of](const std::shared_ptr<cirrus_terasort::record>& lhs) {
		of << lhs->raw_data() << std::endl;
	});
}