#define BOOST_TEST_MODULE cse::utility unittests
#include <cse/utility/concurrency.hpp>
#include <cse/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <chrono>
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
    using namespace std::chrono_literals;
    const auto workDir = cse::utility::temp_dir();
    const auto lockFilePath = workDir.path() / "lock";

    auto fib = boost::fibers::fiber([=] {
        auto lock2 = cse::utility::file_lock(lockFilePath);
        boost::this_fiber::yield();
        BOOST_TEST_REQUIRE(!lock2.try_lock());
        boost::this_fiber::yield();
        BOOST_TEST_REQUIRE(lock2.try_lock());
        boost::this_fiber::yield();
    });

    auto lock1 = cse::utility::file_lock(lockFilePath);
    boost::this_fiber::yield();
    lock1.lock();
    boost::this_fiber::yield();
    lock1.unlock();
    boost::this_fiber::yield();
    BOOST_TEST_REQUIRE(!lock1.try_lock());
    boost::this_fiber::yield();
    BOOST_TEST_REQUIRE(lock1.try_lock());

    fib.join();
}
