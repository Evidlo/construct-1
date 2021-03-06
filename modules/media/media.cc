// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include "media.h"

mapi::header
IRCD_MODULE
{
	"11.7 :Content respository",
	[] // init
	{
		static const std::string dbopts;
		media = std::make_shared<database>("media", dbopts, media_description);
		blocks = db::column{*media, "blocks"};

		// The conf setter callbacks must be manually executed after
		// the database was just loaded to set the cache size.
		conf::reset("ircd.media.blocks.cache.size");
		conf::reset("ircd.media.blocks.cache_comp.size");
	},
	[] // fini
	{
		// The database close contains pthread_join()'s within RocksDB which
		// deadlock under certain conditions when called during a dlclose()
		// (i.e static destruction of this module). Therefor we must manually
		// close the db here first.
		media = std::shared_ptr<database>{};
	}
};

decltype(media_log)
media_log
{
	"matrix.media"
};

decltype(media_blocks_cache_enable)
media_blocks_cache_enable
{
	{ "name",     "ircd.media.blocks.cache.enable"  },
	{ "default",  true                              },
};

decltype(media_blocks_cache_comp_enable)
media_blocks_cache_comp_enable
{
	{ "name",     "ircd.media.blocks.cache_comp.enable"  },
	{ "default",  false                                  },
};

// Blocks column
decltype(media_blocks_descriptor)
media_blocks_descriptor
{
	// name
	"blocks",

	// explain
	R"(
	Key-value store of blocks belonging to files. The key is a hash of
	the block. The key is plaintext sha256-b58 and the block is binary
	up to 32768 bytes.
	)",

	// typing
	{
		typeid(string_view), typeid(string_view)
	},

	{},      // options
	{},      // comparaor
	{},      // prefix transform
	false,   // drop column

	bool(media_blocks_cache_enable)? -1 : 0,

	bool(media_blocks_cache_comp_enable)? -1 : 0,

	// bloom_bits
	10,

	// expect hit
	true,

	// block_size
	32_KiB,

	// meta block size
	512,

	// compression
	""s // no compression
};

decltype(media_description)
media_description
{
	{ "default" }, // requirement of RocksDB

	media_blocks_descriptor,
};

decltype(media_blocks_cache_size)
media_blocks_cache_size
{
	{
		{ "name",     "ircd.media.blocks.cache.size" },
		{ "default",  long(48_MiB)                   },
	}, []
	{
		if(!blocks)
			return;

		const size_t &value{media_blocks_cache_size};
		db::capacity(db::cache(blocks), value);
	}
};

decltype(media_blocks_cache_comp_size)
media_blocks_cache_comp_size
{
	{
		{ "name",     "ircd.media.blocks.cache_comp.size" },
		{ "default",  long(16_MiB)                        },
	}, []
	{
		if(!blocks)
			return;

		const size_t &value{media_blocks_cache_comp_size};
		db::capacity(db::cache_compressed(blocks), value);
	}
};

decltype(media)
media;

decltype(blocks)
blocks;

std::set<m::room::id>
downloading;

ctx::dock
downloading_dock;

m::room::id::buf
download(const string_view &server,
         const string_view &mediaid,
         const m::user::id &user_id,
         const net::hostport &remote)
{
	const m::room::id::buf room_id
	{
		file_room_id(server, mediaid)
	};

	thread_local char rembuf[256];
	if(remote && my_host(string(rembuf, remote)))
		return room_id;

	if(!remote && my_host(server))
		return room_id;

	download(server, mediaid, user_id, remote, room_id);
	return room_id;
}

m::room
download(const string_view &server,
         const string_view &mediaid,
         const m::user::id &user_id,
         const net::hostport &remote,
         const m::room::id &room_id)
try
{
	auto iit
	{
		downloading.emplace(room_id)
	};

	if(!iit.second)
	{
		do
		{
			downloading_dock.wait();
		}
		while(downloading.count(room_id));
		return room_id;
	}

	const unwind uw{[&iit]
	{
		downloading.erase(iit.first);
		downloading_dock.notify_all();
	}};

	if(exists(room_id))
		return room_id;

	const unique_buffer<mutable_buffer> buf
	{
		16_KiB
	};

	const auto pair
	{
		download(buf, server, mediaid, remote)
	};

	const auto &head
	{
		pair.first
	};

	const const_buffer &content
	{
		pair.second
	};

	char mime_type_buf[64];
	const auto &content_type
	{
		magic::mime(mime_type_buf, content)
	};

	if(content_type != head.content_type) log::dwarning
	{
		media_log, "Server %s claims thumbnail %s is '%s' but we think it is '%s'",
		string(remote),
		mediaid,
		head.content_type,
		content_type
	};

	m::vm::copts vmopts;
	vmopts.history = false;
	const m::room room
	{
		room_id, &vmopts
	};

	create(room, user_id, "file");

	const size_t written
	{
		write_file(room, user_id, content, content_type)
	};

	return room;
}
catch(const ircd::server::unavailable &e)
{
	thread_local char rembuf[256];
	throw m::error
	{
		http::BAD_GATEWAY, "M_MEDIA_UNAVAILABLE",
		"Server '%s' is not available for media for '%s/%s' :%s",
		string(rembuf, remote),
		server,
		mediaid,
		e.what()
	};
}

conf::item<seconds>
media_download_timeout
{
	{ "name",     "ircd.media.download.timeout" },
	{ "default",  15L                           },
};

std::pair<http::response::head, unique_buffer<mutable_buffer>>
download(const mutable_buffer &head_buf,
         const string_view &server,
         const string_view &mediaid,
         net::hostport remote,
         server::request::opts *const opts)
{
	thread_local char rembuf[256];
	assert(remote || !my_host(server));
	assert(!remote || !my_host(string(rembuf, remote)));

	if(!remote)
		remote = server;

	window_buffer wb{head_buf};
	thread_local char uri[4_KiB];
	http::request
	{
		wb, host(remote), "GET", fmt::sprintf
		{
			uri, "/_matrix/media/r0/download/%s/%s", server, mediaid
		}
	};

	const const_buffer out_head
	{
		wb.completed()
	};

	// Remaining space in buffer is used for received head
	const mutable_buffer in_head
	{
		data(head_buf) + size(out_head), size(head_buf) - size(out_head)
	};

	//TODO: --- This should use the progress callback to build blocks
	server::request remote_request
	{
		remote, { out_head }, { in_head, {} }, opts
	};

	if(!remote_request.wait(seconds(media_download_timeout), std::nothrow))
		throw m::error
		{
			http::GATEWAY_TIMEOUT, "M_MEDIA_DOWNLOAD_TIMEOUT",
			"Server '%s' did not respond with media for '%s/%s' in time",
			string(rembuf, remote),
			server,
			mediaid
		};

	const auto &code
	{
		remote_request.get()
	};

	if(code != http::OK)
		return {};

	parse::buffer pb{remote_request.in.head};
	parse::capstan pc{pb};
	pc.read += size(remote_request.in.head);
	return std::pair<http::response::head, unique_buffer<mutable_buffer>>
	{
		pc, std::move(remote_request.in.dynamic)
	};
}

size_t
write_file(const m::room &room,
           const m::user::id &user_id,
           const const_buffer &content,
           const string_view &content_type)
{
	//TODO: TXN
	send(room, user_id, "ircd.file.stat", "size",
	{
		{ "value", long(size(content)) }
	});

	//TODO: TXN
	send(room, user_id, "ircd.file.stat", "type",
	{
		{ "value", content_type }
	});

	size_t off{0}, wrote{0};
	while(off < size(content))
	{
		const size_t blksz
		{
			std::min(size(content) - off, size_t(32_KiB))
		};

		const const_buffer &block
		{
			data(content) + off, blksz
		};

		block_set(room, user_id, block);
		wrote += size(block);
		off += blksz;
	}

	assert(off == size(content));
	assert(wrote == off);
	return wrote;
}

size_t
read_each_block(const m::room &room,
                const std::function<void (const const_buffer &)> &closure)
{
	static const m::event::fetch::opts fopts
	{
		m::event::keys::include
		{
			"content", "type"
		}
	};

	size_t ret{0};
	m::room::messages it
	{
		room, 1, &fopts
	};

	// Block buffer
	const unique_buffer<mutable_buffer> buf
	{
		64_KiB
	};

	for(; bool(it); ++it)
	{
		const m::event &event{*it};
		if(at<"type"_>(event) != "ircd.file.block")
			continue;

		const auto &hash
		{
			unquote(at<"content"_>(event).at("hash"))
		};

		const auto &blksz
		{
			at<"content"_>(event).get<size_t>("size")
		};

		const const_buffer &block
		{
			block_get(buf, hash)
		};

		if(unlikely(size(block) != blksz)) throw error
		{
			"File [%s] block [%s] (%s) blksz %zu != %zu",
			string_view{room.room_id},
			string_view{m::get(std::nothrow, it.event_idx(), "event_id", buf)},
			hash,
			blksz,
			size(block)
		};

		assert(size(block) == blksz);
		ret += size(block);
		closure(block);
	}

	return ret;
}

const_buffer
block_get(const mutable_buffer &out,
          const string_view &b58hash)
{
	return read(blocks, b58hash, out);
}

m::event::id::buf
block_set(const m::room &room,
          const m::user::id &user_id,
          const const_buffer &block)
{
	static constexpr const auto bufsz
	{
		b58encode_size(sha256::digest_size)
	};

	char b58buf[bufsz];
	const auto hash
	{
		block_set(mutable_buffer{b58buf}, block)
	};

	return send(room, user_id, "ircd.file.block",
	{
		{ "size",  long(size(block))  },
		{ "hash",  hash               }
	});
}

string_view
block_set(const mutable_buffer &b58buf,
          const const_buffer &block)
{
	const sha256::buf hash
	{
		sha256{block}
	};

	const string_view b58hash
	{
		b58encode(b58buf, hash)
	};

	block_set(b58hash, block);
	return b58hash;
}

void
block_set(const string_view &b58hash,
          const const_buffer &block)
{
	write(blocks, b58hash, block);
}

m::room::id::buf
file_room_id(const string_view &server,
             const string_view &file)
{
	m::room::id::buf ret;
	file_room_id(ret, server, file);
	return ret;
}

m::room::id
file_room_id(m::room::id::buf &out,
             const string_view &server,
             const string_view &file)
{
	if(empty(server) || empty(file))
		throw m::BAD_REQUEST
		{
			"Invalid MXC: empty server or file parameters..."
		};

	thread_local char buf[512];
	const string_view path
	{
		fmt::sprintf
		{
			buf, "%s/%s", server, file
		}
	};

	const sha256::buf hash
	{
		sha256{path}
	};

	out =
	{
		b58encode(buf, hash), my_host()
	};

	return out;
}
