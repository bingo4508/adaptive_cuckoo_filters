/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "cuckoofilter.h"
#include "hash_functions.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include <sys/time.h>
#include <unistd.h>

using cuckoofilter::CuckooFilter;

int main(int argc, char** argv) {
    size_t total_items  = 1000000;
    size_t sht_max_buckets = 0;

    // Timing
    struct  timeval start;
    struct  timeval end;
    unsigned  long insert_t=0, lookup_t=0;

    uint32_t hash1=0;

    // Create a cuckoo filter where each item is of type size_t and
    // use 12 bits for each item:
    //    CuckooFilter<size_t, 12> filter(total_items);
    // To enable semi-sorting, define the storage of cuckoo filter to be
    // PackedTable, accepting keys of size_t type and making 13 bits
    // for each key:
    //   CuckooFilter<size_t, 13, cuckoofilter::PackedTable> filter(total_items);

    CuckooFilter<size_t, 12> filter(total_items);
    // Small hash table storing true negative caused by false positive
    int *sht;
    if(sht_max_buckets > 0)
        sht = new int[sht_max_buckets];

    // Insert items to this cuckoo filter
    size_t num_inserted = 0;
    gettimeofday(&start,NULL);
    for (size_t i = 0; i < total_items; i++, num_inserted++) {
	size_t index;
	uint32_t tag;

	filter.GenerateIndexTagHash(i, &index, &tag);
        if (filter.Add(i, index, tag) != cuckoofilter::Ok) {
            break;
        }
    }
    gettimeofday(&end,NULL);
    insert_t = 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;

    // Check if previously inserted items are in the filter, expected
    // true for all items
    for (size_t i = 0; i < num_inserted; i++) {
        assert(filter.Contain(i) == cuckoofilter::Ok);
    }

    // Check non-existing items, a few false positives expected
    size_t total_queries = 0;
    size_t false_queries = 0;
    size_t target;
    gettimeofday(&start,NULL);
    for (size_t i = total_items; i < 2 * total_items; i++) {
	size_t index;
	uint32_t tag;

	/*if(sht_max_buckets > 0) {
	    MurmurHash3_x86_32(&i, 256, 1384975, &hash1);
	    hash1 = hash1 % sht_max_buckets;
	}*/

	target = i;//1999461;
	if(sht_max_buckets > 0){
    	    filter.GenerateIndexTagHash(target, &index, &tag);
	    hash1 = index % sht_max_buckets;

	    if(sht[hash1] != target){
		if (filter.Contain(target, index, tag) == cuckoofilter::Ok) {
		    false_queries++;
		    if(sht_max_buckets > 0)
			sht[hash1] = target;
		}
	    }
	}else{
		// fp: 1999461
                if (filter.Contain(target) == cuckoofilter::Ok) {
		    //std::cout << i << std::endl;
                    false_queries++;
                }
	}

        /*if (filter.Contain(i) == cuckoofilter::Ok) {
            false_queries++;
        }*/

        total_queries++;
    }
    gettimeofday(&end,NULL);
    lookup_t = 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;


    std::cout << "Insert MOPS : " << (float)total_items/insert_t << "\n";
    std::cout << "Lookup MOPS : " << (float)total_items/lookup_t << "\n";

    // Output the measured false positive rate
    std::cout << "false positive rate is "
              << 100.0 * false_queries / total_queries
              << "%\n";

    return 0;
 }
