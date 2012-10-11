#pragma once

namespace ipc
{

struct server_impl;
struct server
{
    typedef function<void(size_t, const void*, size_t)> on_receive_f;
    typedef function<void()>                            on_event_f; // connected, disconnected
    typedef function<void(size_t)>                      on_client_event_f; // connected, disconnected

    server(
        on_receive_f        const& on_recv,
        on_event_f          const& on_connected,
        on_event_f          const& on_disconnected,
        on_client_event_f   const& on_cl_connected,
        on_client_event_f   const& on_cl_disconnected);

   ~server();

    void send(size_t client, void const* data, size_t size);
    void send(               void const* data, size_t size); // all clients


private:
    boost::shared_ptr<server_impl> pimpl_;
};

}


