#pragma once

#include "live_table.h"
#include "connected_state.h"
#include "cyclic_queue.h"
#include "shared_queue.h"

#include "ipc/server.h"

namespace ipc
{

struct server_impl
    : boost::noncopyable
    , boost::enable_shared_from_this<server_impl>
{
    typedef server::on_receive_f            on_receive_f;
    typedef server::on_event_f              on_event_f;         // connected, disconnected
    typedef server::on_client_con_f         on_client_con_f;
    typedef server::on_client_discon_f      on_client_discon_f;

    typedef boost::shared_ptr<server_impl>  ptr_t;

    static ptr_t create(
        on_receive_f        const& on_recv,
        on_event_f          const& on_connected,
        on_event_f          const& on_disconnected,
        on_client_con_f     const& on_cl_connected,
        on_client_discon_f  const& on_cl_disconnected);

    void send(size_t id, const void* data, size_t size);

    void disconnect_request();

private:
    DECL_LOGGER("ipc.server_impl");

private:
    server_impl(
        on_receive_f        const& on_recv,
        on_event_f          const& on_connected,
        on_event_f          const& on_disconnected,
        on_client_con_f     const& on_cl_connected,
        on_client_discon_f  const& on_cl_disconnected);

    void main_thread_calling(on_event_f const& func);

private:
    typedef cyclic_queue::id_t id_t;

private:
    void on_client_connected    (id_t id, string name);
    void on_client_disconnected (id_t id, bool still_exists);

    void on_connected   ();
    void on_disconnected();

private:
    void do_disconnect();

private:
    typedef server_live_status_provider             live_provider;
    typedef typename live_provider::live_status     live_status;

// only in sending thread
private:
    live_status refresh_connection();                       // returns current connection state
    void        on_check_connection(deadline_timer& timer); // checks by timer

    size_t all_clients_mask();

    void do_send     ();
    void process_send();

// from any thread
private:
    void post2caller(on_event_f const& func); // posts to main thread

// only in receiving thread
private:
    void forward_receive(id_t id, data_wrap::bytes_ptr ptr);
    void process_receive();

private:
    typedef scoped_lock<server2client::lock_type> send_lock_t;
    typedef scoped_lock<client2server::lock_type> recv_lock_t;

private:
    on_receive_f        on_receive_;
    on_event_f          on_connected_;
    on_event_f          on_disconnected_;
    on_client_con_f     on_cl_connected_;
    on_client_discon_f  on_cl_disconnected_;

private:
    shared_connected_state<live_provider>   live_state_;
    live_provider                           live_provider_;

private:
    boost::thread           sending_; // also responsible for connection state maintenance
    io_service              sending_io_;
    optional<server2client> send_sync_;

private:
    typedef deque<pair<id_t, data_wrap::bytes_ptr> >    sendq_t;
    typedef optional<sendq_t>                           sendq_opt;
    typedef boost::mutex::scoped_lock                   int_send_lock_t;

    sendq_opt       sendq_;
    boost::mutex    internal_send_lock_;

private:
    boost::thread           receiving_;
    optional<client2server> recv_sync_;

private:
    bool object_alive_;
};

} // namespace ipc
