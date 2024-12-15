#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <queue>
#include <memory>

namespace fs = std::filesystem;

// Struct for task arguments
struct CopyTask {
    std::string src;
    std::string dest;
    bool is_directory;
};

// Global variables for thread management
std::vector<std::thread> workers;
std::queue<CopyTask> tasks;
std::mutex queue_mutex;
std::condition_variable condition;
bool stop = false;

void copy_file(const std::string &src, const std::string &dest) {
    try {
        std::ifstream src_stream(src, std::ios::binary);
        std::ofstream dest_stream(dest, std::ios::binary);
        dest_stream << src_stream.rdbuf();
    } catch (const std::exception &e) {
        std::cerr << "Error copying file " << src << " to " << dest << ": " << e.what() << std::endl;
    }
}

void copy_directory(const std::string &src, const std::string &dest);

void worker_thread() {
    while (true) {
        CopyTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [] { return stop || !tasks.empty(); });

            if (stop && tasks.empty())
                return;

            task = std::move(tasks.front());
            tasks.pop();
        }

        if (task.is_directory) {
            copy_directory(task.src, task.dest);
        } else {
            copy_file(task.src, task.dest);
        }
    }
}

void copy_directory(const std::string &src, const std::string &dest) {
    try {
        fs::create_directories(dest);
        for (const auto &entry : fs::directory_iterator(src)) {
            std::string new_src = entry.path().string();
            std::string new_dest = dest + "/" + entry.path().filename().string();
            if (fs::is_directory(entry.path())) {
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    tasks.push(CopyTask{new_src, new_dest, true});
                }
                condition.notify_one();
            } else {
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    tasks.push(CopyTask{new_src, new_dest, false});
                }
                condition.notify_one();
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error copying directory " << src << " to " << dest << ": " << e.what() << std::endl;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <source> <destination>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string src = argv[1];
    std::string dest = argv[2];

    if (!fs::exists(src)) {
        std::cerr << "Source path does not exist." << std::endl;
        return EXIT_FAILURE;
    }

    size_t num_threads = std::thread::hardware_concurrency();
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(worker_thread);
    }

    if (fs::is_directory(src)) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(CopyTask{src, dest, true});
        }
        condition.notify_one();
    } else {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(CopyTask{src, dest, false});
        }
        condition.notify_one();
    }

    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();

    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    return EXIT_SUCCESS;
}
