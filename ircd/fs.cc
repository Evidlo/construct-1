/*
 *  charybdis: A slightly useful ircd.
 *  ircd.c: Starts up and runs the ircd.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2008 ircd-ratbox development team
 *  Copyright (C) 2005-2013 charybdis development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */

#include <boost/filesystem.hpp>
#include <ircd/asio.h>

#ifdef IRCD_USE_AIO
	#include <RB_INC_AIO_H
#endif

namespace ircd::fs
{
	using namespace boost::filesystem;

	enum
	{
		NAME  = 0,
		PATH  = 1,
	};

	using ent = std::pair<std::string, std::string>;
	extern const std::array<ent, num_of<index>()> paths;
}

decltype(ircd::fs::paths)
ircd::fs::paths
{{
	{ "prefix",            DPATH         },
	{ "binary dir",        BINPATH       },
	{ "config",            ETCPATH       },
	{ "log",               LOGPATH       },
	{ "libexec dir",       PKGLIBEXECDIR },
	{ "modules",           MODPATH       },
	{ "ircd.conf",         CPATH         },
	{ "ircd binary",       SPATH         },
	{ "db",                DBPATH        },
}};

ircd::fs::init::init()
:aio{std::make_unique<fs::aio>()}
{
}

ircd::fs::init::~init()
noexcept
{
}

bool
ircd::fs::write(const std::string &path,
                const const_raw_buffer &buf)
{
	if(fs::exists(path))
		return false;

	return overwrite(path, buf);
}

bool
ircd::fs::overwrite(const string_view &path,
                    const const_raw_buffer &buf)
{
	return overwrite(std::string{path}, buf);
}

bool
ircd::fs::overwrite(const std::string &path,
                    const const_raw_buffer &buf)
{
	std::ofstream file{path, std::ios::trunc};
	file.write(reinterpret_cast<const char *>(data(buf)), size(buf));
	return true;
}

bool
ircd::fs::append(const std::string &path,
                 const const_raw_buffer &buf)
{
	std::ofstream file{path, std::ios::app};
	file.write(reinterpret_cast<const char *>(data(buf)), size(buf));
	return true;
}

std::string
ircd::fs::read(const std::string &path)
{
	std::ifstream file{path};
	std::noskipws(file);
	std::istream_iterator<char> b{file};
	std::istream_iterator<char> e{};
	return std::string{b, e};
}

ircd::string_view
ircd::fs::read(const string_view &path,
               const mutable_raw_buffer &buf)
{
	return ircd::fs::read(std::string{path}, buf);
}

ircd::string_view
ircd::fs::read(const std::string &path,
               const mutable_raw_buffer &buf)
{
	std::ifstream file{path};
	if(!file.good())
		return {};

	file.read(reinterpret_cast<char *>(data(buf)), size(buf));
	return { reinterpret_cast<const char *>(data(buf)), size_t(file.gcount()) };
}

void
ircd::fs::chdir(const std::string &path)
try
{
	fs::current_path(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::mkdir(const std::string &path)
try
{
	return fs::create_directory(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::string
ircd::fs::cwd()
try
{
	return fs::current_path().string();
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::vector<std::string>
ircd::fs::ls_recursive(const std::string &path)
try
{
	const fs::recursive_directory_iterator end;
	fs::recursive_directory_iterator it(path);

	std::vector<std::string> ret;
	std::transform(it, end, std::back_inserter(ret), []
	(const auto &ent)
	{
		return ent.path().string();
	});

	return ret;
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::vector<std::string>
ircd::fs::ls(const std::string &path)
try
{
	static const fs::directory_iterator end;
	fs::directory_iterator it(path);

	std::vector<std::string> ret;
	std::transform(it, end, std::back_inserter(ret), []
	(const auto &ent)
	{
		return ent.path().string();
	});

	return ret;
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

size_t
ircd::fs::size(const string_view &path)
{
	return ircd::fs::size(std::string{path});
}

size_t
ircd::fs::size(const std::string &path)
{
	return fs::file_size(path);
}

bool
ircd::fs::is_reg(const std::string &path)
try
{
	return fs::is_regular_file(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::is_dir(const std::string &path)
try
{
	return fs::is_directory(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::exists(const std::string &path)
try
{
	return boost::filesystem::exists(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::string
ircd::fs::make_path(const std::initializer_list<std::string> &list)
{
	fs::path ret;
	for(const auto &s : list)
		ret /= fs::path(s);

	return ret.string();
}

const char *
ircd::fs::get(index index)
noexcept try
{
	return std::get<PATH>(paths.at(index)).c_str();
}
catch(const std::out_of_range &e)
{
	return nullptr;
}

const char *
ircd::fs::name(index index)
noexcept try
{
	return std::get<NAME>(paths.at(index)).c_str();
}
catch(const std::out_of_range &e)
{
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
//
// linux aio
//

namespace ircd::fs
{
	ctx::ctx *waiter;
}

void
ircd::fs::notify()
{
	if(waiter)
		notify(*waiter);
}

#ifndef IRCD_USE_AIO

struct ircd::fs::aio
{
	aio() {}
	~aio() noexcept {}
};

#else

struct ircd::fs::aio
{
	aioinit init {0};

	std::string read(const std::string &path);

	aio();
	~aio() noexcept;
};

ircd::fs::aio::aio()
{
	init.aio_threads = 0;
	init.aio_num = 64;
	init.aio_idle_time = 0;
	aio_init(&init);

	auto test{[this]
	{
		const auto got{this->read("/etc/passwd")};
		std::cout << "got: " << got << std::endl;
	}};

	ircd::context
	{
		"aiotest", context::POST | context::DETACH, test
	};
}

ircd::fs::aio::~aio()
noexcept
{
}

/// Reads the file at path into a string; yields your ircd::ctx.
///
std::string
ircd::fs::aio::read(const std::string &path)
{
	const auto fd
	{
		syscall(::open, path.c_str(), O_CLOEXEC, O_RDONLY)
	};

	const unwind close{[&fd]
	{
		syscall(::close, fd);
	}};

	struct stat stat;
	syscall(::fstat, fd, &stat);
	const auto &size{stat.st_size};

	std::string ret(size, char{});
	const mutable_buffer buf
	{
		const_cast<char *>(ret.data()), ret.size()
	};

	struct aiocb cb {0};
	cb.aio_fildes = fd;
	cb.aio_offset = 0;
	cb.aio_buf = buffer::data(buf);
	cb.aio_nbytes = buffer::size(buf);
	cb.aio_reqprio = 0;
	cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	cb.aio_sigevent.sigev_signo = SIGIO;
	cb.aio_sigevent.sigev_value.sival_ptr = nullptr;

	syscall(::aio_read, &cb);

	waiter = ctx::current;
	int errval; do
	{
		ctx::wait();
	}
	while((errval = ::aio_error(&cb)) == EINPROGRESS);

	const ssize_t bytes
	{
		syscall(::aio_return, &cb)
	};

	assert(bytes >= 0);
	ret.resize(size_t(bytes));
	return ret;
}

#endif
