#include "live_table.h"
#include "ipc_helpers.h"

namespace ipc
{

live_table::entry::entry()
    : iteration(0)
{
}

//////////////////////////////////

live_table::id_t live_table::server_id() const
{
    return index2id(srv_idx());
}


live_table::id_t live_table::register_client(string name)
{
    for(size_t i = srv_idx() + 1; i != srv_idx(); i = (i + 1) % max_slots_number)
        if (empty_entry(i))
            return filled_entry(name.c_str(), i);

    throw std::runtime_error("no more empty slots");
}

live_table::id_t live_table::register_server()
{
    if (empty_entry(srv_idx()))
        return filled_entry("server", srv_idx());
    else
        throw std::runtime_error("server is already initialized");
}

bool live_table::exist(id_t id) const
{
    size_t index = id2index(id);
    return !empty_entry(index) && id == index2id(index);
}

bool live_table::alive(id_t id) const
{
    if (!exist(id))
        return false;

    entry const& e = entries[id2index(id)];
    return microsec_clock::universal_time() - e.last_time < milliseconds(disconnect_timeout);
}

void live_table::update(id_t id)
{
    entry& e = entries[id2index(id)];
    e.last_time = microsec_clock::universal_time();
}

void live_table::remove(id_t id)
{
    entry& e = entries[id2index(id)];

    e.name[0] = 0;
    ++e.iteration;
}

live_table::id_t live_table::bit_mask(id_t id)
{
    return 1 << id2index(id);
}

size_t              live_table::id2index(id_t   id   )       { return id % max_slots_number; }
live_table::id_t    live_table::index2id(size_t index) const { return entries[index].iteration * max_slots_number + index; }

string live_table::name(id_t id) const
{
    return &(entries[id2index(id)].name[0]);
}

size_t live_table::srv_idx() const
{
    return 0;
}

bool live_table::empty_entry(size_t index) const
{
    return entries[index].name[0] == 0;
}

live_table::id_t live_table::filled_entry(const char* name, size_t index)
{
    entry& e = entries[index];
    id_t id = index2id(index);

    strncpy(&e.name[0], name, max_name_len - 1);
    update(id);

    return id;
}

/////////////////////////////////////////////////////////
// table_sync


table_sync::table_sync(bool owns)
    : mutex         ("simex.ipc.live_table_mutex" , owns)
    , table_buffer  (move(create_buffer("simex.ipc.live_table_buffer", owns, mutex)))
    , table         (static_cast<live_table*>(table_buffer.pointer()))
{
}

shared_buffer table_sync::create_buffer(string name, bool owns, lock_type& mutex)
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


///////////////////////////////////////////////////////////
// client_live_status_provider

client_live_status_provider::client_live_status_provider(string name, function<void(id_t)> const& cleaning)
    : name_     (name)
    , cleaning_ (cleaning)
{
}

client_live_status_provider::live_status client_live_status_provider::refresh_state() // returns client id and its bit mask if connected
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

void client_live_status_provider::disconnect(bool kill_server)
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



////////////////////////////////////////////////////////////
// server_live_status_provider

server_live_status_provider::server_live_status_provider(connected_f const& conn, disconnected_f const& discon)
    : conn_     (conn  )
    , discon_   (discon)
{
}

server_live_status_provider::live_status server_live_status_provider::refresh_state()
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

void server_live_status_provider::disconnect()
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

server_live_status_provider::clients_t server_live_status_provider::clients() const
{
    boost::mutex::scoped_lock lock(clients_mutex_);
    return alive_clients_;
}


void server_live_status_provider::update_clients_status()
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

void server_live_status_provider::disconnect_all()
{
    while (!alive_clients_.empty())
    {
        auto it = alive_clients_.begin();
        id_t id = *it;

        discon_(id, false);
        alive_clients_.erase(it);
    }
}

} // namespace ipc
