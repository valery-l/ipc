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
    bool alive(id_t id, id_t compare_to) const;

    void udpate(id_t id);
    void remove(id_t id);

    id_t bit_mask(id_t id) const;

    string name(id_t id) const;

private:
    static const size_t max_slots_number = sizeof(id_t) * 8;
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


struct table_sync
{
    typedef named_mutex lock_type;

    auto_rm<lock_type>  mutex;
    shared_buffer       table_buffer;
    live_table*         table;

    static optional<table_sync> create(bool owns)
    {
        auto_rm<lock_type> mutex("simex.ipc.live_table_mutex", owns);
        optional<shared_buffer> sb = shared_buffer::safe_create("simex.ipc.live_table_buffer", sizeof(live_table), owns, *mutex);

        if (!sb)
            return boost::none;

        return table_sync
    }

private:
    table_sync(auto_rm<lock_type>&& mutex, shared_buffer&& buffer)
        : mutex         (forward(mutex ))
        , table_buffer  (forward(buffer))
        , table         (static_cast<live_table*>(table_buffer.pointer()))
    {
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
    client_live_status_provider(string name)
        : name_(name)
    {
    }

    live_status refresh_state() // returns client id and its bit mask if connected
    {
        try
        {
            if (!sync_) // try to connect
                sync_ = table_sync::create(false);

            if (!sync_) // failed
                return boost::none;

            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (!lock.owns() || !sync_->table_->alive(sync_->table_->server_id()))
            {
                lock.release();
                sync_.reset();

                return boost::none;
            }

            if (id_ && !sync_->table_->exist(*id_)) // oops, server has removed me
                return boost::none;

            if (!id_) // wasn't registered
                id_ = in_place(sync_->table_->register_client(name_));
            else
                sync_->table_->update(*id_);

            return client_id(*id_, sync_->table_->bit_mask(*id_));
        }
        catch(interprocess_exception const& err)
        {
            sync_.reset();
        }
    }

    void disconnect(bool kill_server, function<void(live_status const&)> cleaning)
    {
        if (id_)
        {
            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                sync_->table_->remove(*id_);

                if (kill_server)
                    sync_->table_->remove(table_->server_id());
                else if (cleaning)
                    cleaning();
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
};

struct server_live_status_provider
{
    typedef live_table::id_t id_t;

public:
    server_live_status_provider()
    {
    }

    bool refresh_state(function<void(live_status const&, id_t)> cleaning)
    {
        try
        {
            if (!sync_) // try to connect
                sync_ = in_place(true);

            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (!lock.owns() || !sync_->table_->alive(sync_->table_->server_id()))
            {
                lock.release();
                sync_.reset();

                return boost::none;
            }

            if (id_ && !sync_->table_->exist(*id_)) // oops, server has removed me
                return boost::none;

            if (!id_) // wasn't registered
                id_ = in_place(sync_->table_->register_client(name_));
            else
                sync_->table_->update(*id_);

            return client_id(*id_, sync_->table_->bit_mask(*id_));
        }
        catch(interprocess_exception const& err)
        {
            sync_.reset();
        }
    }

    void disconnect(bool kill_server, function<void(live_status const&)> cleaning)
    {
        if (id_)
        {
            lock_t lock(sync_->mutex.get(), time_after_ms(wait_timeout_ms));

            if (lock.owns())
            {
                sync_->table_->remove(*id_);

                if (kill_server)
                    sync_->table_->remove(table_->server_id());
                else if (cleaning)
                    cleaning();
            }
        }

        id_  .reset();
        sync_.reset();
    }

private:
    typedef scoped_lock<table_sync::lock_type> lock_t;

private:
    set<id_t>            alive_clients_;
    optional<table_sync> sync_;
};

}






























