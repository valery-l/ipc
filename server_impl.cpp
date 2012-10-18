#include "server_impl.h"

#include "ipc_helpers.h"
#include <common/data_wrapper.h>
#include "common/ipc_support.h"
#include "common/qt_dispatch.h"

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




////////////////////////////////////////////////////////////
// server_impl
server_impl::ptr_t server_impl::create(
    on_receive_f        const& on_recv,
    on_event_f          const& on_connected,
    on_event_f          const& on_disconnected,
    on_client_con_f     const& on_cl_connected,
    on_client_discon_f  const& on_cl_disconnected)
{
    ptr_t ptr(new server_impl(on_recv, on_connected, on_disconnected, on_cl_connected, on_cl_disconnected));

    // should be started only after shared_ptr created
    ptr->sending_ = std::move(boost::thread(bind(&server_impl::process_send, ptr)));
    return ptr;
}

void server_impl::send(size_t id, const void* data, size_t size)
{
    int_send_lock_t lock(internal_send_lock_);

    if (sendq_)
    {
        sendq_->push_back(make_pair(id, data_wrap::copy(data, size)));

        if (sendq_->size() == 1) // was empty
            sending_io_.post(bind(&server_impl::do_send, this));
    }
}

void server_impl::disconnect_request()
{
    object_alive_ = false;

    // it will disconnect and stop receiving thread too
    sending_io_.post(bind(&server_impl::do_disconnect, this));

    sending_io_.post(bind(&io_service::stop, ref(sending_io_)));
    sending_   .join();
}

server_impl::server_impl(
    on_receive_f        const& on_recv,
    on_event_f          const& on_connected,
    on_event_f          const& on_disconnected,
    on_client_con_f     const& on_cl_connected,
    on_client_discon_f  const& on_cl_disconnected)

    : on_receive_           (on_recv)
    , on_connected_         (on_connected)
    , on_disconnected_      (on_disconnected)
    , on_cl_connected_      (on_cl_connected)
    , on_cl_disconnected_   (on_cl_disconnected)

    , live_provider_(
        bind(&server_impl::on_client_connected   , this, _1, _2),
        bind(&server_impl::on_client_disconnected, this, _1, _2))

    , object_alive_     (true)
{
}

void server_impl::main_thread_calling(on_event_f const& func)
{
    if (object_alive_)
        func();
}

void server_impl::on_client_connected(id_t id, string name)
{
    LogInfo("Client connected: " << id << "; name: " << name);
    post2caller(bind(on_cl_connected_, id, name));
}

void server_impl::on_client_disconnected(id_t id, bool still_exists)
{
    LogInfo("Client disconnected: " << id << (still_exists ? " abnormally" : " normally"));

    if (still_exists) // clearing output buffer from messages, addressed to this client
    {
        send_lock_t lock(send_sync_->mutex, time_after_ms(wait_timeout_ms));
        if (lock.owns())
            send_sync_->buffer.read(live_table::bit_mask(id), 0);
    }

    post2caller(bind(on_cl_disconnected_, id));
}

void server_impl::on_disconnected()
{
    {
        int_send_lock_t lock(internal_send_lock_);
        sendq_.reset();
    }

    LogDebug("Joining receiving thread");
    receiving_.join();
    receiving_ = std::move(boost::thread());

    LogDebug("Disconnecting live provider thread");
    live_provider_.disconnect();

    recv_sync_.reset();
    send_sync_.reset();

    LogInfo("Disconnected");
    post2caller(on_disconnected_);
}

void server_impl::on_connected()
{
    {
        int_send_lock_t lock(internal_send_lock_);
        sendq_ = in_place();
    }

    Assert(receiving_.get_id() == boost::thread::id());
    receiving_ = boost::move(boost::thread(bind(&server_impl::process_receive, this)));

    LogInfo("Connected");
    post2caller(on_connected_);
}

void server_impl::do_disconnect()
{
    if (send_sync_) // still connected?
    {
        live_state_.set_connected(false);
        on_disconnected();
    }
}

server_impl::live_status server_impl::refresh_connection() // returns current connection state
{
    live_status now_connected = live_provider_.refresh_state();
    live_status was_connected = live_state_.set_connected(now_connected);

    if (!now_connected &&  was_connected)
    {
        LogWarn("Was disconnected by the opponent");
        on_disconnected();
    }

    if (now_connected && !was_connected)
    {
        ipc::create(send_sync_);
        ipc::create(recv_sync_);

        Verify(send_sync_ && recv_sync_);
        on_connected();
    }

    return now_connected;
}

void server_impl::do_send()
{
    sendq_t sendq;
    {
        int_send_lock_t lock(internal_send_lock_);

        if (!sendq_ || sendq_->empty())
            return;

        swap(sendq_.get(), sendq);
    }

    if (refresh_connection())
    {
        send_lock_t lock(send_sync_->mutex, time_after_ms(wait_timeout_ms));

        if (lock.owns())
        {
            auto clients = live_provider_.clients();

            while (!sendq.empty())
            {
                auto client = sendq.front().first;
                auto data   = sendq.front().second;

                sendq.pop_front();

                if (client != 0 && clients.count(client) == 0)
                    continue;

                id_t to = (client == 0)
                        ? all_clients_mask()
                        : live_table::bit_mask(client);

                send_sync_->buffer.write(to, data);
            }

            lock.unlock();
            send_sync_->condvar.get().notify_all();
        }
        else
        {
            LogWarn("Cannot lock mutex on send");
            do_disconnect();
        }
    }
}

size_t server_impl::all_clients_mask()
{
    live_provider::clients_t const& clients = live_provider_.clients();

    id_t mask = 0;
    for (auto it = clients.begin(); it != clients.end(); ++it)
        mask |= live_table::bit_mask(*it);

    return mask;
}

void server_impl::on_check_connection(deadline_timer& timer)
{
    refresh_connection();

    timer.expires_from_now(milliseconds(wait_timeout_ms));
    timer.async_wait(bind(&server_impl::on_check_connection, this, ref(timer)));
}

void server_impl::process_send()
{
    bool exit = false;

    while (!exit)
    {
        try
        {
            deadline_timer timer(sending_io_);
            on_check_connection(timer);

            io_service::work w(sending_io_);
            sending_io_.run();

            exit = true;
        }
        catch(std::runtime_error const&)
        {
            LogError("Exception caught in sending thread");
            do_disconnect();
        }
    }
}

void server_impl::post2caller(on_event_f const& func)
{
    qt_dispatch::post(bind(&server_impl::main_thread_calling, shared_from_this(), func));
}

void server_impl::forward_receive(id_t id, data_wrap::bytes_ptr ptr)
{
    on_receive_(id, data_wrap::data(ptr), data_wrap::size(ptr));
}

void server_impl::process_receive()
{
    try
    {
        while (live_state_.is_connected())
        {
            recv_lock_t lock(recv_sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                if( !recv_sync_->buffer.empty() ||
                    timed_timed_wait(lock, recv_sync_->condvar.get(), time_after_ms(wait_timeout_ms)))
                {
                    live_provider::clients_t clients = this->live_provider_.clients();

                    auto posted_receive = [&](id_t id, data_wrap::bytes_ptr ptr)
                    {
                        if (clients.find(id) != clients.end())
                            post2caller(bind(&server_impl::forward_receive, this, id, ptr));
                    };

                    recv_sync_->buffer.read(posted_receive);
                }
                else if (!lock.owns()) // notification is caught, but mutex couldn't be locked
                {
                    LogWarn("Cannot lock mutex on receive after successful condvar waiting");
                    sending_io_.post(bind(&server_impl::do_disconnect, this));
                    break;
                }
            }
            else // mutex couldn't be locked even after timeout
            {
                LogWarn("Cannot lock mutex on receive");
                sending_io_.post(bind(&server_impl::do_disconnect, this));
                break;
            }
        }
    }
    catch(std::runtime_error const& err)
    {
        LogError("Exception caught in receiving thread");
        sending_io_.post(bind(&server_impl::do_disconnect, this));
    }

    LogDebug("Process receive is finished");
}


} // ipc


