/* This is a generated file */
#ifndef __krb5_private_h__
#define __krb5_private_h__

/* $FreeBSD: src/kerberos5/include/krb5-private.h,v 1.1.1.1.2.3 2002/09/01 04:22:00 nectar Exp $ */
/* $DragonFly: src/kerberos5/include/Attic/krb5-private.h,v 1.2 2003/06/17 04:26:17 dillon Exp $ */

#include <stdarg.h>
void
_krb5_crc_init_table (void);

u_int32_t
_krb5_crc_update (
	const char */*p*/,
	size_t /*len*/,
	u_int32_t /*res*/);

int
_krb5_extract_ticket (
	krb5_context /*context*/,
	krb5_kdc_rep */*rep*/,
	krb5_creds */*creds*/,
	krb5_keyblock */*key*/,
	krb5_const_pointer /*keyseed*/,
	krb5_key_usage /*key_usage*/,
	krb5_addresses */*addrs*/,
	unsigned /*nonce*/,
	krb5_boolean /*allow_server_mismatch*/,
	krb5_boolean /*ignore_cname*/,
	krb5_decrypt_proc /*decrypt_proc*/,
	krb5_const_pointer /*decryptarg*/);

krb5_ssize_t
_krb5_get_int (
	void */*buffer*/,
	unsigned long */*value*/,
	size_t /*size*/);

void
_krb5_n_fold (
	const void */*str*/,
	size_t /*len*/,
	void */*key*/,
	size_t /*size*/);

krb5_ssize_t
_krb5_put_int (
	void */*buffer*/,
	unsigned long /*value*/,
	size_t /*size*/);

#endif /* __krb5_private_h__ */
