#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <queue>
#include <string>
#include <vector>

struct CopyTask {
    std::string src;
    std::string dest;
    bool is_directory;
};

std::queue<pthread_t> thread_queue;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void copy_file(const std::string &src, const std::string &dest) {
    int src_fd = open(src.c_str(), O_RDONLY);
    if (src_fd == -1) {
        perror("Failed to open source file");
        return;
    }

    int dest_fd = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dest_fd == -1) {
        perror("Failed to open destination file");
        close(src_fd);
        return;
    }

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        if (write(dest_fd, buffer, bytes_read) == -1) {
            perror("Failed to write to destination file");
            break;
        }
    }

    if (bytes_read == -1) {
        perror("Failed to read from source file");
    }

    close(src_fd);
    close(dest_fd);
}


void *process_task(void *arg) {
    CopyTask *task = (CopyTask *)arg;

    if (task->is_directory) {
        DIR *dir = opendir(task->src.c_str());
        if (!dir) {
            perror("Failed to open source directory");
            free(task);
            return nullptr;
        }

        if (mkdir(task->dest.c_str(), 0755) == -1 && errno != EEXIST) {
            perror("Failed to create directory");
            closedir(dir);
            free(task);
            return nullptr;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            std::string new_src = task->src + "/" + entry->d_name;
            std::string new_dest = task->dest + "/" + entry->d_name;

            struct stat entry_stat;
            if (stat(new_src.c_str(), &entry_stat) == -1) {
                perror("Failed to get file status");
                continue;
            }

            CopyTask *new_task = (CopyTask *)malloc(sizeof(CopyTask));
            new_task->src = new_src;
            new_task->dest = new_dest;
            new_task->is_directory = S_ISDIR(entry_stat.st_mode);

            pthread_t thread;
            if (pthread_create(&thread, nullptr, process_task, new_task) != 0) {
                perror("Failed to create thread");
                free(new_task);
                continue;
            }

            pthread_mutex_lock(&queue_mutex);
            thread_queue.push(thread);
            pthread_mutex_unlock(&queue_mutex);
        }

        closedir(dir);
    } else {
        copy_file(task->src, task->dest);
    }

    free(task);
    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source> <destination>\n", argv[0]);
        return EXIT_FAILURE;
    }

    std::string src = argv[1];
    std::string dest = argv[2];

    struct stat src_stat;
    if (stat(src.c_str(), &src_stat) == -1) {
        perror("Source path does not exist");
        return EXIT_FAILURE;
    }

    CopyTask *initial_task = (CopyTask *)malloc(sizeof(CopyTask));
    initial_task->src = src;
    initial_task->dest = dest;
    initial_task->is_directory = S_ISDIR(src_stat.st_mode);

    pthread_t initial_thread;
    if (pthread_create(&initial_thread, nullptr, process_task, initial_task) != 0) {
        perror("Failed to create initial thread");
        free(initial_task);
        return EXIT_FAILURE;
    }

    pthread_mutex_lock(&queue_mutex);
    thread_queue.push(initial_thread);
    pthread_mutex_unlock(&queue_mutex);

    // Главный поток ожидает завершения всех созданных потоков
    while (true) {
        pthread_mutex_lock(&queue_mutex);
        if (thread_queue.empty()) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        pthread_t thread = thread_queue.front();
        thread_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        pthread_join(thread, nullptr);
    }

    pthread_mutex_destroy(&queue_mutex);
    return EXIT_SUCCESS;
}
