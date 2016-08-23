/* modules/m_monitor.c
 *
 *  Copyright (C) 2005 Lee Hardy <lee@leeh.co.uk>
 *  Copyright (C) 2005 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
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

static const char monitor_desc[] = "Provides the MONITOR facility for tracking user signon and signoff";

static int monitor_init(void);
static void monitor_deinit(void);
static void m_monitor(struct MsgBuf *, client::client &, client::client &, int, const char **);

struct Message monitor_msgtab = {
	"MONITOR", 0, 0, 0, 0,
	{mg_unreg, {m_monitor, 2}, mg_ignore, mg_ignore, mg_ignore, {m_monitor, 2}}
};

mapi_clist_av1 monitor_clist[] = { &monitor_msgtab, NULL };

DECLARE_MODULE_AV2(monitor, monitor_init, monitor_deinit, monitor_clist, NULL, NULL, NULL, NULL, monitor_desc);

static int monitor_init(void)
{
	add_isupport("MONITOR", isupport_intptr, &ConfigFileEntry.max_monitor);
	return 0;
}

static void monitor_deinit(void)
{
	delete_isupport("MONITOR");
}

static void
add_monitor(client::client &client, const char *nicks)
{
	char onbuf[BUFSIZE], offbuf[BUFSIZE];
	client::client *target_p;
	struct monitor *monptr;
	const char *name;
	char *tmp;
	char *p;
	char *onptr, *offptr;
	int mlen, arglen;
	int cur_onlen, cur_offlen;

	/* these two are same length, just diff numeric */
	cur_offlen = cur_onlen = mlen = sprintf(onbuf, form_str(RPL_MONONLINE),
						me.name, client.name, "");
	sprintf(offbuf, form_str(RPL_MONOFFLINE),
			me.name, client.name, "");

	onptr = onbuf + mlen;
	offptr = offbuf + mlen;

	tmp = LOCAL_COPY(nicks);

	for(name = rb_strtok_r(tmp, ",", &p); name; name = rb_strtok_r(NULL, ",", &p))
	{
		if(EmptyString(name) || strlen(name) > NICKLEN-1)
			continue;

		if(rb_dlink_list_length(&client.localClient->monitor_list) >=
			(unsigned long)ConfigFileEntry.max_monitor)
		{
			char buf[100];

			if(cur_onlen != mlen)
				sendto_one(&client, "%s", onbuf);
			if(cur_offlen != mlen)
				sendto_one(&client, "%s", offbuf);

			if(p)
				snprintf(buf, sizeof(buf), "%s,%s", name, p);
			else
				snprintf(buf, sizeof(buf), "%s", name);

			sendto_one(&client, form_str(ERR_MONLISTFULL),
					me.name, client.name,
					ConfigFileEntry.max_monitor, buf);
			return;
		}

		if (!client::clean_nick(name, 0))
			continue;

		monptr = find_monitor(name, 1);

		/* already monitoring this nick */
		if(rb_dlinkFind(&client, &monptr->users))
			continue;

		rb_dlinkAddAlloc(&client, &monptr->users);
		rb_dlinkAddAlloc(monptr, &client.localClient->monitor_list);

		if((target_p = client::find_named_person(name)) != NULL)
		{
			if(cur_onlen + strlen(target_p->name) +
			   strlen(target_p->username) + strlen(target_p->host) + 3 >= BUFSIZE-3)
			{
				sendto_one(&client, "%s", onbuf);
				cur_onlen = mlen;
				onptr = onbuf + mlen;
			}

			if(cur_onlen != mlen)
			{
				*onptr++ = ',';
				cur_onlen++;
			}

			arglen = sprintf(onptr, "%s!%s@%s",
					target_p->name, target_p->username,
					target_p->host);
			onptr += arglen;
			cur_onlen += arglen;
		}
		else
		{
			if(cur_offlen + strlen(name) + 1 >= BUFSIZE-3)
			{
				sendto_one(&client, "%s", offbuf);
				cur_offlen = mlen;
				offptr = offbuf + mlen;
			}

			if(cur_offlen != mlen)
			{
				*offptr++ = ',';
				cur_offlen++;
			}

			arglen = sprintf(offptr, "%s", name);
			offptr += arglen;
			cur_offlen += arglen;
		}
	}

	if(cur_onlen != mlen)
		sendto_one(&client, "%s", onbuf);
	if(cur_offlen != mlen)
		sendto_one(&client, "%s", offbuf);
}

static void
del_monitor(client::client &client, const char *nicks)
{
	struct monitor *monptr;
	const char *name;
	char *tmp;
	char *p;

	if(!rb_dlink_list_length(&client.localClient->monitor_list))
		return;

	tmp = LOCAL_COPY(nicks);

	for(name = rb_strtok_r(tmp, ",", &p); name; name = rb_strtok_r(NULL, ",", &p))
	{
		if(EmptyString(name))
			continue;

		/* not monitored */
		if((monptr = find_monitor(name, 0)) == NULL)
			continue;

		rb_dlinkFindDestroy(&client, &monptr->users);
		rb_dlinkFindDestroy(monptr, &client.localClient->monitor_list);

		free_monitor(monptr);
	}
}

static void
list_monitor(client::client &client)
{
	char buf[BUFSIZE];
	struct monitor *monptr;
	char *nbuf;
	rb_dlink_node *ptr;
	int mlen, arglen, cur_len;

	if(!rb_dlink_list_length(&client.localClient->monitor_list))
	{
		sendto_one(&client, form_str(RPL_ENDOFMONLIST),
				me.name, client.name);
		return;
	}

	cur_len = mlen = sprintf(buf, form_str(RPL_MONLIST),
				me.name, client.name, "");
	nbuf = buf + mlen;

	RB_DLINK_FOREACH(ptr, client.localClient->monitor_list.head)
	{
		monptr = (monitor *)ptr->data;

		if(cur_len + strlen(monptr->name) + 1 >= BUFSIZE-3)
		{
			sendto_one(&client, "%s", buf);
			nbuf = buf + mlen;
			cur_len = mlen;
		}

		if(cur_len != mlen)
		{
			*nbuf++ = ',';
			cur_len++;
		}

		arglen = sprintf(nbuf, "%s", monptr->name);
		cur_len += arglen;
		nbuf += arglen;
	}

	sendto_one(&client, "%s", buf);
	sendto_one(&client, form_str(RPL_ENDOFMONLIST),
			me.name, client.name);
}

static void
show_monitor_status(client::client &client)
{
	char onbuf[BUFSIZE], offbuf[BUFSIZE];
	client::client *target_p;
	struct monitor *monptr;
	char *onptr, *offptr;
	int cur_onlen, cur_offlen;
	int mlen, arglen;
	rb_dlink_node *ptr;

	mlen = cur_onlen = sprintf(onbuf, form_str(RPL_MONONLINE),
					me.name, client.name, "");
	cur_offlen = sprintf(offbuf, form_str(RPL_MONOFFLINE),
				me.name, client.name, "");

	onptr = onbuf + mlen;
	offptr = offbuf + mlen;

	RB_DLINK_FOREACH(ptr, client.localClient->monitor_list.head)
	{
		monptr = (monitor *)ptr->data;

		if((target_p = client::find_named_person(monptr->name)) != NULL)
		{
			if(cur_onlen + strlen(target_p->name) +
			   strlen(target_p->username) + strlen(target_p->host) + 3 >= BUFSIZE-3)
			{
				sendto_one(&client, "%s", onbuf);
				cur_onlen = mlen;
				onptr = onbuf + mlen;
			}

			if(cur_onlen != mlen)
			{
				*onptr++ = ',';
				cur_onlen++;
			}

			arglen = sprintf(onptr, "%s!%s@%s",
					target_p->name, target_p->username,
					target_p->host);
			onptr += arglen;
			cur_onlen += arglen;
		}
		else
		{
			if(cur_offlen + strlen(monptr->name) + 1 >= BUFSIZE-3)
			{
				sendto_one(&client, "%s", offbuf);
				cur_offlen = mlen;
				offptr = offbuf + mlen;
			}

			if(cur_offlen != mlen)
			{
				*offptr++ = ',';
				cur_offlen++;
			}

			arglen = sprintf(offptr, "%s", monptr->name);
			offptr += arglen;
			cur_offlen += arglen;
		}
	}

	if(cur_onlen != mlen)
		sendto_one(&client, "%s", onbuf);
	if(cur_offlen != mlen)
		sendto_one(&client, "%s", offbuf);
}

static void
m_monitor(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	switch(parv[1][0])
	{
		case '+':
			if(parc < 3 || EmptyString(parv[2]))
			{
				sendto_one(&client, form_str(ERR_NEEDMOREPARAMS),
						me.name, source.name, "MONITOR");
				return;
			}

			add_monitor(source, parv[2]);
			break;
		case '-':
			if(parc < 3 || EmptyString(parv[2]))
			{
				sendto_one(&client, form_str(ERR_NEEDMOREPARAMS),
						me.name, source.name, "MONITOR");
				return;
			}

			del_monitor(source, parv[2]);
			break;

		case 'C':
		case 'c':
			clear_monitor(&source);
			break;

		case 'L':
		case 'l':
			list_monitor(source);
			break;

		case 'S':
		case 's':
			show_monitor_status(source);
			break;

		default:
			break;
	}
}
