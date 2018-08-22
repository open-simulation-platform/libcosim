/**
 *  \file
 *  Event loop interface.
 */
#ifndef CSE_EVENT_LOOP_HPP
#define CSE_EVENT_LOOP_HPP

#include <chrono>
#include <cstdint>

#include <cse/detail/macros.hpp>



namespace cse
{

class event_loop;
class socket_event;
class timer_event;
class oneshot_event;
class socket_event_handler;
class timer_event_handler;
class oneshot_event_handler;


/// Socket I/O event type bitmask.
enum class socket_event_type {
    none = 0,

    /// A socket becomes ready for reading.
    read  = 1,

    /// A socket becomes ready for writing.
    write = 2
};

CSE_DETAIL_DEFINE_BITWISE_ENUM_OPERATORS(socket_event_type, int)


/// Native socket handle type
#ifdef _WIN32
using native_socket = std::uintptr_t;
#else
using native_socket = int;
#endif

/// An illegal value for a `native_socket` (useful as an initial value).
constexpr auto invalid_native_socket = static_cast<native_socket>(-1);


/// A socket I/O event which has been added to an `event_loop`.
class socket_event
{
public:
    /**
     *  Starts polling for socket I/O.
     *
     *  \param [in] type
     *      A bitmask that specifies which condition(s) will trigger the event.
     *  \param [in] persist
     *      Whether the event should remain enabled after the first time it
     *      has been triggered.
     *  \param [in] handler
     *      The handler that should be notified of socket I/O events.  This pointer
     *      must remain valid while the event is enabled.
     */
    virtual void enable(
        socket_event_type type,
        bool persist,
        socket_event_handler& handler) = 0;

    /// Disables the event.
    virtual void disable() = 0;

    /// Returns the event loop to which the event has been added.
    virtual cse::event_loop& event_loop() const = 0;

    /// Returns the native socket handle associated with the event.
    virtual cse::native_socket native_socket() const = 0;
};


/// A timer event which has been added to an `event_loop`.
class timer_event
{
public:
    /**
     *  Sets the timer.
     *
     *  \param [in] interval
     *      The time until the event is triggered.  If this is zero, the event
     *      will be triggered as soon as possible, preferably within the current
     *      iteration of the event loop.
     *  \param [in] persist
     *      Whether the event should remain enabled after the first time it
     *      has been triggered. If so, the timer will keep repeating with
     *      the same interval.
     *  \param [in] handler
     *      The handler that should be notified when the timer is triggered.
     *      This pointer must remain valid while the timer is enabled.
     */
    virtual void enable(
        std::chrono::microseconds interval,
        bool persist,
        timer_event_handler& handler) = 0;

    /// Disables the timer.
    virtual void disable() = 0;

    /// Returns the event loop to which the event has been added.
    virtual cse::event_loop& event_loop() const = 0;
};


/// An interface for socket I/O event handlers.
class socket_event_handler
{
public:
    /**
     *  Handles a socket I/O event.
     *
     *  \param [in] event
     *      The event that was triggered.
     *  \param [in] type
     *      A bitmask of the conditions which caused the event to trigger.
     */
    virtual void handle_socket_event(socket_event* event, socket_event_type type) = 0;
};


/// An interface for timer event handlers.
class timer_event_handler
{
public:
    /**
     *  Handles a timer event.
     *
     *  \param [in] event
     *      The event that was triggered.
     */
    virtual void handle_timer_event(timer_event* event) = 0;
};


/**
 *  An event loop interface.
 *
 *  This is a minimal abstraction of an event loop, containing only the
 *  functionality which is needed for the asynchronous part of this library.
 *  The purpose is to enable the library to be used with almost any
 *  third-party event loop (e.g. libevent, libuv, libev, Boost.Asio, etc.).
 *
 *  An implementing class is not required to be thread-safe, because each
 *  instance will only have its methods called from a single thread.  In turn,
 *  the implementing class is required to call the event handlers from within
 *  the same thread.
 */
class event_loop
{
public:
    virtual ~event_loop() = default;

    /**
     *  Adds a socket.
     *
     *  This adds `socket` to the event loop's internal list of sockets to
     *  poll for I/O.
     *
     *  The event is initially disabled and must be activated with
     *  `socket_event::enable()` before it can trigger.
     *
     *  \param [in] socket
     *      The socket (file descriptor) to poll.
     *
     *  \returns
     *      A pointer to an event handle which can be used to enable or disable
     *      the event.  The object it points to is owned by the `event_loop`.
     *      It is guaranteed to remain valid until it is deleted with
     *      `remove_socket()` or the `event_loop` itself is destroyed.
     */
    virtual socket_event* add_socket(native_socket socket) = 0;

    /**
     *  Removes a socket.
     *
     *  \param [in] event
     *      The event to remove. The pointer is invalidated after this.
     */
    virtual void remove_socket(socket_event* event) = 0;

    /**
     *  Adds a timer.
     *
     *  The event is initially disabled and must be activated with
     *  `timer_event::enable()` before it can trigger.
     *
     *  \returns
     *      A pointer to an event handle which can be used to enable or disable
     *      the event.  The object it points to is owned by the `event_loop`.
     *      It is guaranteed to remain valid until it is deleted with
     *      `remove_timer()` or the `event_loop` itself is destroyed.
     */
    virtual timer_event* add_timer() = 0;

    /**
     *  Removes a timer.
     *
     *  \param [in] event
     *      The event to remove. The pointer is invalidated after this.
     */
    virtual void remove_timer(timer_event* event) = 0;

    /**
     *  Runs the event loop.
     *
     *  This function blocks while the loop is running, and returns when the
     *  loop is forcefully stopped with `stop()` or there are no more pending
     *  events.
     *
     *  \returns
     *      Whether the loop was stopped forcefully with `stop()`.
     */
    virtual bool loop() = 0;

    /**
     *  Stops the event loop as soon as possible.
     *
     *  It is unspecified whether handlers for pending events will be called
     *  after this; that depends on the underlying event loop implementation.
     */
    virtual void stop_soon() = 0;
};


} // namespace
#endif // header guard
