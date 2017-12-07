#include "serialization.hpp"
#include "config_instance.hpp"
#include "read_lambda.hpp"
#include "hash_lambda.hpp"
#include "sort_lambda.hpp"

#include "object_store/FullBladeObjectStore.h"
#include "tests/object_store/object_store_internal.h"

#include <stdexcept>
#include <memory>
#include <atomic>
#include <mpi.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <experimental/filesystem>
#include <deque>

using cirrus_terasort::INT_TYPE;
using namespace cirrus_terasort::config_instance;

void init_mpi(int argc, char* argv[]) {
	int status;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &status);
}

int main(int argc, char* argv[]) {

	cirrus::TCPClient tcp_client;
	cirrus_terasort::string_serializer str_ser;
	std::shared_ptr<cirrus::ostore::FullBladeObjectStoreTempl<std::string>>
		store = std::make_shared<cirrus::ostore::FullBladeObjectStoreTempl<std::string>>(
			cirrus_ip, cirrus_port, &tcp_client, str_ser, cirrus_terasort::string_deserializer);

	int proc_rank, num_procs, sync;

	init_mpi(argc, argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &proc_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

	if (proc_rank < num_input_files) { // read nodes

		std::cout << "Starting read..." << std::endl;

		std::shared_ptr<cirrus_terasort::read_lambda> rl =
			std::make_shared<cirrus_terasort::read_lambda>(proc_rank);
		std::vector<std::thread> all;

		for (INT_TYPE i = 0; i < read_threads; i++)
			all.push_back(std::thread(cirrus_terasort::reader, rl, store));
		for (INT_TYPE i = 0; i < read_threads; i++)
			all[i].join();

		for (INT_TYPE i = num_input_files; i < num_input_files + hash_nodes; i++)
			MPI_Send(&sync, 1, MPI_INT, i, proc_rank, MPI_COMM_WORLD);
	}
	else if (proc_rank < num_input_files + hash_nodes) { // hash nodes

		for (INT_TYPE i = 0; i < num_input_files; i++)
			MPI_Recv(&sync, 1, MPI_INT, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		std::cout << "Starting hashing..." << std::endl;

		INT_TYPE total_keys = num_records / read_chunk_size,
			keys_per_process = total_keys / hash_nodes,
			process_index = proc_rank - num_input_files;
		INT_TYPE start = process_index * keys_per_process,
			end = (process_index + 1) * keys_per_process;

		std::shared_ptr<cirrus_terasort::hash_lambda> hl =
			std::make_shared<cirrus_terasort::hash_lambda>(start, end);

		std::vector<std::thread> all;
		for (INT_TYPE i = 0; i < hash_threads; i++)
			all.push_back(std::thread(cirrus_terasort::hasher, hl, proc_rank, store));
		for (INT_TYPE i = 0; i < hash_threads; i++)
			all[i].join();

		hl.reset();

		for (INT_TYPE i = num_input_files + hash_nodes; i < num_input_files + hash_nodes + sort_nodes; i++)
			MPI_Send(&sync, 1, MPI_INT, i, proc_rank, MPI_COMM_WORLD);
	}
	else if(proc_rank < total_processes) {

		for (INT_TYPE i = num_input_files; i < num_input_files + hash_nodes; i++)
			MPI_Recv(&sync, 1, MPI_INT, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		std::cout << "Starting sorting/merging..." << std::endl;

		namespace fs = std::experimental::filesystem;
		std::vector<std::string> dir_members;
		for(auto& i : fs::directory_iterator(hash_output_dir))
			dir_members.push_back(i.path().string());
		std::sort(dir_members.begin(), dir_members.end());

		INT_TYPE process_index = proc_rank - hash_nodes - num_input_files,
			files_per_process = (hash_nodes * hash_threads * hash_modulus) / sort_nodes;
		std::vector<std::string> to_use(dir_members.begin() + process_index * files_per_process,
						dir_members.begin() + (process_index + 1) * files_per_process);

		std::shared_ptr<cirrus_terasort::sort_lambda> sl = 
			std::make_shared<cirrus_terasort::sort_lambda>(to_use);

		std::vector<std::thread> all;
		std::deque<std::future<std::vector<std::shared_ptr<std::string>>>> sorted_futures;
		std::deque<std::vector<std::shared_ptr<std::string>>> sorted;
		for (INT_TYPE i = 0; i < sort_threads; i++) {
			std::promise<std::vector<std::shared_ptr<std::string>>> p;
			sorted_futures.push_back(p.get_future());
			all.push_back(std::thread(cirrus_terasort::sorter, sl, std::move(p)));
		}
		for (INT_TYPE i = 0; i < sort_threads; i++) {
			all[i].join();
			sorted.push_back(sorted_futures.front().get());
			sorted_futures.pop_front();
		}
		std::deque<std::thread> all2;
		std::deque<std::future<std::vector<std::shared_ptr<std::string>>>> sorted_merge_futures;
		while(sorted.size() >= 2) {
			INT_TYPE k = 0;
			for (INT_TYPE i = 0; i / 2 < sort_threads && i + 1 < sorted.size(); i += 2, k++) {
				std::vector<std::shared_ptr<std::string>>& vec1 = sorted.at(i);
				std::vector<std::shared_ptr<std::string>>& vec2 = sorted.at(i + 1);
				std::promise<std::vector<std::shared_ptr<std::string>>> p;
				sorted_merge_futures.push_back(p.get_future());
				all2.push_back(std::thread(cirrus_terasort::merger, std::ref(vec1), std::ref(vec2), std::move(p)));
			}
			for (INT_TYPE j = 0; j < k; j++) {
				all2.front().join();
				sorted.pop_front();
				sorted.pop_front();
				sorted.push_back(sorted_merge_futures.front().get());
				sorted_merge_futures.pop_front();
				all2.pop_front();
			}
		}
		if(sorted.size() != 1)
			throw std::runtime_error("expected a sorted output.");

		std::cout << "Starting write..." << std::endl;

		std::vector<std::shared_ptr<std::string>>& to_write = sorted.front();
		std::ofstream of(std::string(sort_output_dir) + "/" + "terasorted" + std::to_string(total_processes - proc_rank) + ".txt");
		std::for_each(to_write.begin(), to_write.end(), [&of](const std::shared_ptr<std::string>& lhs) {
			of << *lhs << "\n";
		});
	}

    	MPI_Finalize();
}
