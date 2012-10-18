#pragma once
#include "ipc_helpers.h"
#include "live_table.h"
#include "connected_state.h"
#include "cyclic_queue.h"
#include "shared_queue.h"
#include "ipc/client.h"

// to .cpp
#include <common/data_wrapper.h>
#include "ipc_common.h"
#include "common/ipc_support.h"
#include "common/qt_dispatch.h"

namespace ipc
{

struct client_impl
    : boost::noncopyable
    , boost::enable_shared_from_this<client_impl>
{
    typedef client::on_receive_f            on_receive_f;
    typedef client::on_event_f              on_event_f; // connected, disconnected

    typedef boost::shared_ptr<client_impl>  ptr_t;

    static ptr_t create(
        string name,
        on_receive_f    const& on_recv,
        on_event_f      const& on_connected,
        on_event_f      const& on_disconnected)
    {
        ptr_t ptr(new client_impl(name, on_recv, on_connected, on_disconnected));

        // should be started only after shared_ptr created
        ptr->sending_ = std::move(boost::thread(bind(&client_impl::process_send, ptr)));

        return ptr;
    }

    void send(const void* data, size_t size)
    {
        int_send_lock_t lock(internal_send_lock_);

        if (sendq_)
        {
            sendq_->push_back(data_wrap::copy(data, size));

            if (sendq_->size() == 1) // was empty
                sending_io_.post(bind(&client_impl::do_send, this));
        }
    }

    void disconnect_request()
    {
        object_alive_ = false;

        // it will disconnect and stop receiving thread too
        sending_io_.post(bind(&client_impl::do_disconnect, this, false));

        sending_io_.post(bind(&io_service::stop, ref(sending_io_)));
        sending_   .join();
    }

private:
    DECL_LOGGER("ipc.client_impl");

private:
    client_impl(
        string name,
        on_receive_f    const& on_recv,
        on_event_f      const& on_connected,
        on_event_f      const& on_disconnected)

        : on_receive_       (on_recv)
        , on_connected_     (on_connected)
        , on_disconnected_  (on_disconnected)

        , live_provider_    (name, bind(&client_impl::on_last_cleanings, this, _1))

        , object_alive_     (true)
    {
    }

    void main_thread_calling(on_event_f const& func)
    {
        if (object_alive_)
            func();
    }

private:
    typedef scoped_lock  <client2server::lock_type> send_lock_t;
    typedef sharable_lock<server2client::lock_type> recv_lock_t;

private:
    typedef cyclic_queue::id_t id_t;

private:
    typedef
        client_live_status_provider::live_status
        live_status;

private:
    void on_last_cleanings(id_t my_id)
    {
        recv_lock_t lock(recv_sync_->mutex, time_after_ms(wait_timeout_ms));
        if (lock.owns())
            recv_sync_->buffer.read(live_table::bit_mask(my_id), 0);
    }

    void on_disconnected(bool by_error)
    {
        {
            int_send_lock_t lock(internal_send_lock_);
            sendq_.reset();
        }

        receiving_.join();
        receiving_ = std::move(boost::thread());

        // clearing server-to-client buffer, otherwise other client could come to the place (take the same index)
        // of current deleting client. It will let other client to consider messages, intended for current client, as his.
        // the callback is called while live table mutex is locked(!). It is called, only in case of planned disconnect, cause there is no need to
        // clean anything otherwise - server will clean all the buffers
        live_provider_.disconnect(by_error);

        recv_sync_.reset();
        send_sync_.reset();

        LogInfo("Disconnected");
        post2caller(on_disconnected_);
    }

    void on_connected()
    {
        {
            int_send_lock_t lock(internal_send_lock_);
            sendq_ = in_place();
        }

        Assert(receiving_.get_id() == boost::thread::id());
        receiving_ = boost::move(boost::thread(bind(&client_impl::process_receive, this)));

        LogInfo("Connected");
        post2caller(on_connected_);
    }

private:
    void do_disconnect(bool by_error = false)
    {
        if (send_sync_) // still connected?
        {
            send_sync_->buffer.set_invalid();
            recv_sync_->buffer.set_invalid();

            live_state_.set_connected(boost::none);
            on_disconnected(by_error);
        }
    }

    live_status refresh_connection() // returns current connection state
    {
        live_status now_connected = live_provider_.refresh_state();
        live_status was_connected = live_state_.set_connected(now_connected);

        if (!now_connected &&  was_connected)
        {
            LogWarn("Was disconnected by the opponent");
            on_disconnected(true);
        }

        if (now_connected && !was_connected)
        {
            if (open(send_sync_) && open(recv_sync_))
                on_connected();
            else
            {
                LogError("Refresh connection: live provider says \'connected\'', but cannot open send or recv sync objects");

                now_connected = boost::none;
                live_state_.set_connected(now_connected);

                recv_sync_.reset();
                send_sync_.reset();
            }
        }

        return now_connected;
    }

    void do_send()
    {
        sendq_t sendq;
        {
            int_send_lock_t lock(internal_send_lock_);

            if (!sendq_ || sendq_->empty())
                return;

            swap(sendq_.get(), sendq);
        }

        if (live_status status = refresh_connection())
        {
            send_lock_t lock(send_sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                while (!sendq.empty())
                {
                    send_sync_->buffer.write(status.get(), sendq.front());
                    sendq.pop_front();
                }

                lock.unlock();
                send_sync_->condvar.get().notify_all();
            }
            else
            {
                LogError("Cannot lock mutex on send");
                do_disconnect(true);
            }
        }
    }

private:
    void post2caller(on_event_f const& func)
    {
        qt_dispatch::post(bind(&client_impl::main_thread_calling, shared_from_this(), func));
    }

private:
    void on_check_connection(deadline_timer& timer)
    {
        refresh_connection();

        timer.expires_from_now(milliseconds(wait_timeout_ms));
        timer.async_wait(bind(&client_impl::on_check_connection, this, ref(timer)));
    }

    void process_send()
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

    void forward_receive(data_wrap::bytes_ptr ptr)
    {
        on_receive_(data_wrap::data(ptr), data_wrap::size(ptr));
    }

    void process_receive()
    {
        try
        {
            while (live_status status = live_state_.is_connected())
            {
                recv_lock_t lock(recv_sync_->mutex, time_after_ms(wait_timeout_ms));

                if (lock.owns())
                {
                    if(!recv_sync_->buffer.empty() ||
                       timed_timed_wait(lock, recv_sync_->condvar.get(), time_after_ms(wait_timeout_ms)))
                    {
                        auto posted_receive = [&](data_wrap::bytes_ptr ptr)
                        {
                            post2caller(bind(&client_impl::forward_receive, this, ptr));
                        };

                        recv_sync_->buffer.read(live_table::bit_mask(status.get()), posted_receive);
                    }
                    else if (!lock.owns()) // notification is caught, but mutex couldn't be locked
                    {
                        LogError("Cannot lock mutex on receive after successful condvar waiting");
                        sending_io_.post(bind(&client_impl::do_disconnect, this, true));
                        break;
                    }
                }
                else // mutex couldn't be locked even after timeout
                {
                    LogError("Cannot lock mutex on receive");
                    sending_io_.post(bind(&client_impl::do_disconnect, this, true));
                    break;
                }
            }
        }
        catch(std::runtime_error const& err)
        {
            LogError("Exception caught in receiving thread");
            sending_io_.post(bind(&client_impl::do_disconnect, this, true));
        }

        LogDebug("Process receive is finished");
    }

private:
    on_receive_f    on_receive_;
    on_event_f      on_connected_;
    on_event_f      on_disconnected_;

private:
    shared_connected_state
        <client_live_status_provider>   live_state_;
    client_live_status_provider         live_provider_;

private:
    boost::thread           sending_; // also responsible for connection state maintenance
    io_service              sending_io_;
    optional<client2server> send_sync_;

private:
    typedef deque<data_wrap::bytes_ptr>     sendq_t;
    typedef optional<sendq_t>               sendq_opt;
    typedef boost::mutex::scoped_lock       int_send_lock_t;

    sendq_opt       sendq_;
    boost::mutex    internal_send_lock_;

private:
    boost::thread           receiving_;
    optional<server2client> recv_sync_;

private:
    bool object_alive_;
};

} // namespace ipc
