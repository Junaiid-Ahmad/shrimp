#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "mergesam.h"
#include "mergesam_heap.h"
#include "sam_reader.h"
#include "render.h"
#include "../common/util.h"
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

bool found_sam_headers;

static void render_sam_unaligned_to_buffer(pretty * pa, output_buffer * ob) {
	pa->sam_string=ob->base+ob->used;
	ob->used+= render_sam_unaligned_string(pa,ob->base+ob->used,ob->size-ob->used)+1;
	if (ob->used>=ob->size) {
		fprintf(stderr,"An error in allocating size of output has occured");
		exit(1);
	}
}

static void render_fastx_to_buffer(pretty * pa, output_buffer * ob) {
	pa->sam_string=ob->base+ob->used;
	ob->used+= render_fastx_string(pa,ob->base+ob->used,ob->size-ob->used)+1;
	if (ob->used>=ob->size) {
		fprintf(stderr,"An error in allocating size of output has occured");
		exit(1);
	}
}

static void render_sam_to_buffer(pretty * pa, output_buffer * ob) {
	pa->sam_string=ob->base+ob->used;
	ob->used+= render_sam_string(pa,ob->base+ob->used,ob->size-ob->used)+1;
	if (ob->used>=ob->size) {
		fprintf(stderr,"An error in allocating size of output has occured");
		exit(1);
	}
}

static inline double
get_pr_insert_size(double insert_size)
{
  double res;
  res = normal_cdf(insert_size + 10, options.insert_size_mean, options.insert_size_stddev) - normal_cdf(insert_size - 10, options.insert_size_mean, options.insert_size_stddev);
  //int bucket = (insert_size - min_insert_size) / insert_histogram_bucket_size;
  //if (bucket < 0) bucket = 0;
  //if (bucket > 99) bucket = 99;
  //res = (double)insert_histogram[bucket] / (double)insert_histogram_load;
  if (res < 1e-200) res = 1e-200;
  return res;
}



static inline int pa_to_mapq(pretty * pa) {
	assert(1==0);
	if (!pa->has_zs) {
		//return 255;
		return 0;
	}
	if (pa->z[1]==0.0) {
		return 255;
	}
	//fprintf(stderr,"%.20e and %.20e\n",pa->z[0],pa->z[1]);
	return qv_from_pr_corr(pa->z[0]/pa->z[1]);
}

double sum_logs_of_positives(double y, double x) {
	//fprintf(stderr,"sum of logs %e %e\n",y,x);
	if (isinf(x) && x<0) {
		return y;
	}
	if (isinf(y) && y<0) {
		return x;
	}
	return y + log(1+exp(x-y));
}

static inline void pp_ll_zero(pp_ll * ll) {
	ll->head=NULL;
	ll->tail=NULL;
	ll->length=0;
}

static inline void pp_ll_append(pp_ll* ll,pretty * pa) {
	if ( ll->length==0 ) {
		ll->head=pa;
		ll->tail=pa;
		pa->next=NULL;
		ll->length=1;
	} else {
		ll->tail->next=pa;
		ll->tail=pa;
		pa->next=NULL;
		ll->length++;
	}
	//fprintf(stderr,"%p length of %lu\n",ll,ll->length);
}

static inline void pp_ll_append_list(pp_ll * ll1, pp_ll * ll2) {
	if (ll2==NULL) {
		return;
	}
	if (ll2->length>0) {
		if (ll1->length==0) {
			ll1->head=ll2->head;
			ll1->tail=ll2->tail;
			ll1->length=ll2->length;
		} else if (ll1->length>0) {
			ll1->tail->next=ll2->head;
			ll1->tail=ll2->tail;
			ll1->length+=ll2->length;
		}
	}
}

static inline void pp_ll_set(pp_ll * ll,pretty *  pa) {
	ll->length=1;
	ll->head=pa;
	ll->tail=pa;
	pa->next=NULL;
}

static inline void consolidate_paired(pp_ll ** ll,pretty ** unaligned_pa,heap_pa * h) {
	//heap_pa * h=thread_heaps+omp_get_thread_num();
	h->load=0;
	heap_pa_elem e;
	pretty * pa=NULL;
	//keep track of the best pair for each file
	pretty * best_pair_for_file[options.number_of_sam_files];
	if (options.single_best) {
		memset(best_pair_for_file,0,sizeof(pretty*)*options.number_of_sam_files);
	}
	//sum up the z3s over open SAM files
	bool summed[options.number_of_sam_files];
	double z3_sum=0;
	double global_ins_denom=0;
	memset(summed,0,sizeof(bool)*options.number_of_sam_files);
	//set up the z4 min with its max value
	double z4_min=1.0;
	int i;
	for (i=0; i<options.number_of_sam_files; i++) {
		//local_ll - the linked list of SAM structures for a given read, file and mapping class
		// here we iterate over all files for a specific read and mapping class
		pp_ll * local_ll = ll[i]+PAIRED;
		if (local_ll->length>0) {
			pa = local_ll->head;
			//iterate over all SAM structs in this list
			while (pa!=NULL) {
				assert(pa->first_in_pair);
				//all of these should be 'properly paired' - this means here that both sides of the read are mapped
				assert(pa->proper_pair);
				assert((pa->has_zs&(1<<3))!=0); //make sure we have z3
				assert((pa->has_zs&(1<<6))!=0); //make sure we have z6
				// if single_best is on, just grab the best mapping for each file
				if (options.single_best) {
					int mapq_score=pa->mapq+pa->mate_pair->mapq;
					int fileno=pa->fileno;
					if (best_pair_for_file[fileno]==NULL || 
						best_pair_for_file[fileno]->mapq+best_pair_for_file[fileno]->mate_pair->mapq<mapq_score) {
						best_pair_for_file[fileno]=pa;
					}
				} else {
					e.score=pa->mapq+pa->mate_pair->mapq;
					e.second_score=pa->score+pa->mate_pair->score;
					e.rest=pa;
					if (options.strata) {
						heap_pa_insert_bounded_strata(h,&e);
					} else {
						heap_pa_insert_bounded(h,&e);
					}
				}
				assert(pa->mapped && pa->mate_pair!=NULL);
				assert(pa->mate_pair->mapped &&  pa->mp_mapped);
				//sum the z3 field
				if (!summed[pa->fileno]) {
					z3_sum+=pa->z[3];
					summed[pa->fileno]=true;
					global_ins_denom+=pa->z[6];
				}
				//take the min of z4
				z4_min=MIN(z4_min,pa->z[4]);

				pa=pa->next;
			}
		}
	}
	if (options.single_best) {
		//update everything in the best_lists with the new values
		for (i=0; i<options.number_of_sam_files; i++) {
			pretty * pa = best_pair_for_file[i];
			if (pa!=NULL) {
				pa->z[3]=pa->mate_pair->z[3]=z3_sum;
				pa->z[4]=pa->mate_pair->z[4]=z4_min;
			}
		}
		//double ins_sz_denom=0.0;
		//for (i=0; i<options.number_of_sam_files; i++) {
		//	if (best_pair_for_file[i]!=NULL) {
		//		ins_sz_denom+=get_pr_insert_size(best_pair_for_file[i]->isize);
		//	}
		//}
		int best_index=-1;
		int best_mp_z2=0;
		double best_z2=0;
		for (i=0; i<options.number_of_sam_files; i++) {
			if (best_pair_for_file[i]!=NULL) {
				pretty * pa = best_pair_for_file[i];
				//int new_z2=tnlog(pa->z[2]*pa->z[6]/global_ins_denom);
				//int new_mp_z2=tnlog(pa->mate_pair->z[2]*pa->mate_pair->z[6]/global_ins_denom);
				int new_z2=tnlog(pa->z[2]);
				int new_mp_z2=tnlog(pa->mate_pair->z[2]);
				//UPDATE Z2 later on, only for best mapping
				if (best_index==-1 || best_z2 > new_z2 || (best_z2==new_z2 && best_mp_z2>new_mp_z2)) {
					best_z2=new_z2;
					best_mp_z2=new_mp_z2;
					best_index=i;
				}
			}
		}	
		if (best_index!=-1) {
			pretty *pa =  best_pair_for_file[best_index];
			//pa->z[2]=pa->z[2]*pa->z[6]/global_ins_denom;
			//pa->mate_pair->z[2]=pa->mate_pair->z[2]*pa->mate_pair->z[6]/global_ins_denom;
			pa->z[6]=global_ins_denom;
			pa->mate_pair->z[6]=global_ins_denom;
			h->load=0;
			e.score=0;
			e.rest=best_pair_for_file[best_index];
			assert(e.rest!=NULL);
			heap_pa_insert_bounded(h,&e);
		}
	} else {
		//update everything on the heap with the new values
		for (i=0; i<h->load; i++) {
			pretty * pa = h->array[i].rest;
			//update the z2
			//pa->z[2]=pa->z[2]*pa->z[6]/global_ins_denom;
			//pa->mate_pair->z[2]=pa->mate_pair->z[2]*pa->mate_pair->z[6]/global_ins_denom;
			//update the rest
			pa->z[3]=pa->mate_pair->z[3]=z3_sum;
			pa->z[4]=pa->mate_pair->z[4]=z4_min;	
			pa->z[6]=pa->mate_pair->z[6]=global_ins_denom;
		}
	}
	//dump the results in the first files linked list
	pp_ll * results_ll = ll[0]+PAIRED;
	pp_ll_zero(results_ll);	
	//update the link list for side effect - end of single class code, since linked list contains multiple classes
	if (h->load>0 && (options.max_alignments==0 || h->load<=options.max_alignments)) {
		i=h->load>options.max_outputs ? 1 : 0;
		results_ll->head=h->array[i].rest;
		results_ll->tail=h->array[h->load-1].rest;
		for (; i<h->load-1; i++) {
			h->array[i].rest->next=h->array[i+1].rest;
		}
		h->array[i].rest->next=NULL;
		results_ll->length+= h->load - (h->load>options.max_outputs ? 1 : 0);
	}
	//return back at least one unaligned entry that can be used for unaligned stuff later 
	if ((options.sam_unaligned || options.unaligned_fastx) && h->load>0) {
		*unaligned_pa=h->array[0].rest;
	}
}

static inline void consolidate_single(pp_ll ** ll,int map_class,pretty ** unaligned_pa,heap_pa * h) {
	assert(map_class!=UNMAPPED);
	assert(map_class!=PAIRED);
	//heap_pa * h=thread_heaps+omp_get_thread_num();
	h->load=0;
	heap_pa_elem e;
	pretty * pa=NULL;
	//keep track of which files we have summed z1 from
	bool z1s_summed[options.number_of_sam_files];
	memset(z1s_summed,0,sizeof(bool)*options.number_of_sam_files);
	//the running z1 sum tally
	double z1_sum=0;
	//initialize the z4 min to its max value
	pretty * max_pa=NULL;
	int i;
	for (i=0; i<options.number_of_sam_files; i++) {
		//local_ll - the linked list of SAM structures for a given read, file and mapping class
		// here we iterate over all files for a specific read and mapping class
		pp_ll * local_ll = ll[i]+map_class;
		if (local_ll->length>0) {
			//iterate over all entries in the linked list
			pa = local_ll->head;
			while (pa!=NULL) {
				assert(!pa->paired_sequencing || !pa->proper_pair);
				assert(pa->mapped); //should be gauranteed this, just how things are setup
				//if we have not summed this file into z1, do so now
				assert((pa->has_zs&(1<<1))!=0); //assert this SAM entry has the z1 field
				if (!z1s_summed[pa->fileno]) {
					assert((pa->has_zs&(1<<1))!=0);
					z1_sum+=pa->z[1];
					z1s_summed[pa->fileno]=true;
				}
				
				//update the MIN : TODO use proper MIN instead of negative MAX
				//if (map_class!=UNPAIRED) {
				//	assert((pa->has_zs&(1<<4))!=0); //assert this SAM entry has the z4 field
				//	z4_min=MIN(z4_min,pa->z[4]);
				//}
				if (max_pa==NULL || max_pa->z[0]<pa->z[0]) {
					max_pa=pa;
				}

				//insert this read into the heap
				if (options.single_best) {
					e.score=1000*pa->z[0];
				} else {
					e.score=pa->score;
				}
				e.rest=pa;
				e.second_score=0;
				assert(!options.single_best || options.max_outputs==1);
				if (options.strata) {
					heap_pa_insert_bounded_strata(h,&e);
				} else {
					heap_pa_insert_bounded(h,&e);
				}
				pa=pa->next;
			}
		}
	}
	if (max_pa==NULL) {
		return;
	}
	assert(max_pa!=NULL);
	double z4=max_pa->z[4];
	//update the z1 and z4s for all entries on the heap
	for (i=0; i<h->load; i++) {
		assert(z1_sum!=0);
		pretty * pa = h->array[i].rest;
		pa->z[1]=z1_sum;
		if (map_class!=UNPAIRED) {
			pa->z[4]=z4;
		}
	}
	if (options.single_best) {
		assert(h->load==0 || h->load==1);
	}
	//dump the results in the first files linked list
	pp_ll * results_ll = ll[0]+map_class;
	pp_ll_zero(results_ll);	
	//update the link list for side effect - end of single class code, since linked list contains multiple classes
	if (h->load>0 && (options.max_alignments==0 || h->load<=options.max_alignments)) {
		i=h->load>options.max_outputs ? 1 : 0;
		results_ll->head=h->array[i].rest;
		results_ll->tail=h->array[h->load-1].rest;
		for (; i<h->load-1; i++) {
			h->array[i].rest->next=h->array[i+1].rest;
		}
		h->array[i].rest->next=NULL;
		results_ll->length+= h->load - (h->load>options.max_outputs ? 1 : 0);
	}
	//return back at least one unaligned entry that can be used for unaligned stuff later 
	if ((options.sam_unaligned || options.unaligned_fastx) && h->load>0) {
		*unaligned_pa=h->array[0].rest;
	}
}

static inline void remove_offending_fields(pretty * pa) {
	pa->has_ih=false;
	pa->has_hi=false;
	pa->has_h0=false;
	pa->has_h1=false;
	pa->has_h2=false;
}


//This function is entered with the following
//ll - an array of linked lists
//	it has a set of linked lists for each open SAM file
//      it is indexed like this
//             ll[fileno]+map_class
//	this returns a linked list of SAM entry structures for this read, for this file, for this class
//m_ll - a final list where to store SAM structures to be printed (happens outside of this function), they are printed by just printing the sam structures sam_string, so all rendering must be done in here, more specifically in an update sam string function call at the end
//h - a heap for convience
void pp_ll_combine_and_check(pp_ll * m_ll,pp_ll ** ll,heap_pa *h,output_buffer * ob) {
	//want to make a heap
	//check first if paired of not
	assert(m_ll->length==0);
	//heap_pa * h=thread_heaps+omp_get_thread_num();
	h->load=0;
	//heap_pa_elem e;
	pretty * pa = NULL;
	pretty * unaligned_pa=NULL;
	//in the case of paired sequencing
	pp_ll * paired_list=NULL;//ll[0]+PAIRED;
	pp_ll * first_leg_list=NULL;//ll[0]+FIRST_LEG;
	pp_ll * second_leg_list=NULL;//ll[0]+SECOND_LEG;
	//in the case of unpaired sequencing
	pp_ll * unpaired_list=NULL;//ll[0]+UNPAIRED;
	//consolidate in this case means get the relevant mappings
	// for example in the case options.single_best is set only get 
	// the best one for each class
	if (options.paired) {
		consolidate_paired(ll,&unaligned_pa,h);
		paired_list=ll[0]+PAIRED;
		if (options.half_paired) { 
			h->load=0;
			consolidate_single(ll,FIRST_LEG,&unaligned_pa,h);
			first_leg_list=ll[0]+FIRST_LEG;
			h->load=0;
			consolidate_single(ll,SECOND_LEG,&unaligned_pa,h);
			second_leg_list=ll[0]+SECOND_LEG;
		}
	} else if (options.unpaired) {
		consolidate_single(ll,UNPAIRED,&unaligned_pa,h);
		unpaired_list=ll[0]+UNPAIRED;
	}

	bool first_empty = (first_leg_list == NULL || first_leg_list->length<=0) ;
	bool second_empty = (second_leg_list == NULL || second_leg_list->length<=0) ;
	bool paired_empty = (paired_list == NULL || paired_list->length<=0) ;
	
	//here is the spot to compute class priors and finalize the mapq value for the mappings
	//TODO class priors	
	double paired_scale = (!first_empty ? MIN(first_leg_list->head->z[4]*genome_length,1.0) : 1.0 ) * (!second_empty ? MIN(second_leg_list->head->z[4]*genome_length,1.0) : 1.0 ) ;
	double first_leg_scale = 0.0;
	if (!first_empty) {
		//fprintf(stderr,"compute first %e %e\n",first_leg_list->head->z[4],first_leg_list->head->z[5]);
		first_leg_scale = (paired_list->length > 0 ? MIN(paired_list->head->z[4]*genome_length,1.0) : 1.0 ) * (!second_empty ? MIN(second_leg_list->head->z[4]*genome_length,1.0) : 1.0 ) * (first_leg_list->head->z[5]) ;	
	}	
	double second_leg_scale = 0.0;
	if (!second_empty) {
		second_leg_scale = (paired_list->length > 0 ? MIN(paired_list->head->z[4]*genome_length,1.0) : 1.0 ) * (!first_empty ? MIN(first_leg_list->head->z[4]*genome_length,1.0) : 1.0 ) * (second_leg_list->head->z[5]) ;	
		//fprintf(stderr,"compute second %e %e %e %ld\n",second_leg_list->head->z[4],second_leg_list->head->z[5],second_leg_scale,genome_length);
	}
	double class_denom = (!paired_empty ? paired_scale : 0.0) + (!first_empty ? first_leg_scale : 0.0 ) + (!second_empty ? second_leg_scale : 0.0);

	pretty * best_alignment=NULL;
	//fprintf(stderr,"class %e , pair %e, first %e, second %e\n",class_denom,paired_scale,first_leg_scale,second_leg_scale);
	if (class_denom>0) {
		pretty * pa;
		//update the paired mapping qualities
		if (!paired_empty) {
			for (pa=paired_list->head; pa!=NULL; pa=pa->next) {
				//fprintf(stderr,"z2 %e %d, z3 %e %d\n",pa->z[2],tnlog(pa->z[2]),pa->z[3],tnlog(pa->z[3]));
				//fprintf(stderr,"mp z2 %e %d , z3 %e %d\n",pa->mate_pair->z[2],tnlog(pa->mate_pair->z[2]),pa->mate_pair->z[3],tnlog(pa->mate_pair->z[3]));
				pa->mapq=qv_from_pr_corr((pa->z[2]*paired_scale)/(pa->z[3]*class_denom));
				pa->mate_pair->mapq=qv_from_pr_corr((pa->mate_pair->z[2]*paired_scale)/(pa->mate_pair->z[3]*class_denom));
				pretty * max_pa = (pa->mapq > pa->mate_pair->mapq ? pa : pa->mate_pair);
				if (best_alignment==NULL || max_pa->mapq>best_alignment->mapq) {
					best_alignment=max_pa;
				}
			}
		}
		//update the first leg
		if (!first_empty) {
			for (pa=first_leg_list->head; pa!=NULL; pa=pa->next) {
				assert(pa->mapped && !pa->mate_pair->mapped);
				pa->mapq=qv_from_pr_corr((pa->z[0]*first_leg_scale)/(pa->z[1]*class_denom));	
				if (best_alignment==NULL || pa->mapq>best_alignment->mapq) {
					best_alignment=pa;
				}
			}
		}
		//update the second leg
		if (!second_empty) {
			for (pa=second_leg_list->head; pa!=NULL; pa=pa->next) {
				assert(pa->mapped && !pa->mate_pair->mapped);
				pa->mapq=qv_from_pr_corr((pa->z[0]*second_leg_scale)/(pa->z[1]*class_denom));	
				if (best_alignment==NULL || pa->mapq>best_alignment->mapq) {
					best_alignment=pa;
				}
			}
		}
		//update unpaired
		if (unpaired_list!=NULL) {
			for (pa=unpaired_list->head; pa!=NULL; pa=pa->next) {
				pa->mapq=qv_from_pr_corr(pa->z[0]/pa->z[1]);	
				if (best_alignment==NULL || pa->mapq>best_alignment->mapq) {
					best_alignment=pa;
				}
			}
		}
	}	

	//here we just trivially prepare the final outputed list
	//this would be adjusted in case of 'all_contigs' where we would
	//just add one of the final classes
	//TODO 
	pp_ll_append_list(m_ll,paired_list);
	pp_ll_append_list(m_ll,first_leg_list);
	pp_ll_append_list(m_ll,second_leg_list);
	pp_ll_append_list(m_ll,unpaired_list);

	//this part of the if clause deals with printing unmapped SAM entries or fasta or fastq format instead of SAM
	if (m_ll->length==0 && (options.sam_unaligned || options.unaligned_fastx)) {
		if (unaligned_pa==NULL) {
			if (!options.half_paired) { 
				h->load=0;
				consolidate_single(ll,FIRST_LEG,&unaligned_pa,h);
				h->load=0;
				consolidate_single(ll,SECOND_LEG,&unaligned_pa,h);
			}
			h->load=0;
			int i; 
			for (i=0; i<options.number_of_sam_files; i++) {
				pp_ll * local_ll = ll[i]+UNMAPPED;
				if (local_ll->length>0) {
					unaligned_pa=local_ll->head;
					break;
				}
			}
			//pp_ll_combine_and_check_heap(m_ll,ll,UNMAPPED,&unaligned_pa,h);
		}
		if (unaligned_pa!=NULL) {
			m_ll->length=1;
			m_ll->head=unaligned_pa;
			m_ll->tail=unaligned_pa;
			unaligned_pa->next=NULL;
			
			pretty_from_aux_inplace(unaligned_pa);
			if (options.unaligned_fastx) {
				//render_fastx(unaligned_pa, true);
				render_fastx_to_buffer(unaligned_pa,ob);
			} else {
				//render_sam_unaligned(unaligned_pa,true);
				render_sam_unaligned_to_buffer(unaligned_pa,ob);	
			}
			if (unaligned_pa->paired_sequencing) {
				pretty_from_aux_inplace(unaligned_pa->mate_pair);
				if (options.unaligned_fastx) {
					//render_fastx(unaligned_pa->mate_pair, true);
					render_fastx_to_buffer(unaligned_pa->mate_pair,ob);
				} else {
					//render_sam_unaligned(unaligned_pa->mate_pair,true);
					render_sam_unaligned_to_buffer(unaligned_pa->mate_pair,ob);
				}
			}
		}
	//The usual CASE, where we want to print SAM format
	} else if (!options.unaligned_fastx) {

		//past here is only rendering of the final output string
		pa=m_ll->head;
		while(pa!=NULL) {
			//pretty_from_aux_inplace(pa);
			if (options.aligned_fastx) {
				pa->next=NULL;
				pretty_from_aux_inplace(pa);
				//render_fastx(pa, true);
				render_fastx_to_buffer(pa,ob);
			} else {
				remove_offending_fields(pa);
				//render the new sam string in place
				//render_sam(pa, true);
				render_sam_to_buffer(pa, ob);
			}
			//revert_sam_string(pa);
			assert(pa->mapped);
			if (pa->mate_pair!=NULL) {
				assert(pa->mate_pair->mate_pair!=NULL);
				if (options.aligned_fastx) {
					pretty_from_aux_inplace(pa->mate_pair);
					//render_fastx(pa->mate_pair, true);
					render_fastx_to_buffer(pa->mate_pair, ob);
				} else {
					remove_offending_fields(pa->mate_pair);
					//render the new sam string in place
					//render_sam(pa->mate_pair, true);
					render_sam_to_buffer(pa->mate_pair, ob);
				}
				//revert_sam_string(pa->mate_pair);
			}
			pa=pa->next;
		}
	} else {
		m_ll->length=0; m_ll->head=NULL;
	}	
	return;	
	
		
}

static inline void pp_ll_append_and_check(pp_ll* ll,pretty * pa) {
	if (pa->paired_sequencing) {
		if (pa->proper_pair) {
			assert(pa->mapped && pa->mp_mapped);
			//both sides mapped
			if (pa->first_in_pair) {
				pp_ll_append(ll+PAIRED,pa);
			} else {
				pp_ll_append(ll+PAIRED,pa->mate_pair);
			}
		} else if ((options.half_paired || options.sam_unaligned || options.unaligned_fastx)  && (pa->mapped || pa->mp_mapped)) {
			//lets figure out which leg
			assert(!pa->mapped || !pa->mp_mapped);
			if (pa->mapped) {
				if (pa->first_in_pair) {
					pp_ll_append(ll+FIRST_LEG,pa);
				} else {
					pp_ll_append(ll+SECOND_LEG,pa);
				}
			} else {
				assert(pa->mate_pair->mapped);
				if (pa->first_in_pair) {
					pp_ll_append(ll+SECOND_LEG,pa->mate_pair);
				} else {
					pp_ll_append(ll+FIRST_LEG,pa->mate_pair);
				}
			}
		} else if ((options.sam_unaligned || options.unaligned_fastx) && !pa->mapped && !pa->mp_mapped) {
			//unmapped paired
			pp_ll_append(ll+UNMAPPED,pa);
		}
	} else {
		if (pa->mapped) {
			//unpaired and mapped
			pp_ll_append(ll+UNPAIRED,pa);
		} else if (options.sam_unaligned || options.unaligned_fastx) {
			//unpaired and unmapped
			pp_ll_append(ll+UNMAPPED,pa);
		}
	}
}
void grow_sam_pretty(sam_reader * sr) {
	fprintf(stderr,"Cannot double size!\n");
	exit(1);
	/*size_t new_pretty_stack_size=(size_t)(sr->pretty_stack_size*2);
	assert(new_pretty_stack_size>sr->pretty_stack_size);
	fprintf(stderr,"Growing %lu to %lu entries\n",sr->pretty_stack_size,new_pretty_stack_size);
	size_t old_size = sr->pretty_stack_size;
	char * old = (char*)sr->pretty_stack;
	sr->pretty_stack = (pretty*)realloc(sr->pretty_stack,new_pretty_stack_size*sizeof(pretty));
	if (sr->pretty_stack==NULL) {
		fprintf(stderr,"Failed realloc of pretty stack!\n");
		exit(1);
	}
	//memset(sr->pretty_stack + old_size , 0, (new_pretty_stack_size-old_size)*sizeof(pretty));
	sr->pretty_stack_size=new_pretty_stack_size;	
	char * new_p = (char*)sr->pretty_stack;
	int i;
	for (i=0; i<LL_ALL*options.read_rate; i++) {
		pp_ll * ll= sr->pp_lls+i;
		if (ll->head!=NULL) {
			ll->head=(pretty*)(((char*)ll->head)+(new_p-old));
		}
		if (ll->tail!=NULL) {
			ll->tail=(pretty*)(((char*)ll->tail)+(new_p-old));
		}
	}
	size_t ui;
	for (ui=0; ui<old_size; ui++) {
		pretty * pa=sr->pretty_stack+ui;
		if (pa->next!=NULL) {
			pa->next=(pretty*)(((char*)pa->next)+(new_p-old));
		}
		if (pa->mate_pair!=NULL) {
			pa->mate_pair=(pretty*)(((char*)pa->mate_pair)+(new_p-old));
		}
	}*/
}

static inline void revert_sam_string(pretty * pa) {
	char * nill = (char*)memchr(pa->sam_string,'\0',pa->sam_string_length);
	while (nill!=NULL) {
		*nill='\t';
		nill=(char*)memchr(pa->sam_string,'\0',pa->sam_string_length-(nill-pa->sam_string));
	}
}


static inline int sam_header_field_sort(char * a , char * b, char * check_for) {
	bool a_has=(a[1]==check_for[0] && a[2]==check_for[1]);
	bool b_has=(b[1]==check_for[0] && b[2]==check_for[1]);
	if (a_has && b_has) {
		return strcmp(a,b);
	} else if (a_has) {
		return -1;
	} else if (b_has) {
		return 1;
	} else {
		return 0;
	}
}

int sam_header_sort(const void * a,const void * b) {
	char * s1=((char**)a)[0];
	char * s2=((char**)b)[0];
	if (strlen(s1)<4 || strlen(s2)<4) {
		fprintf(stderr,"Failed to sort sam header line!\n");
		fprintf(stderr,"%s and %s\n",s1,s2);
		exit(1);
	}
	if (s1[0]!='@' || s2[0]!='@') {
		fprintf(stderr,"These two lines are not sam-header lines! %s and %s\n",s1,s2);
		exit(1);
	}
	int ret=0;
	//check for HD
	ret=sam_header_field_sort(s1,s2,"HD");
	if (ret!=0) { return ret; };
	//check for SQ
	ret=sam_header_field_sort(s1,s2,"SQ");
	if (ret!=0) { return ret; };
	//check for RG
	ret=sam_header_field_sort(s1,s2,"RG");
	if (ret!=0) { return ret; };
	//check for PG
	ret=sam_header_field_sort(s1,s2,"PG");
	if (ret!=0) { return ret; };
	//check for PG
	ret=sam_header_field_sort(s1,s2,"CO");
	return ret;
}


void sam_close(sam_reader * sr) {
	fb_close(sr->fb);
	free(sr->pretty_stack);
	free(sr->pp_lls);
	free(sr);
}

sam_reader * sam_open(char * sam_filename,fastx_readnames * fxrn) {
	sam_reader * sr = (sam_reader*)malloc(sizeof(sam_reader));
	if (sr==NULL) {
		fprintf(stderr,"thread_open_sam : failed to allocate memory for thread info structure\n");
		exit(1);
	}
	sr->fb=fb_open(sam_filename,options.buffer_size,options.read_size);
	sr->pretty_stack_start=0;
	sr->pretty_stack_end=0;
	fprintf(stderr,"Starting a alignments stack with size %lu\n",options.alignments_stack_size);
	sr->pretty_stack_size=options.alignments_stack_size;
	sr->pretty_stack=(pretty*)malloc(sizeof(pretty)*sr->pretty_stack_size);
	if (sr->pretty_stack==NULL) {
		fprintf(stderr,"Failed to allocate memory for pretty_stack\n");
		exit(1);
	}
	memset(sr->pretty_stack,0,sizeof(pretty)*sr->pretty_stack_size);
	sr->pp_lls=(pp_ll*)malloc(sizeof(pp_ll)*LL_ALL*options.read_rate);
	if (sr->pp_lls==NULL) {
		fprintf(stderr,"Failed to allocate memory for pp_lls.\n");
	}
	memset(sr->pp_lls,0,sizeof(pp_ll)*LL_ALL*options.read_rate);
	sr->inter_offsets=(size_t*)malloc(sizeof(size_t)*options.read_rate);
	sr->pretty_stack_ends=(size_t*)malloc(sizeof(size_t)*options.read_rate);
	fprintf(stderr,"Size of inter_offsets is %d\n",options.read_rate);
	if (sr->inter_offsets==NULL) {
		fprintf(stderr,"Failed to allocate memory for inter_offsets\n");
		exit(1);
	}
	sr->last_tested=0;
	return sr;
}
void parse_sam(sam_reader * sr,fastx_readnames * fxrn) {
	char * current_newline=NULL;
	while (sr->fb->unseen_end!=sr->fb->unseen_inter && sr->last_tested<options.read_rate+fxrn->reads_seen) {
		const size_t pretty_stack_start_mod=sr->pretty_stack_start%sr->pretty_stack_size;
		const size_t pretty_stack_end_mod=sr->pretty_stack_end%sr->pretty_stack_size;
		if (sr->pretty_stack_start>sr->pretty_stack_end && pretty_stack_end_mod==pretty_stack_start_mod) {
			fprintf(stderr,"Stack out of space, need to flush!\n");
			return;
		}
        	size_t unseen_end_mod=sr->fb->unseen_end%sr->fb->size;
        	size_t unseen_inter_mod=sr->fb->unseen_inter%sr->fb->size;
		const size_t space=(unseen_inter_mod>=unseen_end_mod ? sr->fb->size : unseen_end_mod) - unseen_inter_mod;
		current_newline=(char*)memchr(sr->fb->base+unseen_inter_mod,'\n',space);
		if (current_newline==NULL) {
			sr->fb->unseen_inter+=space;
		} else {
			size_t current_newline_index=current_newline-sr->fb->base;
			if (current_newline_index==0 || sr->fb->base[current_newline_index-1]=='\0') {
				*current_newline='\0';
				current_newline_index++;
				for(; current_newline_index<sr->fb->size && sr->fb->base[current_newline_index]=='\0'; current_newline_index++) {sr->fb->base[current_newline_index]='\0';};
				sr->fb->unseen_inter+=current_newline_index-unseen_inter_mod;
				//sr->fb->unseen_start+=current_newline_index-unseen_start_mod;
				unseen_inter_mod=sr->fb->unseen_inter%sr->fb->size;
			} else {
				//fprintf(stderr,"Using %lu\n",unseen_start_mod);
				char * const line=sr->fb->base+unseen_inter_mod;
				//assert(sr->pretty_stack_size>sr->pretty_stack_filled);
				if (line[0]!='@') {
					//line - points to the first char of current line
					//current_newline - points to the next newline character
					const size_t length_of_string = current_newline-line;
					//if (sr->pretty_stack_size==sr->pretty_stack_filled) {	
					//	grow_sam_pretty(sr);
					//}
					for (; sr->last_tested<options.read_rate+fxrn->reads_seen; sr->last_tested++) {
						if (sr->last_tested==fxrn->reads_filled) {
							return;
						}
						//clear the row before using it!!!!!!!!!!
						const size_t read_id = sr->last_tested%options.read_rate;
						//ERRRROROROROROOROROROROROOROROROROORORORRRRR@!!!!
						char * const first_tab = (char*)memchr(line,'\t',length_of_string);
						if (first_tab==NULL) {
							fprintf(stderr,"CANNOT FIND FIRST TAB!\n");
							exit(1);
						}
						const size_t compare_length = first_tab-line;
						assert(compare_length!=0);
						char * const hit_list_read_name=fxrn->read_names+(sr->last_tested%fxrn->reads_inmem)*SIZE_READ_NAME*sizeof(char);
						//char buffer[SIZE_READ_NAME];
						//strncpy(buffer,hit_list_read_name,compare_length);
						//buffer[compare_length]='\0';
						//fprintf(stderr,"%p HL: %s,",sr,buffer);
						//strncpy(buffer,line,compare_length+15);
						//buffer[compare_length+15]='\0';
						//fprintf(stderr,"L: %s, %lu\n",buffer,read_id);
						//fprintf(stderr,"id: %d name: ||%s|| %lu seen %lu last\n",read_id,buffer,fxrn->reads_seen,sr->last_tested);
						//fprintf(stderr,"%s vs %s\n",buffer,hit_list_read_name);	
						if (strncmp(line,hit_list_read_name,compare_length)==0 ) {//&& !isdigit(hit_list_read_name[compare_length])) {	
							//fprintf(stderr,"XXXX %lu END, %lu read id\n",sr->pretty_stack_end,read_id);
							if (sr->pretty_stack_end-sr->pretty_stack_start>=sr->pretty_stack_size) {
								if (sr->last_tested==fxrn->reads_seen) {
									fprintf(stderr,"Alignments stack size is too small! Please use a larger size then '%lu' using '--alignments-stack-size'\n",options.alignments_stack_size);
									exit(1);
								}
								return;	
							}
							const size_t pa_index=(sr->pretty_stack_end++%sr->pretty_stack_size);
							//fprintf(stderr,"Put into index %lu, but read_id %lu\n",pa_index,read_id);
							pretty * const pa=sr->pretty_stack+pa_index;
							//pretty from string inplace memsets entry to 0!!! , assign after otherwise gets wiped!
							pretty_from_string_inplace(line,length_of_string,pa);
							pa->read_id=read_id;
							pa->sam_header=false;
							pa->fileno=sr->fileno;	
							assert(pa->read_name!=NULL);
							assert(pa->mate_pair==NULL);
							if (pa->paired_sequencing) {
								options.paired=true;
								pretty * const previous=sr->pretty_stack + (pa_index+sr->pretty_stack_size-1)%sr->pretty_stack_size;
								//fprintf(stderr,"previous %p\n",previous);
								if (sr->pretty_stack_start+1!=sr->pretty_stack_end && !previous->sam_header && previous->mate_pair==NULL) {
									previous->mate_pair=pa; pa->mate_pair=previous;
									assert(pa->read_name!=NULL);
									assert(pa->mate_pair->read_name!=NULL);
									pp_ll_append_and_check(sr->pp_lls+LL_ALL*pa->read_id,pa);
									assert(pa->read_name!=NULL);
									assert(pa->mate_pair->read_name!=NULL);
								} else {
									pa->mate_pair=NULL;
								}
							} else {
								options.unpaired=true;
								pp_ll_append_and_check(sr->pp_lls+LL_ALL*pa->read_id,pa);
							}
							*current_newline='\0';
							break;
						} else {	
							sr->inter_offsets[read_id]=sr->fb->unseen_start;
							//fprintf(stderr,"Setting interoffset %lu\n",sr->fb->unseen_start);
							sr->pretty_stack_ends[read_id]=sr->pretty_stack_end;
							sr->fb->unseen_start=sr->fb->unseen_inter;
							//fprintf(stderr,"Asign start %lu, %lu\n",read_id,sr->fb->unseen_start);
							//if ((sr->last_tested+1)<(options.read_rate+fxrn->reads_seen)) {
							//	const size_t next_read_id=(read_id+1)%options.read_rate;
							//	memset(sr->pp_lls+LL_ALL*next_read_id,0,sizeof(pp_ll)*LL_ALL);
							//	fprintf(stderr,"Clearing spot %lu\n",next_read_id);
							//}
						}
					}
					
				} else {
					found_sam_headers=true;
					pretty * pa=sr->pretty_stack+sr->pretty_stack_end++%sr->pretty_stack_size;
					pa->sam_string=line;
					*current_newline='\0';
					pp_ll_append(sr->sam_headers,pa);
					pa->sam_header=true;
				}
				if (sr->last_tested!=options.read_rate+fxrn->reads_seen) {
					sr->fb->unseen_inter+=current_newline_index-unseen_inter_mod+1;
					unseen_inter_mod=sr->fb->unseen_inter%sr->fb->size;
				}
			}
			sr->fb->exhausted=false;
		}
	}
	//fprintf(stderr,"RETURN GRACE %lu!=%lu,%lu<%d+%lu \n",sr->fb->unseen_end,sr->fb->unseen_inter, sr->last_tested,options.read_rate,fxrn->reads_seen);
	if (sr->fb->frb.eof==1 && sr->fb->unseen_end==sr->fb->unseen_inter && sr->last_tested==fxrn->reads_seen) {
		//sr->last_tested++;
		fprintf(stderr,"EOF\n");
	}
		//fprintf(stderr,"RETURN %llu - %llu < %llu \n",sr->last_tested, fxrn->reads_seen, read_amount);
	/*if (last_tested<read_amount && (sr->fb->frb.eof==0 || sr->fb->unseen_end!=sr->fb->unseen_start)) {
		fprintf(stderr,"failed to read into buffer, please increase buffer size!\n");
		exit(1);
	}*/
}
