
#define FCGI_PADDING_SIZE	 255
#define FCGI_RECORD_SIZE	 \
    (sizeof(struct fcgi_record_header) + FCGI_CONTENT_SIZE + FCGI_PADDING_SIZE)

#define FCGI_BEGIN_REQUEST	 1
#define FCGI_ABORT_REQUEST	 2
#define FCGI_END_REQUEST	 3
#define FCGI_PARAMS		 4
#define FCGI_STDIN		 5
#define FCGI_STDOUT		 6
#define FCGI_STDERR		 7
#define FCGI_DATA		 8
#define FCGI_GET_VALUES		 9
#define FCGI_GET_VALUES_RESULT	10
#define FCGI_UNKNOWN_TYPE	11
#define FCGI_MAXTYPE		(FCGI_UNKNOWN_TYPE)

/* role in fcgi_begin_request_body */
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

/* flag in fcgi_begin_request_body */
#define FCGI_KEEP_CONN  1

/* proto in fcgi_end_request_body */
#define FCGI_REQUEST_COMPLETE 0
#define FCGI_CANT_MPX_CONN    1
#define FCGI_OVERLOADED       2
#define FCGI_UNKNOWN_ROLE     3

#include "hio-pac1.h"
struct fcgi_record_header 
{
	hio_uint8_t   version;
	hio_uint8_t   type;
	hio_uint16_t  id;
	hio_uint16_t  content_len;
	hio_uint8_t   padding_len;
	hio_uint8_t   reserved;
};

struct fcgi_begin_request_body 
{
	hio_uint16_t  role;
	hio_uint8_t   flags;
	hio_uint8_t   reserved[5];
};

struct fcgi_end_request_body
{
	hio_uint32_t app_status;
	hio_uint8_t proto_status;
	hio_uint8_t reserved[3];
};
#include "hio-upac.h"





static int begin_request ()
{
	struct fcgi_record_header* h;
	struct fcgi_begin_request_body* br;

	h->version = 1;
	h->type = FCGI_BEGIN_REQUEST;
	h->id = HIO_CONST_HTON16(1);
	h->content_len = HIO_HTON16(HIO_SIZEOF(struct fcgi_begin_request_body));
	h->padding_len = 0;

	br->role = HIO_CONST_HTON16(FCGI_RESPONDER);
	br->flags = 0;


	h->type = FCGI_PARAMS;
	h->content_len = 0;


/*
	h->type = FCGI_STDIN;
*/
	
}
