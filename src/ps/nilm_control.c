/* Direct-cable operator interface, TCP9002. One bounded ASCII line/connection.
 * No flash writes, model updates or network configuration commands. */
#include "nilm_control.h"
#include "lwip/tcp.h"
#include <string.h>
static struct tcp_pcb *peer;
static char command[384],reply[65536];
static unsigned used,length,offset,idle_ticks,done;
static void failed(void *arg,err_t err){(void)arg;(void)err;peer=0;}
static void abort_peer(void)
{
    struct tcp_pcb *p=peer;peer=0;
    if(p){tcp_err(p,0);tcp_recv(p,0);tcp_sent(p,0);tcp_poll(p,0,0);tcp_abort(p);}
}
static err_t idle(void *arg,struct tcp_pcb *p)
{(void)arg;(void)p;if(++idle_ticks>=5){abort_peer();return ERR_ABRT;}return ERR_OK;}
static err_t received(void *arg,struct tcp_pcb *p,struct pbuf *b,err_t err)
{
    (void)arg;
    if(!b || err!=ERR_OK){if(b)pbuf_free(b);abort_peer();return ERR_ABRT;}
    if(done || used+b->tot_len>=sizeof(command)){pbuf_free(b);abort_peer();return ERR_ABRT;}
    unsigned count=b->tot_len;pbuf_copy_partial(b,command+used,(u16_t)count,0);
    tcp_recved(p,b->tot_len);pbuf_free(b);used+=count;idle_ticks=0;
    for(unsigned n=0;n<used;n++){
        unsigned char c=(unsigned char)command[n];
        if(c=='\n'){
            if(n!=used-1){abort_peer();return ERR_ABRT;}
            command[n]=0;if(n && command[n-1]=='\r')command[n-1]=0;
            int len=nilm_event_request(command,reply,sizeof(reply));
            if(len<=0 || (unsigned)len>=sizeof(reply)){abort_peer();return ERR_ABRT;}
            length=(unsigned)len;done=1;break;
        }
        if((c<32 && c!='\r') || c>126){abort_peer();return ERR_ABRT;}
    }
    return ERR_OK;
}
static err_t accepted(void *arg,struct tcp_pcb *p,err_t err)
{
    (void)arg;if(peer || err!=ERR_OK){tcp_abort(p);return ERR_ABRT;}
    peer=p;used=length=offset=idle_ticks=done=0;
    tcp_arg(p,0);tcp_recv(p,received);tcp_err(p,failed);tcp_poll(p,idle,2);return ERR_OK;
}
int nilm_control_init(void)
{
    struct tcp_pcb *p=tcp_new_ip_type(IPADDR_TYPE_V4),*l;if(!p)return -1;
    if(tcp_bind(p,IP_ANY_TYPE,9002)!=ERR_OK){tcp_abort(p);return -1;}
    l=tcp_listen(p);if(!l){tcp_abort(p);return -1;}tcp_accept(l,accepted);return 0;
}
void nilm_control_poll(void)
{
    if(!peer || offset==length)return;
    unsigned n=length-offset;if(n>512)n=512;if(n>tcp_sndbuf(peer))n=tcp_sndbuf(peer);if(!n)return;
    err_t err=tcp_write(peer,reply+offset,(u16_t)n,TCP_WRITE_FLAG_COPY);
    if(err==ERR_MEM)return;
    if(err!=ERR_OK){abort_peer();return;}
    offset+=n;(void)tcp_output(peer);
}

