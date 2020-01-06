#define BOOST_TEST_MODULE cse::utility unittests
#include <cse/utility/concurrency.hpp>
#include <cse/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <thread>
#include <vector>


BOOST_AUTO_TEST_CASE(shared_box_copyable)
{
    constexpr int itemCount = 1000;
    cse::utility::shared_box<int> box;

    // reader
    std::thread reader([&]() {
        for (int i = 0; i < itemCount; ++i) {
            BOOST_TEST(box.take() == i);
        }
    });

    // writer
    for (int i = 0; i < itemCount;) {
        if (box.empty()) box.put(i++);
    }

    reader.join();
}


namespace
{
struct noncopyable
{
    explicit noncopyable(int n)
        : number(n)
    {}

    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
    noncopyable(noncopyable&&) = default;
    noncopyable& operator=(noncopyable&&) = default;

    int number = 0;
};
} // namespace


BOOST_AUTO_TEST_CASE(shared_box_noncopyable)
{
    constexpr int itemCount = 1000;
    cse::utility::shared_box<noncopyable> box;

    // reader
    std::thread reader([&]() {
        for (int i = 0; i < itemCount; ++i) {
            BOOST_TEST(box.take().number == i);
        }
    });

    // writer
    for (int i = 0; i < itemCount;) {
        if (box.empty()) box.put(noncopyable(i++));
    }

    reader.join();
}


BOOST_AUTO_TEST_CASE(file_lock)
{
    const auto workDir = cse::utility::temp_dir();
    const auto lockFilePath = workDir.path() / "lock";

    // This fiber won't start immediately
    auto fib = boost::fibers::fiber([=] {
        auto lock2 = cse::utility::file_lock(lockFilePath);
        BOOST_TEST_REQUIRE(!lock2.try_lock());
        lock2.lock(); // Switch to main fiber while waiting for the lock.
        boost::this_fiber::yield();
    });

    auto lock1 = cse::utility::file_lock(lockFilePath);
    BOOST_TEST_REQUIRE(lock1.try_lock());
    boost::this_fiber::yield(); // Start and switch to 'fib'.
    lock1.unlock();
    boost::this_fiber::yield(); // Switch to 'fib' to let it acquire the lock.
    BOOST_TEST_REQUIRE(!lock1.try_lock());
    fib.join();
}
