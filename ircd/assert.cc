// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2019 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#if !defined(NDEBUG) && defined(RB_ASSERT)
#include <RB_INC_SIGNAL_H
#endif

#if !defined(NDEBUG) && defined(RB_ASSERT)
void
__assert_fail(const char *__assertion,
              const char *__file,
              unsigned int __line,
              const char *__function)
{
	if(strcmp(__assertion, "critical") != 0)
		fprintf(stderr, "\nassertion failed [%s +%u] %s :%s\n",
		        __file,
		        __line,
		        __function,
		        __assertion);

	if(strcmp(RB_ASSERT, "quit") == 0)
		ircd::quit();

	#if defined(__x86_64__)
	else if(strcmp(RB_ASSERT, "trap") == 0)
		__asm__ volatile ("int $3");
	#endif

	#if defined(HAVE_EXCEPTION)
	else if(strcmp(RB_ASSERT, "term") == 0)
	{
		std::terminate();
		__builtin_unreachable();
	}
	#endif

	#if defined(HAVE_CSTDLIB)
	else if(strcmp(RB_ASSERT, "abort") == 0)
	{
		abort();
		__builtin_unreachable();
	}
	#endif

	#if defined(HAVE_SIGNAL_H)
	else if(strcmp(RB_ASSERT, "SIGTRAP") == 0)
		raise(SIGTRAP);
	#endif

	#if defined(HAVE_SIGNAL_H)
	else if(strcmp(RB_ASSERT, "SIGQUIT") == 0)
		raise(SIGSTOP);
	#endif

	#if defined(HAVE_SIGNAL_H)
	else if(strcmp(RB_ASSERT, "SIGQUIT") == 0)
		raise(SIGQUIT);
	#endif

	else __builtin_trap();
}
#endif

#if !defined(NDEBUG) && defined(RB_ASSERT)
void
__assert_perror_fail(int __errnum,
                     const char *__file,
                     unsigned int __line,
                     const char *__function)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "perror #%d: ", __errnum);
	__assert_fail(buf, __file, __line, __function);
}
#endif

#if !defined(NDEBUG) && defined(RB_ASSERT)
void
__assert(const char *__assertion,
         const char *__file,
         int __line)
{
	__assert_fail(__assertion, __file, __line, "<no function>");
}
#endif
