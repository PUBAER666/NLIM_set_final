#include "nilm_net.h"
#include "nilm_wire.h"
#include "lwip/tcp.h"
#include "xil_printf.h"
static struct tcp_pcb *client;
static nw_queue queue;
static int stopped;
static int ready;
int nilm_net_ready(void){return ready && !stopped;}
int nilm_net_connected(void){return client!=0;}
static void failed(void *arg,err_t e)
{(void)arg;(void)e;client=0;nw_reset(&queue);}
static void disconnect(void)
{
    struct tcp_pcb *p=client;client=0;nw_reset(&queue);
    if(p) {tcp_err(p,0);tcp_recv(p,0);tcp_abort(p);}
}
static err_t receive(void *arg,struct tcp_pcb *p,struct pbuf *b,err_t e)
{
    (void)arg;(void)e;
    if(!b) {disconnect();return ERR_ABRT;}
    tcp_recved(p,b->tot_len);pbuf_free(b);return ERR_OK;
}
static err_t accept_client(void *arg,struct tcp_pcb *p,err_t e)
{
    (void)arg;
    if(e!=ERR_OK || client || stopped) {tcp_abort(p);return ERR_ABRT;}
    nw_reset(&queue);client=p;tcp_arg(p,0);tcp_recv(p,receive);tcp_err(p,failed);
    tcp_nagle_disable(p);
    xil_printf("NILM NET: client connected, TCP 9000 RAW1\r\n");return ERR_OK;
}
int nilm_net_init(void)
{
    struct tcp_pcb *p=tcp_new_ip_type(IPADDR_TYPE_V4),*listener;
    stopped=0;ready=0;nw_reset(&queue);
    if(!p)return -1;
    if(tcp_bind(p,IP_ANY_TYPE,9000)!=ERR_OK) {tcp_abort(p);return -1;}
    listener=tcp_listen(p);
    if(!listener) {tcp_abort(p);return -1;}
    tcp_accept(listener,accept_client);
    ready=1;
    xil_printf("NILM NET: RAW1 data server @ TCP 9000; connect to stream\r\n");return 0;
}
void nilm_net_push(const nilm_stream_desc *d,const nilm_stream_sample *s,uint64_t ms,uint64_t gaps)
{
    if(client && !nw_push(&queue,d,s,ms,gaps) && (queue.dropped==1 || queue.dropped%25==0))
        xil_printf("NILM NET: slow receiver, dropped_blocks=%u\r\n",(unsigned)queue.dropped);
}
void nilm_net_poll(void)
{
    uint32_t n,space;const unsigned char *p;err_t e;
    if(!client)return;
    p=nw_peek(&queue,&n);if(!n)return;
    space=tcp_sndbuf(client);if(!space)return;
    if(n>space)n=space;
    if(n>1460)n=1460;
    e=tcp_write(client,p,(u16_t)n,TCP_WRITE_FLAG_COPY);
    if(e==ERR_MEM)return;
    if(e!=ERR_OK) {disconnect();return;}
    /* COPY transfers ownership to lwIP; never retain a history/BRAM pointer. */
    nw_consume(&queue,n);(void)tcp_output(client);
}
void nilm_net_stop(void) {stopped=1;disconnect();}
