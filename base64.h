#ifndef BASE64_H
#define BASE64_H
#ifdef __cplusplus
extern "C" {
#endif

void
imap_create_header(char* message, const char *subject);

void imap_add_address(char *message,const char* id, const char* name, const char* email);

void imap_add_header(char* message,const char* id , const char *content);

void imap_add_identify(char* message,const char* id , const char *content);

void imap_add_contect(char *message,const char* content);

#ifdef __cplusplus
}
#endif

#endif // BASE64_H
