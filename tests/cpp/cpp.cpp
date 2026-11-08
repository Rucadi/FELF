#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::mutex mutex;
static int counter = 0;

int main(int argc, char **argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++)
        args.emplace_back(argv[i]);
    std::thread worker([] {
        std::lock_guard<std::mutex> lock(mutex);
        counter += 42;
    });
    worker.join();
    std::cout << "Hello from C++ FELF\n";
    std::cout << "args=" << args.size() << " value=" << counter << "\n";
    return 0;
}
