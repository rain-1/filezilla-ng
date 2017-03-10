/*
 * fzputtygen, based on puttygen.
 */

#define PUTTY_DO_GLOBALS

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include "putty.h"
#include "ssh.h"

void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char* str;

    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    fzprintf(sftpError, "Fatal error: %s", str);
    sfree(str);

    cleanup_exit(1);
}

void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char* str;

    va_start(ap, fmt);
    str = dupvprintf(fmt, ap);
    va_end(ap);
    fzprintf(sftpError, "Error: %s", str);
    sfree(str);
}

/*
 * Stubs to let everything else link sensibly.
 */
void log_eventlog(void *handle, const char *event)
{
}
char *x_get_default(const char *key)
{
    return NULL;
}
void sk_cleanup(void)
{
}

/* For Unix in particular, but harmless if this main() is reused elsewhere */
const int buildinfo_gtk_relevant = FALSE;

int main(int argc, char **argv)
{
    Filename *infilename = NULL;
    int intype = SSH_KEYTYPE_UNOPENABLE;
    int encrypted = 0;
    char* origcomment = 0;
    char* line = 0;
    char* passphrase = 0;
    struct ssh2_userkey *ssh2key = NULL;
    char* fingerprint = 0;

    printf("fzputtygen\n");
    printf("Copyright (C) 2008-2017  Tim Kosse\n");
    printf("Based on PuTTY's puttygen\n");
    printf("Copyright (C) 1997-2017  Simon Tatham and the PuTTY team\n");
    printf("Converts private SSH keys into PuTTY's format.\n");
    printf("This program is used by FileZilla and not intended to be used directly.\n");
    printf("Use the puttygen tool from PuTTY for a human-usable tool.\n");
    printf("\n");
    fflush(stdout);

    while (1) {
	sfree(line);

	line = fgetline(stdin);
	if (!line || !*line || *line == '\n')
	    break;

	line[strlen(line) - 1] = 0;
	char* cmd = line, *args = line;
	
	while (*args) {
	    if (*args == ' ') {
		*(args++) = 0;
		break;
	    }
	    args++;
	}
	if (!*args)
	    args = 0;

	if (!strcmp(cmd, "file")) {
	    char const* ret = NULL;
	    if (ssh2key) {
		ssh2key->alg->freekey(ssh2key->data);
		sfree(ssh2key);
		ssh2key = 0;
	    }
	    sfree(passphrase);
	    passphrase = 0;
	    sfree(fingerprint);
	    fingerprint = 0;
	    
	    if (!args) {
		fzprintf(sftpError, "No argument given");
		continue;
	    }

	    if (infilename) {
		filename_free(infilename);
	    }
	    infilename = filename_from_str(args);

	    intype = key_type(infilename);
	    switch (intype)
	    {
	    case SSH_KEYTYPE_SSH1:
		ret = "incompatible";
		intype = SSH_KEYTYPE_UNOPENABLE;
		break;
	    case SSH_KEYTYPE_SSH2:
		ret = "ok";
		encrypted = ssh2_userkey_encrypted(infilename, &origcomment);
		break;
	    case SSH_KEYTYPE_UNKNOWN:
	    case SSH_KEYTYPE_UNOPENABLE:
	    default:
		ret = "error";
		intype = SSH_KEYTYPE_UNOPENABLE;
		break;
	    case SSH_KEYTYPE_OPENSSH_PEM:
	    case SSH_KEYTYPE_OPENSSH_NEW:
	    case SSH_KEYTYPE_SSHCOM:
		encrypted = import_encrypted(infilename, intype, &origcomment);
		ret = encrypted ? "convertible" : "ok";
		break;
	    }
	    fzprintf(sftpReply, "%s", ret);
	}
	else if (!strcmp(cmd, "encrypted")) {
	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }
	    
	    fzprintf(sftpReply, "%d", encrypted ? 1 : 0);
	}
	else if (!strcmp(cmd, "comment")) {
	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }
	    if (ssh2key && ssh2key->comment) {
		fzprintf(sftpReply, "%s", ssh2key->comment);
	    }
	    else if (origcomment)
		fzprintf(sftpReply, "%s", origcomment);
	    else
		fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "password")) {
	    const char* error = NULL;

	    if (!args) {
		fzprintf(sftpError, "No argument given");
		continue;
	    }

	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }

	    if (!encrypted) {
		fzprintf(sftpError, "File is not encrypted");
		continue;
	    }

	    if (ssh2key) {
		fzprintf(sftpError, "Already opened file");
		continue;
	    }

	    sfree(passphrase);
	    passphrase = strdup(args);

	    switch (intype) {
		case SSH_KEYTYPE_SSH2:
		    ssh2key = ssh2_load_userkey(infilename, passphrase, &error);
		    break;
		case SSH_KEYTYPE_OPENSSH_PEM:
		case SSH_KEYTYPE_OPENSSH_NEW:
		case SSH_KEYTYPE_SSHCOM:
		    ssh2key = import_ssh2(infilename, intype, passphrase, &error);
		    break;
		default:
		    break;
	    }
	    if (ssh2key == SSH2_WRONG_PASSPHRASE) {
		error = "wrong passphrase";
		ssh2key = 0;
	    }
	    if (ssh2key) {
		error = NULL;
	    }
	    else if (!error) {
		error = "unknown error";
	    }

	    if (error)
		fzprintf(sftpError, "Error loading file: %s", error);
	    else
		fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "fingerprint")) {
	    const char* error = 0;

	    if (!fingerprint) {
		if (ssh2key) {
		    fingerprint = ssh2_fingerprint(ssh2key->alg, ssh2key->data);
		}
		else {
		    switch (intype) {
			 case SSH_KEYTYPE_SSH2:
			{
			    void* ssh2blob;
			    int bloblen = 0;
			    char* comment = NULL;

			    ssh2blob = ssh2_userkey_loadpub(infilename, 0, &bloblen, &comment, &error);
			    if (ssh2blob) {
				fingerprint = ssh2_fingerprint_blob(ssh2blob, bloblen);
				sfree(ssh2blob);
			    }
			    else if (!error) {
				error = "unknown error";
			    }

			    if (comment) {
				sfree(origcomment);
				origcomment = comment;
			    }
			    break;
			}
			case SSH_KEYTYPE_OPENSSH_PEM:
			case SSH_KEYTYPE_OPENSSH_NEW:
			case SSH_KEYTYPE_SSHCOM:
			    ssh2key = import_ssh2(infilename, intype, "", &error);
			    if (ssh2key) {
				if (ssh2key != SSH2_WRONG_PASSPHRASE) {
				    error = NULL;
				    fingerprint = ssh2_fingerprint(ssh2key->alg, ssh2key->data);
				}
				else {
				    ssh2key = NULL;
				    error = "wrong passphrase";
				}
			    }
			    else if (!error)
				error = "unknown error";
			    break;
			default:
			    error = "No file loaded";
			    break;
		    }
		}
	    }

	    if (!fingerprint && !error) {
		error = "Could not get fingerprint";
	    }

	    if (error)
		fzprintf(sftpError, "Error loading file: %s", error);
	    else
		fzprintf(sftpReply, "%s", fingerprint);
	}
	else if (!strcmp(cmd, "write")) {
	    Filename* outfilename;

	    int ret;
	    if (!args) {
		fzprintf(sftpError, "No argument given");
		continue;
	    }

	    if (!ssh2key) {
		fzprintf(sftpError, "No key loaded");
		continue;
	    }

	    outfilename = filename_from_str(args);

	    ret = ssh2_save_userkey(outfilename, ssh2key, passphrase);
	     if (!ret) {
		fzprintf(sftpError, "Unable to save SSH-2 private key");
		continue;
	    }

	    filename_free(outfilename);

	    fzprintf(sftpReply, "");
	}
	else
		fzprintf(sftpError, "Unknown command");
    }

    if (infilename) {
	filename_free(infilename);
    }
    sfree(line);
    sfree(passphrase);
    if (ssh2key) {
	ssh2key->alg->freekey(ssh2key->data);
	sfree(ssh2key);
    }
    sfree(fingerprint);
    sfree(origcomment);

    return 0;
}
