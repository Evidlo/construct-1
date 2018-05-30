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
#define HAVE_IRCD_M_VISIBLE_H

namespace ircd::m
{
	bool visible(const event &, const id::user &);
	bool visible(const event &, const id::node &);
	bool visible(const id::event &, const id::user &);
	bool visible(const id::event &, const id::node &);
}