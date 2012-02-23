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

#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

store_conf_t *stores;
channel_conf_t *channels;

int
parse_bool( conffile_t *cfile )
{
	if (!strcasecmp( cfile->val, "yes" ) ||
	    !strcasecmp( cfile->val, "true" ) ||
	    !strcasecmp( cfile->val, "on" ) ||
	    !strcmp( cfile->val, "1" ))
		return 1;
	if (strcasecmp( cfile->val, "no" ) &&
	    strcasecmp( cfile->val, "false" ) &&
	    strcasecmp( cfile->val, "off" ) &&
	    strcmp( cfile->val, "0" ))
		fprintf( stderr, "%s:%d: invalid boolean value '%s'\n",
		         cfile->file, cfile->line, cfile->val );
	return 0;
}

int
parse_int( conffile_t *cfile )
{
	char *p;
	int ret;

	ret = strtol( cfile->val, &p, 10 );
	if (*p) {
		fprintf( stderr, "%s:%d: invalid integer value '%s'\n",
		         cfile->file, cfile->line, cfile->val );
		return 0;
	}
	return ret;
}

int
parse_size( conffile_t *cfile )
{
	char *p;
	int ret;

	ret = strtol (cfile->val, &p, 10);
	if (*p == 'k' || *p == 'K')
		ret *= 1024, p++;
	else if (*p == 'm' || *p == 'M')
		ret *= 1024 * 1024, p++;
	if (*p == 'b' || *p == 'B')
		p++;
	if (*p) {
		fprintf (stderr, "%s:%d: invalid size '%s'\n",
		         cfile->file, cfile->line, cfile->val);
		return 0;
	}
	return ret;
}

int
getcline( conffile_t *cfile )
{
	char *p;

	while (fgets( cfile->buf, cfile->bufl, cfile->fp )) {
		cfile->line++;
		p = cfile->buf;
		if (!(cfile->cmd = next_arg( &p )))
			return 1;
		if (*cfile->cmd == '#')
			continue;
		if (!(cfile->val = next_arg( &p ))) {
			fprintf( stderr, "%s:%d: parameter missing\n",
			         cfile->file, cfile->line );
			continue;
		}
		cfile->rest = p;
		return 1;
	}
	return 0;
}

/* XXX - this does not detect None conflicts ... */
int
merge_ops( int cops, int *mops, int *sops )
{
	int aops;

	aops = *mops | *sops;
	if (*mops & XOP_HAVE_TYPE) {
		if (aops & OP_MASK_TYPE) {
			if (aops & cops & OP_MASK_TYPE) {
			  cfl:
				fprintf( stderr, "Conflicting Sync args specified.\n" );
				return 1;
			}
			*mops |= cops & OP_MASK_TYPE;
			*sops |= cops & OP_MASK_TYPE;
			if (cops & XOP_PULL) {
				if (*sops & OP_MASK_TYPE)
					goto cfl;
				*sops |= OP_MASK_TYPE;
			}
			if (cops & XOP_PUSH) {
				if (*mops & OP_MASK_TYPE)
					goto cfl;
				*mops |= OP_MASK_TYPE;
			}
		} else if (cops & (OP_MASK_TYPE|XOP_MASK_DIR)) {
			if (!(cops & OP_MASK_TYPE))
				cops |= OP_MASK_TYPE;
			else if (!(cops & XOP_MASK_DIR))
				cops |= XOP_PULL|XOP_PUSH;
			if (cops & XOP_PULL)
				*sops |= cops & OP_MASK_TYPE;
			if (cops & XOP_PUSH)
				*mops |= cops & OP_MASK_TYPE;
		}
	}
	if (*mops & XOP_HAVE_EXPUNGE) {
		if (aops & cops & OP_EXPUNGE) {
			fprintf( stderr, "Conflicting Expunge args specified.\n" );
			return 1;
		}
		*mops |= cops & OP_EXPUNGE;
		*sops |= cops & OP_EXPUNGE;
	}
	if (*mops & XOP_HAVE_CREATE) {
		if (aops & cops & OP_CREATE) {
			fprintf( stderr, "Conflicting Create args specified.\n" );
			return 1;
		}
		*mops |= cops & OP_CREATE;
		*sops |= cops & OP_CREATE;
	}
	return 0;
}

int
load_config( const char *where, int pseudo )
{
	conffile_t cfile;
    store_conf_t *store, **storeapp = &stores;
    channel_conf_t *channel, **channelapp = &channels;
    int err;
    char path[_POSIX_PATH_MAX];
	char buf[1024];

	if (!where) {
		nfsnprintf( path, sizeof(path), "%s/." EXE "rc", Home );
		cfile.file = path;
	} else
		cfile.file = where;

	if (!pseudo)
		info( "Reading configuration file %s\n", cfile.file );

	if (!(cfile.fp = fopen( cfile.file, "r" ))) {
		perror( "Cannot open config file" );
		return 1;
	}
	buf[sizeof(buf) - 1] = 0;
	cfile.buf = buf;
	cfile.bufl = sizeof(buf) - 1;
	cfile.line = 0;

    err = 0;
    while (getcline( &cfile )) {
		if (!cfile.cmd)
			continue;
		if (imap_driver.parse_store( &cfile, &store, &err ))
		{
			if (store) {
				if (!store->path)
					store->path = "";
				*storeapp = store;
				storeapp = &store->next;
				*storeapp = 0;
			}
        }else if (!strcasecmp( "Channel", cfile.cmd ))
        {
            channel = nfcalloc( sizeof(*channel) );
            memset(channel,0,sizeof(channel));
            channel->name = nfstrdup( cfile.val );
            while (getcline( &cfile ) && cfile.cmd) {
                if (!strcasecmp( "Account", cfile.cmd ))
                    channel->account = nfstrdup(cfile.val);
                else if (!strcasecmp( "MailBox", cfile.cmd ))
                    channel->mail_box = nfstrdup(cfile.val);
                else if (!strcasecmp( "Label", cfile.cmd ))
                    channel->label = nfstrdup(cfile.val);
                else if (!strcasecmp( "Type", cfile.cmd ))
                    channel->type = nfstrdup(cfile.val);
            }

            if(!channel->name || !channel->account || !channel->mail_box || !channel->label || !channel->type )
            {
                fprintf( stderr, "channel '%s' setting error\n", channel->name );
                err = 1;
            }
            else {
                *channelapp = channel;
                channelapp = &channel->next;
            }
        }
	}
    fclose (cfile.fp);
	return err;
}

int
load_state_config( const char *where, int pseudo )
{
    conffile_t cfile;
    int err;
    char path[_POSIX_PATH_MAX];
    char buf[1024];
    char *sync_time;
    channel_conf_t *channel;

    if (!where) {
        nfsnprintf( path, sizeof(path), "%s/." EXE ".state", Home );
        cfile.file = path;
    } else
        cfile.file = where;

    if (!pseudo)
        info( "Reading configuration file %s\n", cfile.file );

    if (!(cfile.fp = fopen( cfile.file, "r" ))) {
        perror( "Cannot open config file" );
        return 1;
    }
    buf[sizeof(buf) - 1] = 0;
    cfile.buf = buf;
    cfile.bufl = sizeof(buf) - 1;
    cfile.line = 0;

    err = 0;
    memset(stores->prefrence,0,25);

    while (getcline( &cfile )) {
        if (!cfile.cmd)
            continue;
        if (!strcasecmp( "Preference", cfile.cmd ))
        {
            memcpy(stores->prefrence,cfile.val,24);
        }else if(!strcasecmp("Channel",cfile.cmd))
        {
            if(!(sync_time = next_arg(&cfile.rest)))
            {
                fprintf( stderr, "%s:%d: channel %s time missing\n",
                         cfile.file, cfile.line,cfile.val);
                continue;
            }
            for(channel=channels;channel;channel=channel->next)
            {
                if(!strcasecmp(channel->name, cfile.val))
                {
                    memcpy(channel->sync_time,sync_time,strlen(sync_time));
                    break;
                }
            }
        }
    }
    fclose (cfile.fp);
    return err;
}

int
save_state_config( const char *where, int pseudo )
{
    conffile_t cfile;
    int err;
    char path[_POSIX_PATH_MAX];
    char message[100];

    channel_conf_t *channel;

    if (!where) {
        nfsnprintf( path, sizeof(path), "%s/." EXE ".state", Home );
        cfile.file = path;
    } else
        cfile.file = where;

    if (!pseudo)
        info( "Save configuration file %s\n", cfile.file );

    if (!(cfile.fp = fopen( cfile.file, "w" ))) {
        perror( "Cannot open config file" );
        return 1;
    }
    memset(message,0,sizeof(message));
    sprintf(message,"Preference %s\n",stores->prefrence);
    fputs(message,cfile.fp);
    for(channel=channels;channel;channel=channel->next)
    {
        if(channel->sync_time && *channel->sync_time)
        {
            memset(message,0,sizeof(message));
            sprintf(message,"Channel %s %s\n",channel->name,channel->sync_time);
            fputs(message,cfile.fp);
        }
    }

    fclose (cfile.fp);
    return err;
}

void
parse_generic_store( store_conf_t *store, conffile_t *cfg, int *err )
{
	if (!strcasecmp( "Trash", cfg->cmd ))
		store->trash = nfstrdup( cfg->val );
	else if (!strcasecmp( "TrashRemoteNew", cfg->cmd ))
		store->trash_remote_new = parse_bool( cfg );
	else if (!strcasecmp( "TrashNewOnly", cfg->cmd ))
		store->trash_only_new = parse_bool( cfg );
	else if (!strcasecmp( "MaxSize", cfg->cmd ))
		store->max_size = parse_size( cfg );
	else if (!strcasecmp( "MapInbox", cfg->cmd ))
		store->map_inbox = nfstrdup( cfg->val );
	else {
		fprintf( stderr, "%s:%d: unknown keyword '%s'\n",
		         cfg->file, cfg->line, cfg->cmd );
		*err = 1;
	}
}
