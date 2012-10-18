#pragma once

#include "common/stl.h"
#include "common/boost.h"

#include <boost/date_time.hpp>

#include "ipc_helpers.h"
#include "cyclic_queue.h"

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

    table_sync(bool owns)
        : mutex         ("simex.ipc.live_table_mutex" , owns)
        , table_buffer  (move(create_buffer("simex.ipc.live_table_buffer", owns, mutex)))
        , table         (static_cast<live_table*>(table_buffer.pointer()))
    {
    }

    sync_object<lock_type>  mutex;
    shared_buffer           table_buffer;
    live_table*             table;


private:
    shared_buffer create_buffer(string name, bool owns, lock_type& mutex)
    {
        if (owns)
        {
            scoped_lock<lock_type> lock(mutex, time_after_ms(wait_timeout_ms));
            if (lock.owns())
            {
                shared_buffer sb(name, sizeof(live_table), true);
                sb.clear();

                return sb;
            }
            else
                throw std::runtime_error("cannot lock newly created mutex for table");
        }
        else
            return shared_buffer(name, sizeof(live_table), false);
    }
};

struct client_live_status_provider
{
    typedef live_table::id_t    id_t;
    typedef optional<id_t>      live_status;

public:
    client_live_status_provider(string name, function<void(id_t)> const& cleaning)
        : name_     (name)
        , cleaning_ (cleaning)
    {
    }

    live_status refresh_state() // returns client id and its bit mask if connected
    {
        if (sync_ || open(sync_)) // try to connect
        {
            lock_t lock(sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns()                                     &&
                sync_->table->alive(sync_->table->server_id())  &&
                (!id_ || sync_->table->exist(*id_)))
            {
                if (!id_) // wasn't registered
                    id_ = in_place(sync_->table->register_client(name_));
                else
                    sync_->table->update(*id_);

                return *id_;
            }
        }

        sync_.reset();
        id_  .reset();

        return boost::none;
    }

    void disconnect(bool kill_server)
    {
        if (id_)
        {
            lock_t lock(sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                sync_->table->remove(*id_);

                if (kill_server)
                    sync_->table->remove(sync_->table->server_id());
                else
                    cleaning_(live_table::bit_mask(*id_));
            }
        }

        id_  .reset();
        sync_.reset();
    }

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
    server_live_status_provider(connected_f const& conn, disconnected_f const& discon)
        : conn_     (conn  )
        , discon_   (discon)
    {
    }

    live_status refresh_state()
    {
        if (!sync_) // try to connect
            create(sync_);

        Assert(sync_);

        {
            lock_t lock(sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns() &&
                (!id_ || sync_->table->exist(*id_))) // I existed, but some client has removed me
            {
                if (!id_) // wasn't registered
                    id_ = in_place(sync_->table->register_server());
                else
                    sync_->table->update(*id_);

                update_clients_status();
                return true;
            }
        }

        sync_.reset     ();
        id_  .reset     ();
        disconnect_all  ();

        return false;
    }

    void disconnect()
    {
        if (id_)
        {
            lock_t lock(sync_->mutex, time_after_ms(wait_timeout_ms));

            if (lock.owns())
                sync_->table->remove(*id_);
        }

        sync_.reset     ();
        id_  .reset     ();
        disconnect_all  ();
    }

    clients_t clients() const
    {
        boost::mutex::scoped_lock lock(clients_mutex_);
        return alive_clients_;
    }

private:
    void update_clients_status()
    {
        map<size_t, id_t> client_indices;
        for (auto it = alive_clients_.begin(); it != alive_clients_.end(); ++it)
            client_indices[live_table::id2index(*it)] = *it;


        boost::mutex::scoped_lock lock(clients_mutex_);
        for (size_t i = 1; i < live_table::max_slots_number; ++i)
        {
            id_t id = sync_->table->index2id(i);

            auto it = client_indices.find(i);
            if (it != client_indices.end() && it->second != id)
            {
                alive_clients_.erase(it->second);
                discon_(it->second, false);
            }

            bool was_alive = alive_clients_.find(id) != alive_clients_.end();
            bool exist     = sync_->table->exist(id);
            bool is_alive  = sync_->table->alive(id);

            if (was_alive && !is_alive)
            {
                alive_clients_.erase(id);
                sync_->table->remove(id);

                discon_(id, exist);
            }

            if (!was_alive && is_alive)
            {
                alive_clients_.insert(id);
                conn_(id, sync_->table->name(id));
            }

            Assert((!was_alive && !is_alive && exist) == false);
        }
    }

    void disconnect_all()
    {
        while (!alive_clients_.empty())
        {
            auto it = alive_clients_.begin();
            id_t id = *it;

            discon_(id, false);
            alive_clients_.erase(it);
        }
    }

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

}






























