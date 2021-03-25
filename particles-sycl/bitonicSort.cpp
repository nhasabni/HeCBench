/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

#include <assert.h>
#include "common.h"

#define LOCAL_SIZE_LIMIT 512U

#include "bitonicSort_kernels.cpp"

static unsigned int factorRadix2(unsigned int& log2L, unsigned int L){
    if(!L){
        log2L = 0;
        return 0;
    }else{
        for(log2L = 0; (L & 1) == 0; L >>= 1, log2L++);
        return L;
    }
}

void bitonicSort(
    queue &q,
    buffer<unsigned int, 1> &d_DstKey,
    buffer<unsigned int, 1> &d_DstVal,
    buffer<unsigned int, 1> &d_SrcKey,
    buffer<unsigned int, 1> &d_SrcVal,
    unsigned int batch,
    unsigned int arrayLength,
    unsigned int dir
)
{
    if(arrayLength < 2) return;

    //Only power-of-two array lengths are supported so far
    unsigned int log2L;
    unsigned int factorizationRemainder = factorRadix2(log2L, arrayLength);
    assert(factorizationRemainder == 1);

    dir = (dir != 0);

    size_t localWorkSize, globalWorkSize;

    if(arrayLength <= LOCAL_SIZE_LIMIT)
    {
        assert( (batch * arrayLength) % LOCAL_SIZE_LIMIT == 0 );

        //Launch bitonicSortLocal
        localWorkSize  = LOCAL_SIZE_LIMIT / 2;
        globalWorkSize = batch * arrayLength / 2;
        range<1> bs_gws (globalWorkSize);
        range<1> bs_lws (localWorkSize);

        q.submit([&] (handler &cgh) {
          auto dstKey = d_DstKey.get_access<sycl_read_write>(cgh);
          auto dstVal = d_DstVal.get_access<sycl_read_write>(cgh);
          auto srcKey = d_SrcKey.get_access<sycl_read_write>(cgh);
          auto srcVal = d_SrcVal.get_access<sycl_read_write>(cgh);
          accessor<unsigned int, 1, sycl_read_write, access::target::local> l_key(LOCAL_SIZE_LIMIT, cgh);
          accessor<unsigned int, 1, sycl_read_write, access::target::local> l_val(LOCAL_SIZE_LIMIT, cgh);
          cgh.parallel_for<class BitonicSortLocal>(nd_range<1>(bs_gws, bs_lws), [=] (nd_item<1> item) {
            bitonicSortLocal(item, 
                             dstKey.get_pointer(),  
                             dstVal.get_pointer(),  
                             srcKey.get_pointer(),  
                             srcVal.get_pointer(),  
                             l_key.get_pointer(),
                             l_val.get_pointer(),
                             arrayLength,
                             dir);
          });
        });
    }
    else
    {
        //Launch bitonicSortLocal1
        localWorkSize  = LOCAL_SIZE_LIMIT / 2;
        globalWorkSize = batch * arrayLength / 2;
        range<1> bs1_gws (globalWorkSize);
        range<1> bs1_lws (localWorkSize);
        q.submit([&] (handler &cgh) {
          auto dstKey = d_DstKey.get_access<sycl_write>(cgh);
          auto dstVal = d_DstVal.get_access<sycl_write>(cgh);
          auto srcKey = d_SrcKey.get_access<sycl_read>(cgh);
          auto srcVal = d_SrcVal.get_access<sycl_read>(cgh);
          accessor<unsigned int, 1, sycl_read_write, access::target::local> l_key(LOCAL_SIZE_LIMIT, cgh);
          accessor<unsigned int, 1, sycl_read_write, access::target::local> l_val(LOCAL_SIZE_LIMIT, cgh);
          cgh.parallel_for<class BitonicSortLocal1>(nd_range<1>(bs1_gws, bs1_lws), [=] (nd_item<1> item) {
            bitonicSortLocal1(item, 
                              dstKey.get_pointer(),
                              dstVal.get_pointer(),
                              srcKey.get_pointer(),
                              srcVal.get_pointer(),
                              l_key.get_pointer(),
                              l_val.get_pointer());
          });
        });

        for(unsigned int size = 2 * LOCAL_SIZE_LIMIT; size <= arrayLength; size <<= 1)
        {
            for(unsigned stride = size / 2; stride > 0; stride >>= 1)
            {
                if(stride >= LOCAL_SIZE_LIMIT)
                {
                    //Launch bitonicMergeGlobal
                    localWorkSize  = LOCAL_SIZE_LIMIT / 4;
                    globalWorkSize = batch * arrayLength / 2;
                    range<1> bmg_gws (globalWorkSize);
                    range<1> bmg_lws (localWorkSize);

                    q.submit([&] (handler &cgh) {
                      auto dstKey = d_DstKey.get_access<sycl_read_write>(cgh);
                      auto dstVal = d_DstVal.get_access<sycl_read_write>(cgh);
                      accessor<unsigned int, 1, sycl_read_write, access::target::local> l_key(LOCAL_SIZE_LIMIT, cgh);
                      accessor<unsigned int, 1, sycl_read_write, access::target::local> l_val(LOCAL_SIZE_LIMIT, cgh);
                      cgh.parallel_for<class BitonicMergeGlobal>(nd_range<1>(bmg_gws, bmg_lws), [=] (nd_item<1> item) {
                        bitonicMergeGlobal(item, 
                                           dstKey.get_pointer(),
                                           dstVal.get_pointer(),
                                           dstKey.get_pointer(),
                                           dstVal.get_pointer(),
                                           arrayLength,
                                           size,
                                           stride,
                                           dir);
                      });
                    });
                }
                else
                {
                    //Launch bitonicMergeLocal
                    localWorkSize  = LOCAL_SIZE_LIMIT / 2;
                    globalWorkSize = batch * arrayLength / 2;

                    range<1> bml_gws (globalWorkSize);
                    range<1> bml_lws (localWorkSize);

                    q.submit([&] (handler &cgh) {
                      auto dstKey = d_DstKey.get_access<sycl_read_write>(cgh);
                      auto dstVal = d_DstVal.get_access<sycl_read_write>(cgh);
                      accessor<unsigned int, 1, sycl_read_write, access::target::local> l_key(LOCAL_SIZE_LIMIT, cgh);
                      accessor<unsigned int, 1, sycl_read_write, access::target::local> l_val(LOCAL_SIZE_LIMIT, cgh);
                      cgh.parallel_for<class BitonicMergeLocal>(nd_range<1>(bml_gws, bml_lws), [=] (nd_item<1> item) {
                        bitonicMergeLocal(item,
                                          dstKey.get_pointer(),
                                          dstVal.get_pointer(),
                                          dstKey.get_pointer(),
                                          dstVal.get_pointer(),
                                          l_key.get_pointer(),
                                          l_val.get_pointer(),
                                          arrayLength,
                                          size,
                                          stride,
                                          dir);
                      });
                    });
                    break;
                }
            }
        }
    }
}
