// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m::vm
{
	static void _commit(eval &);
	static void _write(eval &, const event &);
	static fault _eval_edu(eval &, const event &);
	static fault _eval_pdu(eval &, const event &);

	template<class... args>
	static fault handle_error(const opts &opts, const fault &code, const string_view &fmt, args&&... a);

	extern "C" fault eval__event(eval &, const event &);
	extern "C" fault eval__commit(eval &, json::iov &, const json::iov &);
	extern "C" fault eval__commit_room(eval &, const room &, json::iov &, const json::iov &);

	static void init();
	static void fini();

	extern hook::site<eval &> issue_hook;    ///< Called when this server is issuing event
	extern hook::site<eval &> conform_hook;  ///< Called for static evaluations of event
	extern hook::site<eval &> fetch_hook;    ///< Called to resolve dependencies
	extern hook::site<eval &> eval_hook;     ///< Called for final event evaluation
	extern hook::site<eval &> post_hook;     ///< Called to apply effects pre-notify
	extern hook::site<eval &> notify_hook;   ///< Called to broadcast successful eval
	extern hook::site<eval &> effect_hook;   ///< Called to apply effects post-notify

	extern conf::item<bool> log_commit_debug;
	extern conf::item<bool> log_accept_debug;
	extern conf::item<bool> log_accept_info;

	extern conf::item<size_t> pool_size;
	extern const ctx::pool::opts pool_opts;
	extern ctx::pool pool;
}

ircd::mapi::header
IRCD_MODULE
{
	"Matrix Virtual Machine",
	ircd::m::vm::init, ircd::m::vm::fini
};

decltype(ircd::m::vm::log_commit_debug)
ircd::m::vm::log_commit_debug
{
	{ "name",     "ircd.m.vm.log.commit.debug" },
	{ "default",  true                         },
};

decltype(ircd::m::vm::log_accept_debug)
ircd::m::vm::log_accept_debug
{
	{ "name",     "ircd.m.vm.log.accept.debug" },
	{ "default",  false                        },
};

decltype(ircd::m::vm::log_accept_info)
ircd::m::vm::log_accept_info
{
	{ "name",     "ircd.m.vm.log.accept.info" },
	{ "default",  false                       },
};

decltype(ircd::m::vm::issue_hook)
ircd::m::vm::issue_hook
{
	{ "name", "vm.issue" }
};

decltype(ircd::m::vm::conform_hook)
ircd::m::vm::conform_hook
{
	{ "name", "vm.conform" }
};

decltype(ircd::m::vm::fetch_hook)
ircd::m::vm::fetch_hook
{
	{ "name", "vm.fetch" }
};

decltype(ircd::m::vm::eval_hook)
ircd::m::vm::eval_hook
{
	{ "name", "vm.eval" }
};

decltype(ircd::m::vm::post_hook)
ircd::m::vm::post_hook
{
	{ "name", "vm.post" }
};

decltype(ircd::m::vm::notify_hook)
ircd::m::vm::notify_hook
{
	{ "name", "vm.notify" }
};

decltype(ircd::m::vm::effect_hook)
ircd::m::vm::effect_hook
{
	{ "name", "vm.effect" }
};

decltype(ircd::m::vm::pool_size)
ircd::m::vm::pool_size
{
	{ "name",     "ircd.m.vm.pool.size" },
	{ "default",  16L                   },
};

decltype(ircd::m::vm::pool_opts)
ircd::m::vm::pool_opts
{
	ctx::DEFAULT_STACK_SIZE,
	0,
	-1,
	0
};

decltype(ircd::m::vm::pool)
ircd::m::vm::pool
{
	"vm", pool_opts
};


//
// init
//

void
ircd::m::vm::init()
{
	pool.min(size_t(pool_size));

	id::event::buf event_id;
	sequence::retired = sequence::get(event_id);
	sequence::committed = sequence::retired;
	sequence::uncommitted = sequence::committed;

	log::info
	{
		log, "BOOT %s @%lu [%s]",
		string_view{m::my_node.node_id},
		sequence::retired,
		sequence::retired? string_view{event_id} : "NO EVENTS"_sv
	};
}

void
ircd::m::vm::fini()
{
	pool.terminate();
	pool.join();

	assert(eval::list.empty());

	event::id::buf event_id;
	const auto retired
	{
		sequence::get(event_id)
	};

	log::info
	{
		log, "HLT '%s' @%lu [%s] %lu:%lu:%lu",
		string_view{m::my_node.node_id},
		retired,
		retired? string_view{event_id} : "NO EVENTS"_sv,
		sequence::retired,
		sequence::committed,
		sequence::uncommitted
	};
}

//
// eval
//

enum ircd::m::vm::fault
ircd::m::vm::eval__commit_room(eval &eval,
                               const room &room,
                               json::iov &event,
                               const json::iov &contents)
{
	// This eval entry point is only used for commits. We try to find the
	// commit opts the user supplied directly to this eval or with the room.
	if(!eval.copts)
		eval.copts = room.copts;

	if(!eval.copts)
		eval.copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	assert(!eval.opts || eval.opts == eval.copts);
	eval.opts = eval.copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	const scope_restore eval_issue
	{
		eval.issue, &event
	};

	const scope_restore eval_room_id
	{
		eval.room_id, room.room_id
	};

	assert(eval.issue);
	assert(eval.room_id);
	assert(eval.copts);
	assert(eval.opts);
	assert(room.room_id);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::push room_id
	{
		event, { "room_id", room.room_id }
	};

	const m::room::head head
	{
		room
	};

	const bool need_tophead{event.at("type") != "m.room.create"};
	const unique_buffer<mutable_buffer> prev_buf{8192};
	static const size_t prev_limit{16};
	const auto prev
	{
		head.make_refs(prev_buf, prev_limit, need_tophead)
	};

	const auto &prev_events{prev.first};
	const auto &depth{prev.second};
	const json::iov::set depth_
	{
		event, !event.has("depth"),
		{
			"depth", [&depth]
			{
				return json::value
				{
					depth == std::numeric_limits<int64_t>::max()? depth : depth + 1
				};
			}
		}
	};

	const m::room::auth auth
	{
		room
	};

	char ae_buf[1024];
	json::array auth_events{json::empty_array};
	if(depth != -1 && event.at("type") != "m.room.create" && opts.add_auth_events)
	{
		static const string_view types[]
		{
			"m.room.create",
			"m.room.join_rules",
			"m.room.power_levels",
		};

		const m::user::id &member
		{
			event.at("type") != "m.room.member"?
				m::user::id{event.at("sender")}:
				m::user::id{}
		};

		auth_events = auth.make_refs(ae_buf, types, member);
	}

	const json::iov::add auth_events_
	{
		event, opts.add_auth_events,
		{
			"auth_events", [&auth_events]() -> json::value
			{
				return auth_events;
			}
		}
	};

	const json::iov::add prev_events_
	{
		event, opts.add_prev_events,
		{
			"prev_events", [&prev_events]() -> json::value
			{
				return prev_events;
			}
		}
	};

	const json::iov::add prev_state_
	{
		event, opts.add_prev_state,
		{
			"prev_state", []
			{
				return json::empty_array;
			}
		}
	};

	return eval(event, contents);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__commit(eval &eval,
                          json::iov &event,
                          const json::iov &contents)
{
	// This eval entry point is only used for commits. If the user did not
	// supply commit opts we supply the default ones here.
	if(!eval.copts)
		eval.copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	assert(!eval.opts || eval.opts == eval.copts);
	eval.opts = eval.copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	assert(!eval.room_id || eval.issue == &event);
	if(!eval.room_id)
		eval.issue = &event;

	const unwind deissue{[&eval]
	{
		// issue is untouched when room_id is set; that indicates it was set
		// and will be unset by another eval function (i.e above).
		if(!eval.room_id)
			eval.issue = nullptr;
	}};

	assert(eval.issue);
	assert(eval.copts);
	assert(eval.opts);
	assert(eval.copts);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::add origin_
	{
		event, opts.add_origin,
		{
			"origin", []() -> json::value
			{
				return my_host();
			}
		}
	};

	const json::iov::add origin_server_ts_
	{
		event, opts.add_origin_server_ts,
		{
			"origin_server_ts", []
			{
				return json::value{ircd::time<milliseconds>()};
			}
		}
	};

	const json::strung content
	{
		contents
	};

	// event_id

	sha256::buf event_id_hash;
	if(opts.add_event_id)
	{
		const json::iov::push _content
		{
			event, { "content", content },
		};

		thread_local char preimage_buf[64_KiB];
		event_id_hash = sha256
		{
			stringify(mutable_buffer{preimage_buf}, event)
		};
	}

	const string_view event_id
	{
		opts.add_event_id?
			make_id(event, eval.event_id, event_id_hash):
			string_view{}
	};

	const json::iov::add event_id_
	{
		event, opts.add_event_id,
		{
			"event_id", [&event_id]() -> json::value
			{
				return event_id;
			}
		}
	};

	// hashes

	char hashes_buf[128];
	const string_view hashes
	{
		opts.add_hash?
			m::event::hashes(hashes_buf, event, content):
			string_view{}
	};

	const json::iov::add hashes_
	{
		event, opts.add_hash,
		{
			"hashes", [&hashes]() -> json::value
			{
				return hashes;
			}
		}
	};

	// sigs

	char sigs_buf[384];
	const string_view sigs
	{
		opts.add_sig?
			m::event::signatures(sigs_buf, event, contents):
			string_view{}
	};

	const json::iov::add sigs_
	{
		event, opts.add_sig,
		{
			"signatures", [&sigs]() -> json::value
			{
				return sigs;
			}
		}
	};

	const json::iov::push content_
	{
		event, { "content", content },
	};

	return eval(event);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__event(eval &eval,
                         const event &event)
try
{
	// Set a member pointer to the event currently being evaluated. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is working on.
	assert(!eval.event_);
	const scope_restore eval_event
	{
		eval.event_, &event
	};

	assert(eval.opts);
	assert(eval.event_);
	assert(eval.id);
	assert(eval.ctx);
	const auto &opts
	{
		*eval.opts
	};

	if(eval.copts && eval.copts->debuglog_precommit)
		log::debug
		{
			log, "Issuing: %s", pretty_oneline(event)
		};

	if(eval.copts && eval.copts->issue)
		issue_hook(event, eval);

	if(opts.conform)
		conform_hook(event, eval);

	// A conforming (with lots of masks) event without an event_id is an EDU.
	const fault ret
	{
		json::get<"event_id"_>(event)?
			_eval_pdu(eval, event):
			_eval_edu(eval, event)
	};

	if(ret != fault::ACCEPT)
		return ret;

	if(opts.notify)
		notify_hook(event, eval);

	if(opts.effects)
		effect_hook(event, eval);

	if(opts.debuglog_accept || bool(log_accept_debug))
		log::debug
		{
			log, "%s", pretty_oneline(event)
		};

	if(opts.infolog_accept || bool(log_accept_info))
		log::info
		{
			log, "%s", pretty_oneline(event)
		};

	return ret;
}
catch(const error &e) // VM FAULT CODE
{
	return handle_error
	(
		*eval.opts, e.code,
		"eval %s %s: %s %s :%s",
		json::get<"event_id"_>(event)?: json::string{"<edu>"},
		reflect(e.code),
		e.what(),
		unquote(json::object(e.content).get("errcode")),
		unquote(json::object(e.content).get("error"))
	);
}
catch(const m::error &e) // GENERAL MATRIX ERROR
{
	return handle_error
	(
		*eval.opts, fault::GENERAL,
		"eval %s #GP (General Protection): %s %s :%s",
		json::get<"event_id"_>(event)?: json::string{"<edu>"},
		e.what(),
		unquote(json::object(e.content).get("errcode")),
		unquote(json::object(e.content).get("error"))
	);
}
catch(const ctx::interrupted &e) // INTERRUPTION
{
	return handle_error
	(
		*eval.opts, fault::INTERRUPT,
		"eval %s #NMI: %s",
		json::get<"event_id"_>(event)?: json::string{"<edu>"},
		e.what()
	);
}
catch(const std::exception &e) // ALL OTHER ERRORS
{
	return handle_error
	(
		*eval.opts, fault::GENERAL,
		"eval %s #GP (General Protection): %s",
		json::get<"event_id"_>(event)?: json::string{"<edu>"},
		e.what()
	);
}

template<class... args>
ircd::m::vm::fault
ircd::m::vm::handle_error(const vm::opts &opts,
                          const vm::fault &code,
                          const string_view &fmt,
                          args&&... a)
{
	if(opts.errorlog & code)
		log::error
		{
			log, fmt, std::forward<args>(a)...
		};

	if(opts.warnlog & code)
		log::warning
		{
			log, fmt, std::forward<args>(a)...
		};

	if(~opts.nothrows & code)
		throw error
		{
			code, fmt, std::forward<args>(a)...
		};

	return code;
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_edu(eval &eval,
                       const event &event)
{
	if(eval.opts->eval)
		eval_hook(event, eval);

	return fault::ACCEPT;
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_pdu(eval &eval,
                       const event &event)
{
	assert(eval.opts);
	const auto &opts
	{
		*eval.opts
	};

	const m::event::id &event_id
	{
		at<"event_id"_>(event)
	};

	const m::room::id &room_id
	{
		at<"room_id"_>(event)
	};

	const string_view &type
	{
		at<"type"_>(event)
	};

	const bool already_exists
	{
		exists(event_id)
	};

	  //TODO: ABA
	if(already_exists && !opts.replays)
		throw error
		{
			fault::EXISTS, "Event has already been evaluated."
		};

	if(opts.verify)
		if(!verify(event))
			throw m::BAD_SIGNATURE
			{
				"Signature verification failed"
			};

	// Fetch dependencies
	if(opts.fetch)
		fetch_hook(event, eval);

	// Obtain sequence number here
	if(opts.write)
	{
		const auto *const &top(eval::seqmax());
		eval.sequence = top?
			std::max(sequence::get(*top) + 1, sequence::committed + 1):
			sequence::committed + 1;

		log::debug
		{
			log, "vm seq %lu[%lu]%lu|%lu|%lu:%lu | acquire",
			vm::sequence::max(),
			sequence::get(eval),
			vm::sequence::uncommitted,
			vm::sequence::committed,
			vm::sequence::retired,
			vm::sequence::min(),
		};

		assert(eval.sequence != 0);
		assert(sequence::uncommitted <= sequence::get(eval));
		assert(sequence::committed < sequence::get(eval));
		assert(sequence::retired < sequence::get(eval));
		assert(eval::sequnique(sequence::get(eval)));
		sequence::uncommitted = sequence::get(eval);
	}

	// Evaluation by module hooks
	if(opts.eval)
		eval_hook(event, eval);

	if(opts.write)
	{
		log::debug
		{
			log, "vm seq %lu:%lu[%lu]%lu|%lu:%lu | commit",
			vm::sequence::max(),
			vm::sequence::uncommitted,
			sequence::get(eval),
			vm::sequence::committed,
			vm::sequence::retired,
			vm::sequence::min(),
		};

		sequence::dock.wait([&eval]
		{
			return eval::seqnext(sequence::committed) == &eval;
		});

		assert(sequence::committed < sequence::get(eval));
		assert(sequence::retired < sequence::get(eval));
		sequence::committed = sequence::get(eval);
		sequence::dock.notify_all();

		log::debug
		{
			log, "vm seq %lu:%lu[%lu|%lu]%lu:%lu | write",
			vm::sequence::max(),
			vm::sequence::uncommitted,
			vm::sequence::committed,
			sequence::get(eval),
			vm::sequence::retired,
			vm::sequence::min(),
		};

		_write(eval, event);
	}

	// pre-notify effect hooks
	bool post_hook_complete{false};
	if(opts.post)
		pool([&event, &eval, &post_hook_complete]
		{
			const unwind notify{[&post_hook_complete]
			{
				post_hook_complete = true;
				sequence::dock.notify_all();
			}};

			post_hook(event, eval);
		});

	if(opts.write)
	{
		sequence::dock.wait([&eval]
		{
			return eval::seqnext(sequence::retired) == &eval;
		});

		assert(sequence::retired < sequence::get(eval));
		sequence::retired = sequence::get(eval);
		sequence::dock.notify_all();

		log::debug
		{
			log, "vm seq %lu:%lu|%lu[%lu|%lu]%lu | retire",
			vm::sequence::max(),
			vm::sequence::uncommitted,
			vm::sequence::committed,
			vm::sequence::retired,
			sequence::get(eval),
			vm::sequence::min(),
		};
	}

	if(opts.post)
	{
		sequence::dock.wait([&post_hook_complete]
		{
			return post_hook_complete;
		});

		log::debug
		{
			log, "vm seq %lu:%lu|%lu[%lu|%lu]%lu | accept",
			vm::sequence::max(),
			vm::sequence::uncommitted,
			vm::sequence::committed,
			vm::sequence::retired,
			sequence::get(eval),
			vm::sequence::min(),
		};
	}

	return fault::ACCEPT;
}

void
ircd::m::vm::_write(eval &eval,
                    const event &event)

{
	assert(eval.opts);
	const auto &opts
	{
		*eval.opts
	};

	const size_t reserve_bytes
	{
		opts.reserve_bytes == size_t(-1)?
			size_t(json::serialized(event) * 1.66):

		opts.reserve_bytes
	};

	db::txn txn
	{
		*dbs::events, db::txn::opts
		{
			reserve_bytes + opts.reserve_index,   // reserve_bytes
			0,                                    // max_bytes (no max)
		}
	};

	// Expose to eval interface
	const scope_restore eval_txn
	{
		eval.txn, &txn
	};

	// Preliminary write_opts
	m::dbs::write_opts wopts;
	m::state::id_buffer new_root_buf;
	wopts.root_out = new_root_buf;
	wopts.present = opts.present;
	wopts.history = opts.history;
	wopts.room_head = opts.room_head;
	wopts.room_refs = opts.room_refs;
	wopts.json_source = opts.json_source;
	wopts.event_idx = eval.sequence;

	if(at<"type"_>(event) == "m.room.create")
	{
		dbs::write(txn, event, wopts);
		_commit(eval);
		return;
	}

	const bool require_head
	{
		opts.head_must_exist || opts.history
	};

	const id::event::buf head
	{
		require_head?
			m::head(std::nothrow, at<"room_id"_>(event)):
			id::event::buf{}
	};

	if(unlikely(require_head && !head))
		throw error
		{
			fault::STATE, "Required head for room %s not found.",
			string_view{at<"room_id"_>(event)}
		};

	const m::room room
	{
		at<"room_id"_>(event), head
	};

	const m::room::state state
	{
		room
	};

	wopts.root_in = state.root_id;
	dbs::write(txn, event, wopts);
	_commit(eval);
}

void
ircd::m::vm::_commit(eval &eval)
{
	assert(eval.txn);
	auto &txn(*eval.txn);

	#ifdef RB_DEBUG
	const auto db_seq_before(db::sequence(*m::dbs::events));
	#endif

	txn();

	#ifdef RB_DEBUG
	const auto db_seq_after(db::sequence(*m::dbs::events));
	#endif

	if(log_commit_debug && !eval.opts->debuglog_accept)
		log::debug
		{
			log, "vm seq %lu:%lu|%lu[%lu]%lu:%lu | wrote | db seq %lu:%lu %zu cells in %zu bytes to events database ...",
			vm::sequence::max(),
			vm::sequence::uncommitted,
			vm::sequence::committed,
			sequence::get(eval),
			vm::sequence::retired,
			vm::sequence::min(),
			db_seq_before,
			db_seq_after,
			txn.size(),
			txn.bytes()
		};
}
