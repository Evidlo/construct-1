// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_M_REGISTER_H

namespace ircd::m
{
	struct registar;
}

struct ircd::m::registar
:json::tuple
<
	json::property<name::username, json::string>,
	json::property<name::bind_email, bool>,
	json::property<name::password, json::string>,
	json::property<name::auth, json::object>,
	json::property<name::device_id, json::string>,
	json::property<name::inhibit_login, bool>,
	json::property<name::initial_device_display_name, json::string>
>
{
	using super_type::tuple;
};
