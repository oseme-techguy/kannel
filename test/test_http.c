/*
 * test_http.c - a simple program to test the new http library
 *
 * Lars Wirzenius
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gwlib/http.h"

#define MAX_THREADS 1024
#define MAX_IN_QUEUE 128

static Counter *counter = NULL;
static long max_requests = 1;
static char **urls = NULL;
static int num_urls = 0;
static int verbose = 1;
static Octstr *auth_username = NULL;
static Octstr *auth_password = NULL;
static Octstr *msg_text = NULL;
static Octstr *ssl_client_certkey_file = NULL;
static Octstr *extra_headers = NULL;
static Octstr *content_file = NULL;
static int file = 0;
static List *split = NULL;
static int use_post = 0; /* defaults to GET method */


static Octstr *post_content_create(void)
{
    Octstr *content;

    if ((content = octstr_read_file(octstr_get_cstr(content_file))) == NULL)
        panic(0, "Cannot read content text");
    debug("", 0, "body content is");
    octstr_dump(content, 0);

    return content;
}

static void start_request(HTTPCaller *caller, List *reqh, long i)
{
    Octstr *url, *content;
    long *id;

    if ((i % 1000) == 0)
	info(0, "Starting fetch %ld", i);
    id = gw_malloc(sizeof(long));
    *id = i;
    url = octstr_create(urls[i % num_urls]);
    if (file) {
        octstr_append(url, octstr_imm("&text="));
        octstr_append(url, msg_text);
    }

    /* add the extra headers that have been read from the file */
    if (split != NULL)
        http_header_combine(reqh, split);

    if (use_post)
        content = post_content_create();
                           
    /*
     * if this is a POST request then pass the required content as body to
     * the HTTP server, otherwise skip the body, the arguments will be
     * urlencoded in the URL itself.
     */
    http_start_request(caller, url, reqh, content, 0, id, ssl_client_certkey_file);

    debug("", 0, "Started request %ld with url:", *id);
    octstr_url_decode(url);
    octstr_dump(url, 0);
    octstr_destroy(url);
    octstr_destroy(msg_text);
    octstr_destroy(content);
}


static int receive_reply(HTTPCaller *caller)
{
    void *id;
    int ret;
    Octstr *final_url;
    List *replyh;
    Octstr *replyb;
    Octstr *type;
    Octstr *charset;
    Octstr *os;

    id = http_receive_result(caller, &ret, &final_url, &replyh, &replyb);
    octstr_destroy(final_url);
    if (id == NULL || ret == -1) {
	error(0, "http GET failed");
	return -1;
    }
    debug("", 0, "Done with request %ld", *(long *) id);
    gw_free(id);

    http_header_get_content_type(replyh, &type, &charset);
    debug("", 0, "Content-type is <%s>, charset is <%s>",
	  octstr_get_cstr(type), 
	  octstr_get_cstr(charset));
    octstr_destroy(type);
    octstr_destroy(charset);
    if (verbose)
        debug("", 0, "Reply headers:");
    while ((os = list_extract_first(replyh)) != NULL) {
        if (verbose)
	    octstr_dump(os, 1);
	octstr_destroy(os);
    }
    list_destroy(replyh, NULL);
    if (verbose) {
        debug("", 0, "Reply body:");
        octstr_dump(replyb, 1);
    }
    octstr_destroy(replyb);

    return 0;
}


static void client_thread(void *arg) 
{
    List *reqh;
    long i, succeeded, failed;
    HTTPCaller *caller;
    char buf[1024];
    long in_queue;

    caller = arg;
    succeeded = 0;
    failed = 0;
    reqh = list_create();
    sprintf(buf, "%ld", (long) gwthread_self());
    http_header_add(reqh, "X-Thread", buf);
    if (auth_username != NULL && auth_password != NULL)
	http_add_basic_auth(reqh, auth_username, auth_password);

    in_queue = 0;
    
    for (;;) {
	while (in_queue < MAX_IN_QUEUE) {
	    i = counter_increase(counter);
	    if (i >= max_requests)
	    	goto receive_rest;
	    start_request(caller, reqh, i);
#if 0
	    gwthread_sleep(0.1);
#endif
	    ++in_queue;
	}
	while (in_queue >= MAX_IN_QUEUE) {
	    if (receive_reply(caller) == -1)
	    	++failed;
    	    else
	    	++succeeded;
	    --in_queue;
	}
    }
    
receive_rest:
    while (in_queue > 0) {
	if (receive_reply(caller) == -1)
	    ++failed;
	else
	    ++succeeded;
    	--in_queue;
    }

    http_destroy_headers(reqh);
    http_caller_destroy(caller);
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}


static void split_headers(Octstr *headers, List **split)
{
    long start;
    long pos;

    *split = list_create();
    start = 0;
    for (pos = 0; pos < octstr_len(headers); pos++) {
        if (octstr_get_char(headers, pos) == '\n') {
            Octstr *line;

            if (pos == start) {
                /* Skip empty lines */
                start = pos + 1;
                continue;
            }
            line = octstr_copy(headers, start, pos - start);
            start = pos + 1;
            list_append(*split, line);
        }
    }
}


static void help(void) 
{
    info(0, "Usage: test_http [options] url ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-q");
    info(0, "    don't print the body or headers of the HTTP response");
    info(0, "-r number");
    info(0, "    make `number' requests, repeating URLs as necessary");
    info(0, "-p domain.name");
    info(0, "    use `domain.name' as a proxy");
    info(0, "-P portnumber");
    info(0, "    connect to proxy at port `portnumber'");
    info(0, "-e domain1:domain2:...");
    info(0, "    set exception list for proxy use");
    info(0, "-u filename");
    info(0, "    read request's &text= string from file 'filename'. It is"); 
    info(0, "    url encoded before it is added to the request");
    info(0, "-x");
    info(0, "    use POST method for the request (default: GET)");
    info(0, "-H filename");
    info(0, "    read HTTP headers from file 'filename' and add them to");
    info(0, "    the request for url 'url'");
    info(0, "-b filename");
    info(0, "    read POST request content from file 'filename' and send");
    info(0, "    it as body of the request");
    info(0, "-s");
    info(0, "    use HTTPS scheme to access SSL-enabled HTTP server");
    info(0, "-c ssl_client_cert_key_file");
    info(0, "    use this file as the SSL certificate and key file");
}

int main(int argc, char **argv) 
{
    int i, opt, num_threads;
    Octstr *proxy;
    List *exceptions;
    long proxy_port;
    Octstr *proxy_username;
    Octstr *proxy_password;
    char *p;
    long threads[MAX_THREADS];
    time_t start, end;
    double run_time;
    FILE *fp;
    int ssl = 0;
    
    gwlib_init();
    
    proxy = NULL;
    proxy_port = -1;
    exceptions = list_create();
    proxy_username = NULL;
    proxy_password = NULL;
    num_threads = 0;
    file = 0;
    fp = NULL;
    
    while ((opt = getopt(argc, argv, "hv:qr:p:P:e:t:a:u:sc:H:xb:")) != EOF) {
	switch (opt) {
	case 'v':
	    log_set_output_level(atoi(optarg));
	    break;
	
	case 'q':
	    verbose = 0;
	    break;
	
	case 'r':
	    max_requests = atoi(optarg);
	    break;
	
	case 't':
	    num_threads = atoi(optarg);
	    if (num_threads > MAX_THREADS)
		num_threads = MAX_THREADS;
	    break;

    case 'u':
        file = 1;
        fp = fopen(optarg, "a");
        if (fp == NULL)
            panic(0, "Cannot open message text file %s", optarg);
        msg_text = octstr_read_file(optarg);
        if (msg_text == NULL)
            panic(0, "Cannot read message text");
        debug("", 0, "message text is");
        octstr_dump(msg_text, 0);
        octstr_url_encode(msg_text);
        fclose(fp);
        break;
	
	case 'h':
	    help();
	    exit(0);
	
	case 'p':
	    proxy = octstr_create(optarg);
	    break;
	
	case 'P':
	    proxy_port = atoi(optarg);
	    break;
	
	case 'e':
	    p = strtok(optarg, ":");
	    while (p != NULL) {
		list_append(exceptions, octstr_create(p));
		p = strtok(NULL, ":");
	    }
	    break;
	
	case 'a':
	    p = strtok(optarg, ":");
	    if (p != NULL) {
		auth_username = octstr_create(p);
		p = strtok(NULL, "");
		if (p != NULL)
		    auth_password = octstr_create(p);
	    }
	    break;

    case 's':
        ssl = 1;
        break;

    case 'c':
	    octstr_destroy(ssl_client_certkey_file);
	    ssl_client_certkey_file = octstr_create(optarg);
        break;

    case 'x':
        use_post = 1;
        break;

    case 'H':
        fp = fopen(optarg, "a");
        if (fp == NULL)
            panic(0, "Cannot open header text file %s", optarg);
        extra_headers = octstr_read_file(optarg);
        if (extra_headers == NULL)
            panic(0, "Cannot read header text");
        debug("", 0, "headers are");
        octstr_dump(extra_headers, 0);
        split_headers(extra_headers, &split);
        fclose(fp);
        break;

    case 'b':
        content_file = octstr_create(optarg);
        break;

	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }
    
    if (optind == argc) {
	help();
	exit(0);
    }

#ifdef HAVE_LIBSSL
    /*
     * check if we are doing a SSL-enabled client version here
     * load the required cert and key file
     */
    if (ssl) {
        if (ssl_client_certkey_file != NULL) {
            use_global_client_certkey_file(ssl_client_certkey_file);
        } else {
            panic(0, "client certkey file need to be given!");
        }
    }
#endif
    
    if (proxy != NULL && proxy_port > 0) {
	http_use_proxy(proxy, proxy_port, exceptions,
	proxy_username, proxy_password);
    }
    octstr_destroy(proxy);
    octstr_destroy(proxy_username);
    octstr_destroy(proxy_password);
    list_destroy(exceptions, octstr_destroy_item);
    
    counter = counter_create();
    urls = argv + optind;
    num_urls = argc - optind;
    
    time(&start);
    if (num_threads == 0)
	client_thread(http_caller_create());
    else {
	for (i = 0; i < num_threads; ++i)
	    threads[i] = gwthread_create(client_thread, http_caller_create());
	for (i = 0; i < num_threads; ++i)
	    gwthread_join(threads[i]);
    }
    time(&end);
    
    counter_destroy(counter);
    
    run_time = difftime(end, start);
    info(0, "%ld requests in %f seconds, %f requests/s.",
	 max_requests, run_time, max_requests / run_time);
    
    octstr_destroy(auth_username);
    octstr_destroy(auth_password);
    octstr_destroy(ssl_client_certkey_file);
    octstr_destroy(extra_headers);
    octstr_destroy(content_file);
    list_destroy(split, octstr_destroy_item);
    
    gwlib_shutdown();
    
    return 0;
}

