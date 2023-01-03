/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/
/*GO BACK END implementation of cTCP*/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

#define N 4
/** Retransmission interval in milliseconds. */
#define RT_TIMEOUT 200
/** Timer interval (for calls to ctcp_timer) in milliseconds. */
#define TIMER 40

/*Set latest send ACK for resend */
void set_latest_send_ACK(ctcp_state_t *state, ctcp_segment_t *send_segment);
/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */
struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *recv_segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */

  /* FIXME: Add other needed fields. */
  linked_list_t *send_segment; /*send segment linked list*/
  uint32_t base; /* The sequence number of the oldest unacknowledged */
  uint32_t next_seqno; /*The smallest unused sequence number*/
  uint32_t current_ackno;
  uint32_t expected_seqno; /*Expected sequence number*/
  uint16_t send_window;
  uint16_t recv_window;
  
  int number_recv_success;

  /*For timer checking*/
  int number_timer_fired; //after 5 times, retransmit.
  int rt_times; /*This value must be less than 6, if it is bigger ==> teardown the connection*/

  ctcp_segment_t *latest_ack_send;
  int need_resend;  
  uint32_t latest_ackno;

};

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  state->base = 1;
  state->next_seqno = 1;
  state->current_ackno = 1;
  state->expected_seqno = 1;

  /*Create empty linked list for storing recv/send segment*/
  state->recv_segments = ll_create();
  state->send_segment = ll_create();
  /* FIXME: Do any other initialization here. */

  //cfg->send_window = N * MAX_SEG_DATA_SIZE;
  cfg->timer = TIMER;
  cfg->rt_timeout = RT_TIMEOUT;
  state->recv_window = cfg->recv_window;
  state->send_window = cfg->send_window;
  state->number_recv_success = 0;
  state->rt_times = state->number_timer_fired = 0;
  state->need_resend = 0;
  state->latest_ackno = 1;
  state->latest_ack_send = calloc(sizeof(ctcp_segment_t),1);

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */
  ll_destroy(state->recv_segments);
  ll_destroy(state->send_segment);

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  if(state->next_seqno < state->base + state->send_window)
  {
    printf("Packet can be sent!\n");
    char *buff = calloc(MAX_SEG_DATA_SIZE,1);
    //memset(buff,0,MAX_SEG_DATA_SIZE);
    int number_bytes = conn_input(state->conn,buff,MAX_SEG_DATA_SIZE);    
    if(number_bytes == -1)
    {
      user_send_fin(state);
      return;
    }

    //buff[number_bytes] = '\0';
    
    ctcp_segment_t *segment = calloc(sizeof(ctcp_segment_t) + number_bytes,1);
    segment->ackno = htonl(state->current_ackno);
    segment->flags |= htonl(ACK);
    segment->window = htons(state->send_window);
    segment->len = htons(number_bytes + sizeof(ctcp_segment_t));
    segment->seqno = htonl(state->next_seqno);
    memcpy(segment->data,buff,number_bytes);
    segment->cksum = cksum(segment,number_bytes + sizeof(ctcp_segment_t));

    /*Update next_seqno*/
    state->next_seqno += number_bytes;

    /*Send segment*/
    conn_send(state->conn,segment,number_bytes + sizeof(ctcp_segment_t));

    /*Them segment vua gui vao linked list de sau nay con gui lai*/
    ll_add_front(state->send_segment,segment);
    print_hdr_ctcp(segment);
    memset(buff,0,MAX_SEG_DATA_SIZE);
    free(buff);
  }
  else {
    printf("Wait for more available space!\n");
  }
}

/*
Note that since packets are delivered one at a time to the upper layer,
if packet k has been received and delivered, then all packets with a sequence number lower than k have
also been delivered. Thus, the use of cumulative acknowledgments is a natural choice for GBN.
*/

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
  if(segment == NULL)return;

#if 1
  /*Check for corruption*/
  uint16_t current_cksum = segment->cksum;
  segment->cksum = 0;
  uint16_t new_cksum = cksum(segment,ntohs(segment->len));
  //printf("Cksum: %x, Org cksum: %x\n",new_cksum,current_cksum);
  
  /*If segment is corrupted, send a special ACK with seqno and ackno = 0 :)*/
  if(new_cksum != current_cksum)
  {
    printf("Corrupted segment!\n");
    user_send_special_ack(state,segment);
    return;
  }
  segment->cksum = current_cksum;
#endif


  //printf("Recev segment str len: %d\n",(int)ntohs(segment->len) - (int)sizeof(ctcp_segment_t));
  /*Receive data segment.Incomming segment must have seq number equal to expected seqno to be display*/
  if( (segment->flags & TH_ACK) && strlen(segment->data) )
  {
    if(ntohl(segment->seqno) == state->expected_seqno)
    {
      //printf("Received data from sender!\n");
      ctcp_output(state);
      print_hdr_ctcp(segment);
      conn_output(state->conn,segment->data,ntohs(segment->len) - sizeof(ctcp_segment_t));
      user_send_ack(state,segment);
    }
    else
    {
	    //printf("Packet is discard due to out of order!");
	    /* resends an ACK for the most recently received in-order packet*/
	    user_send_ack(state,state->latest_ack_send);
	    //state->need_resend = 1;
    }
  }

  /*FIN segment received*/
  if((segment->flags & TH_FIN) != 0)
  {
    user_send_ack(state,segment);
    conn_output(state->conn,0,0);
    ctcp_destroy(state);
    return;
  }
  /*Sender: Receive ACK from Receiver*/
  if( (segment->flags & TH_ACK) && ((ntohs(segment->len) - sizeof(ctcp_segment_t)) == 0) ) 
  {

    if(segment->seqno == htonl(0))
    {
	//printf("Enter special case!\n");
      state->need_resend = 1;
      return;
    }
    if((state->latest_ackno == ntohl(segment->ackno)) && (state->latest_ackno > 1))
    {
      printf("Receive old ACK segment\n!");
      state->need_resend = 1;
      return;
    }

    state->base = ntohl(segment->ackno);
    //state->number_recv_success++;
	/*So packet da nhan thanh cong,free send packet*/
    if(state->base == state->next_seqno)
    {
      state->number_recv_success++;
      state->need_resend = 0;
      state->rt_times = state->number_timer_fired = 0;
      printf("Stop timer\n");
      state->latest_ackno = ntohl(segment->ackno);
    }
   
    //}
    else printf("Start timer\n");
    
	/*Delete segment that have been ACK successfully*/
    while(state->number_recv_success && (state->send_segment->head != NULL) )
    {
      ll_node_t *node = ll_back(state->send_segment);
      free(node->object);
      ll_remove(state->send_segment,node);
      state->number_recv_success--;    
    }

    printf("Sender received ACK, number of unACK sent segment: %d\n",ll_length(state->send_segment));	
  }

}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  int free_buffer_size = 0;
  free_buffer_size = conn_bufspace(state->conn);
  while(free_buffer_size == 0)
  {
    free_buffer_size = conn_bufspace(state->conn);
    if(free_buffer_size)return;
  }
}

void ctcp_timer() {
  /* FIXME */
#if 1
  ctcp_state_t *state = state_list;
  if(state == NULL)
  {
    //perror("There is no connection or some errors occur :>>");
    return;
  }

  if(state->send_segment->length)state->need_resend = 1;
  
  if(state->rt_times == 5)
  {
    
    printf("Teardown the connection!\n");
    state->rt_times = state->number_timer_fired = 0;
    ctcp_destroy(state);
  }

  if(state->number_timer_fired == 5)
  {
    //printf("Start to retransmitted all packet!\n");
    state->number_timer_fired = 0;
    int number_re_send = ll_length(state->send_segment);
     
    ll_node_t *resend_node = ll_back(state->send_segment);
 
    while(number_re_send)
    {
      ctcp_segment_t *resend_segment = ((ctcp_segment_t *)(resend_node->object));
      conn_send(state->conn,resend_segment,strlen(resend_segment->data)+sizeof(ctcp_segment_t));
      print_hdr_ctcp(resend_segment);
      resend_node = resend_node->prev;
      number_re_send --;
    }
    printf("Retransmit success!\n");
    //conn_send(state->conn,state->current_send_seg,state->current_send_seg->len);
    state->rt_times ++ ;
    state->need_resend = 0;
  }

  if(state->need_resend)
  {
    //state->need_resend = 0;
    //perror("Oh no, you haven't received the expected ACK signal :((");
    state->number_timer_fired++;
  }
  else state->number_timer_fired = 0;
  //printf("Need_resend: %d,number_timer_fired:%d,rt_times:%d\n",state->need_resend,state->number_timer_fired,state->rt_times);
#endif
}

void user_send_ack(ctcp_state_t *state, ctcp_segment_t *recv_segment)
{
  int len_data = (int)ntohs(recv_segment->len) - (int)sizeof(ctcp_segment_t);
  state->current_ackno += len_data;
  
  state->expected_seqno += len_data;

  ctcp_segment_t *ack_segment = calloc(sizeof(ctcp_segment_t), 1);
  ack_segment->flags = TH_ACK;
  ack_segment->seqno = htonl(state->next_seqno);
  ack_segment->ackno = htonl(state->expected_seqno);
  ack_segment->len = htons(sizeof(ctcp_segment_t));
  ack_segment->window = htons(state->send_window);
  ack_segment->cksum = cksum(ack_segment,sizeof(ctcp_segment_t));
  conn_send(state->conn,ack_segment,sizeof(ctcp_segment_t));

  /*Update expected sequence number of receiver*/
  set_latest_send_ACK(state,ack_segment);
  free(ack_segment);
}

void user_send_fin(ctcp_state_t *state)
{
  ctcp_segment_t *fin_segment = calloc(sizeof(ctcp_segment_t), 1);
  fin_segment->flags |= TH_FIN;
  fin_segment->seqno = htonl(state->next_seqno);
  fin_segment->ackno = htonl(state->current_ackno);
  fin_segment->len = htons(sizeof(ctcp_segment_t));
  fin_segment->window = htons(state->send_window);
  fin_segment->cksum = cksum(fin_segment,sizeof(ctcp_segment_t));
  conn_send(state->conn,fin_segment,sizeof(ctcp_segment_t));
  free(fin_segment);
}

void user_send_special_ack(ctcp_state_t *state, ctcp_segment_t *recv_segment)
{

  ctcp_segment_t *ack_segment = calloc(sizeof(ctcp_segment_t), 1);
  ack_segment->flags = TH_ACK;
  ack_segment->seqno = htonl(0);
  ack_segment->ackno = htonl(0);
  ack_segment->len = htons(sizeof(ctcp_segment_t));
  ack_segment->window = htons(state->send_window);
  ack_segment->cksum = cksum(ack_segment,sizeof(ctcp_segment_t));
  conn_send(state->conn,ack_segment,sizeof(ctcp_segment_t));
  free(ack_segment);
}
void set_latest_send_ACK(ctcp_state_t *state, ctcp_segment_t *send_segment)
{
  state->latest_ack_send->ackno = send_segment->ackno;
  state->latest_ack_send->cksum = send_segment->cksum;
  state->latest_ack_send->flags = send_segment->flags;
  state->latest_ack_send->len = send_segment->len;
  state->latest_ack_send->seqno = send_segment->seqno;
  state->latest_ack_send->window = send_segment->window;
  //memcpy(send_segment->data,state->current_send_seg->data,strlen(state->current_send_seg->data));
}
