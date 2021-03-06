// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include "rooms.h"

using namespace ircd::m;
using namespace ircd;

static event::id::buf
bootstrap(const string_view &host,
          const m::room::id &room_id,
          const m::user::id &user_id);

resource::response
post__join(client &client,
           const resource::request &request,
           const room::id &room_id)
{
	const string_view &third_party_signed
	{
		unquote(request["third_party_signed"])
	};

	const string_view &server_name
	{
		unquote(request["server_name"])
	};

	const m::room room
	{
		room_id
	};

	m::join(room, request.user_id);

	return resource::response
	{
		client, json::members
		{
			{ "room_id", room_id }
		}
	};
}

event::id::buf
IRCD_MODULE_EXPORT
ircd::m::join(const room &room,
              const id::user &user_id)
{
	if(!exists(room))
	{
		const auto &room_id(room.room_id);
		return bootstrap(room_id.host(), room_id, user_id); //TODO: host
	}

	json::iov event;
	json::iov content;
	const json::iov::push push[]
	{
		{ event,    { "type",        "m.room.member"  }},
		{ event,    { "sender",      user_id          }},
		{ event,    { "state_key",   user_id          }},
		{ content,  { "membership",  "join"           }},
	};

	const m::user user{user_id};
	const m::user::profile profile{user};

	char displayname_buf[256];
	const string_view displayname
	{
		profile.get(displayname_buf, "displayname")
	};

	char avatar_url_buf[256];
	const string_view avatar_url
	{
		profile.get(avatar_url_buf, "avatar_url")
	};

	const json::iov::add _displayname
	{
		content, !empty(displayname),
		{
			"displayname", [&displayname]() -> json::value
			{
				return displayname;
			}
		}
	};

	const json::iov::add _avatar_url
	{
		content, !empty(avatar_url),
		{
			"avatar_url", [&avatar_url]() -> json::value
			{
				return avatar_url;
			}
		}
	};

	return commit(room, event, content);
}

event::id::buf
IRCD_MODULE_EXPORT
ircd::m::join(const m::room::alias &room_alias,
              const m::user::id &user_id)
{
	const room::id::buf room_id
	{
		m::room_id(room_alias)
	};

	if(!exists(room_id))
		return bootstrap(room_alias.host(), room_id, user_id);

	const m::room room
	{
		room_id
	};

	return m::join(room, user_id);
}

conf::item<seconds>
make_join_timeout
{
	{ "name",     "ircd.client.rooms.join.make_join.timeout" },
	{ "default",  15L                                        },
};

conf::item<seconds>
send_join_timeout
{
	{ "name",     "ircd.client.rooms.join_send_join.timeout" },
	{ "default",  45L  /* spinappse */                       },
};

static event::id::buf
bootstrap(const string_view &host,
          const m::room::id &room_id,
          const m::user::id &user_id)
{
	const unique_buffer<mutable_buffer> buf
	{
		16_KiB
	};

	m::v1::make_join::opts mjopts
	{
		net::hostport{host}
	};

	m::v1::make_join request
	{
		room_id, user_id, buf, std::move(mjopts)
	};

	request.wait(seconds(make_join_timeout));
	const auto code
	{
		request.get()
	};

	const json::object &response
	{
		request.in.content
	};

	const json::object &proto
	{
		response.at("event")
	};

	const auto auth_events
	{
		replace(std::string{proto.get("auth_events")}, "\\/", "/")
    };

	const auto prev_events
	{
		replace(std::string{proto.get("prev_events")}, "\\/", "/")
    };

	json::iov event;
	json::iov content;
	const json::iov::push push[]
	{
		{ event,    { "type",          "m.room.member"           }},
		{ event,    { "sender",        user_id                   }},
		{ event,    { "state_key",     user_id                   }},
		{ content,  { "membership",    "join"                    }},
		{ event,    { "prev_events",   prev_events               }},
		{ event,    { "auth_events",   auth_events               }},
		{ event,    { "prev_state",    "[]"                      }},
		{ event,    { "depth",         proto.get<long>("depth")  }},
		{ event,    { "room_id",       room_id                   }},
	};

	const m::user user{user_id};
	const m::user::profile profile{user};

	char displayname_buf[256];
	const string_view displayname
	{
		profile.get(displayname_buf, "displayname")
	};

	char avatar_url_buf[256];
	const string_view avatar_url
	{
		profile.get(avatar_url_buf, "avatar_url")
	};

	const json::iov::add _displayname
	{
		content, !empty(displayname),
		{
			"displayname", [&displayname]() -> json::value
			{
				return displayname;
			}
		}
	};

	const json::iov::add _avatar_url
	{
		content, !empty(avatar_url),
		{
			"avatar_url", [&avatar_url]() -> json::value
			{
				return avatar_url;
			}
		}
	};

	m::vm::copts opts;
	opts.infolog_accept = true;
	const m::event::id::buf event_id
	{
		m::vm::eval
		{
			event, content, opts
		}
	};

	const unique_buffer<mutable_buffer> ebuf
	{
		64_KiB
	};

	const m::event::fetch mevent
	{
		event_id
	};

	const string_view strung
	{
		mevent.source?
			string_view{mevent.source}:
			string_view{data(ebuf), serialized(mevent)}
	};

	const unique_buffer<mutable_buffer> buf2
	{
		16_KiB
	};

	m::v1::send_join::opts sjopts
	{
		net::hostport{host}
	};

	m::v1::send_join sj
	{
		room_id, event_id, strung, buf2, std::move(sjopts)
	};

	sj.wait(seconds(send_join_timeout));

	const auto sjcode
	{
		sj.get()
	};

	return event_id;
}
