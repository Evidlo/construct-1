// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2019 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <RB_INC_SYS_SYSCALL_H
#include <RB_INC_SYS_IOCTL_H
#include <RB_INC_SYS_MMAN_H
#include <RB_INC_SYS_RESOURCE_H
#include <linux/perf_event.h>
#include <RB_INC_VALGRIND_CALLGRIND_H
#include <boost/chrono/chrono.hpp>
#include <boost/chrono/process_cpu_clocks.hpp>

namespace ircd::prof
{
	std::ostream &debug(std::ostream &, const ::perf_event_mmap_page &);

	using read_closure = std::function<void (const type &, const uint64_t &val)>;
	void for_each(const const_buffer &read, const read_closure &);

	template<class... args> event *
	create(group &,
	       const uint32_t &,
	       const uint64_t &,
	       args&&...);

	event &leader(group &);
	event *leader(group *const &);

	extern conf::item<bool> enable;
}

struct ircd::prof::event
:instance_list<event>
{
	perf_event_attr attr;
	fs::fd fd;
	uint64_t id {0};
	size_t map_size {0};
	char *map {nullptr};
	perf_event_mmap_page *head {nullptr};
	const_buffer body;

	uint64_t rdpmc() const;
	long ioctl(const ulong &req, const long &arg = 0);
	void reset(const long & = 0);
	void enable(const long & = 0);
	void disable(const long & = 0);

	event(const int &group,
	      const uint32_t &type,
	      const uint64_t &config,
	      const bool &user,
	      const bool &kernel);

	~event() noexcept;
};

decltype(ircd::prof::enable)
ircd::prof::enable
{
	{ "name",     "ircd.prof.enable" },
	{ "default",  false              },
	{ "persist",  false              },
};

//
// init
//

ircd::prof::init::init()
try
{
	if(!enable)
		return;

	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,          true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,          false,  true);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,         true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,         false,  true);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN,    true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN,    false,  true);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,    true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,    false,  true);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,   true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,   false,  true);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,     true,  false);
	create(system::group, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,     false,  true);
}
catch(const std::exception &e)
{
	log::error
	{
		"Profiling system initialization :%s",
		e.what()
	};

	this->~init();
	throw;
}

ircd::prof::init::~init()
noexcept
{
	system::group.clear();
}

//
// interface
//

void
ircd::prof::reset(group &group)
{
	assert(!group.empty());
	auto &leader(*group.front());
	leader.reset(PERF_IOC_FLAG_GROUP);
}

void
ircd::prof::start(group &group)
{
	assert(!group.empty());
	auto &leader(*group.front());
	leader.enable(PERF_IOC_FLAG_GROUP);
}

void
ircd::prof::stop(group &group)
{
	assert(!group.empty());
	auto &leader(*group.front());
	leader.disable(PERF_IOC_FLAG_GROUP);
}

ircd::prof::event &
ircd::prof::leader(group &group)
{
	assert(!group.empty());
	assert(group.front());
	return *group.front();
}

ircd::prof::event *
ircd::prof::leader(group *const &group)
{
	return group && !group->empty()?
		group->front().get():
		nullptr;
}

template<class... args>
ircd::prof::event *
ircd::prof::create(group &group,
                   const uint32_t &type,
                   const uint64_t &config,
                   args&&... a)
try
{
	const int gfd
	{
		leader(&group)? leader(group).fd : -1
	};

	group.emplace_back(std::make_unique<event>
	(
		gfd, type, config, std::forward<args>(a)...
	));

	return group.back().get();
}
catch(const std::exception &e)
{
	log::dwarning
	{
		"Failed to create event type:%u config:%lu :%s",
		type,
		config,
		e.what()
	};

	return nullptr;
}

//
// resource
//

ircd::prof::resource
ircd::prof::operator-(const resource &a,
                      const resource &b)
{
	resource ret;
	std::transform(begin(a), end(a), begin(b), begin(ret), std::minus<resource::value_type>{});
	return ret;
}

ircd::prof::resource
ircd::prof::operator+(const resource &a,
                      const resource &b)
{
	resource ret;
	std::transform(begin(a), end(a), begin(b), begin(ret), std::plus<resource::value_type>{});
	return ret;
}

ircd::prof::resource &
ircd::prof::operator-=(resource &a,
                       const resource &b)
{
	for(size_t i(0); i < a.size(); ++i)
		a[i] -= b[i];

	return a;
}

ircd::prof::resource &
ircd::prof::operator+=(resource &a,
                       const resource &b)
{
	for(size_t i(0); i < a.size(); ++i)
		a[i] += b[i];

	return a;
}

//
// resource::resource
//

ircd::prof::resource::resource(sample_t)
{
	struct ::rusage ru;
	syscall(::getrusage, RUSAGE_SELF, &ru);

	at(TIME_USER) = ru.ru_utime.tv_sec * 1000000UL + ru.ru_utime.tv_usec;
	at(TIME_KERN) = ru.ru_stime.tv_sec * 1000000UL + ru.ru_stime.tv_usec;
	at(RSS_MAX) = ru.ru_maxrss;
	at(PF_MINOR) = ru.ru_minflt;
	at(PF_MAJOR) = ru.ru_majflt;
	at(BLOCK_IN) = ru.ru_inblock;
	at(BLOCK_OUT) = ru.ru_oublock;
	at(SCHED_YIELD) = ru.ru_nvcsw;
	at(SCHED_PREEMPT) = ru.ru_nivcsw;
}

//
// system
//

decltype(ircd::prof::system::group)
ircd::prof::system::group;

ircd::prof::system
ircd::prof::operator-(const system &a,
                      const system &b)
{
	system ret(a);
	ret -= b;
	return ret;
}

ircd::prof::system
ircd::prof::operator+(const system &a,
                      const system &b)
{
	system ret(a);
	ret += b;
	return ret;
}

ircd::prof::system &
ircd::prof::operator-=(system &a,
                       const system &b)
{
	for(size_t i(0); i < a.size(); ++i)
		for(size_t j(0); j < a[i].size(); ++j)
			a[i][j] -= b[i][j];

	return a;
}

ircd::prof::system &
ircd::prof::operator+=(system &a,
                       const system &b)
{
	for(size_t i(0); i < a.size(); ++i)
		for(size_t j(0); j < a[i].size(); ++j)
			a[i][j] += b[i][j];

	return a;
}

ircd::prof::system &
ircd::prof::hotsample(system &s)
noexcept
{
	thread_local char buf[1024];

	auto &leader
	{
		prof::leader(system::group)
	};

	const const_buffer read
	{
		buf,  size_t(syscall(::read, int(leader.fd), buf, sizeof(buf)))
	};

	for_each(read, [&s]
	(const type &type, const uint64_t &val)
	{
		auto &r0
		{
			s.at(size_t(type.counter))
		};

		auto &r1
		{
			r0.at(size_t(type.dpl))
		};

		r1 = val;
	});

	return s;
}

void
ircd::prof::for_each(const const_buffer &buf,
                     const read_closure &closure)
{
	struct head
	{
		uint64_t nr, te, tr;
	}
	const *const &head
	{
		reinterpret_cast<const struct head *>(data(buf))
	};

	struct body
	{
		uint64_t val, id;
	}
	const *const &body
	{
		reinterpret_cast<const struct body *>(data(buf) + sizeof(struct head))
	};

	// Start with the pseudo-results under TIME_PROF type, these should
	// always be the same for non-hw profiling, so the DPL is meaningless.
	closure(type{dpl::KERNEL, counter::TIME_PROF}, head->te);
	closure(type{dpl::USER, counter::TIME_PROF}, head->tr);

	// Iterate the result list
	for(size_t i(0); i < head->nr; ++i)
		for(auto it(begin(event::list)); it != end(event::list); ++it)
			if((*it)->id == body[i].id)
				return closure(type(**it), body[i].val);
}

ircd::prof::system::system(sample_t)
noexcept
{
	stop(group);
	hotsample(*this);
	start(group);
}

//
// event
//

template<>
decltype(ircd::util::instance_list<ircd::prof::event>::allocator)
ircd::util::instance_list<ircd::prof::event>::allocator
{};

template<>
decltype(ircd::util::instance_list<ircd::prof::event>::list)
ircd::util::instance_list<ircd::prof::event>::list
{
	allocator
};

//
// event::event
//

ircd::prof::event::event(const int &group,
                         const uint32_t &type,
                         const uint64_t &config,
                         const bool &user,
                         const bool &kernel)
:attr{[&]
{
	struct ::perf_event_attr ret {0};
	ret.size = sizeof(ret);

	ret.type = type;
	ret.config = config;
	ret.exclude_user = !user;
	ret.exclude_kernel = !kernel;

	ret.read_format |= PERF_FORMAT_GROUP;
	ret.read_format |= PERF_FORMAT_ID;
	ret.read_format |= PERF_FORMAT_TOTAL_TIME_ENABLED;
	ret.read_format |= PERF_FORMAT_TOTAL_TIME_RUNNING;

	ret.exclude_idle = true;
	ret.exclude_host = false;
	ret.exclude_hv = true;
	ret.exclude_guest = true;
	ret.exclude_callchain_user = true;
	ret.exclude_callchain_kernel = true;

	ret.disabled = true;
	return ret;
}()}
,fd{[this, &group]
{
	ulong flags(0);
	flags |= PERF_FLAG_FD_CLOEXEC;

	const int cpu(-1);
	const pid_t pid(0);
	return int(syscall<SYS_perf_event_open>(&attr, pid, cpu, group, flags));
}()}
,id{[this]
{
	uint64_t ret;
	syscall(::ioctl, int(fd), PERF_EVENT_IOC_ID, &ret);
	return ret;
}()}
,map_size
{
	type == PERF_TYPE_HARDWARE?
		size_t(1UL + 0UL) * info::page_size:
		0UL
}
,map{[this]
{
	int prot(0);
	prot |= PROT_READ;
	prot |= PROT_WRITE;

	int flags(0);
	flags |= MAP_SHARED;

	void *const ret
	{
		map_size?
			::mmap(nullptr, map_size, prot, flags, int(this->fd), 0):
			nullptr
	};

	if(ret == (void *)-1)
		throw std::system_error
		{
			errno, std::system_category()
		};

	if(map_size && ret == nullptr)
		throw error
		{
			"mmap(2) failed on event (fd:%d)", int(fd)
		};

	return reinterpret_cast<char *>(ret);
}()}
,head
{
	map?
		reinterpret_cast<::perf_event_mmap_page *>(map):
		nullptr
}
,body
{
	head?
		map + head->data_offset:
		nullptr,
	head?
		head->data_size:
		0UL
}
{
	assert(size(body) % info::page_size == 0);
	assert(map_size % info::page_size == 0);
}

ircd::prof::event::~event()
noexcept
{
	assert(!map || map_size);
	assert(!map_size || map);

	if(map)
		syscall(::munmap, map, map_size);
}

void
ircd::prof::event::disable(const long &arg)
{
	ioctl(PERF_EVENT_IOC_DISABLE, arg);
}

void
ircd::prof::event::enable(const long &arg)
{
	ioctl(PERF_EVENT_IOC_ENABLE, arg);
}

void
ircd::prof::event::reset(const long &arg)
{
	ioctl(PERF_EVENT_IOC_RESET, arg);
}

long
ircd::prof::event::ioctl(const ulong &req,
                         const long &arg)
{
	return syscall(::ioctl, int(fd), req, arg);
}

uint64_t
ircd::prof::event::rdpmc()
const
{
	assert(head->cap_user_time);
	assert(head->cap_user_rdpmc);

	uint64_t ret;
	uint32_t seq; do
	{
		seq = head->lock;
		__sync_synchronize();

		assert(head->time_enabled == head->time_running);
		ret = head->offset;
		if(head->index)
			ret += x86::rdpmc(head->index - 1);

		__sync_synchronize();
	}
	while(head->lock != seq);

	return ret;
}

//
// type
//

ircd::prof::type::type(const enum dpl &dpl,
                       const enum counter &counter,
                       const enum cacheop &cacheop)
:dpl{dpl}
,counter{counter}
,cacheop{cacheop}
{
}

ircd::prof::type::type(const event &event)
:type{}
{
	this->dpl = event.attr.exclude_kernel? dpl::USER : dpl::KERNEL;
	if(event.attr.type == PERF_TYPE_SOFTWARE) switch(event.attr.config)
	{
		case PERF_COUNT_SW_CPU_CLOCK:
			this->counter = counter::TIME_CPU;
			break;

		case PERF_COUNT_SW_TASK_CLOCK:
			this->counter = counter::TIME_TASK;
			break;

		case PERF_COUNT_SW_PAGE_FAULTS_MIN:
			this->counter = counter::PF_MINOR;
			break;

		case PERF_COUNT_SW_PAGE_FAULTS_MAJ:
			this->counter = counter::PF_MAJOR;
			break;

		case PERF_COUNT_SW_CONTEXT_SWITCHES:
			this->counter = counter::SWITCH_TASK;
			break;

		case PERF_COUNT_SW_CPU_MIGRATIONS:
			this->counter = counter::SWITCH_CPU;
			break;

		default:
			break;
	}
	else if(event.attr.type == PERF_TYPE_HARDWARE) switch(event.attr.config)
	{
		case PERF_COUNT_HW_CPU_CYCLES:
			this->counter = counter::CYCLES;
			break;

		case PERF_COUNT_HW_INSTRUCTIONS:
			this->counter = counter::RETIRES;
			break;

		case PERF_COUNT_HW_CACHE_REFERENCES:
			this->counter = counter::CACHES;
			break;

		case PERF_COUNT_HW_CACHE_MISSES:
			this->counter = counter::CACHES_MISS;
			break;

		case PERF_COUNT_HW_BRANCH_INSTRUCTIONS:
			this->counter = counter::BRANCHES;
			break;

		case PERF_COUNT_HW_BRANCH_MISSES:
			this->counter = counter::BRANCHES_MISS;
			break;

		case PERF_COUNT_HW_STALLED_CYCLES_FRONTEND:
			this->counter = counter::STALLS_READ;
			break;

		case PERF_COUNT_HW_STALLED_CYCLES_BACKEND:
			this->counter = counter::STALLS_RETIRE;
			break;

		default:
			break;
	}
	else if(event.attr.type == PERF_TYPE_HW_CACHE)
	{
		const uint8_t counter(event.attr.config);
		const uint8_t op(event.attr.config >> 8);
		const uint8_t res(event.attr.config >> 16);
		switch(counter)
		{
			case PERF_COUNT_HW_CACHE_L1D:
				this->counter = counter::CACHE_L1D;
				break;

			case PERF_COUNT_HW_CACHE_L1I:
				this->counter = counter::CACHE_L1I;
				break;

			case PERF_COUNT_HW_CACHE_LL:
				this->counter = counter::CACHE_LL;
				break;

			case PERF_COUNT_HW_CACHE_DTLB:
				this->counter = counter::CACHE_TLBD;
				break;

			case PERF_COUNT_HW_CACHE_ITLB:
				this->counter = counter::CACHE_TLBI;
				break;

			case PERF_COUNT_HW_CACHE_BPU:
				this->counter = counter::CACHE_BPU;
				break;

			case PERF_COUNT_HW_CACHE_NODE:
				this->counter = counter::CACHE_NODE;
				break;
		}

		switch(op)
		{
			case PERF_COUNT_HW_CACHE_OP_READ:
				this->cacheop = res == PERF_COUNT_HW_CACHE_RESULT_ACCESS?
					cacheop::READ_ACCESS:
					cacheop::READ_MISS;
				break;

			case PERF_COUNT_HW_CACHE_OP_WRITE:
				this->cacheop = res == PERF_COUNT_HW_CACHE_RESULT_ACCESS?
					cacheop::WRITE_ACCESS:
					cacheop::WRITE_MISS;
				break;

			case PERF_COUNT_HW_CACHE_OP_PREFETCH:
				this->cacheop = res == PERF_COUNT_HW_CACHE_RESULT_ACCESS?
					cacheop::PREFETCH_ACCESS:
					cacheop::PREFETCH_MISS;
				break;
		}
	}
}

//
// internal
//

std::ostream &
ircd::prof::debug(std::ostream &s,
                  const ::perf_event_mmap_page &head)
{
	s << "version:               " << head.version              << std::endl;
	s << "compat:                " << head.compat_version       << std::endl;
	s << "lock:                  " << head.lock                 << std::endl;
	s << "index:                 " << head.index                << std::endl;
	s << "offset:                " << head.offset               << std::endl;
	s << "time_enabled:          " << head.time_enabled         << std::endl;
	s << "time_running:          " << head.time_running         << std::endl;
	s << "cap_user_rdpmc:        " << head.cap_user_rdpmc       << std::endl;
	s << "cap_user_time:         " << head.cap_user_time        << std::endl;
	s << "cap_user_time_zero:    " << head.cap_user_time_zero   << std::endl;
	s << "pmc_width:             " << head.pmc_width            << std::endl;
	s << "time_shift:            " << head.time_shift           << std::endl;
	s << "time_mult:             " << head.time_mult            << std::endl;
	s << "time_offset:           " << head.time_offset          << std::endl;
	s << "data_head:             " << head.data_head            << std::endl;
	s << "data_tail:             " << head.data_tail            << std::endl;
	s << "data_offset:           " << head.data_offset          << std::endl;
	s << "data_size:             " << head.data_size            << std::endl;
	s << "aux_head:              " << head.aux_head             << std::endl;
	s << "aux_tail:              " << head.aux_tail             << std::endl;
	s << "aux_offset:            " << head.aux_offset           << std::endl;
	s << "aux_size:              " << head.aux_size             << std::endl;

	return s;
}

//
// Interface
//

uint64_t
ircd::prof::time_thrd()
{
	struct ::timespec tv;
	syscall(::clock_gettime, CLOCK_THREAD_CPUTIME_ID, &tv);
	return ulong(tv.tv_sec) * 1000000000UL + tv.tv_nsec;
}

uint64_t
ircd::prof::time_proc()
{
	struct ::timespec tv;
	syscall(::clock_gettime, CLOCK_PROCESS_CPUTIME_ID, &tv);
	return ulong(tv.tv_sec) * 1000000000UL + tv.tv_nsec;
}

//
// Interface (cross-platform)
//

uint64_t
ircd::prof::time_real()
{
	return boost::chrono::process_real_cpu_clock::now().time_since_epoch().count();
}

uint64_t
ircd::prof::time_kern()
{
	return boost::chrono::process_system_cpu_clock::now().time_since_epoch().count();
}

uint64_t
ircd::prof::time_user()
{
	return boost::chrono::process_user_cpu_clock::now().time_since_epoch().count();
}

//
// prof::vg
//

void
ircd::prof::vg::stop()
noexcept
{
	#ifdef HAVE_VALGRIND_CALLGRIND_H
	CALLGRIND_STOP_INSTRUMENTATION;
	#endif
}

void
ircd::prof::vg::start()
noexcept
{
	#ifdef HAVE_VALGRIND_CALLGRIND_H
	CALLGRIND_START_INSTRUMENTATION;
	#endif
}

void
ircd::prof::vg::reset()
{
	#ifdef HAVE_VALGRIND_CALLGRIND_H
	CALLGRIND_ZERO_STATS;
	#endif
}

void
ircd::prof::vg::toggle()
{
	#ifdef HAVE_VALGRIND_CALLGRIND_H
	CALLGRIND_TOGGLE_COLLECT;
	#endif
}

void
ircd::prof::vg::dump(const char *const reason)
{
	#ifdef HAVE_VALGRIND_CALLGRIND_H
	CALLGRIND_DUMP_STATS_AT(reason);
	#endif
}

//
// times
//

ircd::prof::times::times(sample_t)
:real{}
,kern{}
,user{}
{
	const auto tp
	{
		boost::chrono::process_cpu_clock::now()
	};

	const auto d
	{
		tp.time_since_epoch()
	};

	this->real = d.count().real;
	this->kern = d.count().system;
	this->user = d.count().user;
}
