#include <ytrace/ytrace.hpp>
#include <random>
#include <thread>
#include <chrono>

static std::mt19937 rng(42);

static void busy_wait(int min_us, int max_us) {
    std::uniform_int_distribution<int> dist(min_us, max_us);
    std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
}

void process_input() {
    ytest("input", "processing started");
    busy_wait(100, 500);
    ytest("input", "processing complete");
}

void validate_data() {
    ytest("validation", "checking constraints");
    busy_wait(50, 200);

    std::uniform_int_distribution<int> result(0, 9);
    if (result(rng) < 2) {
        ytest("validation", "constraint violation detected: %s", "range_check");
    } else {
        ytest("validation", "all constraints passed");
    }
}

void store_result(int value) {
    ytest("storage", "storing value=%d", value);
    busy_wait(200, 1000);
    ytest("storage", "store complete");
}

void send_notification() {
    ytest("notify", "sending notification");
    busy_wait(50, 150);
}

void handle_transaction() {
    static int tx_id = 0;
    ++tx_id;

    ytest("transaction", "begin tx_id=%d", tx_id);

    process_input();
    validate_data();
    store_result(tx_id * 100);
    send_notification();

    ytest("transaction", "commit tx_id=%d", tx_id);
}

int main() {
    yinfo("test_points example starting");

    for (int i = 0; i < 10; ++i) {
        handle_transaction();
    }

    yinfo("test_points example complete");
    return 0;
}
