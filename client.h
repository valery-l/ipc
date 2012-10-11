#pragma once

namespace ipc
{

struct client_impl;
struct client
{
    typedef function<void(const void*, size_t)> on_receive_f;
    typedef function<void()>                    on_event_f; // connected, disconnected

    client(
        string name,
        on_receive_f const& on_recv,
        on_event_f   const& on_connected,
        on_event_f   const& on_disconnected);

   ~client();

    void send(void const* data, size_t size);

private:
    boost::shared_ptr<client_impl> pimpl_;
};

}
