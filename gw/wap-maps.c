/*
 * gw/wap-maps.c - URL mapping 
 * 
 * Bruno Rodrigues  <bruno.rodrigues@litux.org>
 */

#include "gwlib/gwlib.h"

struct url_map_struct {
    Octstr *name;
    Octstr *url;
    Octstr *map_url;
    Octstr *send_msisdn_query;
    Octstr *send_msisdn_header;
    Octstr *send_msisdn_format;
    int accept_cookies;
};

struct user_map_struct {
    Octstr *name;
    Octstr *user;
    Octstr *pass;
    Octstr *msisdn;
};

/* mapping lists */
static List *url_map = NULL;
static List *user_map = NULL;


/********************************************************************
 * Creation and destruction of mapping entries
 */

void wap_map_add_url(Octstr *name, Octstr *url, Octstr *map_url,
                     Octstr *send_msisdn_query,
                     Octstr *send_msisdn_header,
                     Octstr *send_msisdn_format,
                     int accept_cookies) {
    struct url_map_struct *entry;

    if (url_map == NULL) 
        url_map = list_create();

    entry = gw_malloc(sizeof(*entry));
    entry->name = name;
    entry->url = url;
    entry->map_url = map_url;
    entry->send_msisdn_query = send_msisdn_query;
    entry->send_msisdn_header = send_msisdn_header;
    entry->send_msisdn_format = send_msisdn_format;
    entry->accept_cookies = accept_cookies;
    
    list_append(url_map, entry);
}

void wap_map_add_user(Octstr *name, Octstr *user, Octstr *pass,
                      Octstr *msisdn) {
    struct user_map_struct *entry;

    if (user_map == NULL) 
        user_map = list_create();

    entry = gw_malloc(sizeof(*entry));
    entry->name = name;
    entry->user = user;
    entry->pass = pass;
    entry->msisdn = msisdn;
    list_append(user_map, entry);
}

void wap_map_destroy(void) 
{
    long i;
    struct url_map_struct *entry;

    if (url_map != NULL) {
        for (i = 0; i < list_len(url_map); i++) {
            entry = list_get(url_map, i);
            octstr_destroy(entry->name);
            octstr_destroy(entry->url);
            octstr_destroy(entry->map_url);
            octstr_destroy(entry->send_msisdn_query);
            octstr_destroy(entry->send_msisdn_header);
            octstr_destroy(entry->send_msisdn_format);
            gw_free(entry);
        }
        list_destroy(url_map, NULL);
    }
    url_map = NULL;
}

void wap_map_user_destroy(void) 
{
    long i;
    struct user_map_struct *entry;
    if (user_map != NULL) {
        for (i = 0; i < list_len(user_map); i++) {
            entry = list_get(user_map, i);
            octstr_destroy(entry->name);
            octstr_destroy(entry->user);
            octstr_destroy(entry->pass);
            octstr_destroy(entry->msisdn);
            gw_free(entry);
        }
        list_destroy(user_map, NULL);
    }
    user_map = NULL;
}


/********************************************************************
 * Public functions
 */

void wap_map_url_config(char *s)
{
    char *in, *out;
    
    s = gw_strdup(s);
    in = strtok(s, " \t");
    if (!in) 
        return;
    out = strtok(NULL, " \t");
    if (!out) 
        return;
    wap_map_add_url(octstr_imm("unknown"), octstr_create(in), 
                     octstr_create(out), NULL, NULL, NULL, 0);
    gw_free(s);
}

void wap_map_url_config_device_home(char *to)
{
    wap_map_add_url(octstr_imm("Device Home"), octstr_imm("DEVICE:home*"),
                     octstr_create(to), NULL, NULL, NULL, -1);
}

void wap_map_url_config_info(void)
{
    long i;
    for (i = 0; url_map && i < list_len(url_map); i++) {
        struct url_map_struct *entry;
        entry = list_get(url_map, i);
        info(0, "WSP: Added wap-url-map, name <%s>, url <%s>, map-url <%s>, "
                "send-msisdn-query <%s>, send-msisdn-header <%s>, "
                "send-msisdn-format <%s>, accept-cookies <%d>", 
             octstr_get_cstr(entry->name), octstr_get_cstr(entry->url), 
             octstr_get_cstr(entry->map_url), 
             octstr_get_cstr(entry->send_msisdn_query), 
             octstr_get_cstr(entry->send_msisdn_header), 
             octstr_get_cstr(entry->send_msisdn_format), 
             entry->accept_cookies);
    }
}

void wap_map_user_config_info(void)
{
    long i;
    for (i = 0; user_map && i < list_len(user_map); i++) {
        struct user_map_struct *entry;
        entry = list_get(user_map, i);
        info(0, "WSP: Added wap-user-map, name <%s>, user <%s>, pass <%s>, "
                "msisdn <%s>", octstr_get_cstr(entry->name), 
             octstr_get_cstr(entry->user), octstr_get_cstr(entry->pass), 
             octstr_get_cstr(entry->msisdn));
    }
}

void wap_map_url(Octstr **osp, Octstr **send_msisdn_query, 
                 Octstr **send_msisdn_header, 
                 Octstr **send_msisdn_format, int *accept_cookies)
{
    long i;
    Octstr *newurl, *tmp1, *tmp2;

    newurl = tmp1 = tmp2 = NULL;
    *send_msisdn_query = *send_msisdn_header = *send_msisdn_format = NULL;
    *accept_cookies = -1;

    debug("wsp",0,"WSP: Mapping url <%s>", octstr_get_cstr(*osp));
    for (i = 0; url_map && i < list_len(url_map); i++) {
        struct url_map_struct *entry;
        entry = list_get(url_map, i);

        /* 
        debug("wsp",0,"WSP: matching <%s> with <%s>", 
	          octstr_get_cstr(entry->url), octstr_get_cstr(entry->map_url)); 
        */

        /* DAVI: I only have '*' terminated entry->url implementation for now */
        tmp1 = octstr_duplicate(entry->url);
        octstr_delete(tmp1, octstr_len(tmp1)-1, 1); /* remove last '*' */
        tmp2 = octstr_copy(*osp, 0, octstr_len(tmp1));

        debug("wsp",0,"WSP: Matching <%s> with <%s>", 
              octstr_get_cstr(tmp1), octstr_get_cstr(tmp2));

        if (octstr_case_compare(tmp2, tmp1) == 0) {
            /* rewrite url if configured to do so */
            if (entry->map_url != NULL) {
                if (octstr_get_char(entry->map_url, 
                                    octstr_len(entry->map_url)-1) == '*') {
                    newurl = octstr_duplicate(entry->map_url);
                    octstr_delete(newurl, octstr_len(newurl)-1, 1);
                    octstr_append(newurl, octstr_copy(*osp, 
                    octstr_len(entry->url)-1, 
                    octstr_len(*osp)-octstr_len(entry->url)+1));
                } else {
                    newurl = octstr_duplicate(entry->map_url);
                }
                debug("wsp",0,"WSP: URL Rewriten from <%s> to <%s>", 
                      octstr_get_cstr(*osp), octstr_get_cstr(newurl));
                octstr_destroy(*osp);
                *osp = newurl;
            }
            *accept_cookies = entry->accept_cookies;
            *send_msisdn_query = octstr_duplicate(entry->send_msisdn_query);
            *send_msisdn_header = octstr_duplicate(entry->send_msisdn_header);
            *send_msisdn_format = octstr_duplicate(entry->send_msisdn_format);
            octstr_destroy(tmp1);
            octstr_destroy(tmp2);
            break;
        }
        octstr_destroy(tmp1);
        octstr_destroy(tmp2);
    }
}

int wap_map_user(Octstr **msisdn, Octstr *user, Octstr *pass)
{
    long i;
    struct user_map_struct *entry;

    for (i = 0; user_map && i < list_len(user_map); i++) {
        entry = list_get(user_map, i);
        if (octstr_compare(user, entry->user)==0 && 
            octstr_compare(pass, entry->pass)==0) {
            *msisdn = octstr_duplicate(entry->msisdn);
            return 1;
        }
    }
    return 0;
}
