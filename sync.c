/*
 * mbsync - mailbox synchronizer
 * Copyright (C) 2000-2002 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2002-2004 Oswald Buddenhagen <ossi@users.sf.net>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * As a special exception, mbsync may be linked with the OpenSSL library,
 * despite that library's more restrictive license.
 */

#include "isync.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <glib-object.h>

const char *Home;	/* for config */
store_t *mctx;

static const char Flags[] = { 'D', 'F', 'R', 'S', 'T' };

static int
parse_flags( const char *buf )
{
	unsigned flags, i, d;

	for (flags = i = d = 0; i < as(Flags); i++)
		if (buf[d] == Flags[i]) {
			flags |= (1 << i);
			d++;
		}
	return flags;
}

static int
make_flags( int flags, char *buf )
{
	unsigned i, d;

	for (i = d = 0; i < as(Flags); i++)
		if (flags & (1 << i))
			buf[d++] = Flags[i];
	buf[d] = 0;
	return d;
}

static void
makeopts( int dops, store_conf_t *dconf, int *dopts,
          store_conf_t *sconf, int *sopts )
{
	if (dops & (OP_DELETE|OP_FLAGS)) {
		*dopts |= OPEN_SETFLAGS;
		*sopts |= OPEN_OLD;
		if (dops & OP_FLAGS)
			*sopts |= OPEN_FLAGS;
	}
	if (dops & (OP_NEW|OP_RENEW)) {
		*dopts |= OPEN_APPEND;
		if (dops & OP_RENEW)
			*sopts |= OPEN_OLD;
		if (dops & OP_NEW)
			*sopts |= OPEN_NEW;
		if (dops & OP_EXPUNGE)
			*sopts |= OPEN_FLAGS;
		if (dconf->max_size)
			*sopts |= OPEN_SIZE;
	}
	if (dops & OP_EXPUNGE) {
		*dopts |= OPEN_EXPUNGE;
		if (dconf->trash) {
			if (!dconf->trash_only_new)
				*dopts |= OPEN_OLD;
			*dopts |= OPEN_NEW|OPEN_FLAGS;
		} else if (sconf->trash && sconf->trash_remote_new)
			*dopts |= OPEN_NEW|OPEN_FLAGS;
	}
	if (dops & OP_CREATE)
		*dopts |= OPEN_CREATE;
}

static void
dump_box( store_t *ctx )
{
	message_t *msg;
	char fbuf[16]; /* enlarge when support for keywords is added */

	if (Debug)
		for (msg = ctx->msgs; msg; msg = msg->next) {
			make_flags( msg->flags, fbuf );
			printf( "  message %d, %s, %d\n", msg->uid, fbuf, msg->size );
		}
}

static message_t *
findmsg( store_t *ctx, int uid, message_t **nmsg, const char *who )
{
	message_t *msg;

	if (uid > 0) {
		if (*nmsg && (*nmsg)->uid == uid) {
			debug( " %s came in sequence\n", who );
			msg = *nmsg;
		  found:
			*nmsg = msg->next;
			if (!(msg->status & M_DEAD)) {
				msg->status |= M_PROCESSED;
				return msg;
			}
			debug( "  ... but it vanished under our feet!\n" );
		} else {
			for (msg = ctx->msgs; msg; msg = msg->next)
				if (msg->uid == uid) {
					debug( " %s came out of sequence\n", who );
					goto found;
				}
				debug( " %s not present\n", who );
		}
	} else
		debug( " no %s expected\n", who );
	return 0;
}

#define S_DEAD         (1<<0)
#define S_EXPIRED      (1<<1)
#define S_DEL_MASTER   (1<<2)
#define S_DEL_SLAVE    (1<<3)
#define S_EXP_SLAVE    (1<<4)

typedef struct sync_rec {
	struct sync_rec *next;
	/* string_list_t *keywords; */
	int muid, suid;
	unsigned char flags, status;
} sync_rec_t;


#define EX_OK           0
#define EX_FAIL         1
#define EX_STORE_BAD    2
#define EX_RSTORE_BAD   3

static int
expunge( store_t *ctx, store_t *rctx )
{
	driver_t *driver = ctx->conf->driver, *rdriver = rctx->conf->driver;
	message_t *msg;
	msg_data_t msgdata;

	for (msg = ctx->msgs; msg; msg = msg->next)
		if (msg->flags & F_DELETED) {
			if (ctx->conf->trash) {
				if (!ctx->conf->trash_only_new || (msg->status & M_NOT_SYNCED)) {
					debug( "  trashing message %d\n", msg->uid );
					switch (driver->trash_msg( ctx, msg )) {
					case DRV_STORE_BAD: return EX_STORE_BAD;
					default: return EX_FAIL;
					case DRV_OK: break;
					}
				} else
					debug( "  not trashing message %d - not new\n", msg->uid );
			} else if (rctx->conf->trash && rctx->conf->trash_remote_new) {
				if (msg->status & M_NOT_SYNCED) {
					if (!rctx->conf->max_size || msg->size <= rctx->conf->max_size) {
						debug( "  remote trashing message %d\n", msg->uid );
						msgdata.flags = msg->flags;
						switch (driver->fetch_msg( ctx, msg, &msgdata )) {
						case DRV_STORE_BAD: return EX_STORE_BAD;
						default: return EX_FAIL;
						case DRV_OK: break;
						}
						switch (rdriver->store_msg( rctx, &msgdata, 0 )) {
						case DRV_STORE_BAD: return EX_RSTORE_BAD;
						default: return EX_FAIL;
						case DRV_OK: break;
						}
					} else
						debug( "  not remote trashing message %d - too big\n", msg->uid );
				} else
					debug( "  not remote trashing message %d - not new\n", msg->uid );
			}
		}

	switch (driver->close( ctx )) {
	case DRV_STORE_BAD: return EX_STORE_BAD;
	default: return EX_FAIL;
	case DRV_OK: return EX_OK;;
	}
}


//if ((ret = sync_new( chan->mops, sctx, mctx, chan->master, jfp, &srecadd, 0, &smaxuid )) != SYNC_OK ||
//   (ret = sync_new( chan->sops, mctx, sctx, chan->slave, jfp, &srecadd, 1, &mmaxuid )) != SYNC_OK)
int
sms_imap_sync_one(const char *message)
{
    driver_t *tdriver = mctx->conf->driver;
    int  uid;

    msg_data_t msgdata;
    char *copy = malloc(strlen(message)+1);
    memcpy(copy,message,strlen(message));
    *(copy+strlen(message)) = 0;
    msgdata.data = copy;
    msgdata.len = strlen(copy);
    msgdata.flags =  0;
    msgdata.crlf = 0;

    switch (tdriver->store_msg( mctx, &msgdata, &uid )) {
        case DRV_STORE_BAD: return SYNC_SLAVE_BAD ;
        default: return SYNC_FAIL;
        case DRV_OK: break;
    }
    return SYNC_OK;
}

static char *
clean_strdup( const char *s )
{
	char *cs;
	int i;
	cs = nfstrdup( s );
	for (i = 0; cs[i]; i++)
		if (cs[i] == '/')
			cs[i] = '!';
	return cs;
}

int sms_imap_prepare()
{
	store_conf_t *mconf;
	driver_t *mdriver;
    int ret = 0;

	if (!(Home = getenv("HOME"))) {
		fputs( "Fatal: $HOME not set\n", stderr );
		return 1;
	}

	if (load_config( 0, 0 ))
		return 1;

    arc4_init();

    if(!stores || !stores->driver)
        return 1;

    mconf = stores;
    mdriver = mconf->driver;
    if (!(mctx = mdriver->open_store( mconf, mctx ))) {
        ret = 1;
        goto next;
    }

    info( "Channel %s\n", smsLabel);

	mctx->uidvalidity = 0;

	if (!smsLabel || (mctx->conf->map_inbox && !strcmp( mctx->conf->map_inbox, smsLabel )))
		smsLabel = "INBOX";
	mctx->name = smsLabel;
	mdriver->prepare( mctx, OPEN_SIZE | OPEN_CREATE | OPEN_FLAGS );

    /*
    switch (mdriver->select( mctx, 1, INT_MAX, 0, 0 )) {
        case DRV_STORE_BAD: ret = SYNC_SLAVE_BAD; goto next;
        case DRV_BOX_BAD: ret = SYNC_FAIL; goto next;
    }
	info( "%d messages, %d recent\n", mctx->count, mctx->recent );
	dump_box( mctx );
    */

    return ret;
next:
    if (mctx)
        mdriver->close_store( mctx );

    return ret;
}

void sms_imap_close()
{
    if (mctx)
        stores->driver->close_store( mctx );
}


