/*
 * charybdis: an advanced Internet Relay Chat Daemon(ircd).
 *
 * Copyright (C) 2003 Lee H <lee@leeh.co.uk>
 * Copyright (C) 2003-2005 ircd-ratbox development team
 * Copyright (C) 2008 William Pitcock <nenolod@sacredspiral.co.uk>
 * Copyright (C) 2016 Charybdis Development Team
 * Copyright (C) 2016 Jason Volk <jason@zemos.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

using namespace ircd;

namespace ircd {
namespace log  {

// Option toggles
std::array<bool, num_of<facility>()> file_flush;
std::array<bool, num_of<facility>()> console_flush;
std::array<const char *, num_of<facility>()> console_ansi;

// Runtime master switches
std::array<bool, num_of<facility>()> file_out;
std::array<bool, num_of<facility>()> console_out;
std::array<bool, num_of<facility>()> console_err;

// Suppression state (for struct console_quiet)
std::array<bool, num_of<facility>()> quieted_out;
std::array<bool, num_of<facility>()> quieted_err;

// Logfile name and device
std::array<const char *, num_of<facility>()> fname;
std::array<std::ofstream, num_of<facility>()> file;

/*
ConfEntry conf_log_table[] =
{
	{ "file_critical",  CF_QSTRING, NULL, PATH_MAX, &fname[CRITICAL]  },
	{ "file_error",     CF_QSTRING, NULL, PATH_MAX, &fname[ERROR]     },
	{ "file_warning",   CF_QSTRING, NULL, PATH_MAX, &fname[WARNING]   },
	{ "file_notice",    CF_QSTRING, NULL, PATH_MAX, &fname[NOTICE]    },
	{ "file_info",      CF_QSTRING, NULL, PATH_MAX, &fname[INFO]      },
	{ "file_debug",     CF_QSTRING, NULL, PATH_MAX, &fname[DEBUG]     },
};
*/

static void open(const facility &fac);
static void prefix(const facility &fac, const char *const &date);
static void suffix(const facility &fac);

} // namespace log
} // namespace ircd

void
log::init()
{
	//TODO: XXX: config + cmd control + other fancy stuff

	//add_top_conf("log", NULL, NULL, conf_log_table);

	console_err[CRITICAL]    = true;
	console_err[ERROR]       = true;
	console_err[WARNING]     = true;
	console_err[NOTICE]      = true;
	console_out[INFO]        = true;
	console_out[DEBUG]       = ircd::debugmode;

	file_out[CRITICAL]       = true;
	file_out[ERROR]          = true;
	file_out[WARNING]        = true;
	file_out[NOTICE]         = true;
	file_out[INFO]           = true;
	file_out[DEBUG]          = ircd::debugmode;

	file_flush[CRITICAL]     = true;
	file_flush[ERROR]        = true;
	file_flush[WARNING]      = true;
	file_flush[NOTICE]       = false;
	file_flush[INFO]         = false;
	file_flush[DEBUG]        = false;

	console_flush[CRITICAL]  = true;
	console_flush[ERROR]     = true;
	console_flush[WARNING]   = true;
	console_flush[NOTICE]    = false;
	console_flush[INFO]      = false;
	console_flush[DEBUG]     = true;

	console_ansi[CRITICAL]  = "\033[1;5;37;45m";
	console_ansi[ERROR]     = "\033[1;37;41m";
	console_ansi[WARNING]   = "\033[0;30;43m";
	console_ansi[NOTICE]    = "\033[1;37;46m";
	console_ansi[INFO]      = "\033[1;37;42m";
	console_ansi[DEBUG]     = "\033[1;30;47m";
}

void
log::fini()
{
	//remove_top_conf("log");
}

void
log::open()
{
	for_each<facility>([](const facility &fac)
	{
		if(!fname[fac])
			return;

		if(!file_out[fac])
			return;

		if(file[fac].is_open())
			file[fac].close();

		file[fac].clear();
		file[fac].exceptions(std::ios::badbit | std::ios::failbit);
		open(fac);
	});
}

void
log::close()
{
	for_each<facility>([](const facility &fac)
	{
		if(file[fac].is_open())
			file[fac].close();
	});
}

void
log::flush()
{
	for_each<facility>([](const facility &fac)
	{
		file[fac].flush();
	});
}

void
log::open(const facility &fac)
try
{
	const auto &mode(std::ios::app);
	file[fac].open(fname[fac], mode);
}
catch(const std::exception &e)
{
	char buf[BUFSIZE];
	snprintf(buf, sizeof(buf), "!!! Opening log file [%s] failed: %s",
	         fname[fac],
	         e.what());

	std::cerr << buf << std::endl;
	throw;
}

log::console_quiet::console_quiet(const bool &showmsg)
{
	if(showmsg)
		notice("Log messages are now quieted at the console");

	std::copy(begin(console_out), end(console_out), begin(quieted_out));
	std::copy(begin(console_err), end(console_err), begin(quieted_err));
	std::fill(begin(console_out), end(console_out), false);
	std::fill(begin(console_err), end(console_err), false);
}

log::console_quiet::~console_quiet()
{
	std::copy(begin(quieted_out), end(quieted_out), begin(console_out));
	std::copy(begin(quieted_err), end(quieted_err), begin(console_err));
}

log::log::log(const std::string &name)
:name{name}
{
}

log::log::log(const std::string &name,
              const char &snote)
:log{name}
{
}

void
log::mark(const char *const &msg)
{
	for_each<facility>([&msg]
	(const auto &fac)
	{
		mark(fac, msg);
	});
}

void
log::mark(const facility &fac,
          const char *const &msg)
{
	slog(fac, [&msg]
	(std::ostream &s)
	{
		s << "*** " << (msg? msg : "mark") << " ***";
	});
}

void
log::vlog(const facility &fac,
          const char *const &fmt,
          const va_rtti &ap)
{
	char buf[1024];
	fmt::vsnprintf(buf, sizeof(buf), fmt, ap);
	slog(fac, [&buf]
	(std::ostream &s)
	{
		static const auto name{"ircd"};
		s << std::left << std::setw(9) << name << std::right << " :" << buf;
	});
}

void
log::vlog(const facility &fac,
          const std::string &name,
          const char *const &fmt,
          const va_rtti &ap)
{
	char buf[1024];
	fmt::vsnprintf(buf, sizeof(buf), fmt, ap);
	slog(fac, [&buf, &name]
	(std::ostream &s)
	{
		s << std::left << std::setw(9) << name << std::right << " :" << buf;
	});
}

void
log::slog(const facility &fac,
          const std::function<void (std::ostream &)> &closure)
{
	if(!file[fac].is_open() && !console_out[fac] && !console_err[fac])
		return;

	const scope always_newline([&fac]
	{
		suffix(fac);
	});

	//TODO: XXX: Add option toggle for smalldate()
	char date[64];
	microtime(date, sizeof(date));
	prefix(fac, date);

	if(console_err[fac])
		closure(std::cerr);

	if(console_out[fac])
		closure(std::cout);

	if(file[fac].is_open())
		closure(file[fac]);
}

void
log::prefix(const facility &fac,
            const char *const &date)
{
	const auto console_prefix([&fac, &date]
	(auto &stream)
	{
		stream << date << ' ';

		if(console_ansi[fac])
			stream << console_ansi[fac];

		stream << std::setw(8) << std::right << reflect(fac);

		if(console_ansi[fac])
			stream << "\033[0m ";
		else
			stream << ' ';
	});

	if(console_err[fac])
		console_prefix(std::cerr);

	if(console_out[fac])
		console_prefix(std::cout);

	file[fac] << date << ' ' << reflect(fac) << ' ';
}

void
log::suffix(const facility &fac)
{
	const auto console_newline([&fac]
	(auto &stream)
	{
		if(console_flush[fac])
			stream << std::endl;
		else
			stream << '\n';
	});

	if(console_err[fac])
		console_newline(std::cerr);

	if(console_out[fac])
		console_newline(std::cout);

	// If the user's own code triggered an exception while streaming we still want to
	// provide a newline. But if the exception is an actual log file exception don't
	// touch the stream anymore.
	if(unlikely(std::current_exception() && !file[fac].good()))
		return;

	if(!file[fac].is_open())
		return;

	if(file_flush[fac])
		file[fac] << std::endl;
	else
		file[fac] << '\n';
}

const char *
log::reflect(const facility &f)
{
	switch(f)
	{
		case facility::DEBUG:      return "DEBUG";
		case facility::INFO:       return "INFO";
		case facility::NOTICE:     return "NOTICE";
		case facility::WARNING:    return "WARNING";
		case facility::ERROR:      return "ERROR";
		case facility::CRITICAL:   return "CRITICAL";
		case facility::_NUM_:      break; // Allows -Wswitch to remind developer to add reflection here
	};

	return "??????";
}


const char *
ircd::smalldate(const time_t &ltime)
{
	static const size_t MAX_DATE_STRING
	{
		32 // maximum string length for a date string (ircd_defs.h)
	};

	static char buf[MAX_DATE_STRING];
	struct tm *const lt(localtime(&ltime));
	snprintf(buf, sizeof(buf), "%d/%d/%d %02d.%02d",
	         lt->tm_year + 1900,
	         lt->tm_mon + 1,
	         lt->tm_mday,
	         lt->tm_hour,
	         lt->tm_min);

	return buf;
}
