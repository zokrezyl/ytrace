// Long-running example to test socket control
#include <chrono>
#include <iostream>
#include <thread>
#include <ytrace/ytrace.hpp>

void worker() { YTRACE("worker tick"); }

int main() {
  std::cout << "Running... Use socket to control trace points.\n";
  std::cout << "Socket: " << ytrace::TraceManager::instance().get_socket_path()
            << "\n";
  std::cout << "Example: echo 'list' | nc -U "
            << ytrace::TraceManager::instance().get_socket_path() << "\n\n";

  // Register trace point
  worker();

  for (int i = 0; i < 300; ++i) {
    worker();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
