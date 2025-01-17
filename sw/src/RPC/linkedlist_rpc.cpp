/**
 * @author  Zeke Wang <zeke.wang@inf.ethz.ch>
 * (c) 2019, ETH Zurich, Systems Group
 *
 */
//v0: the linked list and the related instructions will be in the cache after a few rounds. 
//v1: replicate enough linked lists such that each list traversal will cause cache misses. 

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <chrono>
#include <thread>
#include <random>
#include <vector>
#include <algorithm>
#include <iostream>

#include <rpc/rpc.h>
#include <boost/program_options.hpp>
#include <malloc.h>
#include <rpc/pmap_clnt.h>
//#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define LIST_PROG    0x23451111
#define LIST_VERS    1
#define LIST_FUNC    1
#define MAXARRAYSIZE 1024*1024
#define NUM_LIST     1024*128

using namespace std::chrono_literals;

struct arrayStruct{
   uint64_t * pAddr;
   uint64_t   size;
};

uint8_t  *pList[NUM_LIST]; //store each replicate of the linked list.
uint64_t *result;          //store the address of the result. 
static unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

// Generate a lookup table for 32bit operating system  
// using macro  
#define R2(n)    n,     n + 2*64,     n + 1*64,     n + 3*64 
#define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16) 
#define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 ) 
// Lookup table that store the reverse of each table 
uint32_t lookuptable[256] = { R6(0), R6(2), R6(1), R6(3) }; 


struct ListElement 
{
   uint64_t     key;
   ListElement* next;
   void*        value_ptr;
   uint64_t     valuesize;
};

void createLinkedList(unsigned char* block, uint64_t numElements, uint64_t valueSize) 
{
   //std::cout << "----------------- LINKED LIST ----------" << std::endl;
   uint64_t localBaseAddr = (uint64_t) block;
   ListElement* ePtr      = (ListElement*) block;
   ListElement* nextPtr;
   uint64_t* valuePtr     = (uint64_t*) (block + sizeof(ListElement) * numElements);
   //align to next cacheline

   for (uint32_t e = 0; e < numElements; ++e) 
   {
      ePtr->key           = e;
      ePtr->value_ptr     = (void*)valuePtr;
      ePtr->valuesize     = valueSize;
      nextPtr             = (ePtr + 1);
      ePtr->next          = (ListElement*) nextPtr;
      if ( e == (numElements-1) )
         ePtr->next       = (ListElement*)NULL;
      //std::cout << std::dec << "Element[" << e << "] key: " << std::hex << ePtr->key << " -> " << ePtr->value_ptr << ", next: " << ePtr->next << std::endl;
      ePtr++;
      for (uint64_t i = 0; i < ( valueSize&(~7) ); i += 8) //initiaze the value array. 
      {
         *valuePtr        = e*100 + i;
         valuePtr++;
      }
   }
   //std::cout << "--------------------------------- END ----------" << std::endl;
}

void printList(ListElement* head) 
{
   std::cout << "----------------- LINKED LIST ----------" << std::endl;
   ListElement* ePtr = head;
   size_t e = 0;
   while (ePtr != nullptr)
   {
      std::cout << std::dec << "Element[" << e << "] key: " << std::hex << ePtr->key << " -> " << ePtr->value_ptr << ", next: " << ePtr->next << std::endl;
      ePtr = ePtr->next;
      e++;
   }
   std::cout << "--------------------------------- END ----------" << std::endl;
}

bool_t xdr_returnArray(XDR *xdrs, struct arrayStruct *objp) 
{ 
   uint32_t numElemnts = objp->size/sizeof(uint64_t);
   if( !xdr_array(       xdrs, 
            (caddr_t *) &(objp->pAddr),  
               (u_int *) &numElemnts, 
                        MAXARRAYSIZE,  
                        sizeof(uint64_t), 
            (xdrproc_t) xdr_long) ) 
   { 
      return (FALSE); 
   } 
   return (TRUE); 
} 

static uint32_t sIndex     = 0;
//This function is called upon the rpc function is called by the remote node...
void list_svc(uint64_t *argp, uint64_t** result, struct svc_req *rqstp)
{
   int keyOffset           = 0;
   //std::cout << "The received key is: " << *argp << "\n";
   while (1)
   {
      /* insert server code here */
      uint32_t reverse_num =  (lookuptable[  sIndex       & 0xff ]<<9) |  
                              (lookuptable[ (sIndex >> 8) & 0xff ]<<1) |
                                          ( (sIndex >>16) & 1        );
      sIndex++;
      if (reverse_num >= NUM_LIST)
      {
         printf("reverse_num = %d is not valid, larger than NUM_LIST. \n", reverse_num);
         return;
      }
      
      ListElement* ePtr       = (ListElement*) pList[reverse_num]; //from the global address.
      
      if (ePtr[keyOffset].key != *argp)
      {
         if ( ePtr[keyOffset].next == (ListElement*)NULL )
         {
            std::cout << "The excepted key is not found: " << *argp << "\n";
            //*result  = 999;
            break;
         }
         keyOffset++;
      }
      else
      {
         *result              = (uint64_t*)(ePtr[keyOffset].value_ptr);
         break;
      }

   }
   return;
}
#if 0
void list_svc(uint64_t *argp, uint64_t** result, struct svc_req *rqstp)
{
   /* insert server code here */
   uint32_t reverse_num =  (lookuptable[    sIndex     & 0xff ]<<9) |  
                           (lookuptable[ (sIndex >> 8) & 0xff ]<<1) |
                                       ( (sIndex >>16) & 1        );
   sIndex++;
   if (reverse_num >= NUM_LIST)
   {
      printf("reverse_num = %d is not valid, larger than NUM_LIST. \n", reverse_num);
      return;
   }
   ListElement* ePtr       = (ListElement*) pList[reverse_num]; //from the global address.
   //std::cout << "The received key is: " << *argp << "\n";
   while (ePtr->key != *argp)
   {
      ePtr       = ePtr->next;
      if ( ePtr == (ListElement*)NULL )
      {
         std::cout << "The excepted key is not found: " << *argp << "\n";
         //*result  = 999;
         break;
      }
   }
   *result           = (uint64_t*)(ePtr->value_ptr);
   return;
}
#endif
//this function is registered...
static void list_prog(struct svc_req *rqstp, register SVCXPRT *transp)
{
   uint64_t  argument;
   uint64_t *result;

   xdrproc_t _xdr_argument, _xdr_result;
   void (*local)(uint64_t *, uint64_t **result, struct svc_req *);
//printf("1.\n");
   switch (rqstp->rq_proc) 
   {
      case NULLPROC:
         (void) svc_sendreply (transp, (xdrproc_t) xdr_void, (char *)NULL);
         return;
   
      case LIST_FUNC:
         _xdr_argument = (xdrproc_t) xdr_long;
         _xdr_result   = (xdrproc_t) xdr_returnArray;
         local         = (void (*) (uint64_t *, uint64_t**, struct svc_req *)) list_svc;
         break;
   
      default:
         svcerr_noproc (transp);
         return;
   }
   memset ((char *)&argument, 0, sizeof (argument));
   if (!svc_getargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) 
   {
      svcerr_decode (transp);
      return;
   }
//printf("2. argument = %d. \n", argument);

   (*local)((uint64_t *)&argument, &result, rqstp); //run the registered function 
//printf("3.\n");
//   for (int i = 0; i < 2; i++)
//      printf("the %d-th value is %d\n", i, result[i] );
//printf("3.1.\n");

   struct arrayStruct sendStruct;
   ListElement* ePtr  = (ListElement*) pList[0]; //from the global address.
   sendStruct.pAddr   = (uint64_t *)result;
   sendStruct.size    = ePtr->valuesize;

   if (result != NULL && !svc_sendreply(transp, (xdrproc_t) _xdr_result, (char *)&sendStruct)) 
   { //send back the result to the client.
      printf("Failed to send back the result.\n");
      svcerr_systemerr (transp);
   }
   if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) 
   {
      fprintf (stderr, "%s", "unable to free arguments");
      exit (1);
   }
//printf("4.\n");

   //free(result); //malloc in the list_svc function. 
   return;
}

//Verify correctness. 
//Bug report: if durations has only one element, it will be a bug...
void printf_stddev(std::vector<double> durations, uint32_t numberOfQueries, uint32_t numberOfElements, uint32_t valueSize) 
{
      //printHashTable((unsigned char*) dmaBuffer, numberOfElements, valueSize);
      //printHashTable((((unsigned char*) dmaBuffer) + hashTableSize), numberOfElements, valueSize);
      //Get stats
      double totalDurationNs = 0.0;
      for (uint32_t r = 0; r < numberOfQueries; ++r) 
         totalDurationNs += durations[r];
         //std::cout << "duration[" << r << "]: " << durations[r] << std::endl;

      double avgDurationNs = totalDurationNs /(double)numberOfQueries;
      double stddev = 0.0;
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         stddev += ((durations[r] - avgDurationNs) * (durations[r] - avgDurationNs));
      }
      stddev /= numberOfQueries;
      std::sort(durations.begin(), durations.end());
      double min = durations[0];
      double max = durations[numberOfQueries-1];
      double p25 = durations[(numberOfQueries/4)-1];
      double p50 = durations[(numberOfQueries/2)-1];
      double p75 = durations[((numberOfQueries*3)/4)-1];
      double p1  = 0.0;
      double p5  = 0.0;
      double p95 = 0.0;
      double p99 = 0.0;
      double iqr = p75 - p25;
      std::cout << "iqr: " << iqr << std::endl;
      double lower_iqr = p25 - (1.5 * iqr);
      double upper_iqr = p75 + (1.5 * iqr);
      if (numberOfQueries >= 100) {
         p1  = durations[((numberOfQueries)/100)-1];
         p5  = durations[((numberOfQueries*5)/100)-1];
         p95 = durations[((numberOfQueries*95)/100)-1];
         p99 = durations[((numberOfQueries*99)/100)-1];
      }
      double pliqr = durations[0];
      double puiqr = durations[0];
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         pliqr = durations[r];
         if (pliqr > lower_iqr) {
            break;
         }
      }
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         if (durations[r] > upper_iqr) {
            break;
         }
         puiqr = durations[r];
      }

      std::cout << "Size[B]: " << std::dec << valueSize << std::endl;
      std::cout << std::fixed  << "Duration[us]: " << avgDurationNs << std::endl;
      std::cout << std::fixed  << "Min: " << min << std::endl;
      std::cout << std::fixed  << "Max: " << max << std::endl;
      std::cout << std::fixed  << "Median: " << p50 << std::endl;
      std::cout << std::fixed  << "1th: " << p1 << std::endl;
      std::cout << std::fixed  << "5th: " << p5 << std::endl;
      std::cout << std::fixed  << "25th: " << p25 << std::endl;
      std::cout << std::fixed  << "75th: " << p75 << std::endl;
      std::cout << std::fixed  << "95th: " << p95 << std::endl;
      std::cout << std::fixed  << "99th: " << p99 << std::endl;
      std::cout << std::fixed  << "Lower IQR: " << pliqr << std::endl;
      std::cout << std::fixed  << "Upper IQR: " << puiqr << std::endl;
      std::cout << std::fixed  << "Duration stddev: " << stddev << std::endl;
      //double throughput = (((double) (transferSize*8)/(avgDurationNs)));
      //std::cout << std::fixed << "Throughput[Gbit/s]: " << throughput << std::endl;
      std::cout << std::fixed << "#" << numberOfElements << "\t" << numberOfQueries  << "\t" << valueSize << "\t" << avgDurationNs;
      std::cout << "\t" << min << "\t" << max << "\t";
      std::cout << p1 << "\t" << p5 << "\t" << p25 << "\t" << p50 << "\t" << p75 << "\t" << p95 << "\t" << p99 << "\t";
      std::cout << pliqr << "\t" << puiqr << "\t" << stddev << std::endl;
}

int main(int argc, char *argv[]) 
{
   //command line arguments
   boost::program_options::options_description programDescription("Allowed options");
   programDescription.add_options()("queries,q", boost::program_options::value<int>(), "Number of queries per iteration")
                                    ("elements,e", boost::program_options::value<int>(), "Number of Elements in hash table")
                                    ("valuesize,s", boost::program_options::value<int>(), "Value size in bytes for latency, kilobytes for thhroughput")
                                    //("repetitions,r", boost::program_options::value<int>(), "Number of repetitions")
                                    ("address", boost::program_options::value<std::string>(), "master ip address")
                                    ("warmup,w", boost::program_options::value<bool>(), "warm up")
                                    ("offload,o", boost::program_options::value<bool>(), "offload pointer chasing");

   boost::program_options::variables_map commandLineArgs;
   boost::program_options::store(boost::program_options::parse_command_line(argc, argv, programDescription), commandLineArgs);
   boost::program_options::notify(commandLineArgs);
   
   int32_t numberOfNodes      = 2;
   int32_t nodeId             = 0;
   uint32_t numberOfQueries   = 1;
   uint32_t numberOfElements  = 1;
   uint32_t valueSize         = 64;
   uint32_t numberRepetitions = 1;
   bool usePtrChase           = false;
   const char* masterAddr     = nullptr;
   bool runWarmup             = true;
   
   if (commandLineArgs.count("queries") > 0) {
      numberOfQueries = commandLineArgs["queries"].as<int>();
   } else {
      std::cerr << "queries argument missing.\n";
      return 1;
   }

   if (commandLineArgs.count("elements") > 0) {
      numberOfElements = commandLineArgs["elements"].as<int>();
   } else {
      std::cerr << "elements argument missing.\n";
      return 1;
   }

   if (commandLineArgs.count("valuesize") > 0) {
      valueSize = commandLineArgs["valuesize"].as<int>();
   } else {
      std::cerr << "valuesize argument missing.\n";
      return 1;
   }

   if (commandLineArgs.count("address") > 0) {
      nodeId = 1;
      masterAddr = commandLineArgs["address"].as<std::string>().c_str();
      std::cout << "master: " << masterAddr << std::endl;
   }

   if (commandLineArgs.count("warmup") > 0) {
      runWarmup = commandLineArgs["warmup"].as<bool>();
   }

   std::cout << "Number of Queries: "  << numberOfQueries << std::endl;
   std::cout << "Number of Elements: " << numberOfElements << std::endl;
   std::cout << "Value Size: "         << valueSize << std::endl;


   //TODO some checks on valueSize
   //assert(sizeof(HashTableEntry) == 64);
   uint64_t ListElementsSize = numberOfElements * sizeof(ListElement);
   if (ListElementsSize % 64 != 0) {
      ListElementsSize += (64 - (ListElementsSize % 64));
   }
   uint64_t ListValuesSize = numberOfElements * valueSize;
   uint64_t LinkedListSize = ListElementsSize + ListValuesSize;
   uint64_t resultSize     = numberOfQueries * valueSize + 64; //sizeof(ListElement);
   uint64_t allocSize      = LinkedListSize + resultSize;
   std::cout << std::dec << "element size: " << ListElementsSize << ", values size: " << ListValuesSize << std::endl;
   std::cout << std::dec << "linked list size: " << LinkedListSize << std::endl;
   std::cout << "allocation size: " << allocSize << std::endl;

   //sender
   double durationNs = 0.0;
   std::vector<double> durations;
   std::vector<uint32_t> queryIndexes;


   /* Default timeout can be changed using clnt_control() */
   static struct timeval TIMEOUT = { 25, 0 };


   //...the client side...
   if (nodeId == 1) 
   {
      std::default_random_engine rand_gen(seed);
      std::uniform_int_distribution<int> distr(0, numberOfElements-1); //random value from 0 to numberOfElements-1

      CLIENT *clnt;
      clnt = clnt_create (masterAddr, LIST_PROG, LIST_VERS, "tcp"); //udp
      if (clnt == NULL) {
         clnt_pcreateerror (masterAddr);
         exit (1);
      }


      //read all hash table entries and values
      for (uint32_t r = 0; r < numberRepetitions+1; ++r) {
         //volatile ListElement* ePtr = elementPtr;
         if (r == 0) 
         {
            if (!runWarmup)
                  continue;
            std::cout << "Warm up..." << std::endl;
         } 
         else 
         {
            std::cout << "Repetition: " << r <<  "/" << numberRepetitions << std::endl;
         }
         //memset((void*) (dmaBuffer+(LinkedListSize/sizeof(uint64_t))), 0, resultSize);
         queryIndexes.clear();
         queryIndexes.reserve(numberOfQueries);

         std::this_thread::sleep_for(2s);
         //auto start = std::chrono::high_resolution_clock::now();

         struct arrayStruct returnArrayInst;
         returnArrayInst.size  = valueSize;
         returnArrayInst.pAddr = (uint64_t *)malloc(valueSize);
         if (returnArrayInst.pAddr == NULL)
         {
            std::cout << "Malloc space for returnArrayInst.pAddr failed, size of: " << valueSize <<" \n";
            exit (1);
         }
         
         for (uint32_t q = 0; q < numberOfQueries; ++q) 
         {
            //std::cout << "Query: " << q << std::endl;

            auto start = std::chrono::high_resolution_clock::now();

            uint32_t randOffset = distr(rand_gen); 
            queryIndexes.push_back(randOffset);
            //key
            uint64_t requestedKey = randOffset; //determine the requested key...
            //printf("The %d-th requested key is %d. \n", q, requestedKey);

            uint64_t clnt_res;
            memset((char *)&clnt_res, 0, sizeof(clnt_res));

            if (clnt_call (clnt,                     //rpc instance
                           LIST_FUNC,                //rpc function id
                           (xdrproc_t) xdr_long,     // parameters
                           (caddr_t)  &requestedKey, //?
                           (xdrproc_t) xdr_returnArray,      //type
                           (caddr_t) &returnArrayInst,      //return value
                           TIMEOUT                   //timeout.
                          ) != RPC_SUCCESS ) 
            {
               std::cout << "No response from server when calling clnt_call. \n";
               return -1;
            }

            //check the result is correct or not. 
            for (int i = 0; i < returnArrayInst.size/sizeof(uint64_t); i++)
            {
               if ( (returnArrayInst.pAddr)[i] != (requestedKey*100 + i*8) )
               {
                  std::cout << "RESULT WRONG: Recevied: " << clnt_res << " Expected: " << requestedKey*100 << "\n";
                  exit(1);
               }
               //else
               //   printf("RESULT RIGHT: %d\t, ", (returnArrayInst.pAddr)[i]);
            }

            auto end   = std::chrono::high_resolution_clock::now();
            durationNs = (std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() / 1000.0);
            if (r != 0)
               durations.push_back(durationNs);
         }
      }
      std::cout << "numberOfQueries: " << numberOfQueries << ", numberOfElements: "  << numberOfElements << ", valueSize: "  << valueSize << "\n"; 
      printf_stddev(durations, numberOfQueries, numberOfElements, valueSize); //valueSize
   } 
   else  //server
   {
//pList[NUM_LIST]
      for (int i = 0; i < NUM_LIST; i++)
      {
         pList[i] = (unsigned char*) memalign(128, allocSize);
         if (pList[i] == NULL)
         {
            printf("Malloc memory for pList[%d] failed. \n", i);
            return 1;
         }
         createLinkedList(pList[i], numberOfElements, valueSize);
      }
      pmap_unset (LIST_PROG, LIST_VERS);
      result       = (uint64_t*) memalign(128, 1024*1024); //should free in the caller function. 
      if (result == NULL)
      {
         printf("Malloc memory for result failed. \n");
         return 1;
      }
      
      register SVCXPRT *transp;
      transp = svcudp_create(RPC_ANYSOCK);
      if (transp == NULL) {
         fprintf (stderr, "%s", "cannot create udp service.");
         exit(1);
      }
      if (!svc_register(transp, LIST_PROG, LIST_VERS, list_prog, IPPROTO_UDP)) {
         fprintf (stderr, "%s", "unable to register (ADD_PROG, ADD_VERS, udp).");
         exit(1);
      }
      transp = svctcp_create(RPC_ANYSOCK, 0, 0);
      if (transp == NULL) {
         fprintf (stderr, "%s", "cannot create tcp service.");
         exit(1);
      }
      if (!svc_register(transp, LIST_PROG, LIST_VERS, list_prog, IPPROTO_TCP)) {
         fprintf (stderr, "%s", "unable to register (ADD_PROG, ADD_VERS, tcp).");
         exit(1);
      }

      svc_run ();
      fprintf (stderr, "%s", "svc_run returned");
      exit (1);
   }

#if 0
   //Verify correctness
   if (nodeId == 1) 
   {
      //printHashTable((unsigned char*) dmaBuffer, numberOfElements, valueSize);
      //printHashTable((((unsigned char*) dmaBuffer) + hashTableSize), numberOfElements, valueSize);
      //Get stats
      double totalDurationNs = 0.0;
      for (uint32_t r = 0; r < numberOfQueries; ++r) 
         totalDurationNs += durations[r];
         //std::cout << "duration[" << r << "]: " << durations[r] << std::endl;

      double avgDurationNs = totalDurationNs /(double)numberOfQueries;
      double stddev = 0.0;
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         stddev += ((durations[r] - avgDurationNs) * (durations[r] - avgDurationNs));
      }
      stddev /= numberOfQueries;
      std::sort(durations.begin(), durations.end());
      double min = durations[0];
      double max = durations[numberOfQueries-1];
      double p25 = durations[(numberOfQueries/4)-1];
      double p50 = durations[(numberOfQueries/2)-1];
      double p75 = durations[((numberOfQueries*3)/4)-1];
      double p1  = 0.0;
      double p5  = 0.0;
      double p95 = 0.0;
      double p99 = 0.0;
      double iqr = p75 - p25;
      std::cout << "iqr: " << iqr << std::endl;
      double lower_iqr = p25 - (1.5 * iqr);
      double upper_iqr = p75 + (1.5 * iqr);
      if (numberOfQueries >= 100) {
         p1  = durations[((numberOfQueries)/100)-1];
         p5  = durations[((numberOfQueries*5)/100)-1];
         p95 = durations[((numberOfQueries*95)/100)-1];
         p99 = durations[((numberOfQueries*99)/100)-1];
      }
      double pliqr = durations[0];
      double puiqr = durations[0];
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         pliqr = durations[r];
         if (pliqr > lower_iqr) {
            break;
         }
      }
      for (uint32_t r = 0; r < numberOfQueries; ++r) {
         if (durations[r] > upper_iqr) {
            break;
         }
         puiqr = durations[r];
      }

      std::cout << "Size[B]: " << std::dec << valueSize << std::endl;
      std::cout << std::fixed  << "Duration[us]: " << avgDurationNs << std::endl;
      std::cout << std::fixed  << "Min: " << min << std::endl;
      std::cout << std::fixed  << "Max: " << max << std::endl;
      std::cout << std::fixed  << "Median: " << p50 << std::endl;
      std::cout << std::fixed  << "1th: " << p1 << std::endl;
      std::cout << std::fixed  << "5th: " << p5 << std::endl;
      std::cout << std::fixed  << "25th: " << p25 << std::endl;
      std::cout << std::fixed  << "75th: " << p75 << std::endl;
      std::cout << std::fixed  << "95th: " << p95 << std::endl;
      std::cout << std::fixed  << "99th: " << p99 << std::endl;
      std::cout << std::fixed  << "Lower IQR: " << pliqr << std::endl;
      std::cout << std::fixed  << "Upper IQR: " << puiqr << std::endl;
      std::cout << std::fixed  << "Duration stddev: " << stddev << std::endl;
      //double throughput = (((double) (transferSize*8)/(avgDurationNs)));
      //std::cout << std::fixed << "Throughput[Gbit/s]: " << throughput << std::endl;
      std::cout << std::fixed << "#" << numberOfElements << "\t" << numberOfQueries  << "\t" << valueSize << "\t" << avgDurationNs;
      std::cout << "\t" << min << "\t" << max << "\t";
      std::cout << p1 << "\t" << p5 << "\t" << p25 << "\t" << p50 << "\t" << p75 << "\t" << p95 << "\t" << p99 << "\t";
      std::cout << pliqr << "\t" << puiqr << "\t" << stddev << std::endl;
   }
#endif

	return 0;

}

