//
// Created by STENBRO on 10/1/2018.
//

class Writer
{
public:
    virtual ~Writer() {}
    virtual void write(char* buffer, size_t size) = 0;
};

template<typename T>
class WriterT : public Writer
{
public:
    static_assert(std::is_trivially_destructible<T>::value, "Reserved to trivially destructible types");

    explicit WriterT(T item)
        : mItem(item)
    {}
    virtual void write(char* buffer, size_t size) { ... }

private:
    T mItem;
};

template<typename T>
WriterT<T> make_writer(T item) { return WriterT<T>{std::move(item)}; }

template<std::size_t Size, std::size_t Align>
class Argument : private NoMove, private NoCopy
{
    static std::size_t const PtrSize = sizeof(void*);
    static_assert(Size >= PtrSize, "Cannot store less than a pointer's worth of data");
    static_assert(Align >= alignof(void*), "Cannot store aligned less than a pointer");

public:
    Argument()
    {
        // Hackish ABI-dependent trick, the setup
        memset(&mScratchPad, 0, PtrSize);
    }

    template<typename T>
    explicit Argument(T item) noexpect(true)
    {
        using WriterType = std::remove_reference<decltype(make_writer(std::move(item)))>::type;

        static_assert(std::is_nothrow_move_constructible<T>::value,
            "T not nothrow move constructible");
        static_assert(std::is_nothrow_move_constructible<WriterType>::value,
            "Writer type not nothrow move constructible");
        static_assert(sizeof(WriterType) <= Size, "Size of item too high");
        static_assert(alignof(WriterType) <= Align, "Alignment of item too high");

        new (this->access_writer()) WriterType(make_writer(std::move(item)));
    }

    ~Argument()
    {
        // Hackish ABI-dependent trick, the usage
        // (where we rely on the fact that the first pointer-size word is the virtual pointer of the Writer struct,
        //  and thus null if no Writer was stored and non-null if one is stored)
        static char const Zero[PtrSize] = {0};
        if (memcmp(&mStorage, Zero, PtrSize) != 0) {
            this->access_writer()->~Writer();
        }
    }

    void write(char* buffer, size_t size)
    {
        this->access_writer().write(buffer, size);
    }

private:
    using ScratchPad = std::aligned_storage<Size, Alignment>::type;

    Writer* access_writer() { return reinterpret_cast<Writer*>(&mScratchPad); }

    ScratchPad mScratchPad;
};

template<std::size_t Nb, std::size_t Size, std::size_t Align>
class Message
{
public:
    template<typename... Args>
    Message(char const* f, std::chrono::nanoseconds t, Args&&... args)
        : mFormat(f)
        , mTimestamp(t)
        , mArguments()
    {
        static_assert(sizeof...(args) <= Nb, "Too many arguments");
        this->write_at(0, std::forward(args)...);
    }

private:
    using Arg = Argument<Size, Align>; // the clever ones have multiple sizes, because logs often have numbers...

    template<typename A, typename... Args>
    void write_at(size_t index, A&& arg, Args&&... args)
    {
        Arg* arg = &mArguments[index];
        arg->~Arg(); // it's UB to write on top of a living argument; fortunately, this destructor is a no-op here
        new (arg) Arg(std::forward(arg));

        this->write_at(index + 1, std::forward(args)...);
    }

    void write_at(size_t) {}

    char const* mFormat;
    std::chrono::nanoseconds mTimestamp;
    std::array<Arg, Nb> mArguments;
};