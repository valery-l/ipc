#include "live_table.h"

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

} // namespace ipc
