#include "client_impl.h"

// temp
#include "cyclic_queue.h"

namespace ipc
{


client::client(
    string name,
    on_receive_f const& on_recv,
    on_event_f   const& on_connected,
    on_event_f   const& on_disconnected)
    : pimpl_(client_impl::create(name, on_recv, on_connected, on_disconnected))
{
//    size_t size = 20 * 1032 + 48;
//    char* data = new char [size];

//    cyclic_queue queue(data, size);
//    queue.clear();

//    struct msg
//    {
//        size_t count;
//        char x [1000];
//    };

//    size_t count = 0;
//    size_t test_count = 0;
//    for (size_t j = 0; j < 1000; ++j)
//    {
//        for (size_t i = 0; i < 20; ++i)
//        {
//            msg w = {count++, {}};
//            queue.push(1, data2bytes(&w, sizeof(msg)));
//        }


//        while (!queue.empty())
//        {
//            Verify(test_count++ == reinterpret_cast<msg*>(queue.top().data())->count);
//            queue.pop();
//        }
//    }

//    exit(1);
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

