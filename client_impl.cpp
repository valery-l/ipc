#include "client_impl.h"

namespace ipc
{


client::client(
    string name,
    on_receive_f const& on_recv,
    on_event_f   const& on_connected,
    on_event_f   const& on_disconnected)
    : pimpl_(client_impl::create(name, on_recv, on_connected, on_disconnected))
{
}

client::~client()
{
    pimpl_->disconnect_request();
}

void client::send(void const* data, size_t size)
{
    pimpl_->send(data, size);
}

} // ipc

