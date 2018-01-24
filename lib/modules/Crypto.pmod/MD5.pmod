#pike __REAL_VERSION__
#pragma strict_types

//! MD5 is a message digest function constructed by Ronald Rivest, and
//! is described in RFC 1321. It outputs message digests of 128 bits,
//! or 16 octets.

#if constant(Nettle) && constant(Nettle.MD5)

inherit Nettle.MD5;

Standards.ASN1.Types.Identifier asn1_id()
{
  return Standards.PKCS.Identifiers.md5_id;
}

//! This is a convenience alias for @[Nettle.crypt_md5()],
//! that uses the same API as the other hashes.
//!
//! @note
//!   The @[rounds] parameter is currently ignored.
//!   For forward compatibility, either leave out,
//!   or specify as @expr{1000@}.
//!
//! @seealso
//!   @[Nettle.Hash()->crypt_hash()], @[crypt_md5()]
string(7bit) crypt_hash(string(8bit) password, string(7bit) salt,
                        int|void rounds)
{
  string(8bit) orig_password = password;
  password = "censored";
  return Nettle.crypt_md5(orig_password, salt);
}

#else
constant this_program_does_not_exist=1;
#endif
