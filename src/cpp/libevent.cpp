// Prevent macro redefinition errors due to <windows.h> being included
// before <winsock2.h>
#ifdef _WIN32
#    define _WINSOCKAPI_
#endif
#include "cse/libevent.hpp"

#ifdef _WIN32
#    include <winsock2.h>
#else
#    include <sys/time.h>
#endif

#include "cse/error.hpp"

#include <boost/numeric/conversion/cast.hpp>
#include <event2/event.h>

#include <algorithm>
#include <cassert>
#include <list>
#include <memory>
#include <system_error>
#include <utility>


namespace cse
{

namespace
{

#ifdef _WIN32
// Helper class that handles startup and termination of the Winsock DLL.
class winsock_dll
{
public:
    winsock_dll()
    {
        WSADATA wsaData;
        if (auto wsaError = WSAStartup(MAKEWORD(2, 2), &wsaData)) {
            throw std::system_error(
                wsaError,
                std::system_category(),
                "Failed to initialize Winsock");
        }
    }

    ~winsock_dll()
    {
        WSACleanup();
    }
};
#endif


class libevent_event_loop : public event_loop
{
    // Helpers for automatic management of libevent resources
    using unique_event_ptr = std::unique_ptr<::event, void (*)(::event*)>;

    static unique_event_ptr make_event(
        ::event_base* base,
        evutil_socket_t fd,
        short events,
        ::event_callback_fn callback,
        void* callbackArg) noexcept
    {
        auto ev = unique_event_ptr(
            ::event_new(base, fd, events, callback, callbackArg),
            &::event_free);
        if (!ev) throw_system_error("libevent: event_new() failed");
        return ev;
    }

    using unique_event_base_ptr =
        std::unique_ptr<::event_base, void (*)(::event_base*)>;

    static unique_event_base_ptr make_event_base()
    {
        auto eb = unique_event_base_ptr(::event_base_new(), &::event_base_free);
        if (!eb) throw_system_error("libevent: event_base_new() failed");
        return eb;
    }

    // I/O event handle
    class socket : public socket_event
    {
    public:
        socket(libevent_event_loop& eventLoop, cse::native_socket socket)
            : eventLoop_(eventLoop)
            , socket_(socket)
            , event_(nullptr, nullptr)
        {
        }

        socket(const socket&) = delete;
        socket& operator=(const socket&) = delete;
        socket(socket&&) = delete;
        socket& operator=(socket&&) = delete;

        ~socket() = default;

        void enable(
            socket_event_type type,
            bool persist,
            socket_event_handler& handler) override
        {
            auto ev = make_event(
                eventLoop_.eventBase_.get(),
                socket_,
                (persist ? EV_PERSIST : 0) |
                    ((type & socket_event_type::read) == socket_event_type::none ? 0 : EV_READ) |
                    ((type & socket_event_type::write) == socket_event_type::none ? 0 : EV_WRITE),
                &call_handler,
                this);

            const auto rc = ::event_add(ev.get(), nullptr);
            if (rc != 0) throw_system_error("libevent: event_add() failed");
            event_ = std::move(ev);
            handler_ = &handler;
        }

        void disable() override
        {
            event_.reset();
        }

        cse::event_loop& event_loop() const override
        {
            return eventLoop_;
        }

        cse::native_socket native_socket() const override
        {
            return socket_;
        }

    private:
        static void call_handler(evutil_socket_t, short what, void* arg)
        {
            const auto e = reinterpret_cast<socket*>(arg);
            e->handler_->handle_socket_event(
                e,
                ((what & EV_READ) ? socket_event_type::read : socket_event_type::none) |
                    ((what & EV_WRITE) ? socket_event_type::write : socket_event_type::none));
        }

        libevent_event_loop& eventLoop_;
        cse::native_socket socket_ = invalid_native_socket;
        unique_event_ptr event_;
        socket_event_handler* handler_ = nullptr;
    };


    class timer : public timer_event
    {
    public:
        timer(libevent_event_loop& eventLoop)
            : eventLoop_(eventLoop)
            , event_(nullptr, nullptr)
        {
        }

        timer(const timer&) = delete;
        timer& operator=(const timer&) = delete;
        timer(timer&&) = delete;
        timer& operator=(timer&&) = delete;

        ~timer() = default;

        void enable(
            std::chrono::microseconds interval,
            bool persist,
            timer_event_handler& handler) override
        {
            auto ev = make_event(
                eventLoop_.eventBase_.get(),
                -1,
                persist ? EV_PERSIST : 0,
                &call_handler,
                this);

            timeval tv;
            tv.tv_sec = boost::numeric_cast<decltype(tv.tv_sec)>(interval.count() / 1'000'000);
            tv.tv_usec = boost::numeric_cast<decltype(tv.tv_usec)>(interval.count() % 1'000'000);
            const auto rc = event_add(ev.get(), &tv);
            if (rc != 0) throw_system_error("libevent: event_add() failed");
            event_ = std::move(ev);
            handler_ = &handler;
        }

        void disable() override
        {
            event_.reset();
        }

        cse::event_loop& event_loop() const override
        {
            return eventLoop_;
        }

    private:
        static void call_handler(evutil_socket_t, short, void* arg)
        {
            const auto e = reinterpret_cast<timer*>(arg);
            e->handler_->handle_timer_event(e);
        }

        libevent_event_loop& eventLoop_;
        unique_event_ptr event_;
        timer_event_handler* handler_ = nullptr;
    };


public:
    libevent_event_loop()
        : eventBase_(make_event_base())
    {
    }


    libevent_event_loop(const libevent_event_loop&) = delete;
    libevent_event_loop& operator=(const libevent_event_loop&) = delete;

    libevent_event_loop(libevent_event_loop&& other) = default;
    libevent_event_loop& operator=(libevent_event_loop&& other) = default;

    ~libevent_event_loop() = default;

    socket_event* add_socket(native_socket socket) override
    {
        socketEvents_.emplace_back(*this, socket);
        return &socketEvents_.back();
    }

    void remove_socket(socket_event* event) override
    {
        auto it = std::find_if(
            socketEvents_.begin(),
            socketEvents_.end(),
            [event](const socket& item) { return event == &item; });
        if (it == socketEvents_.end()) {
            throw std::invalid_argument("Invalid I/O event handle");
        }
        socketEvents_.erase(it);
    }

    timer_event* add_timer() override
    {
        timerEvents_.emplace_back(*this);
        return &timerEvents_.back();
    }

    void remove_timer(timer_event* event) override
    {
        auto it = std::find_if(
            timerEvents_.begin(),
            timerEvents_.end(),
            [event](const timer& item) { return event == &item; });
        if (it == timerEvents_.end()) {
            throw std::invalid_argument("Invalid timer event handle");
        }
        timerEvents_.erase(it);
    }

    bool loop() override
    {
        assert(eventBase_);
        const auto rc = ::event_base_dispatch(eventBase_.get());
        if (rc == 0) {
            return true;
        } else if (rc == 1) {
            return false;
        } else {
            throw_system_error("libevent: event_base_dispatch() failed");
        }
    }

    void stop_soon() override
    {
        assert(eventBase_);
        const auto rc = ::event_base_loopbreak(eventBase_.get());
        if (rc != 0) throw_system_error("libevent: event_base_loopbreak() failed");
    }

    event_base* get_event_base() const
    {
        return eventBase_.get();
    }

private:
#ifdef _WIN32
    // libevent uses Winsock functionality, so it is important that this
    // is declared before eventBase_.
    winsock_dll winsockDLL_;
#endif
    unique_event_base_ptr eventBase_;
    std::list<socket> socketEvents_;
    std::list<timer> timerEvents_;
};

} // anonymous namespace


std::unique_ptr<event_loop> make_libevent_event_loop()
{
    return std::make_unique<libevent_event_loop>();
}


} // namespace cse
