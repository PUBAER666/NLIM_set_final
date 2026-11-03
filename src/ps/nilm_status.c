/* TCP 9001: one bounded JSON line per connection; read-only, no commands.
 * Separate PCB from RAW1 so diagnostics do not claim the data connection. */
#include "nilm_status.h"
#include "lwip/tcp.h"
static struct tcp_pcb *peer;
static char reply[65536];
static unsigned length,offset,idle_ticks;
static void failed(void *arg,err_t e) {(void)arg;(void)e;peer=0;}
static void abort_peer(void)
{
    struct tcp_pcb *p=peer;peer=0;
    if(p){tcp_err(p,0);tcp_recv(p,0);tcp_sent(p,0);tcp_poll(p,0,0);tcp_abort(p);}
}
static err_t idle(void *arg,struct tcp_pcb *p)
{
    (void)arg;(void)p;
    if(++idle_ticks>=5){abort_peer();return ERR_ABRT;}
    return ERR_OK;
}
static err_t received(void *arg,struct tcp_pcb *p,struct pbuf *b,err_t e)
{
    (void)arg;(void)e;
    if(!b){abort_peer();return ERR_ABRT;}
    tcp_recved(p,b->tot_len);pbuf_free(b);return ERR_OK;
}
static err_t accepted(void *arg,struct tcp_pcb *p,err_t e)
{
    int n;(void)arg;
    if(peer || e!=ERR_OK){tcp_abort(p);return ERR_ABRT;}
    n=nilm_device_json(reply,sizeof(reply));
    if(n<=0 || (unsigned)n>=sizeof(reply)){tcp_abort(p);return ERR_ABRT;}
    peer=p;length=(unsigned)n;offset=idle_ticks=0;
    tcp_arg(p,0);tcp_recv(p,received);tcp_err(p,failed);tcp_poll(p,idle,2);
    return ERR_OK;
}
int nilm_status_init(void)
{
    struct tcp_pcb *p=tcp_new_ip_type(IPADDR_TYPE_V4),*l;
    if(!p)return -1;
    if(tcp_bind(p,IP_ANY_TYPE,9001)!=ERR_OK){tcp_abort(p);return -1;}
    l=tcp_listen(p);if(!l){tcp_abort(p);return -1;}
    tcp_accept(l,accepted);return 0;
}
void nilm_status_poll(void)
{
    unsigned n;err_t e;
    if(!peer || offset==length)return;
    n=length-offset;if(n>512)n=512;if(n>tcp_sndbuf(peer))n=tcp_sndbuf(peer);
    if(!n)return;
    e=tcp_write(peer,reply+offset,(u16_t)n,TCP_WRITE_FLAG_COPY);
    if(e==ERR_MEM)return;
    if(e!=ERR_OK){abort_peer();return;}
    offset+=n;(void)tcp_output(peer);
    /* Client closes after newline; idle callback also bounds abandoned peers. */
}

