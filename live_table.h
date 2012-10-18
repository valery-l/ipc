#pragma once
#include "cyclic_queue.h"
#include "ipc_common.h"
#include "ipc_helpers.h"

namespace ipc
{

using std::cout;
using std::endl;

typedef size_t client_id;

typedef function<void(const void*, size_t)>             on_client_receive_f;
typedef function<void(const void*, size_t, client_id)>  on_server_receive_f;

typedef function<void()>            on_client_event_f; // connected, disconnected
typedef function<void(client_id)>   on_server_event_f; // connected, disconnected

struct live_table
{
    typedef cyclic_queue::id_t id_t;

    id_t server_id() const;

    id_t register_client(string name);
    id_t register_server();

    bool exist(id_t id) const;
    bool alive(id_t id) const;

    void update(id_t id);
    void remove(id_t id);

    static  id_t    bit_mask(id_t id     );
    static  size_t  id2index(id_t id     );
            id_t    index2id(size_t index) const;

    string name(id_t id) const;

public:
    static const size_t max_slots_number = sizeof(id_t) * 8;

private:
    static const size_t max_name_len = 64;

    struct entry
    {
        entry();

        boost::posix_time::ptime    last_time;
        size_t                      iteration;
        array<char, max_name_len>   name;
    };

private:
    size_t srv_idx() const;

    bool empty_entry (size_t index) const;
    id_t filled_entry(const char* name, size_t index);

private:
    array<entry, max_slots_number> entries;
};


struct table_sync
{
    typedef named_mutex lock_type;

    table_sync(bool owns);

    sync_object<lock_type>  mutex;
    shared_buffer           table_buffer;
    live_table*             table;

private:
    shared_buffer create_buffer(string name, bool owns, lock_type& mutex);
};



struct client_live_status_provider
{
    typedef live_table::id_t    id_t;
    typedef optional<id_t>      live_status;

public:
    client_live_status_provider(string name, function<void(id_t)> const& cleaning);

    live_status refresh_state(); // returns client id if connected
    void        disconnect(bool kill_server);

private:
    typedef scoped_lock<table_sync::lock_type> lock_t;

private:
    string          name_;
    optional<id_t>  id_;

private:
    optional<table_sync> sync_;

private:
    function<void(id_t)> cleaning_;
};




struct server_live_status_provider
{
    typedef live_table::id_t    id_t;
    typedef set<id_t>           clients_t;

    typedef function<void(id_t, string)>    connected_f;    // client id, name
    typedef function<void(id_t, bool  )>    disconnected_f; // client id, died (exists, but not alive)

    typedef bool live_status;

public:
    server_live_status_provider(connected_f const& conn, disconnected_f const& discon);

    live_status refresh_state(); // just bool
    void        disconnect();

    clients_t clients() const;

private:
    void update_clients_status();
    void disconnect_all       (); //clients

private:
    typedef scoped_lock<table_sync::lock_type> lock_t;

private:
    optional<id_t>       id_;

    mutable boost::mutex clients_mutex_;
    clients_t            alive_clients_;
    optional<table_sync> sync_;

private:
    connected_f     conn_;
    disconnected_f  discon_;
};

} // ipc






























