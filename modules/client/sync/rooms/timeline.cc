// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

ircd::mapi::header
IRCD_MODULE
{
	"Client Sync :Room Timeline"
};

namespace ircd::m::sync
{
	static void _room_timeline_append_txnid(data &, json::stack::object &, const m::event &);
	static void _room_timeline_append(data &, json::stack::array &, const m::event::idx &, const m::event &);
	static event::id::buf _room_timeline_polylog_events(data &, const m::room &, bool &, bool &);
	static bool room_timeline_polylog(data &);
	static bool room_timeline_linear(data &);
	extern const event::keys::include default_keys;
	extern item room_timeline;
}

decltype(ircd::m::sync::room_timeline)
ircd::m::sync::room_timeline
{
	"rooms.timeline",
	room_timeline_polylog,
	room_timeline_linear,
};

decltype(ircd::m::sync::default_keys)
ircd::m::sync::default_keys
{
	"content",
	"depth",
	"event_id",
	"origin_server_ts",
	"prev_events",
	"redacts",
	"room_id",
	"sender",
	"state_key",
	"type",
};

bool
ircd::m::sync::room_timeline_linear(data &data)
{
	if(!data.event_idx)
		return false;

	assert(data.event);
	json::stack::array array
	{
		*data.out, "events"
	};

	_room_timeline_append(data, array, data.event_idx, *data.event);
	return true;
}

bool
ircd::m::sync::room_timeline_polylog(data &data)
{
	if(!apropos(data, data.room_head))
		return false;

	// events
	assert(data.room);
	bool limited{false}, ret{false};
	m::event::id::buf prev
	{
		_room_timeline_polylog_events(data, *data.room, limited, ret)
	};

	// prev_batch
	json::stack::member
	{
		*data.out, "prev_batch", string_view{prev}
	};

	// limited
	json::stack::member
	{
		*data.out, "limited", json::value{limited}
	};

	return ret;
}

ircd::m::event::id::buf
ircd::m::sync::_room_timeline_polylog_events(data &data,
                                             const m::room &room,
                                             bool &limited,
                                             bool &ret)
{
	static const event::fetch::opts fopts
	{
		default_keys
	};

	json::stack::array array
	{
		*data.out, "events"
	};

	// messages seeks to the newest event, but the client wants the oldest
	// event first so we seek down first and then iterate back up. Due to
	// an issue with rocksdb's prefix-iteration this iterator becomes
	// toxic as soon as it becomes invalid. As a result we have to copy the
	// event_id on the way down in case of renewing the iterator for the
	// way back. This is not a big deal but rocksdb should fix their shit.
	ssize_t i(0);
	m::event::id::buf event_id;
	m::room::messages it
	{
		room, &fopts
	};

	for(; it && i < 10; --it, ++i)
	{
		event_id = it.event_id();
		if(!apropos(data, it.event_idx()))
			break;
	}

	limited = i >= 10;
	if(i > 0 && !it)
		it.seek(event_id);

	if(i > 0)
		for(; it && i > -1; ++it, --i)
		{
			const m::event &event(*it);
			const m::event::idx &event_idx(it.event_idx());
			_room_timeline_append(data, array, event_idx, event);
			ret = true;
		}

	return event_id;
}

void
ircd::m::sync::_room_timeline_append(data &data,
                                     json::stack::array &events,
                                     const m::event::idx &event_idx,
                                     const m::event &event)
{
	json::stack::object object
	{
		events
	};

	object.append(event);

	json::stack::object unsigned_
	{
		object, "unsigned"
	};

	json::stack::member
	{
		unsigned_, "age", json::value
		{
			long(vm::current_sequence - event_idx)
		}
	};

	_room_timeline_append_txnid(data, unsigned_, event);
}


void
ircd::m::sync::_room_timeline_append_txnid(data &data,
                                           json::stack::object &unsigned_,
                                           const m::event &event)
{
	if(data.client_txnid)
	{
		json::stack::member
		{
			unsigned_, "transaction_id", data.client_txnid
		};

		return;
	}

	if(json::get<"sender"_>(event) != data.user.user_id)
		return;

	const auto txnid_idx
	{
		data.user_room.get(std::nothrow, "ircd.client.txnid", at<"event_id"_>(event))
	};

	if(!txnid_idx)
		return;

	m::get(std::nothrow, txnid_idx, "content", [&unsigned_]
	(const json::object &content)
	{
		json::stack::member
		{
			unsigned_, "transaction_id", unquote(content.get("transaction_id"))
		};
	});
}
