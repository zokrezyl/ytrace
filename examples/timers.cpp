#include <ytrace/ytrace.hpp>
#include <random>
#include <thread>
#include <chrono>

static std::mt19937 rng(42);

static void busy_wait(int min_us, int max_us) {
    std::uniform_int_distribution<int> dist(min_us, max_us);
    std::this_thread::sleep_for(std::chrono::microseconds(dist(rng)));
}

void parse_request() {
    ytimeit();
    busy_wait(50, 500);
}

void query_database() {
    ytimeit("db_query");
    busy_wait(1000, 5000);
}

void compress_payload() {
    ytimeit("compress");
    busy_wait(200, 2000);
}

void send_response() {
    ytimeit();
    busy_wait(100, 800);
}

void handle_request() {
    ytimeit("request");
    parse_request();
    query_database();
    compress_payload();
    send_response();
}

void background_gc() {
    ytimeit("gc");
    busy_wait(500, 3000);
}

void heartbeat() {
    ytimeit("heartbeat");
    busy_wait(10, 100);
}

int main() {
    std::uniform_int_distribution<int> action(0, 9);

    for (;;) {
        int a = action(rng);
        if (a < 5)
            handle_request();
        else if (a < 8)
            heartbeat();
        else
            background_gc();
    }
}
