/* Get malloc. */
#include <stdlib.h>

/* Get UCHAR_MAX. */
#include <limits.h>

#include <string.h>

# define BASE64_LENGTH(inlen) ((((inlen) + 2) / 3) * 4)

/* C89 compliant way to cast 'char' to 'unsigned char'. */
static inline unsigned char
to_uchar (char ch)
{
  return ch;
}

/* Base64 encode IN array of size INLEN into OUT array of size OUTLEN.
   If OUTLEN is less than BASE64_LENGTH(INLEN), write as many bytes as
   possible.  If OUTLEN is larger than BASE64_LENGTH(INLEN), also zero
   terminate the output buffer. */
void
base64_encode (const char *in, size_t inlen,
               char *out, size_t outlen)
{
  static const char b64str[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  while (inlen && outlen)
    {
      *out++ = b64str[(to_uchar (in[0]) >> 2) & 0x3f];
      if (!--outlen)
        break;
      *out++ = b64str[((to_uchar (in[0]) << 4)
                       + (--inlen ? to_uchar (in[1]) >> 4 : 0))
                      & 0x3f];
      if (!--outlen)
        break;
      *out++ =
        (inlen
         ? b64str[((to_uchar (in[1]) << 2)
                   + (--inlen ? to_uchar (in[2]) >> 6 : 0))
                  & 0x3f]
         : '=');
      if (!--outlen)
        break;
      *out++ = inlen ? b64str[to_uchar (in[2]) & 0x3f] : '=';
      if (!--outlen)
        break;
      if (inlen)
        inlen--;
      if (inlen)
        in += 3;
    }

  if (outlen)
    *out = '\0';
}

/* Allocate a buffer and store zero terminated base64 encoded data
   from array IN of size INLEN, returning BASE64_LENGTH(INLEN), i.e.,
   the length of the encoded data, excluding the terminating zero.  On
   return, the OUT variable will hold a pointer to newly allocated
   memory that must be deallocated by the caller.  If output string
   length would overflow, 0 is returned and OUT is set to NULL.  If
   memory allocation failed, OUT is set to NULL, and the return value
   indicates length of the requested memory block, i.e.,
   BASE64_LENGTH(inlen) + 1. */
size_t
base64_encode_alloc (const char *in, size_t inlen, char **out)
{
  size_t outlen = 1 + BASE64_LENGTH (inlen);

  /* Check for overflow in outlen computation.
   *
   * If there is no overflow, outlen >= inlen.
   *
   * If the operation (inlen + 2) overflows then it yields at most +1, so
   * outlen is 0.
   *
   * If the multiplication overflows, we lose at least half of the
   * correct value, so the result is < ((inlen + 2) / 3) * 2, which is
   * less than (inlen + 2) * 0.66667, which is less than inlen as soon as
   * (inlen > 4).
   */
  if (inlen > outlen)
    {
      *out = NULL;
      return 0;
    }

  *out = malloc (outlen);
  if (!*out)
    return outlen;

  base64_encode (in, inlen, *out, outlen);

  return outlen - 1;
}

size_t
base64_encode_alloc_with_header(const char*in,size_t inlen,char **out)
{
    size_t outlen = 1 + BASE64_LENGTH (inlen);

    if (inlen > outlen)
    {
        *out = NULL;
        return 0;
    }
    outlen += 12;

    *out = malloc (outlen);
    if (!*out)
        return outlen;
    memcpy(*out,"=?UTF-8?B?",10);
    base64_encode (in, inlen, *out+10, outlen-12);
    memcpy(*out+outlen-3,"?=",2);
    *(*out+outlen) = 0;

    return outlen-1;
}
const char imap_subject_header[] = "Subject: ";
const char imap_text_header[] = "\nMIME-Version: 1.0\nContent-Type: text/plain;\n charset=utf-8\nContent-Transfer-Encoding: base64\n";

/* memset message before use */
void
imap_create_header(char* message, const char *subject)
{
    char *encode;
    size_t len;
    memcpy(message,imap_subject_header,strlen(imap_subject_header));
    len = base64_encode_alloc_with_header(subject,strlen(subject),&encode);
    if(len)
    {
        memcpy(message+strlen(message),encode,len);
        free(encode);
    }
    memcpy(message+strlen(message),imap_text_header,strlen(imap_text_header));
}

void imap_add_address(char *message,const char* id, const char* name, const char* email)
{
    char *encode;
    size_t len;
    memcpy(message+strlen(message),id,strlen(id));
    memcpy(message+strlen(message),": ",2);
    len = base64_encode_alloc_with_header(name,strlen(name),&encode);
    if(len)
    {
        memcpy(message+strlen(message),encode,len);
        free(encode);
    }
    memcpy(message+strlen(message)," <",2);
    memcpy(message+strlen(message),email,strlen(email));
    memcpy(message+strlen(message),">\n",2);
}

void imap_add_header(char* message,const char* id , const char *content)
{
    memcpy(message+strlen(message),id,strlen(id));
    memcpy(message+strlen(message),": ",2);
    memcpy(message+strlen(message),content,strlen(content));
    *(message+strlen(message)) = '\n';
}

void imap_add_identify(char* message,const char* id , const char *content)
{
    memcpy(message+strlen(message),id,strlen(id));
    memcpy(message+strlen(message),": <",3);
    memcpy(message+strlen(message),content,strlen(content));
    memcpy(message+strlen(message),">\n",2);
}

void imap_add_contect(char *message,const char* content)
{
    char *encode;
    size_t len,current;
    *(message+strlen(message)) = '\n';
    len = base64_encode_alloc(content,strlen(content),&encode);
    current = 0;
    if (len)
    {
        while (len - current >= 76)
        {
            memcpy(message+strlen(message),encode+current,76);
            *(message+strlen(message)) = '\n';
            current += 76;
        }
        if (len - current)
        {
            memcpy(message+strlen(message),encode+current,len-current);
            *(message+strlen(message)) = '\n';
        }
        free(encode);
    }
}

