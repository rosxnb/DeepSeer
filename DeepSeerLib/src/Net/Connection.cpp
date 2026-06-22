#include <DeepSeer/Net/Connection.hpp>
#include <DeepSeer/Log/Logger.hpp>

namespace DeepSeer
{

Connection::Connection(Socket socket, EventLoop& loop)
    : socket_{std::move(socket)}
    , loop_{loop}
{ }

Connection::~Connection()
{
    close();
}

void
Connection::startRead()
{
    if (closed_)
        return ;

    reading_ = true;
    updateEvents();
}

void
Connection::stopRead()
{
    if (closed_)
        return ;

    reading_ = false;
    updateEvents();
}

void
Connection::write(Buffer& data)
{
    if (closed_)
        return ;

    writeBuf_.move(data);
    updateEvents();

    // try an immediate flush
    if (!writeBuf_.empty())
        handleWrite();
}

void
Connection::write(std::string_view data)
{
    if (closed_)
        return ;

    writeBuf_.add(data);
    updateEvents();

    // try an immediate flush
    if (!writeBuf_.empty())
        handleWrite();
}

void
Connection::shutdownWrite()
{
    if (closed_)
        return ;

    ::shutdown(fd(), SHUT_WR);
}

void
Connection::close()
{
    if (closed_)
        return ;

    loop_.remove(fd());
    socket_.close();
    closed_ = true;
}

void
Connection::updateEvents()
{
    if (closed_)
        return ;

    uint32_t events = 0;
    if (reading_)
        events |= static_cast<uint32_t>(IoEvent::Readable);
    if (!writeBuf_.empty())
        events |= static_cast<uint32_t>(IoEvent::Writable);

    if (events == 0) {
        loop_.remove(fd());
        return ;
    }

    loop_.watch(fd(), events, [this](uint32_t fired) {
        if (fired & static_cast<uint32_t>(IoEvent::Readable)) {
            handleRead();
        }
        if (fired & static_cast<uint32_t>(IoEvent::Writable)) {
            handleWrite();
        }
    });
}

void
Connection::handleRead()
{
    if (closed_)
        return ;

    Buffer buf;
    auto reservation = buf.reserve(Slice::kDefaultCapacity);
    auto result = socket_.read(reservation.data(), reservation.size());

    if (!result) {
        if (onError_)
            onError_(result.error());

        close();
        return ;
    }

    auto n = *result;
    if (n < 0) {
        // EAGAIN / EWOULDBLOCK -- no data available right now
        return ;
    }

    if (n == 0) {
        // EOF -- peer closed the conection
        close();
        if (onClose_)
            onClose_();
        return ;
    }

    buf.commit(static_cast<size_t>(n));
    if (onData_)
        onData_(buf);
}

void
Connection::handleWrite()
{
    if (closed_)
        return ;

    while (!writeBuf_.empty()) {
        auto const& slices = writeBuf_.slices();
        if (slices.empty())
            break;

        auto data = slices.front().data();
        if (data.empty()) {
            writeBuf_.drain(0);
            break;
        }

        auto result = socket_.write(data.data(), data.size());
        if (!result) {
            if (onError_)
                onError_(result.error());
            close();
            return ;
        }

        auto n = *result;
        if (n == 0) {
            // EAGAIN — socket not writable, will retry via event loop
            break;
        }

        writeBuf_.drain(n);
    }

    // If write buffer is fully flushed, stop watching for writable
    updateEvents();
}

} // DeepSeer
