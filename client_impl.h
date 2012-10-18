#pragma once

#include "live_table.h"
#include "connected_state.h"
#include "cyclic_queue.h"
#include "shared_queue.h"

#include "ipc/client.h"

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
        on_event_f      const& on_disconnected);

    void send(const void* data, size_t size);

    void disconnect_request();

private:
    DECL_LOGGER("ipc.client_impl");

private:
    client_impl(
        string name,
        on_receive_f    const& on_recv,
        on_event_f      const& on_connected,
        on_event_f      const& on_disconnected);

    void main_thread_calling(on_event_f const& func);

private:
    typedef cyclic_queue::id_t id_t;

// callbacks from live_table
private:
    void on_last_cleanings(id_t my_id);

    void on_connected   ();
    void on_disconnected(bool by_error);

private:
    typedef
        client_live_status_provider::live_status
        live_status;

// only in sending thread
private:
    void do_disconnect(bool by_error = false);

    live_status refresh_connection();                       // returns current connection state
    void        on_check_connection(deadline_timer& timer); // check by timer

    void do_send     ();
    void process_send(); // main send thread function

// from any thread
private:
    void post2caller(on_event_f const& func); // posts to main thread

// only receive thread
private:
    void forward_receive(data_wrap::bytes_ptr ptr);
    void process_receive();

private:
    typedef scoped_lock  <client2server::lock_type> send_lock_t;
    typedef sharable_lock<server2client::lock_type> recv_lock_t;

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
