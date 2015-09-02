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

int main(int argc, char **argv)
{
    Filename *infilename = NULL, *outfilename = NULL;
    int intype = SSH_KEYTYPE_UNOPENABLE;
    int encrypted = 0;
    char* origcomment = 0;
    char* line = 0;
    char* passphrase = 0;
    struct ssh2_userkey *ssh2key = NULL;
    char* fingerprint = 0;

    printf("fzputtygen\n");
    printf("Copyright (C) 2008-2015  Tim Kosse\n");
    printf("Based on PuTTY's puttygen\n");
    printf("Copyright (C) 1997-2015  Simon Tatham and the PuTTY team\n");
    printf("Converts private SSH keys into PuTTY's format.\n");
    printf("This program is used by FileZilla and not intended to be used directly.\n");
    printf("Use the puttygen tool from PuTTY for a human-usable tool.\n");
    printf("\n");
    fflush(stdout);

    while (1) {
	if (line)
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
	    if (ssh2key) {
		ssh2key->alg->freekey(ssh2key->data);
		sfree(ssh2key);
		ssh2key = 0;
	    }
	    if (passphrase) {
		sfree(passphrase);
		passphrase = 0;
	    }

	    if (!args) {
		fzprintf(sftpError, "No argument given");
		continue;
	    }

	    infilename = filename_from_str(args);

	    intype = key_type(infilename);
	    switch (intype)
	    {
	    case SSH_KEYTYPE_SSH1:
		fzprintf(sftpReply, "3");
		intype = SSH_KEYTYPE_UNOPENABLE;
		break;
	    case SSH_KEYTYPE_SSH2:
		fzprintf(sftpReply, "0");
		break;
	    case SSH_KEYTYPE_UNKNOWN:
	    case SSH_KEYTYPE_UNOPENABLE:
	    default:
		fzprintf(sftpReply, "2");
		intype = SSH_KEYTYPE_UNOPENABLE;
		break;
	    case SSH_KEYTYPE_OPENSSH_PEM:
	    case SSH_KEYTYPE_OPENSSH_NEW:
	    case SSH_KEYTYPE_SSHCOM:
		fzprintf(sftpReply, "1");
		break;
	    }
	}
	else if (!strcmp(cmd, "encrypted")) {
	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }
	    
	    if (intype == SSH_KEYTYPE_SSH2)
		encrypted = ssh2_userkey_encrypted(infilename, &origcomment);
	    else
		encrypted = import_encrypted(infilename, intype, &origcomment);

	    fzprintf(sftpReply, "%d", encrypted ? 1 : 0);
	}
	else if (!strcmp(cmd, "comment")) {
	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }
	    if (origcomment)
		fzprintf(sftpReply, "%s", origcomment);
	    else
		fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "password")) {
	    if (!args) {
		fzprintf(sftpError, "No argument given");
		continue;
	    }

	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }

	    if (passphrase)
		sfree(passphrase);
	    passphrase = strdup(args);
	    fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "load")) {
	    const char* error = 0;

	    if (ssh2key) {
		ssh2key->alg->freekey(ssh2key->data);
		sfree(ssh2key);
		ssh2key = 0;
	    }

	    if (intype == SSH_KEYTYPE_UNOPENABLE) {
		fzprintf(sftpError, "No key file opened");
		continue;
	    }

	    if (encrypted && !passphrase) {
		fzprintf(sftpError, "No password given");
		continue;
	    }

	    switch (intype)
	    {
	    case SSH_KEYTYPE_SSH2:
		ssh2key = ssh2_load_userkey(infilename, passphrase, &error);
		if (ssh2key == SSH2_WRONG_PASSPHRASE)
		{
		    error = "wrong passphrase";
		    ssh2key = 0;
		}
		else if (ssh2key)
		    error = NULL;
		else if (!error)
		    error = "unknown error";
		break;

	    case SSH_KEYTYPE_OPENSSH_PEM:
	    case SSH_KEYTYPE_OPENSSH_NEW:
	    case SSH_KEYTYPE_SSHCOM:
		ssh2key = import_ssh2(infilename, intype, passphrase, &error);
		if (ssh2key) {
		    if (ssh2key != SSH2_WRONG_PASSPHRASE)
			error = NULL;
		    else {
			ssh2key = NULL;
			error = "wrong passphrase";
		    }
		} else if (!error)
		    error = "unknown error";
		break;
	    default:
		assert(0);
	    }

	    if (error)
		fzprintf(sftpError, "Error loading file: %s", error);
	    else
		fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "loadpub")) {
	    const char* error = 0;
	    int ret;
	    void* blob = NULL;
	    int bloblen = 0;
	    if (origcomment) {
		sfree(origcomment);
		origcomment = 0;
	    }

	    switch (intype) {
	    case SSH_KEYTYPE_SSH2:
		{
		    void* ssh2blob;
		    int bloblen = 0;

		    ssh2blob = ssh2_userkey_loadpub(infilename, 0, &bloblen, &origcomment, &error);
		    if (ssh2blob) {
			fingerprint = ssh2_fingerprint_blob(ssh2blob, bloblen);
			sfree(ssh2blob);
		    }
		    else if (!error) {
			error = "unknown error";
		    }
		}
		break;

	    default:
		assert(0);
	    }

	    if (error)
		fzprintf(sftpError, "Error loading file: %s", error);
	    else
		fzprintf(sftpReply, "");
	}
	else if (!strcmp(cmd, "data")) {
	    if (!fingerprint) {
		fzprintf(sftpError, "No key loaded");
		continue;
	    }

	    fzprintf(sftpReply, "%s", fingerprint);
	}
	else if (!strcmp(cmd, "write")) {
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

	    fzprintf(sftpReply, "");
	}
	else
    	    fzprintf(sftpError, "Unknown command");
    }

    if (line)
	sfree(line);
    if (passphrase)
	sfree(passphrase);
    if (ssh2key) {
	ssh2key->alg->freekey(ssh2key->data);
	sfree(ssh2key);
    }
    if (fingerprint)
	sfree(fingerprint);

    return 0;
}
