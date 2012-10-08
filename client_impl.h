#pragma once
#include "sync_objects.h"
#include "live_table.h"
#include "connected_state.h"
#include "cyclic_buffer.h"

// to .cpp
#include <common/data_wrapper.h>
#include "ipc_common.h"
#include "common/ipc_support.h"

namespace ipc
{

struct client_impl
{
    typedef function<void(const void*, size_t)> on_receive_f;
    typedef function<void()>                    on_event_f; // connected, disconnected

    client_impl(
        string name,
        on_receive_f const& on_recv,
        on_event_f const& on_connected,
        on_event_f const& on_disconnected)

        : name_             (name)

        , on_receive_       (on_recv)
        , on_connected_     (on_connected)
        , on_disconnected_  (on_disconnected)

        , sending_  (bind(&client_impl::process_send   , this))
    {
    }

   ~client_impl()
    {
        // it will disconnect and stop receiving thread
        sending_io_.post(&client_impl::do_disconnect, this, false);

        sending_io_.post(&io_service::stop, ref(sending_io_));
        sending_   .join();
    }

    void send(void const* data, size_t size)
    {
        sending_io_.post(bind(&client_impl::do_send, this, data2bytes(data, size)));
    }

private:
    typedef scoped_lock  <client2server::lock_type> send_lock_t;
    typedef sharable_lock<server2client::lock_type> recv_lock_t;

private:
    void on_disconnected(bool by_error)
    {
        receiving_.join();
        swap(receiving_, boost::thread());

        // clearing server-to-client buffer, otherwise other client could come to the place (take the same index)
        // of current deleting client. It will let other client to consider messages, intended for current client, as his.
        if (!by_error)
        {
            recv_lock_t lock(recv_sync_->mutex.get(), time_after_ms(client2server::wait_timeout_ms));
            if (lock.owns())
                mark_recv_buffer_read();
            else
                by_error = true;
        }

        send_sync_.reset();
        post2caller(on_disconnected_);
    }

    void on_connected()
    {
        send_sync_ = in_place(false);

        Assert(receiving_.get_id() == boost::thread::id());
        swap(receiving_, boost::thread(bind(&client_impl::process_receive, this)));

        post2caller(on_connected_);
    }

private:
    void do_disconnect(bool by_error = false)
    {
        live_provider_.disconnect(by_error);
        bool was_connected = live_state_.set_connected(false);

        on_disconnected(by_error);
    }

    bool refresh_connection() // returns current connection state
    {
        bool now_connected = live_provider_.refresh_state();
        bool was_connected = live_state_.set_connected(now_connected);

        if (!now_connected &&  was_connected)
            on_disconnected(true);

        if (now_connected && !was_connected)
            on_connected();

        return now_connected;
    }

    void do_send(bytes_ptr data)
    {
        if (refresh_connection())
        {
            send_lock_t lock(send_sync_->mutex.get(), time_after_ms(client2server::wait_timeout_ms));

            if (lock.owns())
            {
                // TODO: writting to buffer

                send_queue_.push_back(data);
                send_sync_->condvar.notify_one();
            }
            else
                do_disconnect(true);
        }
    }

private:
    void post2caller(on_event_f const& func)
    {
        // TODO: choose between asio an Qt
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
        deadline_timer timer(sending_io_);
        on_check_connection(timer);

        io_service::work(sending_io_);
        sending_io_.run();

        disconnect();
    }

    void process_receive()
    {
        recv_sync_ = in_place(false);

        while (live_state_.is_connected())
        {
            recv_lock_t lock(recv_sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                if(!recv_queue_.empty() || timed_timed_wait(recv_sync_->condvar-l.get(), lock, time_after_ms(wait_timeout_ms)))
                {
                    // TODO: read all the data from the buffer
                    for(auto it = recv_queue_.begin(); it != recv_queue_.end(); ++it)
                    {
                        bytes_ptr data = *it;
                        post2caller(bind(on_receive_, &(*data)[0], data->size()));
                    }

                    mark_recv_buffer_read();
                }
                else if (!lock.owns())
                {
                    sending_io_.post(bind(&client_impl::do_disconnect, this, true));
                    return;
                }
            }
            else
            {
                sending_io_.post(bind(&client_impl::do_disconnect, this, true));
                return;
            }
        }
    }

private:
    string name_;

private:
    on_receive_f    on_receive_;
    on_event_f      on_connected_;
    on_event_f      on_disconnected_;

private:
    shared_connected_state      live_state_;
    client_live_state_provider  live_provider_;

private:
    boost::thread           sending_; // also responsible for connection state maintenance
    io_service              sending_io_;

    optional<client2server> send_sync_;
    list<bytes_ptr>         send_queue_;

private:
    boost::thread           receiving_;
    optional<server2client> recv_sync_;
    cyclic_queue            recv_queue_;
};

} // namespace ipc
