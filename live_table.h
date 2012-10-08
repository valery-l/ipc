#pragma once

#include "common/stl.h"
#include "common/boost.h"

#include <boost/date_time.hpp>

#include "sync_objects.h"

namespace ipc
{

typedef size_t client_id;

typedef function<void(const void*, size_t)>             on_client_receive_f;
typedef function<void(const void*, size_t, client_id)>  on_server_receive_f;

typedef function<void()>            on_client_event_f; // connected, disconnected
typedef function<void(client_id)>   on_server_event_f; // connected, disconnected

struct client
{
    client(string name, on_client_receive_f const& on_recv, on_client_event_f const& event);
   ~client();

    void send(const void* data, size_t size);
};


struct server
{
    server(on_server_receive_f const& on_recv, on_server_event_f const& event);
   ~server();

    void send(const void* data, size_t size);
};


struct live_table
{
    typedef size_t id_t;

    id_t server_id() const;

    id_t register_client(string name);
    id_t register_server();

    bool exist(id_t id) const;
    bool alive(id_t id, id_t compare_to) const;

    void udpate(id_t id);
    void remove(id_t id);

private:
    // in seconds
    const static size_t alive_check_timeout = 1;

    // if current time and last time of client (server) differs by more than disconnect_timeout, it's considered as disconnected
    const static size_t disconnect_timeout  = mutex_timeout * 5;


private:
    static const size_t max_slots_number = 64;
    static const size_t max_name_len     = 64;

    struct entry
    {
        entry();

        boost::posix_time::ptime    last_time;
        size_t                      iteration;
        array<char, max_name_len>   name;
    };

private:
    void srv_idx() const;

    void empty_entry (size_t index) const;
    id_t filled_entry(const char* name, size_t index);

private:
    size_t  id2index(id_t id     ) const;
    id_t    index2id(size_t index) const;

private:
    array<entry, max_points_number> entries;
};

struct client_live_state_provider
{
    client_live_state_provider(string name)
        : name_(name)
    {
        // TODO: initialize table pointer!
    }

    bool refresh_state() // returns current state
    {
        try
        {
            if (!sync_) // try to connect
                sync_ = in_place(false);

            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (!lock.owns() || !table_->alive(table_->server_id()))
            {
                lock.release();
                sync_.reset();

                return false;
            }

            if (id_ && !table_->exist(*id_)) // oops, server has removed me
                return false;

            if (!id_) // wasn't registered
                id_ = in_place(table_->register_client(name));
            else
                table_->update(*id_);

            return true;
        }
        catch(interprocess_exception const& err)
        {
            sync_.reset();
        }
    }

    void disconnect(bool server_too)
    {
        if (id_)
        {
            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                table_->remove(*id_);

                if (server_too)
                    table_->remove(table_->server_id());
            }
        }

        id_  .reset();
        sync_.reset();
    }

private:
    typedef scoped_lock<table_sync::lock_type> lock_t;

private:
    string                      name_;
    optional<live_table::id_t>  id_;
    live_table*                 table_;

private:
    optional<table_sync> sync_;
};

}






























