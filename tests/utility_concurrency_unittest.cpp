#define BOOST_TEST_MODULE cosim::utility unittests
#include <cosim/utility/concurrency.hpp>
#include <cosim/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <thread>


BOOST_AUTO_TEST_CASE(shared_box_copyable)
{
    constexpr int itemCount = 1000;
    cosim::utility::shared_box<int> box;

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
    { }

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
    cosim::utility::shared_box<noncopyable> box;

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


/*
 *  A generalised shared mutex testing function.
 *
 *  `getMutexN` must be functions that return references to 3 separate mutexes.
 *  These may be in the form of actual references (e.g. `mutex&`), or in the
 *  form of mutex wrappers (e.g. `unique_lock<mutex>`).
 */
template<typename F1, typename F2, typename F3>
void test_locking(F1&& getMutex1, F2&& getMutex2, F3&& getMutex3)
{
    auto&& mutex1 = getMutex1();
    auto&& mutex2 = getMutex2();
    auto&& mutex3 = getMutex3();

    // Step 1
    mutex1.lock();
    mutex2.lock_shared();

    auto fib = boost::fibers::fiber(
        boost::fibers::launch::dispatch,
        [&getMutex1, &getMutex2, &getMutex3] {
            auto&& mutex1 = getMutex1();
            auto&& mutex2 = getMutex2();
            auto&& mutex3 = getMutex3();

            // Step 2
            BOOST_TEST_REQUIRE(mutex3.try_lock());
            BOOST_TEST_REQUIRE(!mutex1.try_lock());
            BOOST_TEST_REQUIRE(!mutex1.try_lock_shared());
            BOOST_TEST_REQUIRE(!mutex2.try_lock());
            BOOST_TEST_REQUIRE(mutex2.try_lock_shared());
            mutex3.unlock();

            // Wait for step 3 to complete
            mutex1.lock();

            // Step 4
            mutex1.unlock();
            mutex2.unlock_shared();
        });

    // Wait for step 2 to complete
    mutex3.lock();

    // Step 3
    mutex2.unlock_shared();
    mutex1.unlock();

    // Wait for step 4 to complete
    mutex2.lock();

    // Clean up
    fib.join();
    mutex2.unlock();
    mutex3.unlock();
}


BOOST_AUTO_TEST_CASE(shared_mutex)
{
    cosim::utility::shared_mutex mutex1;
    cosim::utility::shared_mutex mutex2;
    cosim::utility::shared_mutex mutex3;
    test_locking(
        [&]() -> cosim::utility::shared_mutex& { return mutex1; },
        [&]() -> cosim::utility::shared_mutex& { return mutex2; },
        [&]() -> cosim::utility::shared_mutex& { return mutex3; });
}


BOOST_AUTO_TEST_CASE(file_lock)
{
    const auto workDir = cosim::utility::temp_dir();
    test_locking(
        [&] { return cosim::utility::file_lock(workDir.path() / "lockfile1"); },
        [&] { return cosim::utility::file_lock(workDir.path() / "lockfile2"); },
        [&] { return cosim::utility::file_lock(workDir.path() / "lockfile3"); });
}
