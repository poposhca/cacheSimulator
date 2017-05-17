/*
 * cache.c
 */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE; 
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(int param, int value)
{

  switch (param) {
  case CACHE_PARAM_BLOCK_SIZE:
    //Se le manda el tamaño total de la linea en Bytes
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }

}
/************************************************************/

/************************************************************/
void init_cache()
{
  if(!cache_split)
  {
    c1.size = cache_usize/cache_assoc;
    c1.associativity = cache_assoc;
    c1.n_sets = c1.size/cache_block_size;
    //Mascara para el offset
    int bitsOffset = LOG2(cache_block_size);
    c1.index_mask_offset = (1 << bitsOffset) - 1;
    //Mascara para cada linea
    int bitsIndex = LOG2(c1.n_sets);
    c1.index_mask = ((1 << bitsIndex)-1) << bitsOffset;
    c1.LRU_head = (Pcache_line*)malloc(sizeof(cache_line) * c1.n_sets);
    for(int i = 0; i < c1.n_sets; i++)
      c1.LRU_head[i] = NULL;
  }
  else
  {
    int bitsOffset = LOG2(cache_block_size);

    //Cache de datos
    c1.size = cache_dsize/cache_assoc;
    c1.associativity = cache_assoc;
    c1.n_sets = c1.size/cache_block_size;
    c1.index_mask_offset = (1 << bitsOffset) - 1;
    int bitsIndexData = LOG2(c1.n_sets);
    c1.index_mask = ((1 << bitsIndexData) - 1) << bitsOffset;
    c1.LRU_head = (Pcache_line*)malloc(sizeof(cache_line) * c1.n_sets);
    for(int i = 0; i < c1.n_sets; i++)
      c1.LRU_head[i] = NULL;

    //Cahe de instrucciones
    c2.size = cache_isize/cache_assoc;
    c2.associativity = cache_assoc;
    c2.n_sets = c2.size/cache_block_size;
    int bitsIndexInst = LOG2(c2.n_sets);
    c2.index_mask = ((1 << bitsIndexInst) - 1) << bitsOffset;
    c2.LRU_head = (Pcache_line*)malloc(sizeof(cache_line) * c1.n_sets);
    for(int i = 0; i < c1.n_sets; i++)
      c2.LRU_head[i] = NULL;
  }

  //Inicializacion de estadisticas

  cache_stat_inst.accesses = 0;
  cache_stat_inst.misses = 0;
  cache_stat_inst.replacements = 0;
  cache_stat_inst.demand_fetches = 0;
  cache_stat_inst.copies_back = 0;

  cache_stat_data.accesses = 0;
  cache_stat_data.misses = 0;
  cache_stat_data.replacements = 0;
  cache_stat_data.demand_fetches = 0;
  cache_stat_data.copies_back = 0;

}
/************************************************************/

/************************************************************/
void perform_access(unsigned addr,unsigned  access_type)
{

  cache c = c1;
  if(access_type == TRACE_INST_LOAD && cache_split)
    c = c2;

  //Index
  int bitsOffset = LOG2(cache_block_size);
  int index = (addr & c.index_mask) >> bitsOffset;

  //TAG
  int bitsIndex = LOG2(c.n_sets);
  int bitsTag = WORD_SIZE * 8 - (bitsIndex + bitsOffset);
  int tagMask = ((1 << bitsTag) -1) << (bitsIndex + bitsOffset);
  int tag = (addr & tagMask) >> (bitsIndex + bitsOffset);

  //Encontrar linea tomando en cuenta sociatividad
  Pcache_line line = c.LRU_head[index];
  Pcache_line TAGline = NULL;
  int count = 0;
  while (count < c.associativity && line != NULL)
  {
    if(line->tag == tag){
      TAGline = line;
    }
    line = line->LRU_next;
    count++;
  }

  //Tipo de Acceso
  if(access_type == TRACE_INST_LOAD)
    cache_stat_inst.accesses++;
  else
    cache_stat_data.accesses++;

  //Politica WNA
  int wa_flag = TRUE;
  if(!cache_writealloc)
  {
    if(access_type == TRACE_DATA_STORE)
      wa_flag = FALSE;
  }

  //BLOQUE NULL, carga a cache lo que esta en memoria
  if(!line && !TAGline && count < c.associativity)
  {
    if(wa_flag)
    {
      //Crear linea
      Pcache_line newline = (Pcache_line)malloc(sizeof(cache_line));
      newline->tag = tag;
      if(access_type == TRACE_DATA_STORE)
        newline->dirty = 1;
      else
        newline->dirty = 0;
      //Inserta en la lista
      if(c.LRU_head[index])
      {
        Pcache_line *head = &c.LRU_head[index];
        Pcache_line tail = c.LRU_head[index];
        while(tail->LRU_next)
          tail = tail->LRU_next;
        insert(head, &tail, newline);
      }
      //Crea la lista
      else
      {
        newline->LRU_next = NULL;
        newline->LRU_prev = NULL;
        c.LRU_head[index] = newline;
      }
    }
    //Estadisticas
    if(access_type == TRACE_INST_LOAD)
    {
      cache_stat_inst.misses++;
      cache_stat_inst.demand_fetches += words_per_block;
    }
    else
    {
      cache_stat_data.misses++;
      if(wa_flag)
        cache_stat_data.demand_fetches += words_per_block;
      else
        cache_stat_data.copies_back += 1;
    }
    if(!cache_writeback && access_type == TRACE_DATA_STORE)
    {
      cache_stat_data.copies_back += 1;
    }
  }

  //LA LINEA ESTA LLENA Y NO TIENE EL TAG DESEADO
  if(!TAGline && count == c.associativity)
  {
    if(wa_flag)
    {
      //Reemplazo
      Pcache_line newline = (Pcache_line)malloc(sizeof(cache_line));
      newline->tag = tag;
      if(access_type == TRACE_DATA_STORE)
        newline->dirty = 1;
      else
        newline->dirty = 0;
      Pcache_line *head = &c.LRU_head[index];
      if(count < c.associativity)
        insert(head, NULL, newline);
      else
      {
        Pcache_line tail = c.LRU_head[index];
        while(tail->LRU_next)
          tail = tail->LRU_next;
        //Estadistica de reemplazo
        if(cache_writeback)
        {
          if(tail->dirty == 1 && access_type != TRACE_INST_LOAD && cache_split)
          {
            cache_stat_data.copies_back += words_per_block;
          }
          if(tail->dirty == 1 && !cache_split)
          {
            cache_stat_data.copies_back += words_per_block;
          }
        }
        else if(access_type == TRACE_DATA_STORE)
        {
          cache_stat_data.copies_back += 1;
        }
        delete(head, &tail, tail);
        if(c.LRU_head[index])
          insert(head, &tail, newline);
        else
        {
          newline->LRU_next = NULL;
          newline->LRU_prev = NULL;
          c.LRU_head[index] = newline;
        }
      }
    }

    //Estadisticas
    if(access_type == TRACE_INST_LOAD)
    {
      cache_stat_inst.misses++;
      cache_stat_inst.replacements++;
      cache_stat_inst.demand_fetches += words_per_block;
    }
    else
    {
      cache_stat_data.misses++;
      if(wa_flag)
      {
        cache_stat_data.replacements++;
        cache_stat_data.demand_fetches += words_per_block;
      }
      else
        cache_stat_data.copies_back += 1;
    }
  }

  //LINEA NO NULA Y EXISTE EL TAG
  if(TAGline)
  {
    if(access_type == TRACE_DATA_STORE)
    {
      if(cache_writeback)
        TAGline->dirty = 1;
      else
        cache_stat_data.copies_back += 1;
    }
    
    //Politica LRU
    if(c.associativity > 1 && c.LRU_head[index]->tag != tag)
    {
      Pcache_line *head = &c.LRU_head[index];
      Pcache_line tail = TAGline;
      while(tail->LRU_next)
        tail = tail->LRU_next;
      delete(head,&tail,TAGline);
      tail = c.LRU_head[index];
      while(tail->LRU_next)
        tail = tail->LRU_next;
      insert(head,&tail,TAGline);
    }
    
  }
  
}

/************************************************************/

/************************************************************/
void flush()
{
  if(cache_writeback)
  {
    for(int i=0; i<c1.n_sets; i++)
    {
      if(c1.LRU_head[i] != NULL)
      {
        Pcache_line tmp = c1.LRU_head[i];
        while(tmp != NULL)
        {
          if(tmp->dirty == 1){
            cache_stat_data.copies_back += words_per_block;
          }
          tmp = tmp->LRU_next;
        }
      }
    }
  }
}
/************************************************************/

/************************************************************/
void delete(Pcache_line *head, Pcache_line *tail, Pcache_line item)
{
  if (item->LRU_prev) {
    item->LRU_prev->LRU_next = item->LRU_next;
  } else {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next) {
    item->LRU_next->LRU_prev = item->LRU_prev;
  } else {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(Pcache_line *head, Pcache_line *tail, Pcache_line item)
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split) {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  } else {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n", 
	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
	 1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
	 1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches + 
	 cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
	 cache_stat_data.copies_back);
}
/************************************************************/
