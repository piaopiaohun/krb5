/*
 * tests/create/kdb5_mkdums.c
 *
 * Copyright 1990,1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 * Edit a KDC database.
 */

#include <krb5/copyright.h>
#include <krb5/krb5.h>
#include <krb5/kdb.h>
#include <krb5/kdb_dbm.h>
#include <krb5/los-proto.h>
#include <krb5/asn1.h>
#include <krb5/config.h>
#include <krb5/sysincl.h>		/* for MAXPATHLEN */
#include <krb5/ext-proto.h>

#include <com_err.h>
#include <ss/ss.h>
#include <stdio.h>


#define REALM_SEP	'@'
#define REALM_SEP_STR	"@"

struct mblock {
    krb5_deltat max_life;
    krb5_deltat max_rlife;
    krb5_timestamp expiration;
    krb5_flags flags;
    krb5_kvno mkvno;
} mblock = {				/* XXX */
    KRB5_KDB_MAX_LIFE,
    KRB5_KDB_MAX_RLIFE,
    KRB5_KDB_EXPIRATION,
    KRB5_KDB_DEF_FLAGS,
    1
};

int set_dbname_help PROTOTYPE((char *, char *));

static void
usage(who, status)
char *who;
int status;
{
    fprintf(stderr,
	    "usage: %s -p prefix -n num_to_create [-d dbpathname] [-r realmname]\n",
	    who);
    fprintf(stderr, "\t [-D depth] [-k keytype] [-e etype] [-M mkeyname]\n");

    exit(status);
}

krb5_keyblock master_keyblock;
krb5_principal master_princ;
krb5_db_entry master_entry;
krb5_encrypt_block master_encblock;
krb5_pointer master_random;
krb5_context test_context;

static char *progname;
static char *cur_realm = 0;
static char *mkey_name = 0;
static char *mkey_password = 0;
static krb5_boolean manual_mkey = FALSE;
static krb5_boolean dbactive = FALSE;

void
quit()
{
    krb5_error_code retval = krb5_db_fini(test_context);
    memset((char *)master_keyblock.contents, 0, master_keyblock.length);
    if (retval) {
	com_err(progname, retval, "while closing database");
	exit(1);
    }
    exit(0);
}

void add_princ PROTOTYPE((krb5_context, char *));

void
main(argc, argv)
int argc;
char *argv[];
{
    extern char *optarg;	
    int optchar, i, n;
    char tmp[4096], tmp2[BUFSIZ], *str_newprinc;

    krb5_error_code retval;
    char *dbname = 0;
    int keytypedone = 0;
    krb5_enctype etype = 0xffff;
    register krb5_cryptosystem_entry *csentry;
    extern krb5_kt_ops krb5_ktf_writable_ops;
    int num_to_create;
    char principal_string[BUFSIZ];
    char *suffix = 0;
    int depth;

    krb5_init_context(&test_context);
    krb5_init_ets(test_context);

    if (strrchr(argv[0], '/'))
	argv[0] = strrchr(argv[0], '/')+1;

    progname = argv[0];

    memset(principal_string, 0, sizeof(principal_string));
    num_to_create = 0;
    depth = 1;

    while ((optchar = getopt(argc, argv, "D:P:p:n:d:r:k:M:e:m")) != EOF) {
	switch(optchar) {
	case 'D':
	    depth = atoi(optarg);       /* how deep to go */
	    break;
        case 'P':		/* Only used for testing!!! */
	    mkey_password = optarg;
	    break;
	case 'p':                       /* prefix name to create */
	    strcpy(principal_string, optarg);
	    suffix = principal_string + strlen(principal_string);
	    break;
	case 'n':                        /* how many to create */
	    num_to_create = atoi(optarg);
	    break;
	case 'd':			/* set db name */
	    dbname = optarg;
	    break;
	case 'r':
	    cur_realm = optarg;
	    break;
	case 'k':
	    master_keyblock.keytype = atoi(optarg);
	    keytypedone++;
	    break;
	case 'M':			/* master key name in DB */
	    mkey_name = optarg;
	    break;
	case 'e':
	    etype = atoi(optarg);
	    break;
	case 'm':
	    manual_mkey = TRUE;
	    break;
	case '?':
	default:
	    usage(progname, 1);
	    /*NOTREACHED*/
	}
    }

    if (!(num_to_create && suffix)) usage(progname, 1);


    if (retval = krb5_kt_register(test_context, &krb5_ktf_writable_ops)) {
	com_err(progname, retval,
		"while registering writable key table functions");
	exit(1);
    }

    if (!keytypedone)
	master_keyblock.keytype = DEFAULT_KDC_KEYTYPE;

    if (!valid_keytype(master_keyblock.keytype)) {
	com_err(progname, KRB5_PROG_KEYTYPE_NOSUPP,
		"while setting up keytype %d", master_keyblock.keytype);
	exit(1);
    }

    if (etype == 0xffff)
	etype = krb5_keytype_array[master_keyblock.keytype]->system->proto_enctype;

    if (!valid_etype(etype)) {
	com_err(progname, KRB5_PROG_ETYPE_NOSUPP,
		"while setting up etype %d", etype);
	exit(1);
    }
    krb5_use_cstype(test_context, &master_encblock, etype);
    csentry = master_encblock.crypto_entry;

    if (!dbname)
	dbname = DEFAULT_KDB_FILE;	/* XXX? */

    if (!cur_realm) {
	if (retval = krb5_get_default_realm(test_context, &cur_realm)) {
	    com_err(progname, retval, "while retrieving default realm name");
	    exit(1);
	}	    
    }
    if (retval = set_dbname_help(progname, dbname))
	exit(retval);

    for (n = 1; n <= num_to_create; n++) {
      /* build the new principal name */
      /* we can't pick random names because we need to generate all the names 
	 again given a prefix and count to test the db lib and kdb */
      (void) sprintf(suffix, "%d", n);
      (void) sprintf(tmp, "%s-DEPTH-1", principal_string);
      str_newprinc = tmp;
      add_princ(test_context, str_newprinc);

      for (i = 2; i <= depth; i++) {
	tmp2[0] = '\0';
	(void) sprintf(tmp2, "/%s-DEPTH-%d", principal_string, i);
	strcat(tmp, tmp2);
	str_newprinc = tmp;
	add_princ(test_context, str_newprinc);
      }
    }

    (void) (*csentry->finish_key)(&master_encblock);
    (void) (*csentry->finish_random_key)(&master_random);
    retval = krb5_db_fini(test_context);
    memset((char *)master_keyblock.contents, 0, master_keyblock.length);
    if (retval && retval != KRB5_KDB_DBNOTINITED) {
	com_err(progname, retval, "while closing database");
	exit(1);
    }
    exit(0);
}

void
add_princ(context, str_newprinc)
    krb5_context context;
    char * str_newprinc;
{
    krb5_error_code retval;
    krb5_db_entry newentry;
    int one = 1;
    krb5_keyblock key;
    krb5_data pwd, salt;
    krb5_principal newprinc;
    char princ_name[4096];

    sprintf(princ_name, "%s@%s", str_newprinc, cur_realm);
    
    if (retval = krb5_parse_name(context, princ_name, &newprinc)) {
      com_err(progname, retval, "while parsing '%s'", princ_name);
      return;
    }

    pwd.data = princ_name;  /* must be able to regenerate */
    pwd.length = strlen(princ_name);

    if (retval = krb5_principal2salt(context, newprinc, &salt)) {
	com_err(progname, retval, "while converting principal to salt for '%s'", princ_name);
	return;
    }

    retval = krb5_string_to_key(context, &master_encblock, 
				master_keyblock.keytype, &key, &pwd, &salt);
    if (retval) {
	com_err(progname, retval, "while converting password to key for '%s'", princ_name);
	return;
    }

    retval = krb5_kdb_encrypt_key(context, &master_encblock, &key,
				  &newentry.key);
    if (retval) {
	com_err(progname, retval, "while encrypting key for '%s'", princ_name);
	return;
    }

    free(key.contents);

    newentry.principal = newprinc;
    newentry.kvno = 1;
    newentry.max_life = mblock.max_life;
    newentry.max_renewable_life = mblock.max_rlife;
    newentry.mkvno = mblock.mkvno;
    newentry.expiration = mblock.expiration;
    newentry.pw_expiration = mblock.expiration;
    newentry.mod_name = master_princ;
    if (retval = krb5_timeofday(context, &newentry.mod_date)) {
	com_err(progname, retval, "while fetching date");
	memset((char *)newentry.key.contents, 0, newentry.key.length);
	free((char *)newentry.key.contents);
	return;
    }
    newentry.attributes = mblock.flags;
    newentry.salt_type = KRB5_KDB_SALTTYPE_NORMAL;
    newentry.salt_length = 0;
    newentry.salt = 0;
    newentry.alt_key.length = 0;
    newentry.alt_key.contents = 0;
    newentry.alt_salt_length = 0;
    newentry.alt_salt = 0;
    
    retval = krb5_db_put_principal(context, &newentry, &one);
    if (retval) {
	com_err(progname, retval, "while storing principal date");
	free((char *)newentry.key.contents);
	return;
    }
    fprintf(stdout, "Added %s ...\n", princ_name);
    free((char *)newentry.key.contents);
    if (retval) {
	com_err(progname, retval, "while storing entry for '%s'\n", princ_name);
	return;
    }
    if (one != 1)
	com_err(progname, 0, "entry not stored in database (unknown failure)");
    return;
}

int
set_dbname_help(pname, dbname)
char *pname;
char *dbname;
{
    krb5_error_code retval;
    int nentries;
    krb5_boolean more;
    register krb5_cryptosystem_entry *csentry;
    krb5_data pwd, scratch;

    csentry = master_encblock.crypto_entry;

    if (retval = krb5_db_set_name(test_context, dbname)) {
	com_err(pname, retval, "while setting active database to '%s'",
		dbname);
	return(1);
    }
    /* assemble & parse the master key name */

    if (retval = krb5_db_setup_mkey_name(test_context, mkey_name, cur_realm, 0,
					 &master_princ)) {
	com_err(pname, retval, "while setting up master key name");
	return(1);
    }
    if (mkey_password) {
	pwd.data = mkey_password;
	pwd.length = strlen(mkey_password);
	retval = krb5_principal2salt(test_context, master_princ, &scratch);
	if (retval) {
	    com_err(pname, retval, "while calculated master key salt");
	    return(1);
	}
	retval = krb5_string_to_key(test_context, &master_encblock, master_keyblock.keytype,
				    &master_keyblock, &pwd, &scratch);
	if (retval) {
	    com_err(pname, retval,
		    "while transforming master key from password");
	    return(1);
	}
	free(scratch.data);
    } else {
	if (retval = krb5_db_fetch_mkey(test_context, master_princ, &master_encblock,
					manual_mkey, FALSE, 0,
					&master_keyblock)) {
	    com_err(pname, retval, "while reading master key");
	    return(1);
	}
    }
    if (retval = krb5_db_init(test_context)) {
	com_err(pname, retval, "while initializing database");
	return(1);
    }
    if (retval = krb5_db_verify_master_key(test_context, master_princ, &master_keyblock,
					   &master_encblock)) {
	com_err(pname, retval, "while verifying master key");
	(void) krb5_db_fini(test_context);
	return(1);
    }
    nentries = 1;
    if (retval = krb5_db_get_principal(test_context, master_princ, 
				       &master_entry, &nentries, &more)) {
	com_err(pname, retval, "while retrieving master entry");
	(void) krb5_db_fini(test_context);
	return(1);
    } else if (more) {
	com_err(pname, KRB5KDC_ERR_PRINCIPAL_NOT_UNIQUE,
		"while retrieving master entry");
	(void) krb5_db_fini(test_context);
	return(1);
    } else if (!nentries) {
	com_err(pname, KRB5_KDB_NOENTRY, "while retrieving master entry");
	(void) krb5_db_fini(test_context);
	return(1);
    }

    if (retval = (*csentry->process_key)(&master_encblock,
					 &master_keyblock)) {
	com_err(pname, retval, "while processing master key");
	(void) krb5_db_fini(test_context);
	return(1);
    }
    if (retval = (*csentry->init_random_key)(&master_keyblock,
					     &master_random)) {
	com_err(pname, retval, "while initializing random key generator");
	(void) (*csentry->finish_key)(&master_encblock);
	(void) krb5_db_fini(test_context);
	return(1);
    }
    mblock.max_life = master_entry.max_life;
    mblock.max_rlife = master_entry.max_renewable_life;
    mblock.expiration = master_entry.expiration;
    /* don't set flags, master has some extra restrictions */
    mblock.mkvno = master_entry.kvno;

    krb5_db_free_principal(test_context, &master_entry, nentries);
    dbactive = TRUE;
    return 0;
}

