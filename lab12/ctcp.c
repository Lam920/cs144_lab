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

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

/** Retransmission interval in milliseconds. */
#define RT_TIMEOUT 200
/** Timer interval (for calls to ctcp_timer) in milliseconds. */
#define TIMER 40

/*Set current and received segment of connection*/
static void set_curr_recv_seg(ctcp_state_t *state, ctcp_segment_t *recv_segment);
static void set_curr_send_seg(ctcp_state_t *state, ctcp_segment_t *send_segment);

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
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */

  /* FIXME: Add other needed fields. */
  uint32_t seqno;              /* Current sequence number */
  uint32_t next_seqno;         /* Sequence number of next segment to send */
  uint32_t ackno;              /* Current ack number */
  uint16_t window;
  int number_timer_fired; /*Number of timer expired,after 5 times, retransmit*/
  int rt_times; /*This value must be less than 6, if it is bigger ==> teardown the connection*/
  int prev_seqno; /*Previous sequence number, used to check duplicate segment*/
  //int current_recv_ackno;

  ctcp_segment_t *current_send_seg; /*Current send segment, used to resend segment if there is any problem*/
  ctcp_segment_t *current_recv_seg; /*Current receive segment*/
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

  /* FIXME: Do any other initialization here. */
  state->seqno = 1;
  state->next_seqno = 1;
  state->ackno = 1;
  state->prev_seqno = 1;
  //state->current_recv_ackno = 1;

  state->window = MAX_SEG_DATA_SIZE;
  state->rt_times = 0;
  state->number_timer_fired = 0;

  /*Create empty send/recv segment of this connection*/
  state->current_recv_seg = calloc(sizeof(ctcp_segment_t) + MAX_SEG_DATA_SIZE, 1);
  state->current_send_seg = calloc(sizeof(ctcp_segment_t) + MAX_SEG_DATA_SIZE, 1);
  
  /*Init sequence number: 1 */
  state->current_recv_seg->ackno = state->current_recv_seg->seqno = htonl(1);
  state->current_send_seg->ackno = state->current_send_seg->seqno = htonl(1);
  /*set configuration for connection */

  cfg->send_window = MAX_SEG_DATA_SIZE;
  cfg->timer = TIMER;
  cfg->rt_timeout = RT_TIMEOUT;

  //time = current_time();

  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */
  
  free(state->current_recv_seg);
  free(state->current_send_seg);
 
  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */
  conn_t *conn = state->conn;
  char buff[MAX_SEG_DATA_SIZE];
  memset(buff,0,MAX_SEG_DATA_SIZE);

  /*Read input from STDIN*/
  int number_bytes = conn_input(state->conn,buff,MAX_SEG_DATA_SIZE);
  int length = number_bytes + sizeof(ctcp_segment_t);

  /*Create segment to send*/  
  ctcp_segment_t *segment = calloc(length,1);
  if(number_bytes > 0)memcpy(segment->data,buff,number_bytes);
  else if (number_bytes == -1)
  {
    user_send_fin(state); /*Send FIN segment*/
    return;
  }


  segment->flags |= htonl(ACK); //previous: 0 
  segment->seqno = htonl(state->seqno);
  
  segment->len = htons(length);
  segment->ackno = htonl(state->ackno);
  segment->window = htons(MAX_SEG_DATA_SIZE);
  segment->cksum = 0;
  segment->cksum = cksum(segment,length);
  
  state->next_seqno = state->seqno + number_bytes;
  state->seqno = state->next_seqno;  
  /*Set current send segment*/ 
  set_curr_send_seg(state,segment);

  /*Send segment*/
  conn_send(conn,segment,length);
  free(segment);
  
}

void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */
   if(segment == NULL)return;
#if 0  
   if(((segment->flags & TH_ACK) != 0 ) && (segment->len == htons(sizeof(ctcp_segment_t))))
   {
	//print_hdr_ctcp(segment);
   	state->number_timer_fired = state->rt_times = 0;
   	state_list->number_timer_fired = state_list->rt_times = 0;
	//state_list->number_timer_fired = state_list->rt_times = 0;
	//free(segment);
	return;
   }
#endif
  /*Check for corrupted packet by using cksum*/
  uint16_t current_cksum = segment->cksum;
  segment->cksum = 0;
   //if(ntohs(segment->len) != len)return;
  printf("Cksum:%x, cksum org:%x\n",cksum(segment,ntohs(segment->len)),current_cksum);
#if 1
  if(cksum(segment,len) != current_cksum)
  {
   	printf("Corrupted segment!\n");
	  return;
  }
#endif
   /*Set cksum again*/
  segment->cksum = current_cksum;
  
   /*Check for duplicate packet*/
  if(segment->seqno == state->prev_seqno && state->prev_seqno > 1 && strlen(segment->data) > 0 )
  {
	//conn_output(state->conn,state->current_recv_seg->data,len - sizeof(ctcp_segment_t));       
        //printf("Duplicate segment\n");
	//user_send_ack(state,segment);
    state->number_timer_fired = state->rt_times = 0;
	  free(segment);
	  return;
  }
  /*Set current recv segment*/
  set_curr_recv_seg(state,segment); 

  /*Handle FIN segment*/
  if((segment->flags & TH_FIN) != 0)
  {
    user_send_ack(state,segment);
    conn_output(state->conn,0,0);
    //free(segment);
    ctcp_destroy(state);
	  return;
  }
#if 1
  if(((segment->flags & TH_ACK) != 0 ) && (segment->len == htons(sizeof(ctcp_segment_t))))
  {
   	state->number_timer_fired = state->rt_times = 0;
   	state_list->number_timer_fired = state_list->rt_times = 0;
	  return;
  }
#endif
  
  
  ctcp_output(state);
  state->prev_seqno = segment->seqno;
  conn_output(state->conn,segment->data,ntohs(segment->len) - sizeof(ctcp_segment_t));
   /*Send ACK */
  user_send_ack(state,segment);
   
}

void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  /*User code*/
  int free_buffer_size = 0;
  free_buffer_size = conn_bufspace(state->conn);
  while(free_buffer_size < MAX_SEG_DATA_SIZE)
  {
    free_buffer_size = conn_bufspace(state->conn);
    if(free_buffer_size == MAX_SEG_DATA_SIZE)return;
	}
}

/*Timer call periodically*/
void ctcp_timer() {
  /* FIXME */
#if 1
  ctcp_state_t *state = state_list;
  if(state == NULL)
  {
    return;
  }
 
  if(state_list->rt_times == 5)
  {
    printf("Teardown the connection!\n");
    state_list->rt_times = 0;
    state_list->number_timer_fired = 0;
    ctcp_destroy(state);
    return;
  }

  if(state_list->number_timer_fired == 5)
  {
    printf("Retransmitted packet with data: %s\n",state_list->current_send_seg->data);
    print_hdr_ctcp(state_list->current_send_seg);
    conn_send(state_list->conn,state_list->current_send_seg,ntohs(state_list->current_send_seg->len));
    state_list->number_timer_fired = 0;
    state_list->rt_times +=1 ;
    return;
  }

/*Check for not receiving ACK */
if(ntohl(state_list->current_recv_seg->ackno) !=  state_list->next_seqno )
{
       // printf("ACK is not received\n");
	  state_list->number_timer_fired++;
}
else state_list->number_timer_fired = 0;
#endif

}

/*User defined function to send ACK */
void user_send_ack(ctcp_state_t *state, ctcp_segment_t *recv_segment)
{
  /* Confirm that we had receive number bytes of data */
  int len_data = strlen(recv_segment->data);
  state->ackno += len_data;

  ctcp_segment_t *ack_segment = calloc(sizeof(ctcp_segment_t), 1);
  ack_segment->flags = TH_ACK;
  ack_segment->seqno = htonl(state->seqno);
  ack_segment->ackno = htonl(state->ackno);
  ack_segment->len = htons(sizeof(ctcp_segment_t));
  ack_segment->window = htons(MAX_SEG_DATA_SIZE);
  ack_segment->cksum = cksum(ack_segment,sizeof(ctcp_segment_t));
  conn_send(state->conn,ack_segment,sizeof(ctcp_segment_t));
  free(ack_segment);
}

/*User defined function to send FIN*/
void user_send_fin(ctcp_state_t *state)
{
  ctcp_segment_t *ack_segment = calloc(sizeof(ctcp_segment_t), 1);
  ack_segment->flags |= TH_FIN;
  ack_segment->seqno = htonl(state->seqno);
  ack_segment->ackno = htonl(state->ackno);
  ack_segment->len = htons(sizeof(ctcp_segment_t));
  ack_segment->window = htons(MAX_SEG_DATA_SIZE);
  ack_segment->cksum = cksum(ack_segment,sizeof(ctcp_segment_t));
  conn_send(state->conn,ack_segment,sizeof(ctcp_segment_t));
  free(ack_segment);
}


/*Set current receive/send segment field in state of the connection*/
static void set_curr_recv_seg(ctcp_state_t *state, ctcp_segment_t *recv_segment)
{
  int len = ntohs(recv_segment->len) - sizeof(ctcp_segment_t);
  state->current_recv_seg->ackno = recv_segment->ackno;
  state->current_recv_seg->cksum = recv_segment->cksum;
  state->current_recv_seg->flags = recv_segment->flags;
  state->current_recv_seg->len = recv_segment->len;
  memset(state->current_recv_seg->data, 0,MAX_SEG_DATA_SIZE);
  state->current_recv_seg->seqno = recv_segment->seqno;
  state->current_recv_seg->window = recv_segment->window;
  memcpy(state->current_recv_seg->data,recv_segment->data,len);
}

static void set_curr_send_seg(ctcp_state_t *state, ctcp_segment_t *send_segment)
{
  int len = ntohs(send_segment->len) - sizeof(ctcp_segment_t);
  state->current_send_seg->ackno = send_segment->ackno;
  state->current_send_seg->cksum = send_segment->cksum;
  state->current_send_seg->flags = send_segment->flags;
  state->current_send_seg->len = send_segment->len;
  memset(state->current_send_seg->data, 0, MAX_SEG_DATA_SIZE);
  state->current_send_seg->seqno = send_segment->seqno;
  state->current_send_seg->window = send_segment->window;
  memcpy(state->current_send_seg->data,send_segment->data,len);
}
