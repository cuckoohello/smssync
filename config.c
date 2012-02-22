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
group_conf_t *groups;
int global_mops, global_sops;
char *global_sync_state;
char *smsLabel;
char prefrence[50];
char sync_date[50];

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

static int
getopt_helper( conffile_t *cfile, int *cops, int *mops, int *sops, char **sync_state )
{
	char *arg;

	if (!strcasecmp( "Sync", cfile->cmd )) {
		arg = cfile->val;
		do
			if (!strcasecmp( "Push", arg ))
				*cops |= XOP_PUSH;
			else if (!strcasecmp( "Pull", arg ))
				*cops |= XOP_PULL;
			else if (!strcasecmp( "ReNew", arg ))
				*cops |= OP_RENEW;
			else if (!strcasecmp( "New", arg ))
				*cops |= OP_NEW;
			else if (!strcasecmp( "Delete", arg ))
				*cops |= OP_DELETE;
			else if (!strcasecmp( "Flags", arg ))
				*cops |= OP_FLAGS;
			else if (!strcasecmp( "PullReNew", arg ))
				*sops |= OP_RENEW;
			else if (!strcasecmp( "PullNew", arg ))
				*sops |= OP_NEW;
			else if (!strcasecmp( "PullDelete", arg ))
				*sops |= OP_DELETE;
			else if (!strcasecmp( "PullFlags", arg ))
				*sops |= OP_FLAGS;
			else if (!strcasecmp( "PushReNew", arg ))
				*mops |= OP_RENEW;
			else if (!strcasecmp( "PushNew", arg ))
				*mops |= OP_NEW;
			else if (!strcasecmp( "PushDelete", arg ))
				*mops |= OP_DELETE;
			else if (!strcasecmp( "PushFlags", arg ))
				*mops |= OP_FLAGS;
			else if (!strcasecmp( "All", arg ) || !strcasecmp( "Full", arg ))
				*cops |= XOP_PULL|XOP_PUSH;
			else if (strcasecmp( "None", arg ) && strcasecmp( "Noop", arg ))
				fprintf( stderr, "%s:%d: invalid Sync arg '%s'\n",
				         cfile->file, cfile->line, arg );
		while ((arg = next_arg( &cfile->rest )));
		*mops |= XOP_HAVE_TYPE;
	} else if (!strcasecmp( "Expunge", cfile->cmd )) {
		arg = cfile->val;
		do
			if (!strcasecmp( "Both", arg ))
				*cops |= OP_EXPUNGE;
			else if (!strcasecmp( "Master", arg ))
				*mops |= OP_EXPUNGE;
			else if (!strcasecmp( "Slave", arg ))
				*sops |= OP_EXPUNGE;
			else if (strcasecmp( "None", arg ))
				fprintf( stderr, "%s:%d: invalid Expunge arg '%s'\n",
				         cfile->file, cfile->line, arg );
		while ((arg = next_arg( &cfile->rest )));
		*mops |= XOP_HAVE_EXPUNGE;
	} else if (!strcasecmp( "Create", cfile->cmd )) {
		arg = cfile->val;
		do
			if (!strcasecmp( "Both", arg ))
				*cops |= OP_CREATE;
			else if (!strcasecmp( "Master", arg ))
				*mops |= OP_CREATE;
			else if (!strcasecmp( "Slave", arg ))
				*sops |= OP_CREATE;
			else if (strcasecmp( "None", arg ))
				fprintf( stderr, "%s:%d: invalid Create arg '%s'\n",
				         cfile->file, cfile->line, arg );
		while ((arg = next_arg( &cfile->rest )));
		*mops |= XOP_HAVE_CREATE;
	} else if (!strcasecmp( "SyncState", cfile->cmd ))
		*sync_state = expand_strdup( cfile->val );
	else
		return 0;
	return 1;
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

int
mygetline( conffile_t *cfile )
{
    char *p;

    while (fgets( cfile->buf, cfile->bufl, cfile->fp )) {
        cfile->line++;
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
		}
		else if (!strcasecmp( "SMSLabel", cfile.cmd ))
		{
			smsLabel = nfstrdup( cfile.val );
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
    memset(prefrence,0,50);
    memset(sync_date,0,50);

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

    if(mygetline(&cfile))
    {
        memcpy(prefrence,cfile.buf,strlen(cfile.buf));
        prefrence[24] = 0;
    }else
        err = 2;
    if(mygetline(&cfile))
    {
        memcpy(sync_date,cfile.buf,strlen(cfile.buf));
    }else
        err = 3;
    fclose (cfile.fp);
    return err;
}

int
save_state_config( const char *where, int pseudo )
{
    conffile_t cfile;
    int err;
    char path[_POSIX_PATH_MAX];

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

    fputs(prefrence,cfile.fp);
    fputc('\n',cfile.fp);
    fputs(sync_date,cfile.fp);

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
