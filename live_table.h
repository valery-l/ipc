#pragma once

#include "common/stl.h"
#include "common/boost.h"

#include <boost/date_time.hpp>

#include "ipc_helpers.h"
#include "cyclic_queue.h"

namespace ipc
{

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
        : mutex_rm(owns ? "simex.ipc.live_table_mutex" : "")
    {
        if (owns)
            create("simex.ipc.live_table_");
        else
            open  ("simex.ipc.live_table_");

        table = static_cast<live_table*>(table_buffer.pointer());
    }

    remover<lock_type> mutex_rm;

    lock_type       mutex;
    shared_buffer   table_buffer;
    live_table*     table;

private:
    void create(string name)
    {
        mutex = move(lock_type(create_only, (name + "mutex").c_str());

        scoped_lock<lock_type> lock(mutex, time_after_ms(wait_timeout_ms));
        if (lock.owns())
        {
            shared_buffer sb(name + "buffer", sizeof(live_table), true);
            sb.clear();

            table_buffer = move(sb);
        }
        else
            throw std::runtime_error("cannot lock newly created mutex for table");
    }

    void open(string name)
    {
        mutex        = move(lock_type(open_only, (name + "mutex").c_str()));
        table_buffer = move(shared_buffer(name + "buffer", sizeof(live_table), false));
    }
};

struct client_live_status_provider
{
    typedef
        live_table::id_t
        id_t;

    struct client_id
    {
        id_t id, bit_mask;
        client_id(id_t id, id_t bit_mask) : id(id), bit_mask(bit_mask){}
    };

    typedef
        optional<client_id>
        live_status;

public:
    client_live_status_provider(string name, function<void(id_t)> const& cleaning)
        : name_     (name)
        , cleaning_ (cleaning)
    {
    }

    live_status refresh_state() // returns client id and its bit mask if connected
    {
        try
        {
            if (!sync_ && !open(sync_)) // try to connect
                return boost::none;

            lock_t lock(sync_->mutex, time_after_ms(wait_timeout_ms));

            if (!lock.owns() || !sync_->table->alive(sync_->table->server_id()))
            {
                lock.release();
                sync_.reset();

                return boost::none;
            }

            if (id_ && !sync_->table->exist(*id_)) // oops, server has removed me
                return boost::none;

            if (!id_) // wasn't registered
                id_ = in_place(sync_->table->register_client(name_));
            else
                sync_->table->update(*id_);

            return client_id(*id_, sync_->table->bit_mask(*id_));
        }
        catch(interprocess_exception const& err)
        {
            sync_.reset();
        }
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
    typedef live_table::id_t id_t;

    typedef function<void(id_t, string)>    connected_f;    // client id, name
    typedef function<void(id_t, bool  )>    disconnected_f; // client id, died (exists, but not alive)

public:
    server_live_status_provider(connected_f const& conn, disconnected_f const& discon)
        : conn_     (conn  )
        , discon_   (discon)
    {
    }

    bool refresh_state()
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

    set<id_t> const& clients() const
    {
        return alive_clients_;
    }

private:
    void update_clients_status()
    {
        for (size_t i = 1; i < live_table::max_slots_number; ++i)
        {
            id_t id = sync_->table->index2id(i);

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

            Assert(!was_alive && !is_alive && exist == false);
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
    set<id_t>            alive_clients_;
    optional<table_sync> sync_;

private:
    connected_f     conn_;
    disconnected_f  discon_;
};

}






























