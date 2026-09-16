#include "gpumemd/resource_manager.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <latch>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

using gpumemd::Bytes;
using gpumemd::AcquireOptions;
using gpumemd::ErrorCode;
using gpumemd::OperationResult;
using gpumemd::ResourceManager;
using namespace std::chrono_literals;

void constructor_rejects_zero_capacity() {
    bool rejected = false;
    try {
        ResourceManager manager(0);
        (void)manager;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void acquire_release_and_status_are_consistent() {
    ResourceManager manager(10'000);

    auto acquired = manager.acquire("processB", 3'000);
    assert(acquired.ok());
    assert(acquired.amount == 3'000);

    acquired = manager.acquire("processA", 2'000);
    assert(acquired.ok());

    auto snapshot = manager.status();
    assert(snapshot.capacity == 10'000);
    assert(snapshot.used == 5'000);
    assert(snapshot.free == 5'000);
    assert(snapshot.reservations.size() == 2);
    assert(snapshot.reservations[0].name == "processA");
    assert(snapshot.reservations[1].name == "processB");
    Bytes reservation_sum = 0;
    for (const auto& reservation : snapshot.reservations) {
        reservation_sum += reservation.bytes;
    }
    assert(reservation_sum == snapshot.used);

    auto released = manager.release("processB");
    assert(released.ok());
    assert(released.amount == 3'000);
    snapshot = manager.status();
    assert(snapshot.used == 2'000);
    assert(snapshot.free == 8'000);
}

void failed_operations_do_not_change_state() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 75).ok());
    const auto before = manager.status();

    auto result = manager.acquire("owner", 1);
    assert(!result.ok() && result.error == ErrorCode::DuplicateClient);
    result = manager.try_acquire("other", 26);
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);
    result = manager.acquire("zero", 0);
    assert(!result.ok() && result.error == ErrorCode::InvalidSize);
    result = manager.release("missing");
    assert(!result.ok() && result.error == ErrorCode::UnknownClient);
    result = manager.acquire("bad/name", 1);
    assert(!result.ok() && result.error == ErrorCode::InvalidName);
    result = manager.acquire("model:foo", 1);
    assert(!result.ok() && result.error == ErrorCode::InvalidName);
    result = manager.acquire(std::string(65, 'x'), 1);
    assert(!result.ok() && result.error == ErrorCode::InvalidName);

    const auto after = manager.status();
    assert(after.capacity == before.capacity);
    assert(after.used == before.used);
    assert(after.free == before.free);
    assert(after.reservations.size() == before.reservations.size());
    assert(after.reservations[0].name == before.reservations[0].name);
    assert(after.reservations[0].bytes == before.reservations[0].bytes);
}

void client_and_model_reservations_use_separate_namespaces() {
    ResourceManager manager(100);
    assert(manager.try_acquire("foo", 20).ok());
    assert(manager.acquire_model("foo", 30).ok());

    const auto snapshot = manager.status();
    assert(snapshot.used == 50);
    assert(snapshot.reservations.size() == 2);
    assert(snapshot.reservations[0].name == "foo");
    assert(snapshot.reservations[0].bytes == 20);
    assert(snapshot.reservations[1].name == "model:foo");
    assert(snapshot.reservations[1].bytes == 30);
}

void failed_model_replacement_is_atomic() {
    ResourceManager manager(100);
    assert(manager.acquire_model("old", 60).ok());
    assert(manager.try_acquire("client", 40).ok());
    const auto before = manager.status();

    const auto result = manager.replace_model_reservations({"old"}, "new", 70);
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);

    const auto after = manager.status();
    assert(after.used == before.used);
    assert(after.free == before.free);
    assert(after.reservations.size() == before.reservations.size());
    assert(after.reservations[0].name == before.reservations[0].name);
    assert(after.reservations[0].bytes == before.reservations[0].bytes);
    assert(after.reservations[1].name == before.reservations[1].name);
    assert(after.reservations[1].bytes == before.reservations[1].bytes);
}

void duplicate_model_evictions_are_rejected_without_mutation() {
    ResourceManager manager(100);
    assert(manager.acquire_model("old", 60).ok());
    assert(manager.try_acquire("client", 20).ok());
    const auto before = manager.status();

    const auto result = manager.replace_model_reservations({"old", "old"}, "new", 70);
    assert(!result.ok() && result.error == ErrorCode::DuplicateClient);

    const auto after = manager.status();
    assert(after.capacity == before.capacity);
    assert(after.used == before.used);
    assert(after.free == before.free);
    assert(after.reservations.size() == before.reservations.size());
    assert(after.reservations[0].name == before.reservations[0].name);
    assert(after.reservations[0].bytes == before.reservations[0].bytes);
    assert(after.reservations[1].name == before.reservations[1].name);
    assert(after.reservations[1].bytes == before.reservations[1].bytes);
}

void boundary_capacity_is_overflow_safe() {
    ResourceManager manager(std::numeric_limits<Bytes>::max());
    assert(manager.acquire("max", std::numeric_limits<Bytes>::max()).ok());
    auto result = manager.try_acquire("extra", 1);
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);
    const auto snapshot = manager.status();
    assert(snapshot.used == std::numeric_limits<Bytes>::max());
    assert(snapshot.free == 0);
}

void concurrent_acquire_and_release_preserve_accounting() {
    constexpr int thread_count = 8;
    constexpr int operations = 200;
    ResourceManager manager(thread_count);
    std::latch start(thread_count);
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (int thread = 0; thread < thread_count; ++thread) {
        workers.emplace_back([&, thread] {
            start.count_down();
            start.wait();
            for (int operation = 0; operation < operations; ++operation) {
                const std::string name = "worker_" + std::to_string(thread) + "_" +
                                         std::to_string(operation);
                assert(manager.acquire(name, 1).ok());
                const auto snapshot = manager.status();
                assert(snapshot.used <= snapshot.capacity);
                assert(snapshot.used + snapshot.free == snapshot.capacity);
                assert(manager.release(name).ok());
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }
    const auto snapshot = manager.status();
    assert(snapshot.used == 0);
    assert(snapshot.free == snapshot.capacity);
    assert(snapshot.reservations.empty());
}

void try_acquire_is_immediate() {
    ResourceManager manager(100);
    assert(manager.try_acquire("owner", 100).ok());
    const auto result = manager.try_acquire("other", 1);
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);
}

void blocked_acquire_succeeds_after_release() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 100).ok());
    OperationResult result{ErrorCode::Timeout, 0};
    std::thread waiter([&] {
        result = manager.acquire("waiter", 50, AcquireOptions{0, 2s});
    });
    std::this_thread::sleep_for(30ms);
    assert(manager.status().used == 100);
    assert(manager.release("owner").ok());
    waiter.join();
    assert(result.ok() && result.amount == 50);
}

void timed_out_acquire_leaves_state_unchanged() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 100).ok());
    OperationResult result{ErrorCode::Timeout, 0};
    std::thread waiter([&] {
        result = manager.acquire("waiter", 50, AcquireOptions{0, 30ms});
    });
    waiter.join();
    assert(!result.ok() && result.error == ErrorCode::Timeout);
    const auto snapshot = manager.status();
    assert(snapshot.used == 100 && snapshot.reservations.size() == 1);
}

void invalid_timeout_is_rejected() {
    ResourceManager manager(100);
    const auto result = manager.acquire("waiter", 1, AcquireOptions{0, -1ms});
    assert(!result.ok() && result.error == ErrorCode::InvalidTimeout);
}

void oversized_infinite_acquire_fails_immediately() {
    ResourceManager manager(100);
    const auto start = std::chrono::steady_clock::now();
    const auto result = manager.acquire("impossible", 101);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!result.ok() && result.error == ErrorCode::InsufficientMemory);
    assert(elapsed < 200ms);
    assert(manager.status().used == 0);
}

void priority_and_fifo_order_waiters() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 100).ok());
    OperationResult low_result{ErrorCode::Timeout, 0};
    OperationResult high_result{ErrorCode::Timeout, 0};
    std::thread low([&] {
        low_result = manager.acquire("low", 100, AcquireOptions{0, 2s});
    });
    std::this_thread::sleep_for(30ms);
    std::thread high([&] {
        high_result = manager.acquire("high", 100, AcquireOptions{10, 2s});
    });
    std::this_thread::sleep_for(30ms);

    assert(manager.release("owner").ok());
    high.join();
    assert(high_result.ok());
    assert(!low_result.ok());
    assert(manager.release("high").ok());
    low.join();
    assert(low_result.ok());
}

void fitting_request_can_pass_oversized_waiter() {
    ResourceManager manager(100);
    assert(manager.acquire("blocker", 60).ok());
    assert(manager.acquire("releaser", 30).ok());
    OperationResult big_result{ErrorCode::Timeout, 0};
    OperationResult small_result{ErrorCode::Timeout, 0};
    std::thread big([&] {
        big_result = manager.acquire("big", 50, AcquireOptions{0, 2s});
    });
    std::this_thread::sleep_for(30ms);
    std::thread small([&] {
        small_result = manager.acquire("small", 10, AcquireOptions{0, 2s});
    });
    std::this_thread::sleep_for(30ms);
    assert(manager.release("releaser").ok());
    small.join();
    assert(small_result.ok());
    assert(!big_result.ok());
    assert(manager.release("blocker").ok());
    big.join();
    assert(big_result.ok());
}

void queued_name_is_rejected_as_duplicate() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 100).ok());
    OperationResult result{ErrorCode::Timeout, 0};
    std::thread waiter([&] {
        result = manager.acquire("waiting", 1, AcquireOptions{0, 2s});
    });
    std::this_thread::sleep_for(30ms);
    const auto duplicate = manager.try_acquire("waiting", 1);
    assert(!duplicate.ok() && duplicate.error == ErrorCode::DuplicateClient);
    assert(manager.release("owner").ok());
    waiter.join();
    assert(result.ok());
}

void cancel_waiters_unblocks_infinite_requests() {
    ResourceManager manager(100);
    assert(manager.acquire("owner", 100).ok());
    OperationResult result{ErrorCode::Timeout, 0};
    std::thread waiter([&] {
        result = manager.acquire("waiting", 1);
    });
    std::this_thread::sleep_for(30ms);
    manager.cancel_waiters();
    waiter.join();
    assert(!result.ok() && result.error == ErrorCode::Timeout);
    assert(manager.status().used == 100);
}

int main() {
    constructor_rejects_zero_capacity();
    acquire_release_and_status_are_consistent();
    failed_operations_do_not_change_state();
    client_and_model_reservations_use_separate_namespaces();
    failed_model_replacement_is_atomic();
    duplicate_model_evictions_are_rejected_without_mutation();
    boundary_capacity_is_overflow_safe();
    concurrent_acquire_and_release_preserve_accounting();
    try_acquire_is_immediate();
    blocked_acquire_succeeds_after_release();
    timed_out_acquire_leaves_state_unchanged();
    invalid_timeout_is_rejected();
    oversized_infinite_acquire_fails_immediately();
    priority_and_fifo_order_waiters();
    fitting_request_can_pass_oversized_waiter();
    queued_name_is_rejected_as_duplicate();
    cancel_waiters_unblocks_infinite_requests();
}
