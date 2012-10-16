#pragma once
#include "ipc_helpers.h"
#include "live_table.h"
#include "connected_state.h"
#include "cyclic_queue.h"
#include "shared_queue.h"
#include "ipc/server.h"

// to .cpp
#include <common/data_wrapper.h>
#include "ipc_common.h"
#include "common/ipc_support.h"
#include "common/qt_dispatch.h"

namespace ipc
{

struct server_impl
    : boost::noncopyable
    , boost::enable_shared_from_this<server_impl>
{
    typedef server::on_receive_f            on_receive_f;
    typedef server::on_event_f              on_event_f;         // connected, disconnected
    typedef server::on_client_event_f       on_client_event_f;  // connected, disconnected

    typedef boost::shared_ptr<server_impl>  ptr_t;

    static ptr_t create(
        on_receive_f        const& on_recv,
        on_event_f          const& on_connected,
        on_event_f          const& on_disconnected,
        on_client_event_f   const& on_cl_connected,
        on_client_event_f   const& on_cl_disconnected)
    {
        ptr_t ptr(new server_impl(on_recv, on_connected, on_disconnected, on_cl_connected, on_cl_disconnected));

        // should be started only after shared_ptr created
        ptr->sending_ = std::move(boost::thread(bind(&server_impl::process_send, ptr)));
        return ptr;
    }

    void send(size_t id, void const* data, size_t size)
    {
        sending_io_.post(bind(&server_impl::do_send, this, id, data2bytes(data, size)));
    }

    void disconnect_request()
    {
        object_alive_ = false;

        // it will disconnect and stop receiving thread too
        sending_io_.post(bind(&server_impl::do_disconnect, this));

        sending_io_.post(bind(&io_service::stop, ref(sending_io_)));
        sending_   .join();
    }

private:
    DECL_LOGGER("ipc.server_impl");

private:
    server_impl(
        on_receive_f        const& on_recv,
        on_event_f          const& on_connected,
        on_event_f          const& on_disconnected,
        on_client_event_f   const& on_cl_connected,
        on_client_event_f   const& on_cl_disconnected)

        : on_receive_           (on_recv)
        , on_connected_         (on_connected)
        , on_disconnected_      (on_disconnected)
        , on_cl_connected_      (on_cl_connected)
        , on_cl_disconnected_   (on_cl_disconnected)

        , live_provider_(
            bind(&server_impl::on_client_connected   , this, _1),
            bind(&server_impl::on_client_disconnected, this, _1, _2))

        , object_alive_     (true)
    {
    }

    void main_thread_calling(on_event_f const& func)
    {
        if (object_alive_)
            func();
    }

private:
    typedef scoped_lock<server2client::lock_type> send_lock_t;
    typedef scoped_lock<client2server::lock_type> recv_lock_t;

private:
    typedef cyclic_queue::id_t id_t;

private:
    typedef server_live_status_provider             live_provider;
    typedef typename live_provider::live_status     live_status;

private:
    void on_client_connected(id_t id)
    {
        LogInfo("Client connected: " << id);
        post2caller(bind(on_cl_connected_, id));
    }

    void on_client_disconnected(id_t id, bool still_exists)
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

    void on_disconnected()
    {
        LogDebug("Joining receiving thread");
        receiving_.join();
        receiving_ = std::move(boost::thread());

        LogDebug("Disconnecting live provider thread");
        live_provider_.disconnect();

        send_sync_.reset();
        LogInfo("Disconnected");
        post2caller(on_disconnected_);
    }

    void on_connected()
    {
        Assert(receiving_.get_id() == boost::thread::id());
        receiving_ = boost::move(boost::thread(bind(&server_impl::process_receive, this)));

        LogInfo("Connected");
        post2caller(on_connected_);
    }

private:
    void do_disconnect()
    {
        live_state_.set_connected(false);
        on_disconnected();
    }

    live_status refresh_connection() // returns current connection state
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

    void do_send(id_t client, bytes_ptr data)
    {
        if (refresh_connection())
        {
            send_lock_t lock(send_sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                id_t to = ((client == 0) ? all_clients_mask() : live_table::bit_mask(client));
                send_sync_->buffer.write(to, data);

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

    size_t all_clients_mask()
    {
        live_provider::clients_t const& clients = live_provider_.clients();

        id_t mask = 0;
        for (auto it = clients.begin(); it != clients.end(); ++it)
            mask |= live_table::bit_mask(*it);

        return mask;
    }

private:
    void post2caller(on_event_f const& func)
    {
        qt_dispatch::post(bind(&server_impl::main_thread_calling, shared_from_this(), func));
    }

private:
    void on_check_connection(deadline_timer& timer)
    {
        refresh_connection();

        timer.expires_from_now(milliseconds(wait_timeout_ms));
        timer.async_wait(bind(&server_impl::on_check_connection, this, ref(timer)));
    }

    void process_send()
    {
        try
        {
            deadline_timer timer(sending_io_);
            on_check_connection(timer);

            io_service::work w(sending_io_);
            sending_io_.run();
        }
        catch(std::runtime_error const&)
        {
            LogError("Exception caught in sending thread");
        }

        do_disconnect();
    }

    void fwd_receive(id_t id, bytes_ptr data)
    {
        on_receive_(id, &(*data)[0], data->size());
    }

    void process_receive()
    {
        try
        {
            while (live_state_.is_connected())
            {
                using namespace boost::posix_time;

                static ptime time = second_clock::universal_time();

                if (second_clock::universal_time() - time > seconds(10))
                {
                    time = second_clock::universal_time();
                    LogDebug("Process receive works");
                }

                recv_lock_t lock(recv_sync_->mutex, time_after_ms(wait_timeout_ms));

                if (lock.owns())
                {
                    if( !recv_sync_->buffer.empty() ||
                        timed_timed_wait(lock, recv_sync_->condvar.get(), time_after_ms(wait_timeout_ms)))
                    {
                        auto posted_receive = [&](id_t id, bytes_ptr ptr)
                        {
                            live_provider::clients_t const& clients = this->live_provider_.clients();

                            if (clients.find(id) != clients.end())
                                post2caller(bind(&server_impl::fwd_receive, this, id, ptr));
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

private:
    on_receive_f        on_receive_;
    on_event_f          on_connected_;
    on_event_f          on_disconnected_;
    on_client_event_f   on_cl_connected_;
    on_client_event_f   on_cl_disconnected_;

private:
    shared_connected_state<live_provider>   live_state_;
    live_provider                           live_provider_;

private:
    boost::thread           sending_; // also responsible for connection state maintenance
    io_service              sending_io_;
    optional<server2client> send_sync_;

private:
    boost::thread           receiving_;
    optional<client2server> recv_sync_;

private:
    bool object_alive_;
};

} // namespace ipc
