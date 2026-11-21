#include "fixture_instances.h"

#include <mutex>
#include <new>

namespace
{
struct entry
{
    void* instance;
    void (*destroy)(void*);
    entry* next;
};

struct fixture_instances
{
    std::mutex mutex;
    entry* head = nullptr;

    ~fixture_instances()
    {
        while (head != nullptr) {
            destroy(detach(head));
        }
    }

    static entry* detach(entry*& link)
    {
        auto* const item = link;
        link = item->next;
        return item;
    }

    static void destroy(entry* item)
    {
        item->destroy(item->instance);
        delete item;
    }
};

fixture_instances instances;
} // namespace

bool fixture_track_instance(void* instance, void (*destroy)(void*))
{
    if (instance == nullptr) return false;
    const std::lock_guard<std::mutex> lock(instances.mutex);
    auto* const item = new (std::nothrow) entry{instance, destroy, instances.head};
    if (item == nullptr) return false;
    instances.head = item;
    return true;
}

void fixture_free_instance(void* instance)
{
    entry* item = nullptr;
    {
        const std::lock_guard<std::mutex> lock(instances.mutex);
        auto** link = &instances.head;
        while (*link != nullptr && (*link)->instance != instance) {
            link = &(*link)->next;
        }
        if (*link != nullptr) {
            item = fixture_instances::detach(*link);
        }
    }
    if (item != nullptr) fixture_instances::destroy(item);
}
