#define BOOST_TEST_MODULE cse::utility shared_queue unittests
#include <cse/utility/concurrency.hpp>

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
