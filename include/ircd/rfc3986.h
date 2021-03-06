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
#define HAVE_IRCD_RFC3986_H

/// Universal Resource Indicator (URI) grammars & tools
namespace ircd::rfc3986
{
	IRCD_EXCEPTION(ircd::error, error)
	IRCD_EXCEPTION(error, coding_error)
	IRCD_EXCEPTION(coding_error, encoding_error)
	IRCD_EXCEPTION(coding_error, decoding_error)

	struct parser;

	constexpr size_t HOSTNAME_MAX {rfc1035::LABEL_MAX};
	constexpr size_t DOMAIN_MAX {rfc1035::NAME_MAX};

	// urlencoding suite
	string_view encode(const mutable_buffer &, const string_view &url);
	string_view encode(const mutable_buffer &, const json::members &);
	string_view decode(const mutable_buffer &, const string_view &url);

	// extractor suite
	uint16_t port(const string_view &remote); // get portnum from valid remote
	string_view host(const string_view &remote); // get host without portnum
}

namespace ircd
{
	namespace url = rfc3986;
}

struct ircd::rfc3986::parser
{
	using it = const char *;
	using unused = boost::spirit::unused_type;

	template<class R = unused>
	using rule = boost::spirit::qi::rule<it, R, unused, unused, unused>;

	// note in all examples that portnums are always optional
	static const rule<uint16_t> port;

	static const rule<> ip4_octet;
	static const rule<> ip4_address;  // 1.2.3.4
	static const rule<> ip4_literal;  // 1.2.3.4
	static const rule<> ip4_remote;   // 1.2.3.4:12345

	static const rule<> ip6_char;
	static const rule<> ip6_h16;
	static const rule<> ip6_piece;
	static const rule<> ip6_ipiece;
	static const rule<> ip6_ls32;
	static const rule<> ip6_addr[9];
	static const rule<> ip6_address;  // ::1
	static const rule<> ip6_literal;  // [::1]
	static const rule<> ip6_remote;   // [::1]:12345

	static const rule<> ip_address;   // 1.2.3.4 | ::1
	static const rule<> ip_literal;   // 1.2.3.4 | [::1]
	static const rule<> ip_remote;    // 1.2.3.4:12345 | [::1]:12345

	static const rule<> hostname;     // foo
	static const rule<> domain;       // foo.com
	static const rule<> hostport;     // foo.bar.com:12345

	static const rule<> host;         // 1.2.3.4 | ::1 | foo.com
	static const rule<> host_literal; // 1.2.3.4 | [::1] | foo.com

	static const rule<> remote;       // 1.2.3.4:12345 | [::1]:12345 | foo.com:12345
};

// Validator suite
namespace ircd::rfc3986
{
	// Variable rule...
	void valid(const parser::rule<> &, const string_view &);
	bool valid(std::nothrow_t, const parser::rule<> &, const string_view &);

	// convenience: parser::hostname
	void valid_hostname(const string_view &);
	bool valid_hostname(std::nothrow_t, const string_view &);

	// convenience: parser::host
	void valid_host(const string_view &);
	bool valid_host(std::nothrow_t, const string_view &);

	// convenience: parser::domainname
	void valid_domain(const string_view &);
	bool valid_domain(std::nothrow_t, const string_view &);

	// convenience: parser::remote
	void valid_remote(const string_view &);
	bool valid_remote(std::nothrow_t, const string_view &);
}
