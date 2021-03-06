// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <ircd/asio.h>

/// Boost version indicator for compiled header files.
decltype(ircd::boost_version)
ircd::boost_version
{
	BOOST_VERSION / 100000,
	BOOST_VERSION / 100 % 1000,
	BOOST_VERSION % 100,
};

char ircd_boost_version_str_buf[32];
decltype(ircd::boost_version_str)
ircd::boost_version_str
(
	ircd_boost_version_str_buf,
	::snprintf(ircd_boost_version_str_buf, sizeof(ircd_boost_version_str_buf),
	           "%u.%u.%u",
	           boost_version[0],
	           boost_version[1],
	           boost_version[2])
);

/// Record of the ID of the thread static initialization took place on.
decltype(ircd::ios::static_thread_id)
ircd::ios::static_thread_id
{
    std::this_thread::get_id()
};

/// "main" thread for IRCd; the one the main context landed on.
decltype(ircd::ios::main_thread_id)
ircd::ios::main_thread_id;

decltype(ircd::ios::user)
ircd::ios::user;

void
ircd::ios::init(asio::io_context &user)
{
	// Sample the ID of this thread. Since this is the first transfer of
	// control to libircd after static initialization we have nothing to
	// consider a main thread yet. We need something set for many assertions
	// to pass until ircd::main() is entered which will reset this to where
	// ios.run() is really running.
	main_thread_id = std::this_thread::get_id();

	// Set a reference to the user's ios_service
	ios::user = &user;
}

ircd::ios::post::post(std::function<void ()> function)
{
	static descriptor descriptor
	{
		"ircd::ios post"
	};

	post(descriptor, std::move(function));
}

ircd::ios::defer::defer(std::function<void ()> function)
{
	static descriptor descriptor
	{
		"ircd::ios defer"
	};

	defer(descriptor, std::move(function));
}

ircd::ios::dispatch::dispatch(std::function<void ()> function)
{
	static descriptor descriptor
	{
		"ircd::ios dispatch"
	};

	dispatch(descriptor, std::move(function));
}

ircd::ios::dispatch::dispatch(descriptor &descriptor,
                              std::function<void ()> function)
{
	boost::asio::dispatch(get(), handle(descriptor, std::move(function)));
}

ircd::ios::defer::defer(descriptor &descriptor,
                        std::function<void ()> function)
{
	boost::asio::defer(get(), handle(descriptor, std::move(function)));
}

ircd::ios::post::post(descriptor &descriptor,
                      std::function<void ()> function)
{
	boost::asio::post(get(), handle(descriptor, std::move(function)));
}

boost::asio::io_context &
ircd::ios::get()
{
	assert(user);
	return *user;
}

bool
ircd::ios::available()
{
	return bool(user);
}

//
// descriptor
//

template<>
decltype(ircd::util::instance_list<ircd::ios::descriptor>::allocator)
ircd::util::instance_list<ircd::ios::descriptor>::allocator
{};

template<>
decltype(ircd::util::instance_list<ircd::ios::descriptor>::list)
ircd::util::instance_list<ircd::ios::descriptor>::list
{
	allocator
};

decltype(ircd::ios::descriptor::ids)
ircd::ios::descriptor::ids;

//
// descriptor::descriptor
//

ircd::ios::descriptor::descriptor(const string_view &name,
                                  const decltype(allocator) &allocator,
                                  const decltype(deallocator) &deallocator,
                                  const bool &continuation)
:name{name}
,stats{std::make_unique<struct stats>()}
,allocator{allocator}
,deallocator{deallocator}
,continuation{continuation}
{
	assert(allocator);
	assert(deallocator);
}

ircd::ios::descriptor::~descriptor()
noexcept
{
}

void
ircd::ios::descriptor::default_deallocator(handler &handler,
                                           void *const &ptr,
                                           const size_t &size)
{
	::operator delete(ptr, size);
}

void *
ircd::ios::descriptor::default_allocator(handler &handler,
                                         const size_t &size)
{
	return ::operator new(size);
}

//
// descriptor::stats
//

ircd::ios::descriptor::stats::stats()
{
}

ircd::ios::descriptor::stats::~stats()
noexcept
{
}

struct ircd::ios::descriptor::stats &
ircd::ios::descriptor::stats::operator+=(const stats &o)
&
{
	queued += o.queued;
	calls += o.calls;
	faults += o.faults;
	allocs += o.allocs;
	alloc_bytes += o.alloc_bytes;
	frees += o.frees;
	free_bytes += o.free_bytes;
	slice_total += o.slice_total;
	slice_last += o.slice_last;
	return *this;
}

//
// handler
//

decltype(ircd::ios::handler::current) thread_local
ircd::ios::handler::current;

bool
ircd::ios::handler::fault(handler *const &handler)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);

	assert(descriptor.stats);
	auto &stats(*descriptor.stats);
	++stats.faults;

	bool ret(false);
	// leave() isn't called if we return false so the tsc counter
	// needs to be tied off here instead.
	if(!ret)
	{
		stats.slice_last = cycles() - handler->slice_start;
		stats.slice_total += stats.slice_last;

		assert(handler::current == handler);
		handler::current = nullptr;
	}

	return ret;
}

void
ircd::ios::handler::leave(handler *const &handler)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);

	assert(descriptor.stats);
	auto &stats(*descriptor.stats);
	stats.slice_last = cycles() - handler->slice_start;
	stats.slice_total += stats.slice_last;

	assert(handler::current == handler);
	handler::current = nullptr;
}

void
ircd::ios::handler::enter(handler *const &handler)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);

	assert(descriptor.stats);
	auto &stats(*descriptor.stats);
	++stats.calls;

	assert(!handler::current);
	handler::current = handler;
	handler->slice_start = cycles();
}

bool
ircd::ios::handler::continuation(handler *const &handler)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);
	return descriptor.continuation;
}

void
ircd::ios::handler::deallocate(handler *const &handler,
                               void *const &ptr,
                               const size_t &size)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);

	descriptor.deallocator(*handler, ptr, size);

	assert(descriptor.stats);
	auto &stats(*descriptor.stats);
	stats.free_bytes += size;
	++stats.frees;
}

void *
ircd::ios::handler::allocate(handler *const &handler,
                             const size_t &size)
{
	assert(handler && handler->descriptor);
	auto &descriptor(*handler->descriptor);

	assert(descriptor.stats);
	auto &stats(*descriptor.stats);
	stats.alloc_bytes += size;
	++stats.allocs;

	return descriptor.allocator(*handler, size);
}
