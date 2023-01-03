/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/
void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);
 
  /*Set if packet is ARP or IP ?*/
  int is_arp = 0;
  int is_ip = 0;

  printf("*** -> Received packet of length %d \n",len);
  /* fill in code here */
  sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *)packet;

  if(ntohs(ether_hdr->ether_type) == (enum sr_ethertype)ethertype_arp){
    printf("ARP packet!\n");
    is_arp = 1;
    is_ip = 0;
  }
  else if(ntohs(ether_hdr->ether_type) == (enum sr_ethertype)ethertype_ip)
  {
    printf("IP packet is received!\n");
    is_arp = 0;
    is_ip = 1;
  }

  /*Handle ARP packet*/
  if(is_arp)
  {
    sr_arp_hdr_t *arp_hdr = (sr_arp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t));
    if(ntohs(arp_hdr->ar_op) == (enum sr_arp_opcode)arp_op_request)
    {
      sr_arpcache_insert(&(sr->cache),arp_hdr->ar_sha,(arp_hdr->ar_sip));
      sr_send_arp_reply(sr,interface,ether_hdr,arp_hdr,packet,len);
    }
    else if(ntohs(arp_hdr->ar_op) == (enum sr_arp_opcode)arp_op_reply)
    {
      /*Insert ARP in ARP table*/
      struct sr_arpreq *ret_arpreq = sr_arpcache_insert(&(sr->cache),arp_hdr->ar_sha,(arp_hdr->ar_sip));     
      /*Send all packet waiting on this ARP reply*/
      if(ret_arpreq)
      {
      	struct sr_packet *packets = ret_arpreq->packets;
      	while(packets)
      	{
          struct sr_arpentry *sent_gw = sr_arpcache_lookup(&sr->cache,ret_arpreq->ip);
          sr_ip_hdr_t *ip_hdr = ((sr_ip_hdr_t *)(packets->buf + sizeof(sr_ethernet_hdr_t)));

	        memcpy(((sr_ethernet_hdr_t *)packets->buf)->ether_dhost,arp_hdr->ar_sha,ETHER_ADDR_LEN); /*Update destination MAC*/
          memcpy(((sr_ethernet_hdr_t *)packets->buf)->ether_shost,arp_hdr->ar_tha,ETHER_ADDR_LEN);
          sr_send_packet(sr,packets->buf,packets->len,packets->iface);
           packets = packets->next;
      	}
	      sr_arpreq_destroy(&(sr->cache),ret_arpreq);     
	      printf("\nPacket waiting for ARP reply is sent!\n");
      }
 
   }
  }


  if(is_ip)
  {
    /*Get ip header*/
    printf("Enter IP packet!\n");
    sr_ip_hdr_t *ip_hdr = ((sr_ip_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t)));
    uint32_t des_ip = (ip_hdr->ip_dst); /*Get des IP*/
    /*Check for matching in routing table*/
    print_hdr_ip(ip_hdr);
    /*Check for packet is comming into router IF or not*/
    printf("[+]Check for destination is one of the router's interfaces or not!\n");
    
    ip_hdr->ip_ttl = ip_hdr->ip_ttl - 1; /*Update Time to Live*/
    ip_hdr->ip_sum = 0;
    ip_hdr->ip_sum = cksum(ip_hdr,sizeof(sr_ip_hdr_t));
    printf("TTL: %d\n",ip_hdr->ip_ttl);
    
    if(ip_hdr->ip_ttl == 0)
    {
      printf("Send ICMP TTL!\n");
      sr_send_ICMP_error(sr,interface,ether_hdr,ip_hdr,packet,len,11,0,1);
      return;
    }

    /*Check for IP packet is for Interface or not?*/
    if (check_for_if_target(sr->if_list,des_ip) && ((ip_hdr->ip_p) == (enum sr_ip_protocol)ip_protocol_icmp))
    {
      printf("This is ICMP packet for router!\n");
      printf("Resend ICMP packet to client\n");
      sr_icmp_hdr_t *icmp_hdr = ((sr_icmp_hdr_t *)(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));
      if((icmp_hdr->icmp_code == 0) && (icmp_hdr->icmp_type == 8))sr_send_ICMP(sr,interface,ether_hdr,ip_hdr,packet,len,0,0);     
      return;
    }

    else if (check_for_if_target(sr->if_list,des_ip) && ((ip_hdr->ip_p) != (enum sr_ip_protocol)ip_protocol_icmp))
    {
      /*ip_hdr->ip_dst = sr_get_interface(sr,interface)->ip;*/
      sr_send_ICMP_error(sr,interface,ether_hdr,ip_hdr,packet,len,3,3,1);
      printf("Send ICMP unreachable port\n");
      return;
    }

    printf("[+]Check ok!\n");

    struct sr_rt* rt_walker = 0;
    rt_walker = sr->routing_table;
   
    struct sr_rt* entry = longest_prefix_entry(rt_walker,des_ip);
    if(entry)
    {
      print_addr_ip(entry->dest);
      print_addr_ip(entry->gw);
      	
/*print_addr_ip_int(entry->mask.s_addr);*/
#if 1
      struct sr_arpentry *forward_gw = NULL;
      if(entry)
      {
      	forward_gw = sr_arpcache_lookup(&(sr->cache),entry->gw.s_addr);
      	if(forward_gw)
      	{
          print_addr_ip_int(forward_gw->ip);
          memcpy(((sr_ethernet_hdr_t *)packet)->ether_dhost,forward_gw->mac,ETHER_ADDR_LEN); /*Update destination MAC*/
          memcpy(((sr_ethernet_hdr_t *)packet)->ether_shost,sr_get_interface(sr,entry->interface)->addr,ETHER_ADDR_LEN);
          sr_send_packet(sr,packet,len,entry->interface);
          printf("Sent packet to the next hop!\n");
      	}
      	else 
      	{
        	struct sr_arpreq *arpreq = sr_arpcache_queuereq(&(sr->cache),entry->gw.s_addr,packet,len,entry->interface);
        	handle_arpreq(sr,&(sr->cache),arpreq);
      	}
        forward_gw = NULL;
      }

    else printf("No match found\n");   
#endif

    }
  }

}/* end sr_ForwardPacket */


#if 1
void sr_send_arp_reply(struct sr_instance* sr,char* iface,sr_ethernet_hdr_t *send_ether_hdr,sr_arp_hdr_t *send_arp_hdr, uint8_t *packet, unsigned int len)
{
  uint8_t *buff = (uint8_t *)calloc(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t),1);
  sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *)buff;
  sr_arp_hdr_t *arp_hdr = ((sr_arp_hdr_t *)(buff + sizeof(sr_ethernet_hdr_t)));
  ether_hdr->ether_type = htons((enum sr_ethertype)ethertype_arp);
  memcpy(ether_hdr->ether_shost,sr_get_interface(sr,iface)->addr,ETHER_ADDR_LEN);
  memcpy(ether_hdr->ether_dhost,send_ether_hdr->ether_shost,ETHER_ADDR_LEN);
  arp_hdr->ar_hln = send_arp_hdr->ar_hln;
  arp_hdr->ar_hrd = htons(1);
  arp_hdr->ar_pro = htons(2048);
  arp_hdr->ar_pln = send_arp_hdr->ar_pln;
  arp_hdr->ar_op = htons((enum sr_arp_opcode)arp_op_reply);
  arp_hdr->ar_tip = send_arp_hdr->ar_sip;
  arp_hdr->ar_sip = send_arp_hdr->ar_tip;
  memcpy(arp_hdr->ar_sha,ether_hdr->ether_shost,ETHER_ADDR_LEN);
  memcpy(arp_hdr->ar_tha,ether_hdr->ether_dhost,ETHER_ADDR_LEN);
  sr_send_packet(sr,buff,(int)(sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)),iface);
 
  free(buff);
}
#endif

struct sr_rt* longest_prefix_entry(struct sr_rt* routing_table,uint32_t des_ip)
{
    struct sr_rt* rt_walker = 0;
    struct sr_rt* ret_entry = NULL;
    int longest_prefix = 0;
    rt_walker = routing_table;
    while(rt_walker)
    {
      if( (rt_walker->mask.s_addr & des_ip) == rt_walker->dest.s_addr)
      {
        /*printf("Found match entry in routing table");*/
        if((rt_walker->mask.s_addr & des_ip) >= longest_prefix)
        {
          ret_entry = rt_walker;
          longest_prefix = (rt_walker->mask.s_addr & des_ip);
        }
        
      }
      rt_walker = rt_walker->next;
    } 
    return ret_entry;
}

/*Check for packet is comming into router IF or not*/
int check_for_if_target(struct sr_if *ref_if_list, uint32_t des_ip)
{
   while(ref_if_list)
    {
      if(des_ip == ref_if_list->ip)
      {
        printf("Packet is sent to one of router's Interface!\n");
        printf("IF match: %s\n",ref_if_list->name);
        return 1;
      }
      ref_if_list = ref_if_list->next;
    }
    return 0;
}


void sr_send_ICMP(struct sr_instance* sr,char* iface,sr_ethernet_hdr_t *send_ether_hdr,sr_ip_hdr_t *send_ip_hdr, uint8_t *packet, unsigned int len, uint8_t type, uint8_t code)
{
  uint8_t *buff = calloc(len,1);
  memcpy(buff,packet,len);

  sr_ip_hdr_t *ip_hdr = ((sr_ip_hdr_t *)(buff + sizeof(sr_ethernet_hdr_t)));
  sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *)buff;
  sr_icmp_t3_hdr_t *icmp_hdr = ((sr_icmp_t3_hdr_t *)(buff + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));
  uint16_t *id = (uint16_t *)(buff + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t));
  printf("ID ICMP: %x, %x\n",(int)ntohs(id[0]),(int)ntohs(id[1])); 
  /*Update MAC in reply packet*/
  memset(ether_hdr->ether_shost,0,ETHER_ADDR_LEN);
  memset(ether_hdr->ether_dhost,0,ETHER_ADDR_LEN);

  memcpy(ether_hdr->ether_shost,sr_get_interface(sr,iface)->addr,ETHER_ADDR_LEN);
  memcpy(ether_hdr->ether_dhost,send_ether_hdr->ether_shost,ETHER_ADDR_LEN);

  /*Update IP header*/
  /*Step 1: update SRC/DST IP*/

  ip_hdr->ip_src = send_ip_hdr->ip_dst;
  ip_hdr->ip_dst = send_ip_hdr->ip_src;
/*  
  ip_hdr->ip_src = sr_get_interface(sr,iface)->ip;
  ip_hdr->ip_dst = send_ip_hdr->ip_src;
*/
  /*Update ICMP header*/
  icmp_hdr->icmp_code = code;
  icmp_hdr->icmp_type = type;
  sr_send_packet(sr,buff,(int)(len),iface);
  printf("Sizeof ICMP : %d\n",len);
}

void sr_send_ICMP_error(struct sr_instance* sr,char* iface,sr_ethernet_hdr_t *send_ether_hdr,sr_ip_hdr_t *send_ip_hdr,uint8_t *packet,unsigned int len, uint8_t type, uint8_t code,int set)
{
  
  int size_of_icmp = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t) + ICMP_DATA_SIZE;
  uint8_t *buff = calloc(size_of_icmp,1);
  /*memcpy(buff,packet,size_of_icmp);*/
  uint32_t old_ip = send_ip_hdr->ip_dst;
  if(set == 1)send_ip_hdr->ip_dst = sr_get_interface(sr,iface)->ip;
  sr_ip_hdr_t *ip_hdr = ((sr_ip_hdr_t *)(buff + sizeof(sr_ethernet_hdr_t)));
  sr_ethernet_hdr_t *ether_hdr = (sr_ethernet_hdr_t *)buff;
  sr_icmp_t3_hdr_t *icmp_hdr = ((sr_icmp_t3_hdr_t *)(buff + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));
  
  ether_hdr->ether_type = htons((enum sr_ethertype)ethertype_ip);
  /*Update MAC in reply packet*/

  memcpy(ether_hdr->ether_shost,sr_get_interface(sr,iface)->addr,ETHER_ADDR_LEN);
  memcpy(ether_hdr->ether_dhost,send_ether_hdr->ether_shost,ETHER_ADDR_LEN);

  /*Update IP header*/
  /*Step 1: update SRC/DST IP*/
  ip_hdr->ip_src = send_ip_hdr->ip_dst;
  ip_hdr->ip_dst = send_ip_hdr->ip_src;
  ip_hdr->ip_v = 4;
  ip_hdr->ip_hl = 5;
/*  ip_hdr->ip_src = sr_get_interface(sr,iface)->ip;
  ip_hdr->ip_dst = send_ip_hdr->ip_src;
*/  
  ip_hdr->ip_tos = send_ip_hdr->ip_tos;
  ip_hdr->ip_len = htons(sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t)); /*56 bytes*/
  ip_hdr->ip_id = 0;
  ip_hdr->ip_off = send_ip_hdr->ip_off;
  ip_hdr->ip_ttl = 64;
  ip_hdr->ip_p = (enum sr_ip_protocol)ip_protocol_icmp;
  ip_hdr->ip_sum = cksum(ip_hdr,sizeof(ip_hdr) + sizeof(sr_icmp_t3_hdr_t));

  /*Update ICMP header*/
  icmp_hdr->icmp_code = code;
  icmp_hdr->icmp_type = type;
  send_ip_hdr->ip_dst = old_ip;
  memcpy(icmp_hdr->data,send_ip_hdr,sizeof(sr_ip_hdr_t)); 
  if(set == 1)memcpy(icmp_hdr->data + sizeof(sr_ip_hdr_t), packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t), len - 2*sizeof(sr_ip_hdr_t) - sizeof(sr_ethernet_hdr_t));
  icmp_hdr->icmp_sum = cksum(icmp_hdr,sizeof(icmp_hdr) + len - 2*sizeof(sr_ip_hdr_t) - sizeof(sr_ethernet_hdr_t) + sizeof(ip_hdr));
  
  sr_send_packet(sr,buff,size_of_icmp - ICMP_DATA_SIZE,iface);
  printf("Sizeof ICMP : %d\n",98);
 
}
