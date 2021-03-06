/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef _ADAPTIVE_CUCKOO_FILTERS_H_
#define _ADAPTIVE_CUCKOO_FILTERS_H_

#include "cuckoofilter.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <typeinfo>

#include <sys/time.h>
#include <unistd.h>

#define STRING_2

using namespace::cuckoofilter;
using namespace::std;

namespace adaptive_cuckoofilters {
	enum Status {
		Found = 0,
		NotFound = 1,
		NoFilter = 2,
		Nothing = 3,
		NeedRebuild = 4,
		NeedShrink = 5
	};

	enum NCType {
		NUMERIC = 10,
		STRING = 11
	};

	// Parameters
	const size_t grow_shrink_bucket = 5;
	const size_t grow_threshold = 5;

	// nc: Negative cache - small hash table
	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	class AdaptiveCuckooFilters {
		// Storage of items
		CuckooFilterInterface **filter;
		CuckooFilter<ItemType, bits_per_tag> *dummy_filter;

		size_t item_bytes;
		size_t max_filters;
		size_t single_cf_size;

		// True negative cache
		NCType nc[nc_buckets];
		size_t nc_type;

		// Statistics counters
		size_t *fpp;
		size_t *lookup;
		float *lookup_fr_old;
		size_t *insert_keys;
		size_t *insert_keys_old;
		bool *shrink_end;

		size_t overall_fpp, all_lookup, overall_fpp2;
		size_t  num_grow, num_shrink, true_neg;
		size_t NC_hit;

		// Memory
		size_t cur_mem;
		size_t mem_budget;

		// Temp variables
		size_t raw_index, t_nc_hash, t_status;
		uint32_t index, tag, hash1;
		
		// Prevent the just growed filter from shrink
		uint32_t pinned_filter;

		inline bool CompareNC(size_t idx, const ItemType& item) {
#ifdef STRING_2
			string s(item);
			return nc[idx].compare(s) == 0;
#else
			return item == nc[idx];	
#endif
		}

		inline void InsertNC(size_t idx, const ItemType& item) {
			// TODO: Won't this cuase memory leak?
			nc[idx] = item;
		}

		inline void ClearNC(size_t idx) {
#ifdef STRING_2
			nc[idx].clear();
#else
			nc[idx] = 0;
#endif
		}

		vector<string> &split(const string &s, char delim, vector<string> &elems) {
			stringstream ss(s);
			string item;
			while (getline(ss, item, delim)) {
				elems.push_back(item);
			}
			return elems;
		}


		vector<string> split(const string &s, char delim) {
			vector<string> elems;
			split(s, delim, elems);
			return elems;
		}

public:
		explicit AdaptiveCuckooFilters(size_t b, size_t f, size_t c, size_t mem):overall_fpp(0), cur_mem(0), num_grow(0), num_shrink(0), true_neg(0), all_lookup(0), NC_hit(0){
			max_filters = f;
			single_cf_size = c;
			item_bytes = b;

			filter = new CuckooFilterInterface *[max_filters];
			dummy_filter = new CuckooFilter<ItemType, bits_per_tag>(4, false, false);

			fpp = new size_t[max_filters];
			lookup = new size_t[max_filters];
			lookup_fr_old = new float[max_filters];	
			insert_keys = new size_t[max_filters];
			insert_keys_old = new size_t[max_filters];
			shrink_end = new bool[max_filters];


			for(size_t i=0; i<max_filters; i++){
				fpp[i] = 0;
				lookup[i] = 1;
				lookup_fr_old[i] = max_filters;
				insert_keys[i] = 0;
				insert_keys_old[i] = 0;
				shrink_end[i] = false;

				filter[i] = new CuckooFilter<ItemType, bits_per_tag> (single_cf_size, false, false);
//cout << "new filter: " << i << endl;
				cur_mem += filter[i]->SizeInBytes();
			}

			// test
			/*cout << "test: \n";
			cout << "0: " << filter[0]->num_buckets << endl;
			cout << "1: " << filter[1]->num_buckets << endl;
			filter[1] = new CuckooFilter<ItemType, 32> (single_cf_size*3, false, false);
			cout << "0: " << filter[0]->num_buckets << endl;
			cout << "1: " << filter[1]->num_buckets << endl;
			*/
			// end test
/*			cout << SizeInBytes() << endl;
			for(size_t i=0; i<max_filters; i++){
				delete filter[i];
				filter[i] = new CuckooFilter<ItemType, 4> (single_cf_size);
			}
			cout << SizeInBytes() << endl;*/
			// End Test shrink
			NCType s;
			if(typeid(s) == typeid(string))
				nc_type = STRING;
			else
				nc_type = NUMERIC;

			mem_budget = mem - FixedSizeInBytes();
		}

		~AdaptiveCuckooFilters() {
			/*for(size_t i=0; i < max_filters; i++) {
				delete filter[i];
			}
			delete filter;*/
		}
		
		bool Add(const ItemType& item, uint32_t *hash1);
		bool Delete(const ItemType& item);

		Status Lookup(const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash);
		Status AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash);

		Status GrowFilter(const uint32_t idx, vector<ItemType>& keys, bool grow_bucket);
		bool ShrinkFilter(uint32_t idx, vector<ItemType>& keys);
		uint32_t GetVictimFilter();


		// This should be better
		void DumpStats() const;
		bool LoadStatsToOptimize();

		// This should be worser
		void DumpFilter() const;
		bool LoadFilter();

		Status  InsertKeysToFilter(const uint32_t idx, vector<ItemType>& keys);

		void Info();

		void SetMemBudget(size_t mem) {
			mem_budget = mem - FixedSizeInBytes();
		}

		size_t FixedSizeInBytes() {
			//return 0;
			return nc_buckets*item_bytes;
			// Pointers(64 bit) + fpp(4 bit) + lookup(32 bit) + negative cache
			return (100*max_filters/8) + (nc_buckets* item_bytes);
			// Pointers + stats + negative cache
			return ((4*max_filters)*sizeof(size_t)) + (nc_buckets * item_bytes);
//			return ((max_filters)*sizeof(size_t)) + (nc_buckets * item_bytes);
//			return nc_buckets * item_bytes;
//			return 0;
		}

		size_t FilterSizeInBytes() {
			// TODO: need real size
			size_t bytes = 0;
			for(size_t i=0; i< max_filters; i++){
				if(filter[i] != NULL)
					bytes += filter[i]->SizeInBytes();
			}
			//cout << "bytes: " << bytes << " cur_mem: " << cur_mem << endl;
			assert(bytes == cur_mem);
			return bytes;
		}

		size_t SizeInBytes() {
			return FilterSizeInBytes() + FixedSizeInBytes();
		}

		size_t IncrementalOptimalFilterSize(uint32_t idx, size_t fingerprint_size) {
			size_t new_size = 0;

			if (insert_keys_old[idx] == 0)
				insert_keys_old[idx] = insert_keys[idx];

			float t_1 = (float)2*insert_keys[idx]/pow(2, fingerprint_size);
			float t_2 = (float)lookup[idx]/all_lookup;
			float t_3 = lookup_fr_old[idx];
			float t_4 = filter[idx]->num_buckets*pow(2, fingerprint_size)/(2*insert_keys_old[idx]);

			//cout << t_1 << " " << t_2 << " " << t_3 << " " << t_4 << endl;
			//cout << insert_keys_old[idx] << endl;
			new_size = t_1*t_2*t_3*t_4;
			
			insert_keys_old[idx] = insert_keys[idx];
			lookup_fr_old[idx] = (float)all_lookup/lookup[idx];
			return new_size;
		}

		bool LocalOptimalFilterSize(uint32_t idx) {
			float true_fpp = (float)fpp[idx]/lookup[idx];
			float target_fpp = (float)(2*filter[idx]->num_items)/(filter[idx]->num_buckets*pow(2, filter[idx]->fingerprint_size));
	   	 	float bound = 0.001;
			size_t b;

			if(abs(true_fpp - target_fpp) > bound){
				b = IncrementalOptimalFilterSize(idx, bits_per_tag);
				if (b > 0) {
					int delta = b - filter[idx]->num_buckets;
					//if(delta > 0) cout << "Grow: " << delta << endl;
					//if(delta < 0) cout << "Shrink: " << delta << endl;

					bool overlap_mode;
					if (b*4 < filter[idx]->num_items/0.95)
						overlap_mode = true;
					else
						overlap_mode = false;


					ClearFilter(idx);

					//cout << b << endl;

					filter[idx] = new CuckooFilter<ItemType, bits_per_tag> (b, true, overlap_mode);
					cur_mem += filter[idx]->SizeInBytes();

					return true;
				}
			}
			return false;
		}
		
		void GlobalOptimalFilterSize(size_t* l, size_t* n) {
			if(l == NULL || n == NULL) {
				l = lookup;
				n = insert_keys;
			}

			if(all_lookup == 0) {
				return;
			}

			size_t mem_budget_bytes = mem_budget; 
			size_t b;

			//ClearAllCounter();
			//ClearAllFilter();

			// Calculate Optimal Filter Size	
			double z1=0;
			for(size_t i=0; i<max_filters; i++) {
				z1 += sqrt(l[i]*n[i]);
			}

			z1 = pow(((double)(4*bits_per_tag*z1)/(mem_budget_bytes*8)), 2);

//cout << "all_lookup: " << all_lookup << " all_lookup_old: " << all_lookup_old << endl;

			size_t sum = 0;

			// Ad-hoc solution to make the fake "Optimal" filters reach the budget
			int buckets_to_shrink = 0;

			for(size_t i=0 ;i<max_filters; i++) {
//cout << "l: " << l[i] << " n: " << n[i] << " z1: " << z1 << endl;
				b = (size_t)sqrt(l[i]*n[i]/z1);
				//cout << "capacity: " << b*4 << " items: " << filter[i]->num_items << endl;

				// TODO: 4b[i] > n[i] constraint not satisfied
				//assert(b*4 >= insert_keys[i]);
				/*if(b*4 < insert_keys[i]/0.95) {
					size_t qq = filter[i]->num_buckets;
					b = qq;
				}*/				
//				b = filter[i]->num_buckets;
//
				bool overlap_mode;
				if (filter[i] != NULL && b*4 < filter[i]->num_items/0.95)
					overlap_mode = true;
				else
					overlap_mode = false;

				ClearFilter(i);	
	
				sum += b*4*bits_per_tag/8;
				if(b > 0) {
					filter[i] = new CuckooFilter<ItemType, bits_per_tag> (b, true, overlap_mode);
//cout << "new filter: " << i << endl;
					cur_mem += filter[i]->SizeInBytes();
				}
			}

//cout << "cur_mem: " << cur_mem << endl;
//cout << "--------------------------------\n";

			// Forget history
			//ClearAllCounter();
		}

		void ClearFilter(size_t i) {
			if(filter[i] != NULL) {
				//cout << "delete filter: " << i << " mem: " << SizeInBytes() <<  endl;
				cur_mem -= filter[i]->SizeInBytes();
				delete filter[i];
				filter[i] = NULL;
			}
		}

		void ClearAllFilter() {
			for(size_t i=0;i<max_filters;i++) {
				if(filter[i] != NULL) {
					cur_mem -= filter[i]->SizeInBytes();
					delete filter[i];
					filter[i] = NULL;
				}
			}
		}

		void ClearCounter(size_t i) {
			lookup[i] = 1;
			fpp[i] = 0;
		}

		void ClearAllCounter() {
			for(size_t i=0;i<max_filters;i++) {
				lookup[i] = 0;
				lookup_fr_old[i] = 0;
				all_lookup = 0;
			//	fpp[i] = 0;
			}
		}

		// Temp
		size_t getFilterBucket(size_t i) {
			return filter[i]->num_buckets;
		}
	};

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Add(
			const ItemType& item, uint32_t *hash1) {
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		*hash1 = raw_index % max_filters;
		t_nc_hash = raw_index % nc_buckets;

		// Instantiate the filter if the filter[hash1] is NULL, need to read keys from disk	
		if(filter[*hash1] == NULL){
			return false;
		} else {
			// Add the item
			t_status = filter[*hash1]->Add(index, tag);
//cout<<"Add: "<<index<<"/"<<tag<<endl;

			if (t_status == cuckoofilter::Ok) {
				insert_keys[*hash1]++;
				return true;
			} else if(t_status == cuckoofilter::MayClearNC) {
				insert_keys[*hash1]++;
				if(CompareNC(t_nc_hash, item)) {
					//cout << "Clear "<<item<<endl;
					ClearNC(t_nc_hash);
				}
				return true;
			} else {
				return false;
			}
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Lookup(
			const ItemType& item, size_t *status, uint32_t *hash1, size_t *r_index, size_t *nc_hash) {
//TODO: change the pointer to reference!!
		dummy_filter->GenerateIndexTagHash(item, item_bytes, nc_type==STRING, &raw_index, &index, &tag);
		*hash1 = raw_index % max_filters;
		*nc_hash = raw_index % nc_buckets;

		lookup[*hash1]++;
		all_lookup++;
		if (filter[*hash1] != NULL) {
//cout<<"Lookup: "<<*hash1<<" - "<<filter[*hash1]->num_buckets<<endl;
//cout << " nitem: " << filter[*hash1]->num_items << endl;
//cout << " nbucket: " << filter[*hash1]->num_buckets << endl;
//cout << " finger: " << filter[*hash1]->fingerprint_size << endl;

			*status = filter[*hash1]->Contain(index, tag, r_index);
//cout << "2\n";
			if (*status == cuckoofilter::Ok) {
				return Found;
			}else if (*status == cuckoofilter::NotSure) {
				if (CompareNC(*nc_hash, item)) {
					NC_hit++;
					true_neg++;
					return NotFound;
				}else{
					return Found;
				}
			}else{
				true_neg++;
				return NotFound;
			}
		}else{
			// If no filter, always refurn found
			return Found;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::Delete(
			const ItemType& item) {
		return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		AdaptToFalsePositive(const ItemType& item, const size_t status, const uint32_t hash1, const size_t r_index, const size_t nc_hash) {
//cout << "adapt: " << hash1 << endl;
		fpp[hash1]++;
		overall_fpp++;

		if (filter[hash1] != NULL) {
			if (status == cuckoofilter::Ok) {
				filter[hash1]->AdaptFalsePositive(r_index);
				InsertNC(nc_hash, item);	// Not Sure if this is needed
			} else if (status == cuckoofilter::NotSure) {
				InsertNC(nc_hash, item);
			}
		}
		//TODO: when to trigger rebuild
		// Algorithm 1

		float target_fpp;
		if ( filter[hash1] != NULL )
			target_fpp = (float)2*filter[hash1]->num_items/(filter[hash1]->num_buckets*pow(2, filter[hash1]->fingerprint_size));
		else
			target_fpp = 1.0;
		float true_fpp = (float)fpp[hash1]/lookup[hash1];

		if (target_fpp == 1.0 || (true_fpp > target_fpp*5 && lookup[hash1] > 50)) {
//		if(fpp[hash1] > grow_threshold){
//			cout << "target: " << target_fpp << " true: " << true_fpp << " lookup: " << lookup[hash1] << endl;

//			cout << "true_fpp: " << true_fpp << " target_fpp: " << target_fpp << endl;
			return NeedRebuild;
		}else{
//			cout << "!target: " << target_fpp << " true: " << true_fpp << " lookup: " << lookup[hash1] << endl;
		}

		// Algorithm 2
/*		float true_fpp = (float)fpp[hash1]/overall_fpp;
		float target_fpp = (float)(2*filter[hash1]->num_items)/(filter[hash1]->num_buckets*pow(2, filter[hash1]->fingerprint_size));
		float bound = 1.01;
		if(true_fpp > (target_fpp + bound)){
//			return NeedRebuild;
		}*/

		return Nothing;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::GrowFilter(const uint32_t idx, vector<ItemType>& keys, bool grow_bucket) {
		size_t ori_mem, new_mem, new_size;
		size_t fingerprint_size = bits_per_tag;

		bool force = false;
		bool overlap_mode = false;

		pinned_filter = idx;
		shrink_end[idx] = false;

		if(filter[idx] != NULL) {
			ori_mem = filter[idx]->SizeInBytes();
/*
			do {
				rebuild_time[idx]++;
				//new_size = single_cf_size*pow(2, (rebuild_time[idx]));
				new_size = single_cf_size*(rebuild_time[idx]+1);
			}while(new_size < filter[idx]->num_items);
*/
			fingerprint_size = filter[idx]->fingerprint_size;
			if(!grow_bucket) {
				num_grow ++;
				// Algorithm 1
/*				if(filter[idx]->fingerprint_size+4 <= 16) {
//cout << "!!!!!!!!!!!!\n";
					fingerprint_size = filter[idx]->fingerprint_size+4;
					force = true;
					new_size = filter[idx]->num_buckets;
	
					//Optimal size
					size_t optimal_new_size = IncrementalOptimalFilterSize(idx, fingerprint_size);
					//cout << "Optimal Grow - " << optimal_new_size << " " << new_size << endl;
	
					if(optimal_new_size > 0 && optimal_new_size > new_size){
				//		cout << "Optimal!!!!\n";
	//					fingerprint_size -= 4;
				//		new_size = optimal_new_size;
					}
				}else{
					new_size = filter[idx]->num_buckets*2;
					force = true;
				}

*/
				// Algorithm 2
				if((filter[idx]->num_buckets*4 - filter[idx]->num_items)*filter[idx]->fingerprint_size
					 >= filter[idx]->num_items*4) {
					//cout << "Grow Fingerprint\n";
					fingerprint_size += 4;
					if(fingerprint_size > 32) fingerprint_size = 32;
					new_size = filter[idx]->num_buckets;
				}else{
					//size_t optimal_new_size = IncrementalOptimalFilterSize(idx, fingerprint_size);
	
					new_size = filter[idx]->num_buckets+grow_shrink_bucket;
					/*if(false && optimal_new_size > filter[idx]->num_buckets) {
						new_size = optimal_new_size;
						cout << "opt: " << optimal_new_size << " ori: " << filter[idx]->num_buckets << endl;
					}*/
				}
				force = true;
			}else{
				// Pure caused by insertion error (should be rare)
				size_t t_new_size = (size_t)(filter[idx]->num_buckets*1.1);
				if(t_new_size - filter[idx]->num_buckets > 0)
					new_size = t_new_size;
				else
					new_size = t_new_size+2;
				force = true;
			}

			if (new_size*4 < filter[idx]->num_items/0.95)
				overlap_mode = true;

			ClearFilter(idx);	
		} else {
			ori_mem = 0;
			new_size = single_cf_size;
		}

		//cout << "fingerprint: " << fingerprint_size << endl;
		switch(fingerprint_size){
			case 4:
				filter[idx] = new CuckooFilter<ItemType, 4> (new_size, force, overlap_mode);
				break;
			case 8:
				filter[idx] = new CuckooFilter<ItemType, 8> (new_size, force, overlap_mode);
				break;
			case 12:
				filter[idx] = new CuckooFilter<ItemType, 12> (new_size, force, overlap_mode);
				break;
			case 16:
				filter[idx] = new CuckooFilter<ItemType, 16> (new_size, force, overlap_mode);
				break;
			case 20:
				filter[idx] = new CuckooFilter<ItemType, 20> (new_size, force, overlap_mode);
				break;
			case 24:
				filter[idx] = new CuckooFilter<ItemType, 24> (new_size, force, overlap_mode);
				break;
			case 28:
				filter[idx] = new CuckooFilter<ItemType, 28> (new_size, force, overlap_mode);
				break;
			case 32:
				filter[idx] = new CuckooFilter<ItemType, 32> (new_size, force, overlap_mode);
				break;
			default:
				assert(false);
		}

//cout << "new filter: " << idx << endl;
		
		new_mem = filter[idx]->SizeInBytes();

		// Update statistics
		//cout<<"new_:"<<new_mem<<" ori_mem:"<<ori_mem<<endl;
		//cout<<"new_mem:"<<new_mem<<" ori_mem:"<<ori_mem<<endl;

		assert(new_mem >= ori_mem);

		cur_mem += filter[idx]->SizeInBytes();;
		
		ClearCounter(idx);

//cout << "Info: Grow filter " << idx << " mem: " << cur_mem << " ,budget: " << mem_budget << endl;
//cout << "size: " << filter[idx]->SizeInBytes() << " bucket: " << filter[idx]->num_buckets << endl;

		for(size_t i=0; i< keys.size(); i++){
			dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
			if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
				// Shit, bad luck
				//cout << "filter full, load: " << (float)filter[idx]->num_items/(filter[idx]->num_buckets*4) << endl;
				return NeedRebuild;
			}
		}
		
		if(cur_mem*1.1 > mem_budget){
			return NeedShrink;
		} else {
			return Nothing;
		}
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		ShrinkFilter(uint32_t idx, vector<ItemType>& keys) {
		if(cur_mem < mem_budget)
			return true;

		// Algorithm: 
		// 1. Shrink the fingerprint first
		// 2. If the fingerprint is already 4 bit, shrink the bucket size
		// 3. If cannot shrink bucket size anymore, set shrink_end[idx] = true

//cout << "Shrink Mem: " << cur_mem << endl;
		assert(filter[idx] != NULL);
		pinned_filter = idx;

		size_t ori_mem, new_mem=0;
		ori_mem = filter[idx]->SizeInBytes();

		size_t ori_fp_size = filter[idx]->fingerprint_size;
		size_t new_fp_size = ori_fp_size;

		size_t new_size;
		bool force;
		bool overlap_mode = false;

		//TODO:WTF
		//size_t new_size = single_cf_size*pow(2,(rebuild_time[idx]-1));
		//size_t new_size = single_cf_size*rebuild_time[idx];

		// If the filter is in it's minimal state, don't new it
		// If the capacity of the new filter is smaller than the keys size, don't new it
		//
		num_shrink++;

		// Algorithm 1 - may cause infinite loop
/*		new_size = keys.size()/0.95/4;
		force = true;
		if(new_size >= filter[idx]->num_buckets) {
			new_size = filter[idx]->num_buckets;
			new_fp_size = ori_fp_size - 4;
		}
*/

		// Algorithm 2
/*		new_fp_size = ori_fp_size - 4;
		// Just shrink the fingerprint, because this will not read disk
		force = true;
		new_size = filter[idx]->num_buckets;
		if(new_fp_size < 4) {
			//cout << idx << " shrink to end\n";
			new_fp_size = 4;
			//LAB
			shrink_end[idx] = true;
		//	new_size = keys.size()/0.8;
		//	force = false;

			//LAB: cannot shrink to smaller
			size_t optimal_new_size = IncrementalOptimalFilterSize(idx, new_fp_size);
//			cout << "Optimal Shrink - " << optimal_new_size << " " << new_size << endl;

			if(optimal_new_size > 0) {
		//		new_size = optimal_new_size;
		//		force = true;
			}
		}
*/

		// Algorithm 3
		new_fp_size = ori_fp_size;
		force = true;
		// Shrink bucket size first
		if(filter[idx]->num_buckets > grow_shrink_bucket) {
			//cout << "wow: ";
			//cout << filter[idx]->num_buckets << "/" << grow_shrink_bucket<< endl;
			new_size = filter[idx]->num_buckets-grow_shrink_bucket;
			//cout << new_size << endl;
		}else{
			new_size = 0;
		}
		// If cannot the bucket size is not shinkable, shrink the fingerprint
		if(new_size*4 < keys.size()/0.95) {
			//cout << "Shrink fingerprint: " << idx << endl;
			new_fp_size = ori_fp_size - 4;
			if(new_fp_size < 4) {
				new_fp_size = 4;
			}
			new_size = filter[idx]->num_buckets;
			shrink_end[idx] = true;	
		}

	

		// Evil part: Shrink the bucket! -> May cause false negative now
		bool evil_shrink = false;
		if(evil_shrink) {
			new_fp_size = ori_fp_size;
			new_size = filter[idx]->num_buckets/2;
			//cout << "Shrink to " << new_size << endl;
			if(new_size == 0) {
				new_fp_size = ori_fp_size - 4;
			}
		}

		if (force && new_size*4 < filter[idx]->num_items/0.95)
			overlap_mode = true;

		ClearFilter(idx);
		if(new_size > 0) {
			while(true) {
				switch(new_fp_size){
					case 4:
						filter[idx] = new CuckooFilter<ItemType, 4> (new_size, force, overlap_mode);
						break;
					case 8:
						filter[idx] = new CuckooFilter<ItemType, 8> (new_size, force, overlap_mode);
						break;
					case 12:
						filter[idx] = new CuckooFilter<ItemType, 12> (new_size, force, overlap_mode);
						break;
					case 16:
						filter[idx] = new CuckooFilter<ItemType, 16> (new_size, force, overlap_mode);
						break;
					case 20:
						filter[idx] = new CuckooFilter<ItemType, 20> (new_size, force, overlap_mode);
						break;
					case 24:
						filter[idx] = new CuckooFilter<ItemType, 24> (new_size, force, overlap_mode);
						break;
					case 28:
						filter[idx] = new CuckooFilter<ItemType, 28> (new_size, force, overlap_mode);
						break;
					case 32:
						filter[idx] = new CuckooFilter<ItemType, 32> (new_size, force, overlap_mode);
						break;
					default:
						assert(false);
				}
				new_mem = filter[idx]->SizeInBytes();
	
//cout << "new filter: " << idx << endl;
//cout << new_size << endl;
//cout << "ori_fp: "<<ori_fp_size<<" new_fp: "<<new_fp_size<<endl;
//cout << "new_mem: " << new_mem << " ori_mem: " << ori_mem << endl;
//			assert(new_mem <= ori_mem);
				for(size_t i=0; i< keys.size(); i++){
					dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
					if (filter[idx]->Add(index, tag) != cuckoofilter::Ok) {
						// TODO: the break will cause false negative now!
						if(evil_shrink)
							break;

					 // Just delete the filter
					//cout<< "Shrink fail: "<<idx<<endl;
					//cout << "Bucket: "<<filter[idx]->num_buckets << " Keys:" << keys.size() << endl;
					//assert(false);
						delete filter[idx];
						filter[idx] = NULL;
						new_mem = 0;
						break;
					}
				}

				if(filter[idx] != NULL) {
					break;
				} else {
					new_size = ceil(new_size/0.9);
//				cout << "Try again, cur_mem: " << cur_mem << endl;
				}
			}

//cout << "YO: " << new_size << endl;
			cur_mem += filter[idx]->SizeInBytes();
		}
		ClearCounter(idx);
//cout << "Info: Shrink filter " << idx << " mem: " << cur_mem << " ,budget: " << mem_budget << endl;
//cout << "ori_mem: "<<ori_mem<<" new_mem: "<<new_mem<<endl;

//cout << "Shrink - bucket: " << filter[idx]->num_buckets << endl;


		if(cur_mem*1.1 > mem_budget)
			return false;
		else
			return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		DumpStats() const {
			ofstream f("acf.stat");	
			if (f.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					f << lookup[i] << ' ' << insert_keys[i] << ' ' <<fpp[i] << endl;
				}
			}
			f.close();

			ofstream ff("acf.lookup");	
			if (ff.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					ff << lookup[i] << endl;
				}
			}
			ff.close();

			ofstream fff("acf.fpp");	
			if (fff.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					if(fpp[i] > 0)
						fff << fpp[i] << endl;
				}
			}
			fff.close();
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		LoadStatsToOptimize() {
			ifstream f("acf.stat");
			string line;
			size_t *l = new size_t[max_filters];
			size_t *n = new size_t[max_filters];
			size_t i=0;
			vector<string> tv;
			if (f.is_open()) {
				while (getline (f, line) ) {
					tv = split(line, ' ');
					l[i] = atoi(tv[0].c_str());
					n[i] = atoi(tv[1].c_str());			
					i++;
				}
			}else{
				return false;
			}
			f.close();

			GlobalOptimalFilterSize(l, n);
			//cout << "Theory: " << sum << " Real: " << cur_mem << endl;
			
			cout << "Load Done." << endl;
			return true;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		DumpFilter() const {
			ofstream f("acf.filter");	
			if (f.is_open()) {
				for(size_t i=0; i<max_filters ;i++){
					if(filter[i] != NULL)
						f << filter[i]->num_buckets << " " << filter[i]->fingerprint_size << endl;
					else
						f << "0 0" << endl;
				}
			}
			f.close();
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	bool
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		LoadFilter() {
			ifstream f("acf.filter");
			string line;
			size_t i=0, b;
			size_t *bs = new size_t[max_filters];
			size_t *fp = new size_t[max_filters];
			vector<string> tv;

			if (f.is_open()) {
				while (getline (f, line) ) {
					tv = split(line, ' ');
					bs[i] = atoi(tv[0].c_str());
					fp[i] = atoi(tv[1].c_str());
					i++;
				}
			}else{
				return false;
			}
			f.close();
			
			cur_mem = 0;
			for(i=0 ;i<max_filters; i++){
				if(filter[i] != NULL){
					delete filter[i];
					filter[i] = NULL;
				}

				//cout << bs[i] << endl;
				if(bs[i] > 0){
					switch(fp[i]){
						case 4:
							filter[i] = new CuckooFilter<ItemType, 4> (bs[i], true, false);
							break;
						case 8:
							filter[i] = new CuckooFilter<ItemType, 8> (bs[i], true, false);
							break;
						case 12:
							filter[i] = new CuckooFilter<ItemType, 12> (bs[i], true, false);
							break;
						case 16:
							filter[i] = new CuckooFilter<ItemType, 16> (bs[i], true, false);
							break;
						case 20:
							filter[i] = new CuckooFilter<ItemType, 20> (bs[i], true, false);
							break;
						case 24:
							filter[i] = new CuckooFilter<ItemType, 24> (bs[i], true, false);
							break;
						case 28:
							filter[i] = new CuckooFilter<ItemType, 28> (bs[i], true, false);
							break;
						case 32:
							filter[i] = new CuckooFilter<ItemType, 32> (bs[i], true, false);
							break;
						default:
							assert(false);
					}
					cur_mem += filter[i]->SizeInBytes();
				}
			}			
			cout << "Load Done." << endl;
			return true;
	}


	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	void
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		Info() {
		size_t num_insert=0, num_lookup=0, capacity=0;

		for(size_t i=0; i<max_filters;i++) {
			num_insert += insert_keys[i];
			num_lookup += lookup[i];
			if(filter[i] != NULL)
				capacity += filter[i]->num_buckets*4;

			//cout << "Filter " << i << ": " << filter[i]->num_buckets << "/" << filter[i]->fingerprint_size << endl;
		//	cout << filter[i]->num_buckets << endl; 
		}

/*		cout << "NC:\n";
		size_t nc_c=0;
		for(size_t i=0; i< nc_buckets; i++) {
			if(nc[i].length() > 0){
				cout << nc[i] << endl;
				nc_c++;
			}
		}*/

		cout << "Adaptive Cuckoo Filters Status:\n"
			<< "Filter size: " << SizeInBytes() << " bytes\n"
			<< "Overhead: " << 100*(float)FixedSizeInBytes()/(FilterSizeInBytes()+FixedSizeInBytes()) << " %" << endl
			<< "Filters: " << max_filters << endl << endl
			<< "Insert: " << num_insert << endl
			<< "Load factor: " << (float)num_insert/capacity << endl
			<< "NC hit: " << NC_hit << endl 
		//	<< "Lookup: " << num_lookup << endl
		//	<< "False Pos: " << overall_fpp << endl
		//	<< "True Neg: " << true_neg << endl
			<< "Grow: " << num_grow << endl
			<< "Shrink: " << num_shrink << endl << endl
		//	<< "fpp: " << 100*(float)overall_fpp/(overall_fpp+true_neg) << " %" << endl
			<< "Disk access: " << (overall_fpp + num_grow) << endl;

		return;
	}

	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	uint32_t
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		GetVictimFilter() {
			//TODO: Choose the right victim
			// Choose the least lookuped one
			int min=-1, max = 0;
			uint32_t idx=-1;

			if(idx == -1) {
				// Shrink the previous growed filter first
				for(uint32_t i=0;i<max_filters;i++){
					if(pinned_filter != i && filter[i] != NULL  && shrink_end[i]==false&&
						(min == -1 || (int)lookup[i] < min) && 
//						(filter[i]->num_buckets*4 > single_cf_size*2 || filter[i]->fingerprint_size > bits_per_tag) 
						((float)filter[i]->num_items/filter[i]->num_buckets*4 < 0.9 || filter[i]->fingerprint_size > bits_per_tag)
						) {
						min = lookup[i];
						idx = i;
					}
				}
			}

			// Seems better
			if(idx == -1) {
				for(uint32_t i=0;i<max_filters;i++){
					if(filter[i] != NULL && pinned_filter != i && shrink_end[i]==false&&
						(min == -1 || (int)lookup[i] < min)) {
						min = lookup[i];
						idx = i;
					}
				}
			}

			if(idx == -1) {
				// If all the filters's fingerprint have been shrink, reset it.
				for(uint32_t i=0;i<max_filters;i++)
					if(shrink_end[i] == true)
						shrink_end[i] = false;
			}
//cout << "Choose " << idx << endl;
			pinned_filter = -1;
			assert(idx != -1);
			return idx;
		}


	template <typename ItemType,
			  typename NCType,
			  size_t nc_buckets, 
			  size_t bits_per_tag>
	Status
	AdaptiveCuckooFilters<ItemType, NCType, nc_buckets, bits_per_tag>::
		InsertKeysToFilter(const uint32_t idx, vector<ItemType>& keys) {
		if(filter[idx] == NULL) {
			return Nothing;
		}
		for(size_t i=0; i< keys.size(); i++){
			dummy_filter->GenerateIndexTagHash(keys[i], item_bytes, nc_type==STRING, &raw_index, &index, &tag);
			if (filter[idx]->Add(index, tag) == cuckoofilter::NotEnoughSpace) {
				// Shit, bad luck
				return NeedRebuild;
			}
		}
		return Nothing;
	}
}

#endif
