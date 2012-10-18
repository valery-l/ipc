#include "server_impl.h"

namespace ipc
{

server::server(
    on_receive_f        const& on_recv,
    on_event_f          const& on_connected,
    on_event_f          const& on_disconnected,
    on_client_con_f     const& on_cl_connected,
    on_client_discon_f  const& on_cl_disconnected)

    : pimpl_(server_impl::create(on_recv, on_connected, on_disconnected, on_cl_connected, on_cl_disconnected))
{
}

server::~server()
{
    pimpl_->disconnect_request();
}

void server::send(size_t client, const void* data, size_t size)
{
    pimpl_->send(client, data, size);
}

void server::send(const void* data, size_t size)
{
    pimpl_->send(0, data, size);
}

} // ipc
