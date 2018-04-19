/*
 * et-c-krb5_err.c:
 * This file is automatically generated; please do not edit it.
 */
#if defined(_WIN32)
# include "win-mac.h"
#endif

#if !defined(_WIN32)
extern void initialize_krb5_error_table (void);
#endif

#define N_(x) (x)

/* Lclint doesn't handle null annotations on arrays
   properly, so we need this typedef in each
   generated .c file.  */
/*@-redef@*/
typedef /*@null@*/ const char *ncptr;
/*@=redef@*/

static ncptr const text[] = {
	N_("No error"),
	N_("Client's entry in database has expired"),
	N_("Server's entry in database has expired"),
	N_("Requested protocol version not supported"),
	N_("Client's key is encrypted in an old master key"),
	N_("Server's key is encrypted in an old master key"),
	N_("Client not found in Kerberos database"),
	N_("Server not found in Kerberos database"),
	N_("Principal has multiple entries in Kerberos database"),
	N_("Client or server has a null key"),
	N_("Ticket is ineligible for postdating"),
	N_("Requested effective lifetime is negative or too short"),
	N_("KDC policy rejects request"),
	N_("KDC can't fulfill requested option"),
	N_("KDC has no support for encryption type"),
	N_("KDC has no support for checksum type"),
	N_("KDC has no support for padata type"),
	N_("KDC has no support for transited type"),
	N_("Client's credentials have been revoked"),
	N_("Credentials for server have been revoked"),
	N_("TGT has been revoked"),
	N_("Client not yet valid - try again later"),
	N_("Server not yet valid - try again later"),
	N_("Password has expired"),
	N_("Preauthentication failed"),
	N_("Additional pre-authentication required"),
	N_("Requested server and ticket don't match"),
	N_("Server principal valid for user2user only"),
	N_("KDC policy rejects transited path"),
	N_("A service is not available that is required to process the request"),
	N_("KRB5 error code 30"),
	N_("Decrypt integrity check failed"),
	N_("Ticket expired"),
	N_("Ticket not yet valid"),
	N_("Request is a replay"),
	N_("The ticket isn't for us"),
	N_("Ticket/authenticator don't match"),
	N_("Clock skew too great"),
	N_("Incorrect net address"),
	N_("Protocol version mismatch"),
	N_("Invalid message type"),
	N_("Message stream modified"),
	N_("Message out of order"),
	N_("Illegal cross-realm ticket"),
	N_("Key version is not available"),
	N_("Service key not available"),
	N_("Mutual authentication failed"),
	N_("Incorrect message direction"),
	N_("Alternative authentication method required"),
	N_("Incorrect sequence number in message"),
	N_("Inappropriate type of checksum in message"),
	N_("Policy rejects transited path"),
	N_("Response too big for UDP, retry with TCP"),
	N_("KRB5 error code 53"),
	N_("KRB5 error code 54"),
	N_("KRB5 error code 55"),
	N_("KRB5 error code 56"),
	N_("KRB5 error code 57"),
	N_("KRB5 error code 58"),
	N_("KRB5 error code 59"),
	N_("Generic error (see e-text)"),
	N_("Field is too long for this implementation"),
	N_("Client not trusted"),
	N_("KDC not trusted"),
	N_("Invalid signature"),
	N_("Key parameters not accepted"),
	N_("Certificate mismatch"),
	N_("No ticket granting ticket"),
	N_("Realm not local to KDC"),
	N_("User to user required"),
	N_("Can't verify certificate"),
	N_("Invalid certificate"),
	N_("Revoked certificate"),
	N_("Revocation status unknown"),
	N_("Revocation status unavailable"),
	N_("Client name mismatch"),
	N_("KDC name mismatch"),
	N_("Inconsistent key purpose"),
	N_("Digest in certificate not accepted"),
	N_("Checksum must be included"),
	N_("Digest in signed-data not accepted"),
	N_("Public key encryption not supported"),
	N_("KRB5 error code 82"),
	N_("KRB5 error code 83"),
	N_("KRB5 error code 84"),
	N_("The IAKERB proxy could not find a KDC"),
	N_("The KDC did not respond to the IAKERB proxy"),
	N_("KRB5 error code 87"),
	N_("KRB5 error code 88"),
	N_("KRB5 error code 89"),
	N_("Preauthentication expired"),
	N_("More preauthentication data is required"),
	N_("KRB5 error code 92"),
	N_("An unsupported critical FAST option was requested"),
	N_("KRB5 error code 94"),
	N_("KRB5 error code 95"),
	N_("KRB5 error code 96"),
	N_("KRB5 error code 97"),
	N_("KRB5 error code 98"),
	N_("KRB5 error code 99"),
	N_("No acceptable KDF offered"),
	N_("KRB5 error code 101"),
	N_("KRB5 error code 102"),
	N_("KRB5 error code 103"),
	N_("KRB5 error code 104"),
	N_("KRB5 error code 105"),
	N_("KRB5 error code 106"),
	N_("KRB5 error code 107"),
	N_("KRB5 error code 108"),
	N_("KRB5 error code 109"),
	N_("KRB5 error code 110"),
	N_("KRB5 error code 111"),
	N_("KRB5 error code 112"),
	N_("KRB5 error code 113"),
	N_("KRB5 error code 114"),
	N_("KRB5 error code 115"),
	N_("KRB5 error code 116"),
	N_("KRB5 error code 117"),
	N_("KRB5 error code 118"),
	N_("KRB5 error code 119"),
	N_("KRB5 error code 120"),
	N_("KRB5 error code 121"),
	N_("KRB5 error code 122"),
	N_("KRB5 error code 123"),
	N_("KRB5 error code 124"),
	N_("KRB5 error code 125"),
	N_("KRB5 error code 126"),
	N_("KRB5 error code 127"),
	N_("$Id$"),
	N_("Invalid flag for file lock mode"),
	N_("Cannot read password"),
	N_("Password mismatch"),
	N_("Password read interrupted"),
	N_("Illegal character in component name"),
	N_("Malformed representation of principal"),
	N_("Can't open/find Kerberos configuration file"),
	N_("Improper format of Kerberos configuration file"),
	N_("Insufficient space to return complete information"),
	N_("Invalid message type specified for encoding"),
	N_("Credential cache name malformed"),
	N_("Unknown credential cache type"),
	N_("Matching credential not found"),
	N_("End of credential cache reached"),
	N_("Request did not supply a ticket"),
	N_("Wrong principal in request"),
	N_("Ticket has invalid flag set"),
	N_("Requested principal and ticket don't match"),
	N_("KDC reply did not match expectations"),
	N_("Clock skew too great in KDC reply"),
	N_("Client/server realm mismatch in initial ticket request"),
	N_("Program lacks support for encryption type"),
	N_("Program lacks support for key type"),
	N_("Requested encryption type not used in message"),
	N_("Program lacks support for checksum type"),
	N_("Cannot find KDC for requested realm"),
	N_("Kerberos service unknown"),
	N_("Cannot contact any KDC for requested realm"),
	N_("No local name found for principal name"),
	N_("Mutual authentication failed"),
	N_("Replay cache type is already registered"),
	N_("No more memory to allocate (in replay cache code)"),
	N_("Replay cache type is unknown"),
	N_("Generic unknown RC error"),
	N_("Message is a replay"),
	N_("Replay cache I/O operation failed"),
	N_("Replay cache type does not support non-volatile storage"),
	N_("Replay cache name parse/format error"),
	N_("End-of-file on replay cache I/O"),
	N_("No more memory to allocate (in replay cache I/O code)"),
	N_("Permission denied in replay cache code"),
	N_("I/O error in replay cache i/o code"),
	N_("Generic unknown RC/IO error"),
	N_("Insufficient system space to store replay information"),
	N_("Can't open/find realm translation file"),
	N_("Improper format of realm translation file"),
	N_("Can't open/find lname translation database"),
	N_("No translation available for requested principal"),
	N_("Improper format of translation database entry"),
	N_("Cryptosystem internal error"),
	N_("Key table name malformed"),
	N_("Unknown Key table type"),
	N_("Key table entry not found"),
	N_("End of key table reached"),
	N_("Cannot write to specified key table"),
	N_("Error writing to key table"),
	N_("Cannot find ticket for requested realm"),
	N_("DES key has bad parity"),
	N_("DES key is a weak key"),
	N_("Bad encryption type"),
	N_("Key size is incompatible with encryption type"),
	N_("Message size is incompatible with encryption type"),
	N_("Credentials cache type is already registered."),
	N_("Key table type is already registered."),
	N_("Credentials cache I/O operation failed"),
	N_("Credentials cache permissions incorrect"),
	N_("No credentials cache found"),
	N_("Internal credentials cache error"),
	N_("Error writing to credentials cache"),
	N_("No more memory to allocate (in credentials cache code)"),
	N_("Bad format in credentials cache"),
	N_("No credentials found with supported encryption types"),
	N_("Invalid KDC option combination (library internal error)"),
	N_("Request missing second ticket"),
	N_("No credentials supplied to library routine"),
	N_("Bad sendauth version was sent"),
	N_("Bad application version was sent (via sendauth)"),
	N_("Bad response (during sendauth exchange)"),
	N_("Server rejected authentication (during sendauth exchange)"),
	N_("Unsupported preauthentication type"),
	N_("Required preauthentication key not supplied"),
	N_("Generic preauthentication failure"),
	N_("Unsupported replay cache format version number"),
	N_("Unsupported credentials cache format version number"),
	N_("Unsupported key table format version number"),
	N_("Program lacks support for address type"),
	N_("Message replay detection requires rcache parameter"),
	N_("Hostname cannot be canonicalized"),
	N_("Cannot determine realm for host"),
	N_("Conversion to service principal undefined for name type"),
	N_("Initial Ticket response appears to be Version 4 error"),
	N_("Cannot resolve network address for KDC in requested realm"),
	N_("Requesting ticket can't get forwardable tickets"),
	N_("Bad principal name while trying to forward credentials"),
	N_("Looping detected inside krb5_get_in_tkt"),
	N_("Configuration file does not specify default realm"),
	N_("Bad SAM flags in obtain_sam_padata"),
	N_("Invalid encryption type in SAM challenge"),
	N_("Missing checksum in SAM challenge"),
	N_("Bad checksum in SAM challenge"),
	N_("Keytab name too long"),
	N_("Key version number for principal in key table is incorrect"),
	N_("This application has expired"),
	N_("This Krb5 library has expired"),
	N_("New password cannot be zero length"),
	N_("Password change failed"),
	N_("Bad format in keytab"),
	N_("Encryption type not permitted"),
	N_("No supported encryption types (config file error?)"),
	N_("Program called an obsolete, deleted function"),
	N_("unknown getaddrinfo failure"),
	N_("no data available for host/domain name"),
	N_("host/domain name not found"),
	N_("service name unknown"),
	N_("Cannot determine realm for numeric host address"),
	N_("Invalid key generation parameters from KDC"),
	N_("service not available"),
	N_("Ccache function not supported: read-only ccache type"),
	N_("Ccache function not supported: not implemented"),
	N_("Invalid format of Kerberos lifetime or clock skew string"),
	N_("Supplied data not handled by this plugin"),
	N_("Plugin does not support the operation"),
	N_("Invalid UTF-8 string"),
	N_("FAST protected pre-authentication required but not supported by KDC"),
	N_("Auth context must contain local address"),
	N_("Auth context must contain remote address"),
	N_("Tracing unsupported"),
    "mit-krb5", /* Text domain */
    0
};

#include <com_err.h>

const struct error_table et_krb5_error_table = { text, -1765328384L, 256 };

#if !defined(_WIN32)
void initialize_krb5_error_table (void)
    /*@modifies internalState@*/
{
    (void) add_error_table (&et_krb5_error_table);
}
#endif