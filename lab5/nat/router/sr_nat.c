#include <signal.h>
#include <assert.h>
#include "sr_nat.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int sr_nat_init(struct sr_nat *nat) { /* Initializes the nat */

  assert(nat);

  /* Acquire mutex lock */
  pthread_mutexattr_init(&(nat->attr));
  pthread_mutexattr_settype(&(nat->attr), PTHREAD_MUTEX_RECURSIVE);
  int success = pthread_mutex_init(&(nat->lock), &(nat->attr));

  /* Initialize timeout thread */

  pthread_attr_init(&(nat->thread_attr));
  pthread_attr_setdetachstate(&(nat->thread_attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(nat->thread_attr), PTHREAD_SCOPE_SYSTEM);
  pthread_create(&(nat->thread), &(nat->thread_attr), sr_nat_timeout, nat);

  /* CAREFUL MODIFYING CODE ABOVE THIS LINE! */

  nat->mappings = NULL; /*initial, no mapping*/
  /* Initialize any variables here */

  return success;
}


int sr_nat_destroy(struct sr_nat *nat) {  /* Destroys the nat (free memory) */

  pthread_mutex_lock(&(nat->lock));

  /* free nat memory here */

  pthread_kill(nat->thread, SIGKILL);
  return pthread_mutex_destroy(&(nat->lock)) &&
    pthread_mutexattr_destroy(&(nat->attr));

}

void *sr_nat_timeout(void *nat_ptr) {  /* Periodic Timout handling */
  struct sr_nat *nat = (struct sr_nat *)nat_ptr;
  while (1) {
    sleep(1.0);
    pthread_mutex_lock(&(nat->lock));

    time_t curtime = time(NULL);

    /* handle periodic tasks here */
    /*Check for unused entry and delete all of them if happen*/
    struct sr_nat_mapping *entry = nat->mappings;
    struct sr_nat_mapping *next = NULL, *prev = NULL;
    prev = entry;
    while(entry)
    {
      /*For ICMP*/

      if( difftime(curtime,entry->last_updated) > 60.0 && entry->type == (sr_nat_mapping_type)nat_mapping_icmp)
      {
        printf("ICMP/TCP query timeout");
        /*Delete node in mapping table */
        if(entry == nat->mappings)
        {
          nat->mappings = entry->next;
          struct sr_nat_mapping *temp = entry;
          entry = entry->next;
          free(temp);
        }
        else
        {
          prev->next = entry->next;
          struct sr_nat_mapping *temp = entry;
          entry = entry->next;
          free(temp);         
        }
      }
      else
      {
        prev = entry;
        entry = entry->next;
      }
    }


    pthread_mutex_unlock(&(nat->lock));
  }
  return NULL;
}

/* Get the mapping associated with given external port.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_external(struct sr_nat *nat,
    uint16_t aux_ext, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy */
  struct sr_nat_mapping *copy = NULL;
  struct sr_nat_mapping *entry = nat->mappings;
  /*Find matching entry in the mapping table*/
  while(entry)
  {
    if( (entry->aux_ext) == aux_ext && (entry->type == type) )
    {
      break;
    }
    entry = entry->next;
  }
  /*Copy and return matching entry*/
  if(entry)
  {
    copy = (struct sr_nat_mapping *)calloc(sizeof(struct sr_nat_mapping),1);
    memcpy(copy,entry,sizeof(struct sr_nat_mapping));
  }

  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Get the mapping associated with given internal (ip, port) pair.
   You must free the returned structure if it is not NULL. */
struct sr_nat_mapping *sr_nat_lookup_internal(struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {

  pthread_mutex_lock(&(nat->lock));

  /* handle lookup here, malloc and assign to copy. */
  struct sr_nat_mapping *copy = NULL;
  struct sr_nat_mapping *entry = nat->mappings;
  /*Find matching entry in the mapping table*/
  while(entry)
  {
    if( (entry->aux_int == aux_int) && (entry->type == type) && (entry->ip_int == ip_int) )
    {
      break;
    }
    entry = entry->next;
  }
  if(entry)
  {
    copy = (struct sr_nat_mapping *)calloc(sizeof(struct sr_nat_mapping),1);
    memcpy(copy,entry,sizeof(struct sr_nat_mapping));
  }

  pthread_mutex_unlock(&(nat->lock));
  return copy;
}

/* Insert a new mapping into the nat's mapping table.
   Actually returns a copy to the new mapping, for thread safety.
 */
struct sr_nat_mapping *sr_nat_insert_mapping(struct sr_instance* sr,struct sr_nat *nat,
  uint32_t ip_int, uint16_t aux_int, sr_nat_mapping_type type ) {
  static int assigned_port = 1024;
  pthread_mutex_lock(&(nat->lock));

  /* handle insert here, create a mapping, and then return a copy of it */
  struct sr_nat_mapping *mapping = NULL;
  struct sr_nat_mapping *new_mapping = NULL;
  struct sr_nat_mapping *check_map = NULL;
/*  
  for(check_map = nat->mappings;check_map != NULL;check_map = check_map->next)
  {
    if((check_map->ip_int == ip_int) && (check_map->aux_int == ip_int) && (check_map->type == type))
    {
      printf("This mapping already in mapping table\n");
      return check_map;
    }
  }
*/
  printf("1\n");
  new_mapping = calloc(sizeof(struct sr_nat_mapping),1);
  new_mapping->aux_int = aux_int; /*Port of sending packet /internal port*/
  new_mapping->ip_int = ip_int; /*Internal IP to map in mapping table / IP of sending packet*/
  new_mapping->type = type;
  new_mapping->aux_ext = assigned_port++ ; /*Assigned mapping port for external IP*/
  new_mapping->ip_ext = sr_get_interface(sr,"eth2")->ip; /*Set ip of external IP*/
  new_mapping->last_updated = time(NULL); /*Moi lan handle packet, update last_update*/
  printf("2\n");
  new_mapping->next = nat->mappings; /*Add this mapping entry into the head of the linked list*/
  nat->mappings = new_mapping;
  printf("3\n");

  mapping = (struct sr_nat_mapping *)calloc(sizeof(struct sr_nat_mapping),1);
  memcpy(mapping,new_mapping,sizeof(struct sr_nat_mapping)); /*Coppy and return new mapping entry*/
  printf("4\n");
  pthread_mutex_unlock(&(nat->lock));
  return mapping;
}

